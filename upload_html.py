#!/usr/bin/env python3
"""
ESP32 Filesystem Upload Tool
Uploads files to ESP32 via FilesystemManager API (no USB/platformio required!)

Features:
- Recursive directory scanning (supports js/core/, js/modules/, etc.)
- Sync mode: deletes remote files not present locally
- Watch mode: auto-upload on file changes
- History backup: saves stats.json, playlists.json before sync operations

Usage:
    python upload_html.py                    # Upload index.html once
    python upload_html.py --watch            # Watch index.html for changes
    python upload_html.py --file js/core/app.js  # Upload specific file
    python upload_html.py --all              # Upload all HTML/JS/CSS files
    python upload_html.py --js               # Upload only JS files (recursive)
    python upload_html.py --sync             # Full sync: upload all + delete orphans
    python upload_html.py --list             # List files on ESP32
    python upload_html.py --delete /js/old.js  # Delete specific file on ESP32
    python upload_html.py --backup           # Backup critical files from ESP32
    python upload_html.py --no-backup        # Skip auto-backup on sync/all
"""

import requests
import time
import sys
import os
from pathlib import Path
from datetime import datetime

# Configuration
ESP32_IP = "192.168.1.85"
API_BASE = f"http://{ESP32_IP}/api/fs"
DATA_DIR = "data"
JS_DIR = "data/js"
CSS_DIR = "data/css"
HISTORY_DIR = ".history"

# Files to backup automatically (critical user data on ESP32)
BACKUP_FILES = [
    '/stats.json',
    '/playlists.json',
    '/config.json',
    '/sequences.json'
]

# ============================================================================
# FILE OPERATIONS
# ============================================================================

def upload_file(file_path, esp32_ip=ESP32_IP, target_path=None):
    """Upload a file to ESP32 via FilesystemManager API"""
    if not os.path.exists(file_path):
        print(f"❌ Error: File not found: {file_path}")
        return False
    
    file_size = os.path.getsize(file_path)
    
    # Determine target path on ESP32
    if target_path:
        target_filename = target_path
    else:
        file_path_obj = Path(file_path)
        if 'data' in file_path_obj.parts:
            data_idx = file_path_obj.parts.index('data')
            relative_parts = file_path_obj.parts[data_idx + 1:]
            target_filename = '/' + '/'.join(relative_parts)
        else:
            target_filename = '/' + os.path.basename(file_path)
    
    # Normalize path
    target_filename = target_filename.replace('\\', '/')
    if not target_filename.startswith('/'):
        target_filename = '/' + target_filename
    
    endpoint = f"http://{esp32_ip}/api/fs/upload"
    
    try:
        print(f"📤 {file_path} → {target_filename} ({file_size} bytes)...", end=" ")
        start_time = time.time()
        
        with open(file_path, 'rb') as f:
            files = {'file': (target_filename, f)}
            response = requests.post(endpoint, files=files, timeout=30)
        
        elapsed = time.time() - start_time
        
        if response.status_code == 200:
            print(f"✅ ({elapsed:.2f}s)")
            return True
        else:
            print(f"❌ HTTP {response.status_code}")
            return False
            
    except requests.exceptions.ConnectionError:
        print(f"❌ Connection error")
        return False
    except Exception as e:
        print(f"❌ {e}")
        return False


def download_file(remote_path, esp32_ip=ESP32_IP):
    """Download a file from ESP32, returns content or None"""
    if not remote_path.startswith('/'):
        remote_path = '/' + remote_path
    
    # Use raw file access endpoint
    endpoint = f"http://{esp32_ip}{remote_path}"
    
    try:
        response = requests.get(endpoint, timeout=10)
        if response.status_code == 200:
            return response.content
        return None
    except Exception:
        return None


