// ============================================================================
// FILESYSTEM MANAGER - Reusable library for ESP32 LittleFS management
// ============================================================================
// Features:
//   - RESTful API for file operations (list, read, write, upload, delete, clear)
//   - Recursive directory traversal with std::function
//   - Binary file detection (prevents editing .bin, .jpg, .png, .pdf, .zip)
//   - JSON response helpers (error, success, data)
//   - Automatic content-type detection for downloads
//   - WebServer integration (requires WebServer.h)
//   - ArduinoJson integration (requires ArduinoJson.h)
//   - LittleFS support (requires LittleFS.h)
//
// Usage:
//   1. Include this header in your project
//   2. Create FilesystemManager instance: FilesystemManager fsManager(server);
//   3. Call fsManager.registerRoutes(); in setup()
//   4. Optional: Use helper methods for custom filesystem operations
//
// ============================================================================

#ifndef FILESYSTEM_MANAGER_H
#define FILESYSTEM_MANAGER_H

#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <functional>

using namespace std;

class FilesystemManager {
private:
  WebServer& server;
  
  // Binary file extensions that cannot be edited
  const char* binaryExtensions[8] = {
    ".bin", ".jpg", ".jpeg", ".png", ".gif", ".pdf", ".zip", ".gz"
  };
  
  /**
   * Check if file is binary (not editable)
   * @param path File path (e.g., "/data/file.bin")
   * @return true if binary, false if text
   */
  bool isBinaryFile(const String& path) {
    for (int i = 0; i < 8; i++) {
      if (path.endsWith(binaryExtensions[i])) {
        return true;
      }
    }
    return false;
  }
  
  /**
   * Detect content type from file extension
   * @param path File path
   * @return Content-Type string (e.g., "text/html; charset=UTF-8")
   */
  String getContentType(const String& path) {
    if (path.endsWith(".html")) return "text/html; charset=UTF-8";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".txt")) return "text/plain; charset=UTF-8";
    if (path.endsWith(".xml")) return "application/xml";
    if (path.endsWith(".pdf")) return "application/pdf";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif")) return "image/gif";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".woff")) return "font/woff";
    if (path.endsWith(".woff2")) return "font/woff2";
    return "application/octet-stream";
  }
  
  /**
   * Normalize path (ensure leading slash, no double slashes)
   * @param path Raw path
   * @return Normalized path
   */
  String normalizePath(String path) {
    if (!path.startsWith("/")) path = "/" + path;
    while (path.indexOf("//") >= 0) {
      path.replace("//", "/");
    }
    return path;
  }
  
