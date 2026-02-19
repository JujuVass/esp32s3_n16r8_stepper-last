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

/**
 * FilesystemManager - REST API for LittleFS file operations
 * 
 * Provides HTTP endpoints for file management:
 *   GET  /filesystem        - Serve filesystem.html UI
 *   GET  /api/fs/list       - Get file list (recursive, JSON)
 *   GET  /api/fs/download   - Download file
 *   GET  /api/fs/read       - Read file content (text only)
 *   POST /api/fs/write      - Save edited file
 *   POST /api/fs/upload     - Upload file (multipart form)
 *   POST /api/fs/delete     - Delete file
 *   POST /api/fs/clear      - Clear all files
 */
class FilesystemManager {
private:
  WebServer& server;

  // Binary file extensions that cannot be edited
  static const char* binaryExtensions[8];

  // Private helper methods
  bool isBinaryFile(const String& path);
  String normalizePath(String path);
  
  // JSON response helpers: uses free functions from APIRoutes.h (DRY)

  // Route handlers (called by registerRoutes lambdas)
  void handleListFiles();
  void handleDownloadFile();
  void handleReadFile();
  void handleWriteFile();
  void handleUploadFile();
  void handleDeleteFile();
  void handleClearAllFiles();

public:
  /**
   * Constructor
   * @param webServer Reference to WebServer instance (must be initialized)
   */
  FilesystemManager(WebServer& webServer);

  /**
   * Register all filesystem API routes with WebServer
   * Call this in setup() after server.begin()
   */
  void registerRoutes();

  // ==========================================================================
  // PUBLIC HELPER METHODS (can be used externally for custom operations)
  // ==========================================================================

  /**
   * Get MIME content type for a file path based on extension.
   * Static so it can be called without an instance (e.g., from APIRoutes).
   * @param path File path (only extension is checked)
   * @return MIME type string
   */
  static String getContentType(const String& path);

  /**
   * Check if file exists
   * @param path File path
   * @return true if exists, false otherwise
   */
  bool fileExists(const String& path);

  /**
   * Get file size
   * @param path File path
   * @return File size in bytes, or -1 if not found
   */
  long getFileSize(const String& path);

  /**
   * Create directory
   * @param path Directory path
   * @return true on success
   */
  bool createDirectory(const String& path);

  /**
   * Create parent directories for a path (like mkdir -p)
   * @param path Full file path (e.g., "/js/core/app.js")
   */
  void createParentDirs(const String& path);

  /**
   * Get filesystem stats with hierarchical calculation
   * @param usedBytes Output: bytes used
   * @param totalBytes Output: total bytes
   * @param freeBytes Output: free bytes
   */
  void getFilesystemStats(uint32_t& usedBytes, uint32_t& totalBytes, uint32_t& freeBytes);

  /**
   * Read entire file content
   * @param path File path
   * @return File content as String, or empty if not found
   */
  String readFileContent(const String& path);

  /**
   * Write file content
   * @param path File path
   * @param content Content to write
   * @return true on success
   */
  bool writeFileContent(const String& path, const String& content);
};

#endif // FILESYSTEM_MANAGER_H
