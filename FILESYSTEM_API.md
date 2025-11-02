# Filesystem Manager API Documentation

## Overview

The **FilesystemManager** provides a complete RESTful API for browsing, managing, and editing files in ESP32's LittleFS filesystem. It maintains a **hierarchical folder structure** in the JSON response.

## API Routes

### 1. Filesystem Browser UI
```
GET /filesystem
```
Serves the interactive HTML/CSS/JS filesystem browser interface.

**Response:** HTML page with embedded UI

---

### 2. List Files (Hierarchical)
```
GET /api/fs/list
```
Returns all files and folders with **nested structure** (folders contain their children).

**Response (200 OK):**
```json
{
  "files": [
    {
      "name": "index.html",
      "path": "/index.html",
      "size": 1024,
      "time": 1698765432,
      "isDir": false
    },
    {
      "name": "logs",
      "path": "/logs",
      "size": 0,
      "time": 1698765000,
      "isDir": true,
      "children": [
        {
          "name": "log_20251102_0.txt",
          "path": "/logs/log_20251102_0.txt",
          "size": 512,
          "time": 1698765100,
          "isDir": false
        },
        {
          "name": "log_20251102_1.txt",
          "path": "/logs/log_20251102_1.txt",
          "size": 256,
          "time": 1698765200,
          "isDir": false
        }
      ]
    },
    {
      "name": "data",
      "path": "/data",
      "size": 0,
      "time": 1698765000,
      "isDir": true,
      "children": [
        {
          "name": "config.json",
          "path": "/data/config.json",
          "size": 2048,
          "time": 1698765300,
          "isDir": false
        }
      ]
    }
  ],
  "usedBytes": 3840,
  "totalBytes": 524288,
  "freeSpace": 520448
}
```

**Key Features:**
- ✅ **Hierarchical structure** - Folders contain `"children"` array
- ✅ **Recursive depth** - No limit on nesting levels
- ✅ **Metadata** - Each item has `name`, `path`, `size`, `time`, `isDir`
- ✅ **Stats** - Total, used, and free space in bytes

---

### 3. Download File
```
GET /api/fs/download?path=/path/to/file
```
Downloads a file with automatic Content-Type detection.

**Query Parameters:**
- `path` (required) - File path (e.g., `/index.html`, `/logs/log.txt`)

**Response (200 OK):**
- Binary data with appropriate `Content-Type` header
- `Content-Disposition: attachment` header for browser download

**Supported Content-Types:**
| Extension | Type |
|-----------|------|
| `.txt` | `text/plain; charset=UTF-8` |
| `.html` | `text/html; charset=UTF-8` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.json` | `application/json` |
| `.xml` | `application/xml` |
| `.pdf` | `application/pdf` |
| `.png` | `image/png` |
| `.jpg/.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| `.svg` | `image/svg+xml` |
| Other | `application/octet-stream` |

**Error Responses:**
```json
// 400 Bad Request
{ "error": "Missing path parameter" }

// 404 Not Found
{ "error": "File not found" }

// 500 Internal Error
{ "error": "Failed to open file" }
```

---

### 4. Read File Content
```
GET /api/fs/read?path=/path/to/file.txt
```
Reads file content as plain text (for editing in UI).

**Query Parameters:**
- `path` (required) - File path (must be text file)

**Response (200 OK):**
- Plain text content
- Content-Type: `text/plain; charset=UTF-8`

**Error Responses:**
```
400 Bad Request: "Binary files cannot be edited"
404 Not Found: "File not found"
500 Internal Error: "Failed to open file"
```

**Binary Files (Cannot Edit):**
- `.bin`, `.jpg`, `.jpeg`, `.png`, `.gif`, `.pdf`, `.zip`, `.gz`

---

### 5. Write File Content
```
POST /api/fs/write
Content-Type: application/json

{
  "path": "/data/config.json",
  "content": "{\"setting\": \"value\"}"
}
```
Saves edited file content. Updates existing files or creates new ones.

**Request Body (JSON):**
```json
{
  "path": "/path/to/file.txt",     // Required
  "content": "file content here"    // Required
}
```

**Response (200 OK):**
```json
{
  "success": true,
  "message": "File saved"
}
```

**Error Responses:**
```json
// 400 Bad Request
{ "success": false, "error": "Missing path" }
{ "success": false, "error": "Binary files cannot be edited" }

// 500 Internal Error
{ "success": false, "error": "Failed to open file for writing" }
```

---

### 6. Upload File
```
POST /api/fs/upload
Content-Type: multipart/form-data

[Binary file data]
```
Uploads one or multiple files using multipart form data.

**Request:**
- Form field: `file` (binary data)
- Filename determines destination path

**Response (200 OK):**
```json
{
  "success": true,
  "message": "File uploaded"
}
```

**Example using cURL:**
```bash
curl -F "file=@localfile.txt" http://192.168.1.100/api/fs/upload
```

**Example using JavaScript:**
```javascript
const formData = new FormData();
formData.append('file', fileInputElement.files[0]);
fetch('/api/fs/upload', {
  method: 'POST',
  body: formData
}).then(r => r.json()).then(console.log);
```