def backup_critical_files(esp32_ip=ESP32_IP, silent=False):
    """
    Backup critical files from ESP32 to .history/ folder
    Files are suffixed with datetime: stats_20241209_164530.json
    Returns number of files backed up
    """
    # Create history directory
    history_path = Path(HISTORY_DIR)
    history_path.mkdir(exist_ok=True)
    
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    backed_up = 0
    
    if not silent:
        print("💾 Backing up critical files from ESP32...")
    
    for remote_file in BACKUP_FILES:
        content = download_file(remote_file, esp32_ip)
        
        if content and len(content) > 2:  # Skip empty files (just "{}" or "[]")
            # Create backup filename: /stats.json -> stats_20241209_164530.json
            base_name = remote_file.lstrip('/').replace('/', '_')
            name_part = base_name.rsplit('.', 1)[0]
            ext_part = base_name.rsplit('.', 1)[1] if '.' in base_name else 'txt'
            backup_name = f"{name_part}_{timestamp}.{ext_part}"
            
            backup_path = history_path / backup_name
            backup_path.write_bytes(content)
            
            if not silent:
                print(f"   📄 {remote_file} → .history/{backup_name} ({len(content)} bytes)")
            backed_up += 1
    
    if not silent:
        if backed_up > 0:
            print(f"   ✅ {backed_up} file(s) backed up to .history/")
        else:
            print(f"   ℹ️  No critical files found on ESP32 (or all empty)")
        print()
    
    return backed_up


def list_history_files():
    """List all backup files in .history/ folder, grouped by type"""
    history_path = Path(HISTORY_DIR)
    if not history_path.exists():
        return {}
    
    # Group files by base name (stats, playlists, etc.)
    files_by_type = {}
    for f in history_path.glob('*.json'):
        # Parse filename: stats_20251209_171803.json -> type=stats, timestamp=20251209_171803
        parts = f.stem.rsplit('_', 2)  # Split from right to get name_date_time
        if len(parts) >= 3:
            file_type = parts[0]  # stats, playlists, config, sequences
            timestamp = f"{parts[1]}_{parts[2]}"
        else:
            file_type = f.stem
            timestamp = "unknown"
        
        if file_type not in files_by_type:
            files_by_type[file_type] = []
        files_by_type[file_type].append({
            'path': f,
            'name': f.name,
            'timestamp': timestamp,
            'size': f.stat().st_size
        })
    
    # Sort each type by timestamp (most recent first)
    for file_type in files_by_type:
        files_by_type[file_type].sort(key=lambda x: x['timestamp'], reverse=True)
    
    return files_by_type


def restore_file(filename, esp32_ip=ESP32_IP):
    """Restore a specific backup file to ESP32"""
    history_path = Path(HISTORY_DIR)
    file_path = history_path / filename
    
    if not file_path.exists():
        print(f"❌ File not found: .history/{filename}")
        return False
    
    # Determine target path on ESP32 from filename
    # stats_20251209_171803.json -> /stats.json
    parts = filename.rsplit('_', 2)
    if len(parts) >= 3:
        base_name = parts[0]
        ext = file_path.suffix
        target_path = f"/{base_name}{ext}"
    else:
        print(f"❌ Cannot parse filename: {filename}")
        return False
    
    print(f"📤 Restoring {filename} → {target_path}...")
    return upload_file(str(file_path), esp32_ip, target_path)


def restore_latest(esp32_ip=ESP32_IP):
    """Restore the latest backup of each critical file type"""
    files_by_type = list_history_files()
    
    if not files_by_type:
        print("❌ No backup files found in .history/")
        return 0
    
    print("🔄 Restoring latest backups to ESP32...")
    print()
    
    restored = 0
    for file_type, files in files_by_type.items():
        if files:
            latest = files[0]  # Already sorted, most recent first
            target_path = f"/{file_type}.json"
            print(f"   📄 {latest['name']} → {target_path}")
            if upload_file(str(latest['path']), esp32_ip, target_path):
                restored += 1
    
    print()
    print(f"✅ {restored} file(s) restored to ESP32")
    return restored


