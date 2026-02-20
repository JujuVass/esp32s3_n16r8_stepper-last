// ============================================================================
// FILESYSTEM IMPLEMENTATION
// ============================================================================

#include "core/filesystem/FileSystem.h"
#include "core/UtilityEngine.h"

// Forward declaration — engine is set after UtilityEngine constructor
extern UtilityEngine* engine;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

FileSystem::FileSystem()
  : _mounted(false) {}

// ============================================================================
// MOUNT
// ============================================================================

bool FileSystem::mount() {
  if (engine) engine->info("Attempting LittleFS mount with safety checks...");
  else Serial.println("[FileSystem] Attempting LittleFS mount with safety checks...");

  // STEP 1: Try mounting WITHOUT auto-format (safer)
  _mounted = LittleFS.begin(false);

  if (!_mounted) {
    if (engine) engine->warn("LittleFS mount failed - filesystem may be corrupted");
    else Serial.println("[FileSystem] LittleFS mount failed - filesystem may be corrupted");

    // STEP 2: Try manual format in controlled way
    if (engine) engine->info("Attempting manual LittleFS format...");
    else Serial.println("[FileSystem] Attempting manual LittleFS format...");

    if (LittleFS.format()) {
      if (engine) engine->info("Format successful, remounting...");
      else Serial.println("[FileSystem] Format successful, remounting...");

      // STEP 3: Try mounting again after format
      _mounted = LittleFS.begin(false);

      if (_mounted) {
        if (engine) engine->info("LittleFS mounted after format");
        else Serial.println("[FileSystem] LittleFS mounted after format");
      } else {
        if (engine) engine->error("CRITICAL: LittleFS still won't mount after format! Running in DEGRADED mode");
        else {
          Serial.println("[FileSystem] CRITICAL: LittleFS still won't mount after format!");
          Serial.println("[FileSystem] Running in DEGRADED mode (no filesystem)");
        }
      }
    } else {
      if (engine) engine->error("Format failed - hardware issue or severe corruption. Running in DEGRADED mode");
      else {
        Serial.println("[FileSystem] Format failed - hardware issue or severe corruption");
        Serial.println("[FileSystem] Running in DEGRADED mode (no filesystem)");
      }
    }
  } else {
    if (engine) engine->info("LittleFS mounted successfully");
    else Serial.println("[FileSystem] LittleFS mounted successfully");
  }

  // STEP 4: If mounted, verify filesystem health
  if (_mounted) {
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    String msg = "LittleFS: " + String(totalBytes / 1024) + " KB total, " +
                 String(usedBytes / 1024) + " KB used (" +
                 String((usedBytes * 100.0f) / totalBytes, 1) + "%)";
    if (engine) engine->info(msg);
    else Serial.println("[FileSystem] " + msg);
  }

  return _mounted;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

bool FileSystem::fileExists(const String& path) const {
  if (!_mounted) return false;

  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  bool isFile = !f.isDirectory();
  f.close();
  return isFile;
}

bool FileSystem::directoryExists(const String& path) const {
  if (!_mounted) return false;

  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  bool isDir = f.isDirectory();
  f.close();
  return isDir;
}

String FileSystem::readFileAsString(const String& path, size_t maxSize) {
  if (!_mounted || !fileExists(path)) {
    return "";
  }

  File file = LittleFS.open(path, "r");
  if (!file) return "";

  // Limit read size for safety
  size_t readSize = min((size_t)file.size(), maxSize);
  
  // Bulk read for performance
  char* buf = new (std::nothrow) char[readSize + 1];
  if (!buf) {
    file.close();
    return "";
  }
  
  size_t bytesRead = file.readBytes(buf, readSize);
  buf[bytesRead] = '\0';
  String content(buf);
  delete[] buf;

  file.close();
  return content;
}

bool FileSystem::writeFileAsString(const String& path, const String& data) {
  if (!_mounted) return false;

  File file = LittleFS.open(path, "w");
  if (!file) {
    if (engine) engine->error("Failed to open file for writing: " + path);
    else Serial.println("[FileSystem] Failed to open file for writing: " + path);
    return false;
  }

  size_t written = file.print(data);

  // Flush to ensure data is written before closing
  file.flush();
  file.close();

  // Verify write completed successfully
  if (written != data.length()) {
    if (engine) engine->warn("Write incomplete: " + String(written) + "/" + String(data.length()) + " bytes to " + path);
    else Serial.println("[FileSystem] Write incomplete: " + String(written) + "/" + String(data.length()) + " bytes to " + path);
    return false;
  }

  return true;
}

bool FileSystem::deleteFile(const String& path) {
  if (!_mounted || !fileExists(path)) return false;
  return LittleFS.remove(path);
}

bool FileSystem::createDirectory(const String& path) {
  if (!_mounted) return false;
  if (directoryExists(path)) return true;  // Already exists
  return LittleFS.mkdir(path);
}

// ============================================================================
// DISK USAGE
// ============================================================================

uint32_t FileSystem::getTotalBytes() const {
  if (!_mounted) return 0;
  return LittleFS.totalBytes();
}

uint32_t FileSystem::getUsedBytes() const {
  if (!_mounted) return 0;
  return LittleFS.usedBytes();
}

float FileSystem::getDiskUsagePercent() const {
  uint32_t total = getTotalBytes();
  if (total == 0) return 0.0f;
  uint32_t used = getUsedBytes();
  return (used * 100.0f) / total;
}

// ============================================================================
// JSON HELPERS
// ============================================================================

bool FileSystem::loadJsonFile(const String& path, JsonDocument& doc) {
  if (!fileExists(path)) {
    if (engine) engine->error("JSON file not found: " + path);
    else Serial.println("[FileSystem] JSON file not found: " + path);
    return false;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    if (engine) engine->error("Failed to open file for reading: " + path);
    else Serial.println("[FileSystem] Failed to open file for reading: " + path);
    return false;
  }

  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    if (engine) engine->error("JSON parse error in " + path + ": " + String(err.c_str()));
    else Serial.println("[FileSystem] JSON parse error in " + path + ": " + String(err.c_str()));
    return false;
  }

  return true;
}

bool FileSystem::saveJsonFile(const String& path, const JsonDocument& doc) {
  // Ensure parent directory exists
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = path.substring(0, lastSlash);
    createDirectory(dir);
  }

  File file = LittleFS.open(path, "w");
  if (!file) {
    if (engine) engine->error("Failed to open file for writing: " + path);
    else Serial.println("[FileSystem] Failed to open file for writing: " + path);
    return false;
  }

  size_t bytesWritten = serializeJson(doc, file);

  // Flush before close
  file.flush();

  // Check file handle is still valid after flush
  if (!file) {
    if (engine) engine->error("CRITICAL: File corrupted during JSON write: " + path);
    else Serial.println("[FileSystem] CRITICAL: File corrupted during JSON write: " + path);
    return false;
  }

  file.close();

  // Verify bytes were actually written
  if (bytesWritten == 0) {
    if (engine) engine->error("Failed to write JSON to: " + path);
    else Serial.println("[FileSystem] Failed to write JSON to: " + path);
    return false;
  }

  // Paranoid check: verify file exists after write
  if (!fileExists(path)) {
    if (engine) engine->error("CRITICAL: JSON file vanished after write: " + path);
    else Serial.println("[FileSystem] CRITICAL: JSON file vanished after write: " + path);
    return false;
  }

  return true;
}