---

### 7. Delete File
```
POST /api/fs/delete
Content-Type: application/json

{
  "path": "/path/to/file.txt"
}
```
Deletes a single file or empty directory.

**Request Body (JSON):**
```json
{
  "path": "/logs/old_log.txt"  // Required
}
```

**Response (200 OK):**
```json
{
  "success": true,
  "message": "File deleted"
}
```

**Error Responses:**
```json
{ "success": false, "error": "Missing path" }
{ "success": false, "error": "File not found" }
{ "success": false, "error": "Failed to delete file" }
```

---

### 8. Clear All Files
```
POST /api/fs/clear
```
**WARNING:** Deletes ALL files and folders recursively!

**Response (200 OK):**
```json
{
  "success": true,
  "deletedCount": 42
}
```

---

## JSON Response Format

### Success Response (Standard)
```json
{
  "success": true,
  "message": "Operation completed"
}
```

### Error Response (Standard)
```json
{
  "success": false,
  "error": "Description of what went wrong"
}
```

### List Response (Special)
```json
{
  "files": [ /* hierarchical array */ ],
  "usedBytes": 12345,
  "totalBytes": 524288,
  "freeSpace": 511943
}
```

---

## Folder Structure Example

```
/
├── index.html            (1024 bytes)
├── style.css             (2048 bytes)
├── filesystem.html       (15360 bytes)
├── logs/                 (directory)
│   ├── log_20251102_0.txt
│   ├── log_20251102_1.txt
│   └── log_20251101_0.txt
└── data/                 (directory)
    ├── config.json
    ├── settings.json
    └── cache/            (directory)
        ├── temp.bin
        └── session.dat
```

When `/api/fs/list` is called, the response maintains this hierarchy with `"children"` arrays.

---

## Path Normalization Rules

1. **Automatic leading slash** - `"file.txt"` → `"/file.txt"`
2. **Remove double slashes** - `"//logs/file.txt"` → `"/logs/file.txt"`
3. **Relative to root** - All paths are absolute from filesystem root `/`

---

## Binary File Protection

The following extensions **cannot be edited** via the UI:
- `.bin` - Binary firmware
- `.jpg`, `.jpeg` - JPEG images
- `.png` - PNG images
- `.gif` - GIF images
- `.pdf` - PDF documents
- `.zip` - ZIP archives
- `.gz` - Gzip archives

Attempting to read/write binary files returns:
```json
{
  "success": false,
  "error": "Binary files cannot be edited"
}
```

---

## Usage Examples

### JavaScript / Fetch API

**List files:**
```javascript
fetch('/api/fs/list')
  .then(r => r.json())
  .then(data => {
    console.log('Total files:', data.files.length);
    console.log('Used space:', data.usedBytes, 'bytes');
  });
```

**Download file:**
```javascript
fetch('/api/fs/download?path=/logs/log.txt')
  .then(r => r.blob())
  .then(blob => {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'log.txt';
    a.click();
  });
```

**Edit and save file:**
```javascript
const content = "New file content";
fetch('/api/fs/write', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    path: '/data/config.json',
    content: content
  })
}).then(r => r.json()).then(console.log);
```

**Delete file:**
```javascript
fetch('/api/fs/delete', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ path: '/logs/old_log.txt' })
}).then(r => r.json()).then(console.log);
```

### cURL Examples

**List files:**
```bash
curl http://192.168.1.100/api/fs/list | jq
```

**Download file:**
```bash
curl http://192.168.1.100/api/fs/download?path=/logs/log.txt -o log.txt
```

**Save file:**
```bash
curl -X POST http://192.168.1.100/api/fs/write \
  -H "Content-Type: application/json" \
  -d '{"path":"/test.txt","content":"Hello"}'
```

**Delete file:**
```bash
curl -X POST http://192.168.1.100/api/fs/delete \
  -H "Content-Type: application/json" \
  -d '{"path":"/test.txt"}'
```

---

## Performance Considerations

- **Hierarchical recursion** - Efficiently traverses folders without flattening
- **Single pass** - `/api/fs/list` scans filesystem once
- **Large files** - Use streaming for downloads (not buffered in memory)
- **Memory efficient** - Uses `std::function` for stack-safe recursion
- **No limits** - Supports unlimited nesting depth

---

## Implementation Notes

The FilesystemManager is implemented in `include/FilesystemManager.h` as a C++ class:

```cpp
// Instantiation in setup()
FilesystemManager filesystemManager(server);

// Register all routes
filesystemManager.registerRoutes();

// Optional: Use helper methods
bool exists = filesystemManager.fileExists("/config.json");
long size = filesystemManager.getFileSize("/config.json");
uint32_t used, total, free;
filesystemManager.getFilesystemStats(used, total, free);
```

---

## HTTP Status Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 400 | Bad Request (missing/invalid params) |
| 404 | Not Found (file/path doesn't exist) |
| 500 | Internal Server Error |

---

**Last Updated:** November 2, 2025  
**Version:** 1.0 (Hierarchical Filesystem Structure)