def show_history():
    """Display available backup files"""
    files_by_type = list_history_files()
    
    if not files_by_type:
        print("📂 No backup files in .history/")
        return
    
    print("📂 Available backups in .history/:")
    print()
    
    for file_type, files in sorted(files_by_type.items()):
        print(f"  {file_type}:")
        for i, f in enumerate(files[:5]):  # Show max 5 per type
            marker = "→ " if i == 0 else "  "
            ts = f['timestamp']
            # Format: 20251209_171803 -> 2025-12-09 17:18:03
            formatted_ts = f"{ts[0:4]}-{ts[4:6]}-{ts[6:8]} {ts[9:11]}:{ts[11:13]}:{ts[13:15]}"
            print(f"    {marker}{f['name']} ({f['size']} bytes) - {formatted_ts}")
        if len(files) > 5:
            print(f"    ... and {len(files) - 5} more")
    print()
    print("  Use: --restore <filename>  or  --restore last")


def delete_file(remote_path, esp32_ip=ESP32_IP):
    """Delete a file on ESP32"""
    # Normalize path
    if not remote_path.startswith('/'):
        remote_path = '/' + remote_path
    
    endpoint = f"http://{esp32_ip}/api/fs/delete"
    
    try:
        print(f"🗑️  Deleting {remote_path}...", end=" ")
        response = requests.post(endpoint, json={"path": remote_path}, timeout=10)
        
        if response.status_code == 200:
            print("✅")
            return True
        else:
            print(f"❌ HTTP {response.status_code}")
            return False
            
    except Exception as e:
        print(f"❌ {e}")
        return False


def list_remote_files(esp32_ip=ESP32_IP, path="/"):
    """List files on ESP32 recursively, returns list of paths"""
    endpoint = f"http://{esp32_ip}/api/fs/list"
    files = []
    
    def extract_files(items, base_path=""):
        """Recursively extract file paths from hierarchical JSON"""
        for item in items:
            item_path = item.get('path', base_path + '/' + item.get('name', ''))
            if item.get('isDir', False):
                # Recurse into children
                children = item.get('children', [])
                extract_files(children, item_path)
            else:
                files.append(item_path)
    
    try:
        response = requests.get(endpoint, timeout=10)
        if response.status_code == 200:
            data = response.json()
            # API returns { files: [...], usedBytes, totalBytes, freeSpace }
            extract_files(data.get('files', []))
        return files
    except Exception as e:
        print(f"❌ Error listing files: {e}")
        return []


# ============================================================================
# FILE COLLECTION (LOCAL)
# ============================================================================

def get_all_files_recursive(base_dir, extensions=None):
    """Get all files recursively from a directory"""
    files = []
    base_path = Path(base_dir)
    
    if not base_path.exists():
        return files
    
    for item in base_path.rglob('*'):
        if item.is_file():
            if extensions is None or item.suffix.lower() in extensions:
                files.append(str(item))
    
    return files


def get_local_files():
    """Get all uploadable local files with their ESP32 target paths"""
    files = {}
    
    # HTML files in data/
    for html_file in ['index.html', 'filesystem.html', 'setup.html']:
        local_path = os.path.join(DATA_DIR, html_file)
        if os.path.exists(local_path):
            files['/' + html_file] = local_path
    
    # JS files (recursive)
    if os.path.exists(JS_DIR):
        for js_file in get_all_files_recursive(JS_DIR, ['.js']):
            # Convert to ESP32 path
            rel_path = os.path.relpath(js_file, DATA_DIR)
            esp_path = '/' + rel_path.replace('\\', '/')
            files[esp_path] = js_file
    
    # CSS files (recursive)
    if os.path.exists(CSS_DIR):
        for css_file in get_all_files_recursive(CSS_DIR, ['.css']):
            rel_path = os.path.relpath(css_file, DATA_DIR)
            esp_path = '/' + rel_path.replace('\\', '/')
            files[esp_path] = css_file
    
    return files


# ============================================================================
# SYNC OPERATIONS
# ============================================================================

