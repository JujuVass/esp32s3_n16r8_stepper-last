// ============================================================================
// FILESYSTEM MANAGER IMPLEMENTATION
// ============================================================================
// REST API for LittleFS file operations on ESP32
// ============================================================================

#include "communication/FilesystemManager.h"

using namespace std;

// ============================================================================
// STATIC DATA
// ============================================================================
const char* FilesystemManager::binaryExtensions[8] = {
  ".bin", ".jpg", ".jpeg", ".png", ".gif", ".pdf", ".zip", ".gz"
};

// ============================================================================
// CONSTRUCTOR
// ============================================================================
FilesystemManager::FilesystemManager(WebServer& webServer) : server(webServer) {}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

bool FilesystemManager::isBinaryFile(const String& path) {
  for (int i = 0; i < 8; i++) {
    if (path.endsWith(binaryExtensions[i])) {
      return true;
    }
  }
  return false;
}

String FilesystemManager::getContentType(const String& path) {
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

String FilesystemManager::normalizePath(String path) {
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") >= 0) {
    path.replace("//", "/");
  }
  return path;
}

void FilesystemManager::sendJsonError(int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  String response;
  serializeJson(doc, response);
  server.send(code, "application/json", response);
}

void FilesystemManager::sendJsonApiError(int code, const char* message) {
  JsonDocument doc;
  doc["success"] = false;
  doc["error"] = message;
  String response;
  serializeJson(doc, response);
  server.send(code, "application/json", response);
}

void FilesystemManager::sendJsonSuccess(const char* message) {
  JsonDocument doc;
  doc["success"] = true;
  if (message) {
    doc["message"] = message;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ============================================================================
// ROUTE REGISTRATION
// ============================================================================

void FilesystemManager::registerRoutes() {
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
    handleListFiles();
  });

  // GET /api/fs/download - Download file
  server.on("/api/fs/download", HTTP_GET, [this]() {
    handleDownloadFile();
  });

  // GET /api/fs/read - Read file content for editing
  server.on("/api/fs/read", HTTP_GET, [this]() {
    handleReadFile();
  });

  // POST /api/fs/write - Save edited file
  server.on("/api/fs/write", HTTP_POST, [this]() {
    handleWriteFile();
  });

  // POST /api/fs/upload - Upload file (multipart)
  server.on("/api/fs/upload", HTTP_POST,
    [this]() {
      sendJsonSuccess("File uploaded");
    },
    [this]() {
      handleUploadFile();
    }
  );

  // POST /api/fs/delete - Delete file
  server.on("/api/fs/delete", HTTP_POST, [this]() {
    handleDeleteFile();
  });

  // POST /api/fs/clear - Clear all files
  server.on("/api/fs/clear", HTTP_POST, [this]() {
    handleClearAllFiles();
  });
}

// ============================================================================
// ROUTE HANDLERS
// ============================================================================

void FilesystemManager::handleListFiles() {
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

void FilesystemManager::handleDownloadFile() {
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

void FilesystemManager::handleReadFile() {
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

void FilesystemManager::handleWriteFile() {
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

  size_t written = file.print(content);
  
  // 🛡️ PROTECTION: Flush to ensure data written to flash
  file.flush();
  
  // 🛡️ VALIDATION: Verify write completed
  if (!file) {
    sendJsonApiError(500, "File corrupted during write (flush failed)");
    return;
  }
  
  file.close();
  
  // 🛡️ CHECK: Verify expected bytes written
  if (written != content.length()) {
    String errorMsg = "Incomplete write: " + String(written) + "/" + String(content.length()) + " bytes";
    sendJsonApiError(500, errorMsg.c_str());
    return;
  }

  sendJsonSuccess("File saved");
}

void FilesystemManager::handleUploadFile() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;
  static size_t totalWritten = 0;

  if (upload.status == UPLOAD_FILE_START) {
    String filename = normalizePath(upload.filename);

    // Create parent directories if needed
    createParentDirs(filename);

    uploadFile = LittleFS.open(filename, "w");
    totalWritten = 0;
    if (!uploadFile) {
      // Error will be caught on WRITE attempt
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      totalWritten += written;
      
      // 🛡️ PROTECTION: Verify all bytes were written
      if (written != upload.currentSize) {
        uploadFile.close();
        uploadFile = File();  // Invalidate to prevent further writes
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      // 🛡️ PROTECTION: Flush before closing
      uploadFile.flush();
      uploadFile.close();
    }
  }
}

void FilesystemManager::handleDeleteFile() {
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

void FilesystemManager::handleClearAllFiles() {
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

// ============================================================================
// PUBLIC HELPER METHODS
// ============================================================================

bool FilesystemManager::fileExists(const String& path) {
  return LittleFS.exists(normalizePath(path));
}

long FilesystemManager::getFileSize(const String& path) {
  String normalizedPath = normalizePath(path);
  if (!LittleFS.exists(normalizedPath)) return -1;

  File file = LittleFS.open(normalizedPath, "r");
  if (!file) return -1;

  long size = file.size();
  file.close();
  return size;
}

bool FilesystemManager::createDirectory(const String& path) {
  return LittleFS.mkdir(normalizePath(path));
}

void FilesystemManager::createParentDirs(const String& path) {
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash <= 0) return;  // No parent dir or root

  String dirPath = path.substring(0, lastSlash);

  // Check each level and create if needed
  String currentPath = "";
  int start = 1;  // Skip leading /

  while (start < (int)dirPath.length()) {
    int nextSlash = dirPath.indexOf('/', start);
    if (nextSlash == -1) nextSlash = dirPath.length();

    currentPath = dirPath.substring(0, nextSlash);

    if (!LittleFS.exists(currentPath)) {
      LittleFS.mkdir(currentPath);
    }

    start = nextSlash + 1;
  }
}

void FilesystemManager::getFilesystemStats(uint32_t& usedBytes, uint32_t& totalBytes, uint32_t& freeBytes) {
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

String FilesystemManager::readFileContent(const String& path) {
  String normalizedPath = normalizePath(path);
  if (!LittleFS.exists(normalizedPath)) return "";

  File file = LittleFS.open(normalizedPath, "r");
  if (!file) return "";

  String content = file.readString();
  file.close();
  return content;
}

bool FilesystemManager::writeFileContent(const String& path, const String& content) {
  String normalizedPath = normalizePath(path);
  File file = LittleFS.open(normalizedPath, "w");
  if (!file) return false;

  size_t written = file.print(content);
  
  // 🛡️ PROTECTION: Flush before closing
  file.flush();
  
  // 🛡️ VALIDATION: Check file still valid and write succeeded
  bool success = (file && written == content.length());
  
  file.close();
  return success;
}
