#!/usr/bin/env python3
"""
ESP32 Filesystem Upload Tool
Uploads files to ESP32 via FilesystemManager API (no USB/platformio required!)

Usage:
    python upload_html.py                    # Upload index.html once
    python upload_html.py --watch            # Watch index.html for changes and auto-upload
    python upload_html.py --file filesystem.html   # Upload specific file
    python upload_html.py --watch --file filesystem.html  # Watch specific file
    python upload_html.py --all              # Upload all HTML files at once
"""

import requests
import time
import sys
import os
from pathlib import Path
from datetime import datetime

# Configuration
ESP32_IP = "192.168.1.81"
API_UPLOAD_ENDPOINT = f"http://{ESP32_IP}/api/fs/upload"
DEFAULT_HTML = "data/index.html"
DEFAULT_FILESYSTEM_HTML = "data/filesystem.html"

def upload_file(file_path, esp32_ip=ESP32_IP):
    """Upload a file to ESP32 via FilesystemManager API"""
    if not os.path.exists(file_path):
        print(f"❌ Error: File not found: {file_path}")
        return False
    
    file_size = os.path.getsize(file_path)
    target_filename = os.path.basename(file_path)
    endpoint = f"http://{esp32_ip}/api/fs/upload"
    
    try:
        print(f"📤 Uploading {file_path} ({file_size} bytes) to {esp32_ip}...")
        start_time = time.time()
        
        with open(file_path, 'rb') as f:
            files = {'file': (target_filename, f)}
            response = requests.post(endpoint, files=files, timeout=10)
        
        elapsed = time.time() - start_time
        
        if response.status_code == 200:
            try:
                data = response.json()
                if data.get('success'):
                    print(f"✅ Upload successful! ({elapsed:.2f}s)")
                    print(f"🌐 View at: http://{esp32_ip}/")
                    if target_filename == 'filesystem.html':
                        print(f"📁 Filesystem browser: http://{esp32_ip}/filesystem")
                    return True
                else:
                    print(f"❌ Upload failed: {data.get('error', 'Unknown error')}")
                    return False
            except:
                # If response isn't JSON, consider it success if status is 200
                print(f"✅ Upload successful! ({elapsed:.2f}s) - 200 OK")
                print(f"🌐 View at: http://{esp32_ip}/")
                return True
        else:
            print(f"❌ Upload failed: HTTP {response.status_code}")
            print(f"   Response: {response.text[:200]}")
            return False
            
    except requests.exceptions.ConnectionError:
        print(f"❌ Connection error: Cannot reach ESP32 at {esp32_ip}")
        print(f"   - Check that ESP32 is powered on and connected to WiFi")
        print(f"   - Check that IP address is correct: {esp32_ip}")
        return False
    except Exception as e:
        print(f"❌ Upload error: {e}")
        return False

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
                time.sleep(0.5)  # Check every 500ms
                
            except FileNotFoundError:
                print(f"⚠️  File deleted, waiting for it to reappear...")
                time.sleep(1)
                last_mtime = 0
                
    except KeyboardInterrupt:
        print(f"\n\n👋 Stopped watching")

def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Upload files to ESP32 without USB cable or platformio',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python upload_html.py                    # Upload index.html
  python upload_html.py --watch            # Auto-upload index.html on changes
  python upload_html.py --file filesystem.html   # Upload filesystem.html
  python upload_html.py --watch --file filesystem.html  # Watch filesystem.html
  python upload_html.py --all              # Upload all HTML files
  python upload_html.py --ip 192.168.1.100 # Use custom IP
        """
    )
    
    parser.add_argument('--watch', '-w', action='store_true',
                        help='Watch file for changes and auto-upload')
    parser.add_argument('--file', '-f',
                        help='File to upload (e.g., index.html, filesystem.html)')
    parser.add_argument('--all', '-a', action='store_true',
                        help='Upload all files (index.html, filesystem.html, etc)')
    parser.add_argument('--ip', default=ESP32_IP,
                        help=f'ESP32 IP address (default: {ESP32_IP})')
    
    args = parser.parse_args()
    
    # Determine file(s) to upload
    if args.file:
        file_path = args.file
        # Support relative paths from data/
        if not os.path.exists(file_path) and os.path.exists(f"data/{file_path}"):
            file_path = f"data/{file_path}"
        files_to_upload = [file_path]
    elif args.all:
        files_to_upload = []
        if os.path.exists(DEFAULT_HTML):
            files_to_upload.append(DEFAULT_HTML)
        if os.path.exists(DEFAULT_FILESYSTEM_HTML):
            files_to_upload.append(DEFAULT_FILESYSTEM_HTML)
        if not files_to_upload:
            print("❌ No files found to upload")
            sys.exit(1)
    else:
        files_to_upload = [DEFAULT_HTML]
    
    print("=" * 60)
    print("  ESP32 Filesystem Upload Tool")
    print("=" * 60)
    print(f"  Target: {args.ip}")
    print(f"  Method: FilesystemManager API (/api/fs/upload)")
    print(f"  Files:  {', '.join(files_to_upload)}")
    print("=" * 60)
    print()
    
    if args.watch:
        if len(files_to_upload) > 1:
            print("❌ Error: --watch only supports single file")
            print("   Use --watch with --file or without --all")
            sys.exit(1)
        watch_file(files_to_upload[0], args.ip)
    else:
        all_success = True
        for file_path in files_to_upload:
            success = upload_file(file_path, args.ip)
            if not success:
                all_success = False
            if len(files_to_upload) > 1:
                print()  # Spacing between multiple uploads
        sys.exit(0 if all_success else 1)

if __name__ == '__main__':
    main()

