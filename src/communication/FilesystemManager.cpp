// ============================================================================
// FILESYSTEM MANAGER IMPLEMENTATION
// ============================================================================
// REST API for LittleFS file operations on ESP32
// ============================================================================

#include "communication/FilesystemManager.h"
#include "communication/APIRoutes.h"
#include "core/UtilityEngine.h"
#include "core/GlobalState.h"
#include <algorithm>

extern UtilityEngine* engine;

using namespace std;

// ============================================================================
// CONSTRUCTOR
// ============================================================================
FilesystemManager::FilesystemManager(AsyncWebServer& webServer) : server(webServer) {}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

bool FilesystemManager::isBinaryFile(const String& path) {
  return std::ranges::any_of(binaryExtensions, [&path](const char* ext) {
    return path.endsWith(ext);
  });
}

String FilesystemManager::getContentType(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=UTF-8";
  if (path.endsWith(".css")) return "text/css; charset=UTF-8";
  if (path.endsWith(".js")) return "application/javascript; charset=UTF-8";
  if (path.endsWith(".json")) return "application/json; charset=UTF-8";
  if (path.endsWith(".txt")) return "text/plain; charset=UTF-8";
  if (path.endsWith(".xml")) return "application/xml";
  if (path.endsWith(".pdf")) return "application/pdf";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".woff")) return "font/woff";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".log")) return "text/plain; charset=UTF-8";
  return "application/octet-stream";
}

String FilesystemManager::normalizePath(String path) {
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") >= 0) {
    path.replace("//", "/");
  }
  return path;
}


// ============================================================================
// ROUTE REGISTRATION
// ============================================================================

void FilesystemManager::registerRoutes() {
  // GET /filesystem - Serve filesystem.html from LittleFS
  server.on("/filesystem", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (LittleFS.exists("/filesystem.html")) {
      AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/filesystem.html", "text/html; charset=UTF-8");
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      request->send(response);
    } else {
      request->send(404, "text/plain", "File not found: filesystem.html");
    }
  });

  // GET /api/fs/list - List all files recursively
  server.on("/api/fs/list", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleListFiles(request);
  });

  // GET /api/fs/download - Download file
  server.on("/api/fs/download", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleDownloadFile(request);
  });

  // GET /api/fs/read - Read file content for editing
  server.on("/api/fs/read", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleReadFile(request);
  });

  // POST /api/fs/write - Save edited file (needs body collection)
  server.on("/api/fs/write", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      handleWriteFile(request);
    },
    NULL,       // no upload handler
    collectBody // body collector
  );

  // POST /api/fs/upload - Upload file (multipart)
  server.on("/api/fs/upload", HTTP_POST,
    // onRequest handler (called after upload completes)
    [this](AsyncWebServerRequest* request) {
      if (_uploadFailed) {
        sendJsonError(request, 500, "Upload failed: incomplete write");
        _uploadFailed = false;
      } else {
        sendJsonSuccess(request, "File uploaded");
      }
    },
    // onUpload handler (called for each chunk)
    [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool isFinal) {
      handleUploadFile(request, filename, index, data, len, isFinal);
    }
  );

  // POST /api/fs/delete - Delete file (needs body collection)
  server.on("/api/fs/delete", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      handleDeleteFile(request);
    },
    NULL,       // no upload handler
    collectBody // body collector
  );

  // POST /api/fs/clear - Clear all files (needs body collection)
  server.on("/api/fs/clear", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      handleClearAllFiles(request);
    },
    NULL,       // no upload handler
    collectBody // body collector
  );
}

// ============================================================================
// ROUTE HANDLERS
// ============================================================================

// Helper function for recursive directory listing (extracted from lambda - S1188)
static void listDirRecursive(const char* dirname, const JsonArray& parentArray, uint32_t& usedBytes) {
  File root = LittleFS.open(dirname);
  if (!root || !root.isDirectory()) {
    return;
  }

  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    auto fileName = String(file.name());
    String path = String(dirname) + "/" + fileName;
    if (path.startsWith("//")) path = path.substring(1);

    JsonObject fileObj = parentArray.add<JsonObject>();
    fileObj["name"] = fileName;
    fileObj["path"] = path;
    fileObj["size"] = (int)file.size();
    fileObj["time"] = (int)file.getLastWrite();
    fileObj["isDir"] = file.isDirectory();

    if (!file.isDirectory()) {
      usedBytes += file.size();
    } else {
      JsonArray childrenArray = fileObj["children"].to<JsonArray>();
      listDirRecursive(path.c_str(), childrenArray, usedBytes);
    }
  }
  root.close();
}

void FilesystemManager::handleListFiles(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray filesArray = doc["files"].to<JsonArray>();

  uint32_t usedBytes = 0;
  uint32_t totalBytes = LittleFS.totalBytes();

  // Start recursion from root
  listDirRecursive("/", filesArray, usedBytes);

  doc["usedBytes"] = usedBytes;
  doc["totalBytes"] = totalBytes;
  doc["freeSpace"] = totalBytes - usedBytes;

  sendJsonDoc(request, doc);
}