def sync_files(esp32_ip=ESP32_IP, delete_orphans=True):
    """Sync local files to ESP32, optionally deleting orphans"""
    print("🔄 Syncing files to ESP32...")
    print()
    
    # Get local files
    local_files = get_local_files()
    local_paths = set(local_files.keys())
    
    print(f"📁 Local files: {len(local_files)}")
    for path in sorted(local_paths):
        print(f"   {path}")
    print()
    
    # Get remote files (only in /js/, /css/, and HTML at root)
    print("📡 Fetching remote file list...")
    remote_files = list_remote_files(esp32_ip)
    
    # Filter to only web assets (not stats.json, playlists.json, etc.)
    web_extensions = ['.html', '.js', '.css']
    remote_web_files = [f for f in remote_files if any(f.endswith(ext) for ext in web_extensions)]
    remote_paths = set(remote_web_files)
    
    print(f"📡 Remote web files: {len(remote_web_files)}")
    for path in sorted(remote_paths):
        print(f"   {path}")
    print()
    
    # Files to upload (local but not remote, or we always upload for freshness)
    to_upload = local_paths
    
    # Files to delete (remote but not local)
    to_delete = remote_paths - local_paths
    
    # Upload files
    print(f"📤 Uploading {len(to_upload)} files...")
    success_count = 0
    for esp_path in sorted(to_upload):
        local_path = local_files[esp_path]
        if upload_file(local_path, esp32_ip, esp_path):
            success_count += 1
    print(f"   ✅ {success_count}/{len(to_upload)} uploaded")
    print()
    
    # Delete orphan files
    if delete_orphans and to_delete:
        print(f"🗑️  Deleting {len(to_delete)} orphan files...")
        for remote_path in sorted(to_delete):
            delete_file(remote_path, esp32_ip)
        print()
    elif to_delete:
        print(f"⚠️  {len(to_delete)} orphan files on ESP32 (use --sync to delete):")
        for path in sorted(to_delete):
            print(f"   {path}")
        print()
    
    print("✅ Sync complete!")


# ============================================================================
# WATCH MODE
# ============================================================================

def watch_file(file_path, esp32_ip=ESP32_IP):
    """Watch file for changes and auto-upload"""
    print(f"👀 Watching {file_path} for changes...")
    print(f"   Press Ctrl+C to stop")
    print()
    
    last_mtime = 0
    
    try:
        while True:
            try:
                current_mtime = os.path.getmtime(file_path)
                
                if current_mtime != last_mtime and last_mtime != 0:
                    print(f"\n🔄 Change detected at {datetime.now().strftime('%H:%M:%S')}")
                    upload_file(file_path, esp32_ip)
                    print()
                
                last_mtime = current_mtime
                time.sleep(0.5)
                
            except FileNotFoundError:
                print(f"⚠️  File deleted, waiting...")
                time.sleep(1)
                last_mtime = 0
                
    except KeyboardInterrupt:
        print(f"\n\n👋 Stopped watching")


# ============================================================================
# MAIN
# ============================================================================

