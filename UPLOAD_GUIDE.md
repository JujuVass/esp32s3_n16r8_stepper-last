# ESP32 Filesystem Upload & Live Editing

## Quick Start

### 1. Upload Firmware (First Time Only)
```bash
cd freenove_esp32s3n8r8_stepper
platformio run --target upload        # Upload firmware
platformio run --target uploadfs      # Upload LittleFS (filesystem.html)
```

### 2. Live HTML Upload (Without USB/PlatformIO)
```bash
# Update IP address in upload_html.py first
python upload_html.py --help

# Upload index.html once
python upload_html.py

# Auto-watch and upload index.html on changes
python upload_html.py --watch

# Upload filesystem.html
python upload_html.py --file filesystem.html

# Watch filesystem.html for changes
python upload_html.py --watch --file filesystem.html

# Upload all HTML files
python upload_html.py --all

# Use custom ESP32 IP
python upload_html.py --ip 192.168.1.100
```

## Features

### ✅ Filesystem Browser UI
- **Hierarchical folder structure** - See files organized in folders
- **Tree expansion** - Click ▶️ to expand/collapse folders
- **File operations**:
  - 📥 **Download** - Download any file
  - ✏️ **Edit** - Edit text files inline
  - 🗑️ **Delete** - Remove files
  - 📤 **Upload** - Drag & drop to upload
- **Statistics** - Total size, file count, free space

**Access:** `http://<ESP32_IP>/filesystem`

### 📡 FilesystemManager API
All operations use the REST API (`include/FilesystemManager.h`):
- `GET /api/fs/list` - List files with hierarchy
- `GET /api/fs/download?path=/file.txt` - Download file
- `GET /api/fs/read?path=/file.txt` - Read for editing
- `POST /api/fs/write` - Save edited file
- `POST /api/fs/upload` - Upload file (multipart)
- `POST /api/fs/delete` - Delete file
- `POST /api/fs/clear` - Clear all files

**Full API docs:** See `FILESYSTEM_API.md`

## Workflow: Editing HTML Without USB

### Scenario: Update index.html
```bash
# 1. First time: Upload firmware
platformio run --target upload --project-conf platformio.ini

# 2. Then: Use Python script for quick iterations
python upload_html.py --watch

# 3. Edit index.html in your editor
# 4. Script auto-uploads on save (0.5s detection)
# 5. Refresh browser to see changes
```

### Scenario: Customize Filesystem Browser
```bash
# 1. Edit data/filesystem.html
# 2. Watch for changes
python upload_html.py --watch --file filesystem.html

# 3. Script auto-uploads filesystem.html when saved
# 4. Refresh http://<IP>/filesystem in browser
```

## Upload Script Configuration

Edit `upload_html.py` to change:
```python
ESP32_IP = "192.168.1.81"  # Your ESP32 IP address
```

Or pass via command line:
```bash
python upload_html.py --ip 192.168.1.50
```

## Folder Structure

```
data/
  ├── index.html           # Main web interface
  ├── style.css            # Styling (embedded in HTML now)
  ├── filesystem.html      # Filesystem browser UI (NEW!)
  └── www/                 # Static assets directory
      └── (uploaded files)
```

## File Upload Hierarchy

When you upload files via the filesystem UI or Python script:
- **Desktop file:** `mydata.csv`
- **Destination:** `/mydata.csv` (root of filesystem)
- **Access:** `http://ESP32/filesystem` to see all files

You can organize files by uploading to paths like `/data/config.json` (creates `/data/` folder).

## Binary File Protection

These extensions **cannot be edited** via the UI:
- `.bin` - Firmware binaries
- `.jpg`, `.jpeg`, `.png`, `.gif` - Images
- `.pdf` - PDF documents
- `.zip`, `.gz` - Compressed archives

**Why?** Prevents accidental corruption of binary data. Download and edit locally instead.

## Performance Notes

- **Hierarchical rendering** - Efficient even with 1000+ files
- **No recursion limit** - Supports unlimited folder nesting
- **Auto-refresh** - UI updates every 5 seconds
- **File size limit** - Limited by ESP32 free RAM (typically 320KB available)
- **Upload limit** - Per-file limited to available LittleFS space (typically 2MB-4MB)

## Troubleshooting

### ❌ "Connection error: Cannot reach ESP32"
- Check ESP32 IP address is correct
- Verify ESP32 is powered on and connected to WiFi
- Check firewall isn't blocking port 80

### ❌ "File not found" error
- Ensure file is in `data/` directory
- Use correct relative path

### ❌ HTML changes not showing
- Hard refresh browser: `Ctrl+Shift+Del` (Windows) or `Cmd+Shift+Del` (Mac)
- Check filesystem browser at `http://ESP32/filesystem` to verify upload

### ❌ Filesystem.html shows all files flat
- **Update to latest version** - Old HTML didn't support hierarchy
- Run: `python upload_html.py --file filesystem.html`

## Development Workflow

### Fast Iteration Loop
```bash
1. Start watch script:
   python upload_html.py --watch --file filesystem.html

2. Edit data/filesystem.html in VS Code

3. Save file → Auto-uploads in 0.5s

4. Refresh browser to see changes

5. Repeat 2-4 until happy
```

### No USB Required!
Once firmware is uploaded, you never need USB again for:
- HTML/CSS updates
- JavaScript changes
- Configuration file edits
- Static asset uploads

Everything flows through `http://ESP32/api/fs/*` API.

## Command Reference

```bash
# Firmware operations (require USB + PlatformIO)
platformio run                          # Build firmware
platformio run --target upload          # Upload firmware to ESP32
platformio run --target uploadfs        # Upload /data files to LittleFS

# HTML updates (no USB needed!)
python upload_html.py                   # One-time upload
python upload_html.py --watch           # Auto-update on changes
python upload_html.py --all             # Upload all HTML files
python upload_html.py --file FILE       # Upload specific file
python upload_html.py --ip 192.168.1.X  # Custom IP

# Web-based (via browser)
http://ESP32/filesystem                 # File browser UI
http://ESP32/api/fs/list                # JSON file list
```

---

**Last Updated:** November 2, 2025  
**Version:** 1.0 - Hierarchical Filesystem Support + Python Upload Tool