void FilesystemManager::handleDownloadFile(AsyncWebServerRequest* request) {
  if (!request->hasParam("path")) {
    sendJsonError(request, 400, "Missing path parameter");
    return;
  }

  String path = normalizePath(request->getParam("path")->value());

  if (!LittleFS.exists(path)) {
    sendJsonError(request, 404, "File not found");
    return;
  }

  String filename = path.substring(path.lastIndexOf('/') + 1);
  AsyncWebServerResponse* response = request->beginResponse(LittleFS, path, getContentType(path), true);
  response->addHeader("Content-Disposition", "attachment; filename=" + filename);
  request->send(response);
}

void FilesystemManager::handleReadFile(AsyncWebServerRequest* request) {
  if (!request->hasParam("path")) {
    request->send(400, "text/plain", "Missing path");
    return;
  }

  String path = normalizePath(request->getParam("path")->value());

  if (isBinaryFile(path)) {
    request->send(400, "text/plain", "Binary files cannot be edited");
    return;
  }

  if (!LittleFS.exists(path)) {
    request->send(404, "text/plain", "File not found");
    return;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    request->send(500, "text/plain", "Failed to open file");
    return;
  }

  String content = file.readString();
  file.close();

  request->send(200, "text/plain; charset=UTF-8", content);
}

void FilesystemManager::handleWriteFile(AsyncWebServerRequest* request) {
  JsonDocument doc;
  if (!parseJsonBody(request, doc)) return;

  String path = normalizePath(doc["path"].as<String>());
  String content = doc["content"].as<String>();

  if (path.isEmpty()) {
    sendJsonError(request, 400, "Missing path");
    return;
  }

  if (isBinaryFile(path)) {
    sendJsonError(request, 400, "Binary files cannot be edited");
    return;
  }

  File file = LittleFS.open(path, "w");
  if (!file) {
    sendJsonError(request, 500, "Failed to open file for writing");
    return;
  }

  size_t written = file.print(content);

  // 🛡️ PROTECTION: Flush to ensure data written to flash
  file.flush();

  // 🛡️ VALIDATION: Verify write completed
  if (!file) {
    sendJsonError(request, 500, "File corrupted during write (flush failed)");
    return;
  }

  file.close();

  // 🛡️ CHECK: Verify expected bytes written
  if (written != content.length()) {
    String errorMsg = "Incomplete write: " + String(written) + "/" + String(content.length()) + " bytes";
    sendJsonError(request, 500, errorMsg.c_str());
    return;
  }

  sendJsonSuccess(request, "File saved");
}

void FilesystemManager::handleUploadFile(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool isFinal) {
  static File uploadFile;
  static size_t totalWritten = 0;

  if (index == 0) {
    // UPLOAD_FILE_START equivalent
    // Track upload activity for UI overlay & light WS mode
    lastUploadActivityTime = millis();

    String normalizedFilename = normalizePath(filename);

    // 🛡️ PROTECTION: Check available space BEFORE accepting upload
    const size_t contentLength = request->contentLength();
    const size_t available = LittleFS.totalBytes() - LittleFS.usedBytes();

    if (contentLength > 0 && contentLength > available) {
      if (engine) engine->error("Upload rejected: file too large (" + String(contentLength) + " bytes needed, only " + String(available) + " bytes available)");
      else Serial.printf("Upload rejected: file too large (%d bytes needed, only %d bytes available)\n",
                   contentLength, available);
      _uploadFailed = true;
      return;
    }

    // Create parent directories if needed
    createParentDirs(normalizedFilename);

    uploadFile = LittleFS.open(normalizedFilename, "w");
    totalWritten = 0;
    if (!uploadFile) {
      _uploadFailed = true;
    }
  }

  if (uploadFile && len > 0) {
    // UPLOAD_FILE_WRITE equivalent
    size_t written = uploadFile.write(data, len);
    totalWritten += written;

    // 🛡️ PROTECTION: Verify all bytes were written
    if (written != len) {
      uploadFile.close();
      uploadFile = File();  // Invalidate to prevent further writes
      _uploadFailed = true;
    }

    // 🛡️ WATCHDOG: Yield CPU between chunks for system responsiveness
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (isFinal) {
    // UPLOAD_FILE_END equivalent
    if (uploadFile) {
      // 🛡️ PROTECTION: Flush before closing
      uploadFile.flush();
      uploadFile.close();

      // 🛡️ STABILITY: Let LittleFS GC + TCP stack settle before next request
      vTaskDelay(pdMS_TO_TICKS(UPLOAD_POST_CLOSE_DELAY_MS));
    }
    // Keep upload activity timestamp fresh for batch detection
    lastUploadActivityTime = millis();
  }
}

void FilesystemManager::handleDeleteFile(AsyncWebServerRequest* request) {
  JsonDocument doc;
  if (!parseJsonBody(request, doc)) return;

  String path = normalizePath(doc["path"].as<String>());

  if (path.isEmpty()) {
    sendJsonError(request, 400, "Missing path");
    return;
  }

  if (!LittleFS.exists(path)) {
    sendJsonError(request, 404, "File not found");
    return;
  }

  if (LittleFS.remove(path)) {
    sendJsonSuccess(request, "File deleted");
  } else {
    sendJsonError(request, 500, "Failed to delete file");
  }
}

void FilesystemManager::handleClearAllFiles(AsyncWebServerRequest* request) {
  int deletedCount = 0;

  // Recursive lambda for directory traversal
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
  sendJsonDoc(request, doc);
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


