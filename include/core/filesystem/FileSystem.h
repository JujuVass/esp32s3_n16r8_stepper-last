// ============================================================================
// FILESYSTEM - LittleFS Operations & JSON Helpers
// ============================================================================
// LittleFS filesystem wrapper providing:
// - File CRUD operations (read, write, delete)
// - Directory management
// - Disk usage reporting
// - JSON file load/save helpers
// ============================================================================

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class FileSystem {
public:
  // ========================================================================
  // LIFECYCLE
  // ========================================================================

  FileSystem();

  /**
   * Mount LittleFS with multi-level safety (try mount → format → remount)
   * @return true if mounted, false if running in degraded mode
   */
  bool mount();

  // ========================================================================
  // FILESYSTEM STATE
  // ========================================================================

  /** Check if filesystem is mounted and ready */
  bool isReady() const { return _mounted; }

  // ========================================================================
  // FILE OPERATIONS
  // ========================================================================

  /** Check if file exists (not a directory) */
  bool fileExists(const String& path) const;

  /** Check if directory exists */
  bool directoryExists(const String& path) const;

  /**
   * Read file contents as string
   * @param path Absolute file path
   * @param maxSize Safety limit (default 1MB)
   * @return File contents, or empty string on error
   */
  String readFileAsString(const String& path, size_t maxSize = 1024 * 1024);

  /**
   * Write/overwrite file with string contents (flush + validate)
   * @return true if write successful
   */
  bool writeFileAsString(const String& path, const String& data);

  /** Delete file from filesystem */
  bool deleteFile(const String& path);

  /** Create directory (no-op if already exists) */
  bool createDirectory(const String& path);

  // ========================================================================
  // DISK USAGE
  // ========================================================================

  uint32_t getTotalBytes() const;
  uint32_t getUsedBytes() const;
  uint32_t getAvailableBytes() const { return getTotalBytes() - getUsedBytes(); }
  float getDiskUsagePercent() const;

  // ========================================================================
  // JSON HELPERS
  // ========================================================================

  /**
   * Load JSON from file (deserialize)
   * @param path File path (e.g., "/playlist.json")
   * @param doc JsonDocument to populate
   * @return true if successful
   */
  bool loadJsonFile(const String& path, JsonDocument& doc);

  /**
   * Save JSON to file (serialize + flush + verify)
   * @param path File path
   * @param doc JsonDocument to save
   * @return true if successful
   */
  bool saveJsonFile(const String& path, const JsonDocument& doc);

private:
  bool _mounted;
};

#endif // FILESYSTEM_H