def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Upload/sync files to ESP32 via HTTP API',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python upload_html.py                    # Upload index.html
  python upload_html.py --watch            # Auto-upload index.html on changes
  python upload_html.py --file js/core/app.js  # Upload specific file
  python upload_html.py --all              # Upload all HTML/JS/CSS files
  python upload_html.py --js               # Upload only JS files (recursive)
  python upload_html.py --sync             # Full sync: upload all + delete orphans
  python upload_html.py --list             # List files on ESP32
  python upload_html.py --delete /js/old.js  # Delete specific file on ESP32
  python upload_html.py --backup           # Backup stats.json, playlists.json from ESP32
  python upload_html.py --list-restore     # List available backups
  python upload_html.py --restore          # Restore latest backup of each file
  python upload_html.py --restore stats_20251209_171803.json  # Restore specific file
        """
    )
    
    parser.add_argument('--watch', '-w', action='store_true',
                        help='Watch file for changes and auto-upload')
    parser.add_argument('--file', '-f',
                        help='Specific file to upload (e.g., js/core/app.js)')
    parser.add_argument('--all', '-a', action='store_true',
                        help='Upload all files (HTML, JS, CSS) - no delete')
    parser.add_argument('--js', action='store_true',
                        help='Upload only JS files (recursive)')
    parser.add_argument('--sync', '-s', action='store_true',
                        help='Full sync: upload all + delete orphan files on ESP32')
    parser.add_argument('--list', '-l', action='store_true',
                        help='List files on ESP32')
    parser.add_argument('--delete', '-d',
                        help='Delete specific file on ESP32 (e.g., /js/old.js)')
    parser.add_argument('--backup', '-b', action='store_true',
                        help='Backup critical files (stats, playlists) from ESP32 to .history/')
    parser.add_argument('--list-restore', action='store_true',
                        help='List available backup files in .history/')
    parser.add_argument('--restore', '-r', nargs='?', const='last',
                        help='Restore backup: without arg = latest, or specify filename')
    parser.add_argument('--no-backup', action='store_true',
                        help='Skip automatic backup before --sync or --all')
    parser.add_argument('--ip', default=ESP32_IP,
                        help=f'ESP32 IP address (default: {ESP32_IP})')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("  ESP32 Filesystem Upload Tool")
    print("=" * 60)
    print(f"  Target: {args.ip}")
    print("=" * 60)
    print()
    
    # List restore (show available backups)
    if args.list_restore:
        show_history()
        return
    
    # Restore mode
    if args.restore:
        if args.restore == 'last':
            # --restore without argument or --restore last: restore latest of each type
            restore_latest(args.ip)
        else:
            # --restore <filename>: restore specific file
            restore_file(args.restore, args.ip)
        return
    
    # Backup mode (explicit)
    if args.backup:
        backup_critical_files(args.ip)
        return
    
    # List mode
    if args.list:
        print("📡 Files on ESP32:")
        files = list_remote_files(args.ip)
        for f in sorted(files):
            print(f"   {f}")
        print(f"\n   Total: {len(files)} files")
        return
    
    # Delete mode
    if args.delete:
        delete_file(args.delete, args.ip)
        return
    
    # Sync mode (with auto-backup)
    if args.sync:
        if not args.no_backup:
            backup_critical_files(args.ip)
        sync_files(args.ip, delete_orphans=True)
        return
    
    # Collect files to upload
    if args.file:
        file_path = args.file
        if not os.path.exists(file_path) and os.path.exists(f"data/{file_path}"):
            file_path = f"data/{file_path}"
        files_to_upload = [file_path]
    elif args.js:
        files_to_upload = get_all_files_recursive(JS_DIR, ['.js'])
        if not files_to_upload:
            print("❌ No JS files found")
            sys.exit(1)
    elif args.all:
        # Auto-backup before --all (bulk upload could overwrite)
        if not args.no_backup:
            backup_critical_files(args.ip)
        local_files = get_local_files()
        files_to_upload = list(local_files.values())
        if not files_to_upload:
            print("❌ No files found")
            sys.exit(1)
    else:
        files_to_upload = [os.path.join(DATA_DIR, 'index.html')]
    
    print(f"📁 Files to upload: {len(files_to_upload)}")
    for f in files_to_upload:
        print(f"   {f}")
    print()
    
    # Watch mode
    if args.watch:
        if len(files_to_upload) > 1:
            print("❌ Error: --watch only supports single file")
            sys.exit(1)
        watch_file(files_to_upload[0], args.ip)
    else:
        # Upload
        success = 0
        for file_path in files_to_upload:
            if upload_file(file_path, args.ip):
                success += 1
        
        print()
        print(f"{'✅' if success == len(files_to_upload) else '⚠️ '} {success}/{len(files_to_upload)} files uploaded")
        
        if success == len(files_to_upload):
            print(f"🌐 View at: http://{args.ip}/")
        
        sys.exit(0 if success == len(files_to_upload) else 1)


if __name__ == '__main__':
    main()