public:
  /**
   * Constructor
   * @param webServer Reference to WebServer instance (must be initialized)
   */
  FilesystemManager(WebServer& webServer) : server(webServer) {}
  
  /**
   * Register all filesystem API routes with WebServer
   * Call this in setup() after server.begin()
   * 
   * Routes registered:
   *   GET  /filesystem           - Serve filesystem.html UI
   *   GET  /api/fs/list          - Get file list (recursive, JSON)
   *   GET  /api/fs/download      - Download file
   *   GET  /api/fs/read          - Read file content (text only)
   *   POST /api/fs/write         - Save edited file
   *   POST /api/fs/upload        - Upload file (multipart form)
   *   POST /api/fs/delete        - Delete file
   *   POST /api/fs/clear         - Clear all files
   */
  void registerRoutes() {
    // GET /filesystem - Serve filesystem.html from LittleFS
    server.on("/filesystem", HTTP_GET, [this]() {
      if (LittleFS.exists("/filesystem.html")) {
        File file = LittleFS.open("/filesystem.html", "r");
        if (file) {
          server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
          server.streamFile(file, "text/html; charset=UTF-8");
          file.close();
        } else {
          server.send(500, "text/plain", "Error opening filesystem.html");
        }
      } else {
        server.send(404, "text/plain", "File not found: filesystem.html");
      }
    });
    
    // GET /api/fs/list - List all files recursively
    server.on("/api/fs/list", HTTP_GET, [this]() {
      listFilesRecursive();
    });
    
    // GET /api/fs/download - Download file
    server.on("/api/fs/download", HTTP_GET, [this]() {
      downloadFile();
    });
    
    // GET /api/fs/read - Read file content for editing
    server.on("/api/fs/read", HTTP_GET, [this]() {
      readFile();
    });
    
    // POST /api/fs/write - Save edited file
    server.on("/api/fs/write", HTTP_POST, [this]() {
      writeFile();
    });
    
    // POST /api/fs/upload - Upload file (multipart)
    server.on("/api/fs/upload", HTTP_POST, 
      [this]() {
        sendJsonSuccess("File uploaded");
      },
      [this]() {
        uploadFile();
      }
    );
    
    // POST /api/fs/delete - Delete file
    server.on("/api/fs/delete", HTTP_POST, [this]() {
      deleteFile();
    });
    
    // POST /api/fs/clear - Clear all files
    server.on("/api/fs/clear", HTTP_POST, [this]() {
      clearAllFiles();
    });
  }
  
  /**
   * Get list of files recursively as JSON with folder hierarchy
   * Returns nested structure: folders contain files and subfolders
   * Used by /api/fs/list route
   */
  void listFilesRecursive() {
    JsonDocument doc;
    JsonArray filesArray = doc["files"].to<JsonArray>();
    
    uint32_t usedBytes = 0;
    uint32_t totalBytes = LittleFS.totalBytes();
    
    // Recursive lambda that builds hierarchical structure
    function<void(const char*, JsonArray&)> listDir = [&](const char* dirname, JsonArray& parentArray) {
      File root = LittleFS.open(dirname);
      if (!root || !root.isDirectory()) {
        return;
      }
      
      File file = root.openNextFile();
      while (file) {
        String fileName = String(file.name());
        String path = String(dirname) + "/" + fileName;
        if (path.startsWith("//")) path = path.substring(1);
        
        // Create object for this file/folder
        JsonObject fileObj = parentArray.add<JsonObject>();
        fileObj["name"] = fileName;
        fileObj["path"] = path;
        fileObj["size"] = (int)file.size();
        fileObj["time"] = (int)file.getLastWrite();
        fileObj["isDir"] = file.isDirectory();
        
        // Count bytes only for files (not directories)
        if (!file.isDirectory()) {
          usedBytes += file.size();
        }
        
        // If it's a directory, add nested "children" array and recurse
        if (file.isDirectory()) {
          JsonArray childrenArray = fileObj["children"].to<JsonArray>();
          listDir(path.c_str(), childrenArray);
        }
        
        file = root.openNextFile();
      }
      root.close();
    };
    
    // Start recursion from root
    listDir("/", filesArray);
    
    doc["usedBytes"] = usedBytes;
    doc["totalBytes"] = totalBytes;
    doc["freeSpace"] = totalBytes - usedBytes;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }
  
  /**
   * Download file with appropriate content-type
   * Query param: path (required)
   */
  void downloadFile() {
    if (!server.hasArg("path")) {
      sendJsonError(400, "Missing path parameter");
      return;
    }
    
    String path = normalizePath(server.arg("path"));
    
    if (!LittleFS.exists(path)) {
      sendJsonError(404, "File not found");
      return;
    }
    
    File file = LittleFS.open(path, "r");
    if (!file) {
      sendJsonError(500, "Failed to open file");
      return;
    }
    
    String filename = path.substring(path.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
    server.streamFile(file, getContentType(path));
    file.close();
  }
  
  /**
   * Read file content (text only, prevents binary editing)
   * Query param: path (required)
   */
  void readFile() {
    if (!server.hasArg("path")) {
      server.send(400, "text/plain", "Missing path");
      return;
    }
    
    String path = normalizePath(server.arg("path"));
    
    if (isBinaryFile(path)) {
      server.send(400, "text/plain", "Binary files cannot be edited");
      return;
    }
    
    if (!LittleFS.exists(path)) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    
    File file = LittleFS.open(path, "r");
    if (!file) {
      server.send(500, "text/plain", "Failed to open file");
      return;
    }
    
    String content = file.readString();
    file.close();
    
    server.send(200, "text/plain; charset=UTF-8", content);
  }
  
  /**
   * Write file content (from editor)
   * JSON body: {"path": "...", "content": "..."}
   */
  void writeFile() {
    if (!server.hasArg("plain")) {
      sendJsonApiError(400, "Missing JSON body");
      return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      sendJsonApiError(400, "Invalid JSON");
      return;
    }
    
    String path = normalizePath(doc["path"].as<String>());
    String content = doc["content"].as<String>();
    
    if (path.isEmpty()) {
      sendJsonApiError(400, "Missing path");
      return;
    }
    
    if (isBinaryFile(path)) {
      sendJsonApiError(400, "Binary files cannot be edited");
      return;
    }
    
    File file = LittleFS.open(path, "w");
    if (!file) {
      sendJsonApiError(500, "Failed to open file for writing");
      return;
    }
    
    file.print(content);
    file.close();
    
    sendJsonSuccess("File saved");
  }
  
  /**
   * Upload file (multipart form data)
   * Called by server as upload handler
   */
  void uploadFile() {
    HTTPUpload& upload = server.upload();
    static File uploadFile;
    
    if (upload.status == UPLOAD_FILE_START) {
      String filename = normalizePath(upload.filename);
      uploadFile = LittleFS.open(filename, "w");
      if (!uploadFile) {
        // Error will be caught on WRITE attempt
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (uploadFile) {
        uploadFile.write(upload.buf, upload.currentSize);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (uploadFile) {
        uploadFile.close();
      }
    }
  }
  
  /**
   * Delete single file
   * JSON body: {"path": "..."}
   */
  void deleteFile() {
    if (!server.hasArg("plain")) {
      sendJsonApiError(400, "Missing JSON body");
      return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      sendJsonApiError(400, "Invalid JSON");
      return;
    }
    
    String path = normalizePath(doc["path"].as<String>());
    
    if (path.isEmpty()) {
      sendJsonApiError(400, "Missing path");
      return;
    }
    
    if (!LittleFS.exists(path)) {
      sendJsonApiError(404, "File not found");
      return;
    }
    
    if (LittleFS.remove(path)) {
      sendJsonSuccess("File deleted");
    } else {
      sendJsonApiError(500, "Failed to delete file");
    }
  }
  
  /**
   * Clear all files from filesystem (recursive delete)
   */
  void clearAllFiles() {
    int deletedCount = 0;
    
    // Recursive lambda using std::function
    function<void(const char*)> clearDir = [&](const char* dirname) {
      File root = LittleFS.open(dirname);
      if (!root || !root.isDirectory()) return;
      
      File file = root.openNextFile();
      while (file) {
        String path = String(dirname) + "/" + String(file.name());
        if (path.startsWith("//")) path = path.substring(1);
        
        if (file.isDirectory()) {
          clearDir(path.c_str());
        } else {
          file.close();
          if (LittleFS.remove(path)) {
            deletedCount++;
          }
        }
        
        file = root.openNextFile();
      }
    };
    
    clearDir("/");
    
    JsonDocument doc;
    doc["success"] = true;
    doc["deletedCount"] = deletedCount;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }
  
  // =========================================================================
  // HELPER METHODS (can be used externally for custom operations)
  // =========================================================================
  
  /**
   * Check if file exists
   * @param path File path
   * @return true if exists, false otherwise
   */
  bool fileExists(const String& path) {
    return LittleFS.exists(normalizePath(path));
  }
  
  /**
   * Get file size
   * @param path File path
   * @return File size in bytes, or -1 if not found
   */
  long getFileSize(const String& path) {
    String normalizedPath = normalizePath(path);
    if (!LittleFS.exists(normalizedPath)) return -1;
    
    File file = LittleFS.open(normalizedPath, "r");
    if (!file) return -1;
    
    long size = file.size();
    file.close();
    return size;
  }
  
  /**
   * Create directory
   * @param path Directory path
   * @return true on success
   */
  bool createDirectory(const String& path) {
    return LittleFS.mkdir(normalizePath(path));
  }
  
  /**
   * Get filesystem stats with hierarchical calculation
   * @param usedBytes Output: bytes used
   * @param totalBytes Output: total bytes
   * @param freeBytes Output: free bytes
   */
  void getFilesystemStats(uint32_t& usedBytes, uint32_t& totalBytes, uint32_t& freeBytes) {
    totalBytes = LittleFS.totalBytes();
    // Calculate used bytes by scanning filesystem (files only, not directories)
    usedBytes = 0;
    
    function<void(const char*)> calculateUsage = [&](const char* dirname) {
      File root = LittleFS.open(dirname);
      if (!root || !root.isDirectory()) return;
      
      File file = root.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          // Only count file sizes, not directory entries
          usedBytes += file.size();
        } else {
          // Recurse into subdirectories
          String path = String(dirname) + "/" + String(file.name());
          if (path.startsWith("//")) path = path.substring(1);
          calculateUsage(path.c_str());
        }
        file = root.openNextFile();
      }
    };
    
    calculateUsage("/");
    freeBytes = totalBytes - usedBytes;
  }
  
  /**
   * Read entire file content
   * @param path File path
   * @return File content as String, or empty if not found
   */
  String readFileContent(const String& path) {
    String normalizedPath = normalizePath(path);
    if (!LittleFS.exists(normalizedPath)) return "";
    
    File file = LittleFS.open(normalizedPath, "r");
    if (!file) return "";
    
    String content = file.readString();
    file.close();
    return content;
  }
  
  /**
   * Write file content
   * @param path File path
   * @param content Content to write
   * @return true on success
   */
  bool writeFileContent(const String& path, const String& content) {
    String normalizedPath = normalizePath(path);
    File file = LittleFS.open(normalizedPath, "w");
    if (!file) return false;
    
    bool success = (file.print(content) == content.length());
    file.close();
    return success;
  }
  
private:
  /**
   * Send JSON error response
   * @param code HTTP status code
   * @param message Error message
   */
  void sendJsonError(int code, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    String response;
    serializeJson(doc, response);
    server.send(code, "application/json", response);
  }
  
  /**
   * Send JSON API error response (with success=false)
   * @param code HTTP status code
   * @param message Error message
   */
  void sendJsonApiError(int code, const char* message) {
    JsonDocument doc;
    doc["success"] = false;
    doc["error"] = message;
    String response;
    serializeJson(doc, response);
    server.send(code, "application/json", response);
  }
  
  /**
   * Send JSON success response
   * @param message Optional success message
   */
  void sendJsonSuccess(const char* message = nullptr) {
    JsonDocument doc;
    doc["success"] = true;
    if (message) {
      doc["message"] = message;
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }
};

#endif // FILESYSTEM_MANAGER_H
