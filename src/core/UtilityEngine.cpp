// ============================================================================
// UTILITY ENGINE IMPLEMENTATION
// ============================================================================

#include "core/UtilityEngine.h"
#include "core/Types.h"
#include "core/Config.h"       // For STEPS_PER_MM
#include "core/GlobalState.h"  // For stats (StatsTracking)
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <math.h>

// EEPROM addresses for preferences
#define EEPROM_SIZE 128   // Increased: 0-3 for logging/stats/sensors, 4-100 for WiFi config
#define EEPROM_ADDR_LOGGING_ENABLED 0
#define EEPROM_ADDR_LOG_LEVEL 1
#define EEPROM_ADDR_STATS_ENABLED 2  // Stats recording enabled (1=enabled, 0=disabled)
#define EEPROM_ADDR_SENSORS_INVERTED 3  // Sensors inverted (0=normal, 1=inverted)
#define EEPROM_ADDR_CHECKSUM 127     // 🛡️ NEW: Checksum for data integrity validation

// 🛡️ EEPROM PROTECTION: Calculate XOR checksum of critical bytes
static uint8_t calculateEEPROMChecksum() {
  uint8_t checksum = 0;
  // Only checksum the critical data (logging, stats, sensors)
  for (int i = 0; i < 4; i++) {
    checksum ^= EEPROM.read(i);
  }
  return checksum;
}

// 🛡️ EEPROM PROTECTION: Verify checksum validity
static bool verifyEEPROMChecksum() {
  uint8_t calculated = calculateEEPROMChecksum();
  uint8_t stored = EEPROM.read(EEPROM_ADDR_CHECKSUM);
  return (calculated == stored);
}

// NOTE: isPaused removed - use config.isPaused instead (Phase 4D cleanup)
// NOTE: currentState moved to SystemConfig struct - accessed via config.currentState

// ============================================================================
// CONSTRUCTOR
// ============================================================================
UtilityEngine::UtilityEngine(WebSocketsServer& webSocketServer)
  : webSocket(webSocketServer),
    littleFsMounted(false),
    currentLogLevel(LOG_INFO),
    loggingEnabled(true),  // Default: logs enabled
    logBufferWriteIndex(0),
    lastLogFlush(0) {
  
  // Initialize log buffer
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    logBuffer[i].valid = false;
  }
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Load logging preferences from EEPROM
  loadLoggingPreferences();
}

// ============================================================================
// LIFECYCLE METHODS
// ============================================================================

bool UtilityEngine::initialize() {
  Serial.println("\n[UtilityEngine] Initializing...");
  
  // 🛡️ ROBUST LittleFS INITIALIZATION with multi-level protection
  Serial.println("[UtilityEngine] Attempting LittleFS mount with safety checks...");
  
  // STEP 1: Try mounting WITHOUT auto-format (safer)
  littleFsMounted = LittleFS.begin(false);
  
  if (!littleFsMounted) {
    Serial.println("[UtilityEngine] ⚠️ LittleFS mount failed - filesystem may be corrupted");
    
    // STEP 2: Try manual format in controlled way
    Serial.println("[UtilityEngine] 🔧 Attempting manual LittleFS format...");
    if (LittleFS.format()) {
      Serial.println("[UtilityEngine] ✅ Format successful, remounting...");
      
      // STEP 3: Try mounting again after format
      littleFsMounted = LittleFS.begin(false);
      
      if (littleFsMounted) {
        Serial.println("[UtilityEngine] ✅ LittleFS mounted after format");
      } else {
        Serial.println("[UtilityEngine] ❌ CRITICAL: LittleFS still won't mount after format!");
        Serial.println("[UtilityEngine] 🚨 Running in DEGRADED mode (no filesystem)");
      }
    } else {
      Serial.println("[UtilityEngine] ❌ Format failed - hardware issue or severe corruption");
      Serial.println("[UtilityEngine] 🚨 Running in DEGRADED mode (no filesystem)");
    }
  } else {
    Serial.println("[UtilityEngine] ✅ LittleFS mounted successfully");
  }
  
  // STEP 4: If mounted, verify filesystem health
  if (littleFsMounted) {
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    Serial.printf("[UtilityEngine] 💾 LittleFS: %u KB total, %u KB used (%.1f%%)\n", 
      totalBytes / 1024, usedBytes / 1024, (usedBytes * 100.0) / totalBytes);
  }
  
  // STEP 5: Create logs directory if filesystem is healthy
  if (littleFsMounted) {
    if (!directoryExists("/logs")) {
      Serial.println("[UtilityEngine] 📁 Creating /logs directory...");
      if (createDirectory("/logs")) {
        Serial.println("[UtilityEngine] ✅ /logs directory created");
      } else {
        Serial.println("[UtilityEngine] ⚠️ Failed to create /logs - continuing anyway");
      }
    }
    
    // Clean up epoch date log files (created before NTP sync)
    Serial.println("[UtilityEngine] 🧹 Cleaning up epoch log files...");
    File logsDir = LittleFS.open("/logs");
    if (logsDir && logsDir.isDirectory()) {
      File logFile = logsDir.openNextFile();
      while (logFile) {
        String fileName = String(logFile.name());
        if (fileName.indexOf("1970") >= 0) {
          String fullPath = "/logs/" + fileName;
          logFile.close();
          if (LittleFS.remove(fullPath)) {
            Serial.println("[UtilityEngine] 🗑️ Removed epoch file: " + fullPath);
          }
          logFile = logsDir.openNextFile();
        } else {
          logFile = logsDir.openNextFile();
        }
      }
    }
  } else {
    Serial.println("[UtilityEngine] ⚠️ Log directory/cleanup SKIPPED (filesystem not mounted)");
  }
  
  // Wait for NTP sync (should be done by main firmware)
  delay(1000);
  
  // STEP 6: Initialize log file ONLY if filesystem is mounted
  if (littleFsMounted) {
    if (!initializeLogFile()) {
      Serial.println("[UtilityEngine] ⚠️ Log file initialization deferred (NTP not ready)");
      // Not fatal - will retry on first log message
    }
  } else {
    Serial.println("[UtilityEngine] ⚠️ Log file init SKIPPED (filesystem not available - logs to Serial only)");
  }
  
  Serial.println("[UtilityEngine] ✅ Initialization complete (degraded mode = " + String(!littleFsMounted ? "YES" : "NO") + ")");
  return true;
}

void UtilityEngine::shutdown() {
  if (globalLogFile) {
    flushLogBuffer(true);  // Force flush pending logs
    globalLogFile.println("========================================");
    globalLogFile.println("SESSION ENDING - Engine shutdown");
    globalLogFile.println("========================================");
    globalLogFile.close();
  }
}

// ============================================================================
// LOGGING METHODS
// ============================================================================

void UtilityEngine::log(LogLevel level, const String& message) {
  // Master switch: if logging disabled, skip everything
  if (!loggingEnabled) return;
  
  if (level > currentLogLevel) return;  // Skip if below current level
  
  const char* prefix = getLevelPrefix(level);
  
  // 1. Serial output (always)
  Serial.print(prefix);
  Serial.println(message);
  
  // 2. WebSocket broadcast (if clients connected)
  if (webSocket.connectedClients() > 0) {
    JsonDocument doc;
    doc["type"] = "log";
    doc["level"] = String(prefix).substring(1, String(prefix).length() - 2);  // Remove "[ ]"
    doc["message"] = message;
    
    String payload;
    serializeJson(doc, payload);
    webSocket.broadcastTXT(payload);
  }
  
  // 3. Buffer log for async file write (if filesystem ready)
  if (littleFsMounted) {
    // Initialize log file if not yet open (delayed initialization for NTP sync)
    if (!globalLogFile && isTimeSynchronized()) {
      initializeLogFile();
    }
    
    if (globalLogFile) {
      logBuffer[logBufferWriteIndex].timestamp = millis();
      logBuffer[logBufferWriteIndex].level = level;
      logBuffer[logBufferWriteIndex].message = String(prefix) + message;
      logBuffer[logBufferWriteIndex].valid = true;
      
      logBufferWriteIndex = (logBufferWriteIndex + 1) % LOG_BUFFER_SIZE;
    }
  }
}

void UtilityEngine::flushLogBuffer(bool forceFlush) {
  if (!globalLogFile || !littleFsMounted) return;
  
  unsigned long now = millis();
  
  // Count valid entries
  int validEntries = 0;
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    if (logBuffer[i].valid) validEntries++;
  }
  
  float bufferUsagePercent = (validEntries * 100.0) / LOG_BUFFER_SIZE;
  
  // CRITICAL: Force flush if buffer is 80% full
  bool shouldForce = (bufferUsagePercent >= 80.0);
  
  // Skip flush if motor moving (unless critical) - DISABLED after Phase 3.3 (currentState in config)
  // NOTE: We could check config.currentState here if needed, but for now we flush normally
  // if (currentState == STATE_RUNNING && !isPaused && !shouldForce && !forceFlush) {
  //   return;
  // }
  
  // Flush every 5 seconds OR if forced/critical
  if (!forceFlush && !shouldForce && now - lastLogFlush < 5000) return;
  
  if (validEntries == 0) {
    lastLogFlush = now;
    return;
  }
  
  // Get current time for timestamps
  time_t currentTime = time(nullptr);
  struct tm *tmstruct = localtime(&currentTime);
  bool timeValid = (tmstruct->tm_year > (2020 - 1900));
  
  // Write all valid entries in one batch
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    if (logBuffer[i].valid) {
      if (timeValid) {
        char ts[30];
        time_t logTime = currentTime - ((now - logBuffer[i].timestamp) / 1000);
        struct tm *logTm = localtime(&logTime);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", logTm);
        globalLogFile.print("[");
        globalLogFile.print(ts);
        globalLogFile.print("] ");
      } else {
        globalLogFile.print("[T+");
        globalLogFile.print(logBuffer[i].timestamp / 1000);
        globalLogFile.print("s] ");
      }
      
      globalLogFile.println(logBuffer[i].message);
      logBuffer[i].valid = false;
      logBuffer[i].message = "";
    }
  }
  
  // 🛡️ PROTECTION: Flush AND verify file is still valid after write
  if (globalLogFile) {
    globalLogFile.flush();
    // Check if file is still writable after flush
    if (!globalLogFile) {
      Serial.println("[UtilityEngine] ⚠️ Log file corrupted during flush - reinitializing...");
      initializeLogFile();  // Try to recover
    }
  }
  
  lastLogFlush = now;
}

// ============================================================================
// FILESYSTEM METHODS
// ============================================================================

bool UtilityEngine::fileExists(const String& path) const {
  if (!littleFsMounted) return false;
  
  if (!LittleFS.exists(path)) return false;
  
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  
  bool isFile = !f.isDirectory();
  f.close();
  return isFile;
}

bool UtilityEngine::directoryExists(const String& path) const {
  if (!littleFsMounted) return false;
  
  if (!LittleFS.exists(path)) return false;
  
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  
  bool isDir = f.isDirectory();
  f.close();
  return isDir;
}

String UtilityEngine::readFileAsString(const String& path, size_t maxSize) {
  if (!littleFsMounted || !fileExists(path)) {
    return "";
  }
  
  File file = LittleFS.open(path, "r");
  if (!file) return "";
  
  // Limit read size for safety
  size_t readSize = min((size_t)file.size(), maxSize);
  String content;
  content.reserve(readSize + 1);
  
  while (file.available() && readSize > 0) {
    content += (char)file.read();
    readSize--;
  }
  
  file.close();
  return content;
}

bool UtilityEngine::writeFileAsString(const String& path, const String& data) {
  if (!littleFsMounted) return false;
  
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("[UtilityEngine] ❌ Failed to open file for writing: " + path);
    return false;
  }
  
  size_t written = file.print(data);
  
  // 🛡️ PROTECTION: Flush to ensure data is written before closing
  file.flush();
  file.close();
  
  // 🛡️ VALIDATION: Verify write completed successfully
  if (written != data.length()) {
    Serial.println("[UtilityEngine] ⚠️ Write incomplete: " + String(written) + "/" + String(data.length()) + " bytes to " + path);
    return false;
  }
  
  return true;
}

bool UtilityEngine::deleteFile(const String& path) {
  if (!littleFsMounted || !fileExists(path)) return false;
  
  return LittleFS.remove(path);
}

bool UtilityEngine::createDirectory(const String& path) {
  if (!littleFsMounted) return false;
  
  if (directoryExists(path)) return true;  // Already exists
  
  return LittleFS.mkdir(path);
}

uint32_t UtilityEngine::getTotalBytes() const {
  if (!littleFsMounted) return 0;
  
  // LittleFS total size (depends on partition defined in platformio.ini)
  // For ESP32-S3 with default config: 8MB SPIFFS
  return 8 * 1024 * 1024;  // 8MB
}

uint32_t UtilityEngine::getUsedBytes() const {
  if (!littleFsMounted) return 0;
  
  File root = LittleFS.open("/");
  if (!root) return 0;
  
  uint32_t used = 0;
  File file = root.openNextFile();
  
  while (file) {
    used += file.size();
    file = root.openNextFile();
  }
  
  return used;
}

float UtilityEngine::getDiskUsagePercent() const {
  uint32_t total = getTotalBytes();
  if (total == 0) return 0.0;
  
  uint32_t used = getUsedBytes();
  return (used * 100.0) / total;
}

int UtilityEngine::clearAllFiles() {
  if (!littleFsMounted) return 0;
  
  int deletedCount = 0;
  File root = LittleFS.open("/");
  
  if (!root) return 0;
  
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    file.close();
    
    if (LittleFS.remove(name)) {
      deletedCount++;
    }
    
    file = root.openNextFile();
  }
  
  return deletedCount;
}

// ============================================================================
// WEBSOCKET RELAY METHODS
// ============================================================================

void UtilityEngine::broadcastWebSocket(const String& message) {
  if (webSocket.connectedClients() > 0) {
    String msg = message;  // Create non-const copy for broadcastTXT
    webSocket.broadcastTXT(msg);
  }
}

uint8_t UtilityEngine::getConnectedClients() const {
  return webSocket.connectedClients();
}

// ============================================================================
// UTILITY METHODS
// ============================================================================

String UtilityEngine::getFormattedTime(const char* format) const {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  
  char buffer[64];
  strftime(buffer, sizeof(buffer), format, timeinfo);
  
  return String(buffer);
}

bool UtilityEngine::isTimeSynchronized() const {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  
  // Check if year is reasonable (not 1970 epoch)
  return (timeinfo->tm_year > (2020 - 1900));
}

UtilityEngine::EngineStatus UtilityEngine::getStatus() const {
  EngineStatus status;
  status.filesystemReady = littleFsMounted;
  status.fileOpen = (bool)globalLogFile;
  status.timeSynced = isTimeSynchronized();
  status.connectedClients = webSocket.connectedClients();
  status.totalBytes = getTotalBytes();
  status.usedBytes = getUsedBytes();
  status.diskUsagePercent = getDiskUsagePercent();
  status.currentLogLevel = currentLogLevel;
  status.currentLogFile = currentLogFileName;
  
  return status;
}

void UtilityEngine::printStatus() const {
  EngineStatus status = getStatus();
  
  Serial.println("\n[UtilityEngine] ══════════════════════════════════════");
  Serial.println("[UtilityEngine] STATUS REPORT");
  Serial.println("[UtilityEngine] ══════════════════════════════════════");
  Serial.print("[UtilityEngine] Filesystem: ");
  Serial.println(status.filesystemReady ? "✅ Ready" : "❌ Failed");
  Serial.print("[UtilityEngine] Log File: ");
  Serial.println(status.fileOpen ? "✅ Open" : "❌ Closed");
  Serial.print("[UtilityEngine] Time Sync: ");
  Serial.println(status.timeSynced ? "✅ Synchronized" : "⏳ Pending");
  Serial.print("[UtilityEngine] WebSocket Clients: ");
  Serial.println(status.connectedClients);
  Serial.print("[UtilityEngine] Disk Usage: ");
  Serial.print(status.diskUsagePercent, 1);
  Serial.print("% (");
  Serial.print(status.usedBytes / 1024);
  Serial.print("KB / ");
  Serial.print(status.totalBytes / 1024 / 1024);
  Serial.println("MB)");
  Serial.print("[UtilityEngine] Log Level: ");
  Serial.println(getLevelPrefix(status.currentLogLevel));
  Serial.print("[UtilityEngine] Current Log: ");
  Serial.println(status.currentLogFile);
  Serial.println("[UtilityEngine] ══════════════════════════════════════\n");
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

bool UtilityEngine::initializeLogFile() {
  if (!littleFsMounted) return false;
  
  if (!isTimeSynchronized()) {
    Serial.println("[UtilityEngine] ⏳ NTP not synced yet - cannot create log file");
    return false;
  }
  
  currentLogFileName = generateLogFilename();
  
  globalLogFile = LittleFS.open(currentLogFileName, "a");
  if (!globalLogFile) {
    Serial.println("[UtilityEngine] ❌ Failed to open log file: " + currentLogFileName);
    return false;
  }
  
  Serial.println("[UtilityEngine] ✅ Log file opened: " + currentLogFileName);
  
  // Write session header
  char ts[30];
  time_t now = time(nullptr);
  struct tm *tmstruct = localtime(&now);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmstruct);
  
  globalLogFile.println("");
  globalLogFile.println("========================================");
  globalLogFile.print("SESSION START: ");
  globalLogFile.println(ts);
  globalLogFile.println("========================================");
  globalLogFile.flush();
  
  return true;
}

String UtilityEngine::generateLogFilename() {
  time_t now = time(nullptr);
  struct tm *tmstruct = localtime(&now);
  
  char dateBuf[20];
  strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", tmstruct);
  String dateStr = String(dateBuf);
  
  // Find max suffix by scanning /logs directory
  int maxSuffix = -1;
  
  File scanDir = LittleFS.open("/logs");
  if (scanDir) {
    File file = scanDir.openNextFile();
    while (file) {
      String fileName = String(file.name());
      String prefix = "log_" + dateStr + "_";
      
      if (fileName.startsWith(prefix) && fileName.endsWith(".txt")) {
        // Extract suffix: "log_20250102_5.txt" → 5
        String suffixStr = fileName.substring(
          prefix.length(),
          fileName.length() - 4  // Remove .txt
        );
        int suffix = suffixStr.toInt();
        if (suffix > maxSuffix) maxSuffix = suffix;
      }
      
      file = scanDir.openNextFile();
    }
    scanDir.close();
  }
  
  return String(LOG_FILE_PATTERN) + dateStr + "_" + String(maxSuffix + 1) + LOG_FILE_EXTENSION;
}

const char* UtilityEngine::getLevelPrefix(LogLevel level) const {
  switch (level) {
    case LOG_ERROR:   return "[ERROR] ";
    case LOG_WARNING: return "[WARN]  ";
    case LOG_INFO:    return "[INFO]  ";
    case LOG_DEBUG:   return "[DEBUG] ";
    default:          return "[LOG]   ";
  }
}

String UtilityEngine::escapeJsonString(const String& input) const {
  String output;
  output.reserve(input.length() + 20);
  
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input[i];
    switch(c) {
      case '\\': output += "\\\\"; break;
      case '\"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += c;
    }
  }
  return output;
}

void UtilityEngine::listDirectoryContents(const String& path) const {
  if (!littleFsMounted) return;
  
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return;
  
  Serial.println("[UtilityEngine] Contents of " + path + ":");
  
  File file = dir.openNextFile();
  while (file) {
    Serial.print("[UtilityEngine]   - ");
    Serial.print(file.name());
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    
    file = dir.openNextFile();
  }
}

// ============================================================================
// JSON HELPERS - Phase 3.1
// ============================================================================

/**
 * Load JSON from file (deserialize)
 */
bool UtilityEngine::loadJsonFile(const String& path, JsonDocument& doc) {
  // Check if file exists
  if (!fileExists(path)) {
    error("JSON file not found: " + path);
    return false;
  }
  
  // Open file for reading
  File file = LittleFS.open(path, "r");
  if (!file) {
    error("Failed to open file for reading: " + path);
    return false;
  }
  
  // Deserialize JSON from file
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  
  if (err) {
    error("JSON parse error in " + path + ": " + String(err.c_str()));
    return false;
  }
  
  info("✅ Loaded JSON from: " + path);
  return true;
}

/**
 * Save JSON to file (serialize)
 */
bool UtilityEngine::saveJsonFile(const String& path, const JsonDocument& doc) {
  // Ensure parent directory exists
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = path.substring(0, lastSlash);
    createDirectory(dir);
  }
  
  // Open file for writing
  File file = LittleFS.open(path, "w");
  if (!file) {
    error("Failed to open file for writing: " + path);
    return false;
  }
  
  // Serialize JSON to file
  size_t bytesWritten = serializeJson(doc, file);
  
  // 🛡️ PROTECTION: Flush before close to ensure data is written to flash
  file.flush();
  
  // 🛡️ VERIFICATION: Check file handle is still valid after flush
  if (!file) {
    error("❌ CRITICAL: File corrupted during JSON write (flush failed): " + path);
    return false;
  }
  
  file.close();
  
  // 🛡️ VALIDATION: Verify bytes were actually written
  if (bytesWritten == 0) {
    error("Failed to write JSON to: " + path);
    return false;
  }
  
  // 🛡️ PARANOID CHECK: Read back and verify file exists and has data
  if (!fileExists(path)) {
    error("❌ CRITICAL: JSON file vanished after write: " + path);
    return false;
  }
  
  info("✅ Saved JSON to: " + path + " (" + String(bytesWritten) + " bytes)");
  return true;
}

/**
 * Validate JSON schema (basic key presence check)
 */
bool UtilityEngine::validateJsonSchema(const JsonDocument& doc, const String& requiredKeys) {
  // Parse the required keys string (space or comma separated)
  String keys = requiredKeys;
  keys.replace(',', ' ');  // Normalize comma to space
  
  int startIdx = 0;
  bool allPresent = true;
  
  while (startIdx < keys.length()) {
    // Skip whitespace
    while (startIdx < keys.length() && (keys[startIdx] == ' ' || keys[startIdx] == '\t')) {
      startIdx++;
    }
    
    if (startIdx >= keys.length()) break;
    
    // Find end of key
    int endIdx = startIdx;
    while (endIdx < keys.length() && keys[endIdx] != ' ' && keys[endIdx] != '\t') {
      endIdx++;
    }
    
  // Extract and check key
    String key = keys.substring(startIdx, endIdx);
    if (!doc[key].is<JsonVariant>() || doc[key].isNull()) {
      warn("Missing required JSON key: " + key);
      allPresent = false;
    }
    
    startIdx = endIdx;
  }
  
  return allPresent;
}

// ============================================================================
// SYSTEM CONFIGURATION PERSISTENCE - Phase 3.2
// ============================================================================

/**
 * Load system configuration from config.json
 * Deserializes all system state variables into SystemConfig struct
 */
bool UtilityEngine::loadSystemConfig(SystemConfig& config, const String& configPath) {
  info("📂 Loading system configuration from: " + configPath);
  
  // Create a large JSON document to hold all config data
  JsonDocument doc;
  
  // Use Phase 3.1 JSON helper to load file
  if (!loadJsonFile(configPath, doc)) {
    warn("System config file not found, using defaults");
    return false;
  }
  
  // ========================================================================
  // EXTRACT STATE
  // ========================================================================
  if (doc["currentState"].is<int>()) {
    config.currentState = (SystemState)doc["currentState"].as<int>();
  }
  if (doc["executionContext"].is<int>()) {
    config.executionContext = (ExecutionContext)doc["executionContext"].as<int>();
  }
  if (doc["currentMovement"].is<int>()) {
    config.currentMovement = (MovementType)doc["currentMovement"].as<int>();
  }
  config.needsInitialCalibration = doc["needsInitialCalibration"] | true;
  
  // ========================================================================
  // EXTRACT POSITION LIMITS
  // ========================================================================
  config.minStep = doc["minStep"] | 0L;
  config.maxStep = doc["maxStep"] | 0L;
  config.totalDistanceMM = doc["totalDistanceMM"] | 0.0;
  config.maxDistanceLimitPercent = doc["maxDistanceLimitPercent"] | 100.0;
  config.effectiveMaxDistanceMM = doc["effectiveMaxDistanceMM"] | 0.0;
  
  // ========================================================================
  // EXTRACT CONTACT SENSING
  // ========================================================================
  config.lastStartContactState = doc["lastStartContactState"] | (int)HIGH;
  config.lastEndContactState = doc["lastEndContactState"] | (int)HIGH;
  config.currentMotorDirection = doc["currentMotorDirection"] | (bool)HIGH;
  config.wasAtStart = doc["wasAtStart"] | false;
  config.measuredCyclesPerMinute = doc["measuredCyclesPerMinute"] | 0.0;
  
  // ========================================================================
  // EXTRACT MOTION EXECUTION
  // ========================================================================
  config.isPaused = doc["isPaused"] | false;
  config.movingForward = doc["movingForward"] | true;
  config.startStep = doc["startStep"] | 0L;
  config.targetStep = doc["targetStep"] | 0L;
  config.hasReachedStartStep = doc["hasReachedStartStep"] | false;
  // Note: lastStepForDistance now in global StatsTracking struct (not persisted)
  
  // ========================================================================
  // EXTRACT MOTION CONFIG → writes to global 'motion' variable
  // NOTE: No mutex needed here - called at boot before motorTask starts
  // ========================================================================
  if (doc["motion"].is<JsonObject>()) {
    JsonObject motionObj = doc["motion"];
    motion.startPositionMM = motionObj["startPositionMM"] | 0.0;
    motion.targetDistanceMM = motionObj["targetDistanceMM"] | 50.0;
    motion.speedLevelForward = motionObj["speedLevelForward"] | 5.0;
    motion.speedLevelBackward = motionObj["speedLevelBackward"] | 5.0;
  }
  
  // ========================================================================
  // EXTRACT OSCILLATION CONFIG & STATE
  // ========================================================================
  if (doc["oscillation"].is<JsonObject>()) {
    JsonObject oscObj = doc["oscillation"];
    config.oscillation.centerPositionMM = oscObj["centerPositionMM"] | 0.0;
    config.oscillation.amplitudeMM = oscObj["amplitudeMM"] | 20.0;
    config.oscillation.waveform = (OscillationWaveform)(oscObj["waveform"] | (int)OSC_SINE);
    config.oscillation.frequencyHz = oscObj["frequencyHz"] | 0.5;
    config.oscillation.cycleCount = oscObj["cycleCount"] | 0;
    config.oscillation.returnToCenter = oscObj["returnToCenter"] | true;
  }
  
  config.actualOscillationSpeedMMS = doc["actualOscillationSpeedMMS"] | 0.0;
  
  // ========================================================================
  // EXTRACT PURSUIT CONFIG
  // ========================================================================
  if (doc["pursuit"].is<JsonObject>()) {
    JsonObject pursuitObj = doc["pursuit"];
    config.pursuit.targetStep = pursuitObj["targetStep"] | 0L;
    config.pursuit.maxSpeedLevel = pursuitObj["maxSpeedLevel"] | 10.0;
  }
  
  // ========================================================================
  // EXTRACT ZONE EFFECTS (new format + legacy fallback)
  // ========================================================================
  // Try new zoneEffect format first
  if (doc["zoneEffect"].is<JsonObject>()) {
    JsonObject zoneObj = doc["zoneEffect"];
    config.decelZone.enabled = zoneObj["enabled"] | false;
    config.decelZone.enableStart = zoneObj["enableStart"] | true;
    config.decelZone.enableEnd = zoneObj["enableEnd"] | true;
    config.decelZone.zoneMM = zoneObj["zoneMM"] | 50.0;
    config.decelZone.speedEffect = (SpeedEffect)(zoneObj["speedEffect"] | (int)SPEED_DECEL);
    config.decelZone.speedCurve = (SpeedCurve)(zoneObj["speedCurve"] | (int)CURVE_SINE);
    config.decelZone.speedIntensity = zoneObj["speedIntensity"] | 75.0;
    config.decelZone.randomTurnbackEnabled = zoneObj["randomTurnbackEnabled"] | false;
    config.decelZone.turnbackChance = zoneObj["turnbackChance"] | 30;
    config.decelZone.endPauseEnabled = zoneObj["endPauseEnabled"] | false;
    config.decelZone.endPauseIsRandom = zoneObj["endPauseIsRandom"] | false;
    config.decelZone.endPauseDurationSec = zoneObj["endPauseDurationSec"] | 1.0;
    config.decelZone.endPauseMinSec = zoneObj["endPauseMinSec"] | 0.5;
    config.decelZone.endPauseMaxSec = zoneObj["endPauseMaxSec"] | 2.0;
  }
  // Fallback to legacy decelZone format
  else if (doc["decelZone"].is<JsonObject>()) {
    JsonObject decelObj = doc["decelZone"];
    bool enabled = decelObj["enabled"] | false;
    config.decelZone.enabled = enabled;
    config.decelZone.speedEffect = enabled ? SPEED_DECEL : SPEED_NONE;
    config.decelZone.zoneMM = decelObj["zoneMM"] | 50.0;
    config.decelZone.speedIntensity = decelObj["effectPercent"] | 75.0;
    config.decelZone.speedCurve = (SpeedCurve)(decelObj["mode"] | (int)CURVE_SINE);
    // Legacy format doesn't have these - use defaults
    config.decelZone.randomTurnbackEnabled = false;
    config.decelZone.turnbackChance = 30;
    config.decelZone.endPauseEnabled = false;
    config.decelZone.endPauseIsRandom = false;
    config.decelZone.endPauseDurationSec = 1.0;
    config.decelZone.endPauseMinSec = 0.5;
    config.decelZone.endPauseMaxSec = 2.0;
  }
  
  // ========================================================================
  // EXTRACT CHAOS CONFIG & STATE
  // ========================================================================
  if (doc["chaos"].is<JsonObject>()) {
    JsonObject chaosObj = doc["chaos"];
    config.chaos.centerPositionMM = chaosObj["centerPositionMM"] | 110.0;
    config.chaos.amplitudeMM = chaosObj["amplitudeMM"] | 50.0;
    config.chaos.maxSpeedLevel = chaosObj["maxSpeedLevel"] | 5.0;
    config.chaos.durationSeconds = chaosObj["durationSeconds"] | 0UL;
    config.chaos.crazinessPercent = chaosObj["crazinessPercent"] | 50.0;
  }
  
  // ========================================================================
  // EXTRACT SEQUENCER
  // ========================================================================
  config.sequenceLineCount = doc["sequenceLineCount"] | 0;
  config.nextLineId = doc["nextLineId"] | 1;
  
  info("✅ System configuration loaded successfully");
  return true;
}

/**
 * Save system configuration to config.json
 * Serializes all SystemConfig fields to persistent storage
 */
bool UtilityEngine::saveSystemConfig(const SystemConfig& config, const String& configPath) {
  info("💾 Saving system configuration to: " + configPath);
  
  // Create a large JSON document
  JsonDocument doc;
  
  // ========================================================================
  // SAVE STATE
  // ========================================================================
  doc["currentState"] = (int)config.currentState;
  doc["executionContext"] = (int)config.executionContext;
  doc["currentMovement"] = (int)config.currentMovement;
  doc["needsInitialCalibration"] = config.needsInitialCalibration;
  
  // ========================================================================
  // SAVE POSITION LIMITS
  // ========================================================================
  doc["minStep"] = config.minStep;
  doc["maxStep"] = config.maxStep;
  doc["totalDistanceMM"] = config.totalDistanceMM;
  doc["maxDistanceLimitPercent"] = config.maxDistanceLimitPercent;
  doc["effectiveMaxDistanceMM"] = config.effectiveMaxDistanceMM;
  
  // ========================================================================
  // SAVE CONTACT SENSING
  // ========================================================================
  doc["lastStartContactState"] = config.lastStartContactState;
  doc["lastEndContactState"] = config.lastEndContactState;
  doc["currentMotorDirection"] = config.currentMotorDirection;
  doc["wasAtStart"] = config.wasAtStart;
  doc["measuredCyclesPerMinute"] = config.measuredCyclesPerMinute;
  
  // ========================================================================
  // SAVE MOTION EXECUTION
  // ========================================================================
  doc["isPaused"] = config.isPaused;
  doc["movingForward"] = config.movingForward;
  doc["startStep"] = config.startStep;
  doc["targetStep"] = config.targetStep;
  doc["hasReachedStartStep"] = config.hasReachedStartStep;
  // Note: lastStepForDistance now in global StatsTracking struct (not persisted)
  
  // ========================================================================
  // SAVE MOTION CONFIG → reads from global 'motion' variable
  // NOTE: Caller should hold motionMutex if called during runtime
  // ========================================================================
  JsonObject motionObj = doc["motion"].to<JsonObject>();
  motionObj["startPositionMM"] = motion.startPositionMM;
  motionObj["targetDistanceMM"] = motion.targetDistanceMM;
  motionObj["speedLevelForward"] = motion.speedLevelForward;
  motionObj["speedLevelBackward"] = motion.speedLevelBackward;
  
  // ========================================================================
  // SAVE OSCILLATION CONFIG & STATE
  // ========================================================================
  JsonObject oscObj = doc["oscillation"].to<JsonObject>();
  oscObj["centerPositionMM"] = config.oscillation.centerPositionMM;
  oscObj["amplitudeMM"] = config.oscillation.amplitudeMM;
  oscObj["waveform"] = (int)config.oscillation.waveform;
  oscObj["frequencyHz"] = config.oscillation.frequencyHz;
  oscObj["cycleCount"] = config.oscillation.cycleCount;
  oscObj["returnToCenter"] = config.oscillation.returnToCenter;
  
  doc["actualOscillationSpeedMMS"] = config.actualOscillationSpeedMMS;
  
  // ========================================================================
  // SAVE PURSUIT CONFIG
  // ========================================================================
  JsonObject pursuitObj = doc["pursuit"].to<JsonObject>();
  pursuitObj["targetStep"] = config.pursuit.targetStep;
  pursuitObj["maxSpeedLevel"] = config.pursuit.maxSpeedLevel;
  
  // ========================================================================
  // SAVE ZONE EFFECTS (Speed + Special Effects)
  // ========================================================================
  JsonObject zoneObj = doc["zoneEffect"].to<JsonObject>();
  zoneObj["enabled"] = config.decelZone.enabled;
  zoneObj["enableStart"] = config.decelZone.enableStart;
  zoneObj["enableEnd"] = config.decelZone.enableEnd;
  zoneObj["zoneMM"] = config.decelZone.zoneMM;
  zoneObj["speedEffect"] = (int)config.decelZone.speedEffect;
  zoneObj["speedCurve"] = (int)config.decelZone.speedCurve;
  zoneObj["speedIntensity"] = config.decelZone.speedIntensity;
  zoneObj["randomTurnbackEnabled"] = config.decelZone.randomTurnbackEnabled;
  zoneObj["turnbackChance"] = config.decelZone.turnbackChance;
  zoneObj["endPauseEnabled"] = config.decelZone.endPauseEnabled;
  zoneObj["endPauseIsRandom"] = config.decelZone.endPauseIsRandom;
  zoneObj["endPauseDurationSec"] = config.decelZone.endPauseDurationSec;
  zoneObj["endPauseMinSec"] = config.decelZone.endPauseMinSec;
  zoneObj["endPauseMaxSec"] = config.decelZone.endPauseMaxSec;
  // Legacy compatibility - also save as decelZone
  JsonObject decelObj = doc["decelZone"].to<JsonObject>();
  decelObj["enabled"] = config.decelZone.enabled && (config.decelZone.speedEffect == SPEED_DECEL);
  decelObj["zoneMM"] = config.decelZone.zoneMM;
  decelObj["effectPercent"] = config.decelZone.speedIntensity;
  decelObj["mode"] = (int)config.decelZone.speedCurve;
  
  // ========================================================================
  // SAVE CHAOS CONFIG & STATE
  // ========================================================================
  JsonObject chaosObj = doc["chaos"].to<JsonObject>();
  chaosObj["centerPositionMM"] = config.chaos.centerPositionMM;
  chaosObj["amplitudeMM"] = config.chaos.amplitudeMM;
  chaosObj["maxSpeedLevel"] = config.chaos.maxSpeedLevel;
  chaosObj["durationSeconds"] = config.chaos.durationSeconds;
  chaosObj["crazinessPercent"] = config.chaos.crazinessPercent;
  
  // ========================================================================
  // SAVE SEQUENCER
  // ========================================================================
  doc["sequenceLineCount"] = config.sequenceLineCount;
  doc["nextLineId"] = config.nextLineId;
  
  // Use Phase 3.1 JSON helper to save file
  bool success = saveJsonFile(configPath, doc);
  
  if (success) {
    info("✅ System configuration saved successfully");
  } else {
    error("❌ Failed to save system configuration");
  }
  
  return success;
}

// ============================================================================
// LOGGING PREFERENCES - EEPROM PERSISTENCE
// ============================================================================

/**
 * Save logging preferences to EEPROM
 * EEPROM Layout:
 * - Byte 0: loggingEnabled (0=disabled, 1=enabled)
 * - Byte 1: currentLogLevel (0-3: ERROR, WARN, INFO, DEBUG)
 * - Byte 127: Checksum (XOR of bytes 0-2)
 */
void UtilityEngine::saveLoggingPreferences() {
  EEPROM.write(EEPROM_ADDR_LOGGING_ENABLED, loggingEnabled ? 1 : 0);
  EEPROM.write(EEPROM_ADDR_LOG_LEVEL, (uint8_t)currentLogLevel);
  
  // 🛡️ NEW: Write checksum for data integrity
  uint8_t checksum = calculateEEPROMChecksum();
  EEPROM.write(EEPROM_ADDR_CHECKSUM, checksum);
  
  // 🛡️ COMMIT WITH RETRY
  const int maxRetries = 3;
  bool committed = false;
  
  for (int attempt = 0; attempt < maxRetries && !committed; attempt++) {
    if (attempt > 0) {
      Serial.println("[UtilityEngine] ⚠️ EEPROM commit retry #" + String(attempt));
    }
    committed = EEPROM.commit();
    if (!committed) delay(50 * (attempt + 1));
  }
  
  if (committed) {
    Serial.println("[UtilityEngine] 💾 Logging preferences saved to EEPROM with checksum");
  } else {
    Serial.println("[UtilityEngine] ❌ EEPROM commit failed after retries!");
  }
}

/**
 * Load logging preferences from EEPROM
 * Reads saved preferences or uses defaults if uninitialized (0xFF) or corrupted
 */
void UtilityEngine::loadLoggingPreferences() {
  uint8_t enabledByte = EEPROM.read(EEPROM_ADDR_LOGGING_ENABLED);
  uint8_t levelByte = EEPROM.read(EEPROM_ADDR_LOG_LEVEL);
  
  // 🛡️ NEW: Check EEPROM integrity with checksum
  bool checksumValid = verifyEEPROMChecksum();
  
  // Check if EEPROM is uninitialized (fresh ESP32) OR corrupted
  if (enabledByte == 0xFF || !checksumValid) {
    if (!checksumValid && enabledByte != 0xFF) {
      Serial.println("[UtilityEngine] ⚠️ EEPROM CORRUPTION DETECTED! Checksum mismatch - resetting to defaults");
    }
    
    // First boot or corruption: use defaults and save them
    loggingEnabled = true;
    currentLogLevel = LOG_INFO;
    saveLoggingPreferences();
    
    if (enabledByte == 0xFF) {
      Serial.println("[UtilityEngine] 🔧 First boot: initialized EEPROM with default logging preferences");
    } else {
      Serial.println("[UtilityEngine] 🔧 EEPROM repaired: reset to default logging preferences");
    }
  } else {
    // Load saved preferences
    loggingEnabled = (enabledByte == 1);
    
    // Validate log level (0-3 range)
    if (levelByte <= LOG_DEBUG) {
      currentLogLevel = (LogLevel)levelByte;
    } else {
      Serial.println("[UtilityEngine] ⚠️ Invalid log level in EEPROM: " + String(levelByte) + " - using default");
      currentLogLevel = LOG_INFO;  // Fallback to default
    }
    
    Serial.print("[UtilityEngine] 📂 Loaded logging preferences from EEPROM (checksum OK): ");
    Serial.print(loggingEnabled ? "ENABLED" : "DISABLED");
    Serial.print(", Level: ");
    Serial.println(getLevelPrefix(currentLogLevel));
  }
  
  // Load stats recording preference
  uint8_t statsByte = EEPROM.read(EEPROM_ADDR_STATS_ENABLED);
  if (statsByte == 0xFF) {
    // First boot: enable stats by default
    statsRecordingEnabled = true;
    EEPROM.write(EEPROM_ADDR_STATS_ENABLED, 1);
    
    // 🛡️ COMMIT WITH RETRY
    bool committed = false;
    for (int i = 0; i < 3 && !committed; i++) {
      committed = EEPROM.commit();
      if (!committed) delay(50 * (i + 1));
    }
    
    Serial.println("[UtilityEngine] 🔧 First boot: stats recording enabled by default");
  } else {
    statsRecordingEnabled = (statsByte == 1);
    Serial.print("[UtilityEngine] 📂 Stats recording: ");
    Serial.println(statsRecordingEnabled ? "ENABLED" : "DISABLED");
  }
  
  // Load sensors inversion preference
  loadSensorsInverted();
}

// ============================================================================
// STATS RECORDING PREFERENCE
// ============================================================================

/**
 * Enable/disable stats recording (saved in EEPROM with checksum)
 */
void UtilityEngine::setStatsRecordingEnabled(bool enabled) {
  statsRecordingEnabled = enabled;
  EEPROM.write(EEPROM_ADDR_STATS_ENABLED, enabled ? 1 : 0);
  
  // 🛡️ Update checksum after changing stats preference
  uint8_t checksum = calculateEEPROMChecksum();
  EEPROM.write(EEPROM_ADDR_CHECKSUM, checksum);
  
  // 🛡️ COMMIT WITH RETRY
  const int maxRetries = 3;
  bool committed = false;
  
  for (int attempt = 0; attempt < maxRetries && !committed; attempt++) {
    if (attempt > 0) {
      Serial.println("[UtilityEngine] ⚠️ Stats EEPROM retry #" + String(attempt));
    }
    committed = EEPROM.commit();
    if (!committed) delay(50 * (attempt + 1));
  }
  
  if (!committed) {
    Serial.println("[UtilityEngine] ❌ Stats EEPROM commit failed!");
  }
  
  info(String("📊 Stats recording: ") + (enabled ? "ENABLED" : "DISABLED") + " (saved to EEPROM with checksum)");
}

// ============================================================================
// SENSORS INVERSION PREFERENCE
// ============================================================================

/**
 * Load sensors inversion preference from EEPROM
 */
void UtilityEngine::loadSensorsInverted() {
  uint8_t inverted = EEPROM.read(EEPROM_ADDR_SENSORS_INVERTED);
  if (inverted == 0xFF) {
    // First boot: normal mode by default
    sensorsInverted = false;
    EEPROM.write(EEPROM_ADDR_SENSORS_INVERTED, 0);
    EEPROM.commit();
    Serial.println("[UtilityEngine] 🔧 First boot: sensors mode = NORMAL");
  } else {
    sensorsInverted = (inverted == 1);
    Serial.print("[UtilityEngine] 📂 Sensors mode: ");
    Serial.println(sensorsInverted ? "INVERTED" : "NORMAL");
  }
}

/**
 * Save sensors inversion preference to EEPROM
 */
void UtilityEngine::saveSensorsInverted() {
  EEPROM.write(EEPROM_ADDR_SENSORS_INVERTED, sensorsInverted ? 1 : 0);
  
  // Update checksum
  uint8_t checksum = calculateEEPROMChecksum();
  EEPROM.write(EEPROM_ADDR_CHECKSUM, checksum);
  
  // Commit with retry
  const int maxRetries = 3;
  bool committed = false;
  
  for (int attempt = 0; attempt < maxRetries && !committed; attempt++) {
    if (attempt > 0) {
      Serial.println("[UtilityEngine] ⚠️ Sensors EEPROM retry #" + String(attempt));
    }
    committed = EEPROM.commit();
    if (!committed) delay(50 * (attempt + 1));
  }
  
  if (!committed) {
    Serial.println("[UtilityEngine] ❌ Sensors EEPROM commit failed!");
  }
  
  info(String("🔄 Sensors mode: ") + (sensorsInverted ? "INVERTED" : "NORMAL") + " (saved to EEPROM)");
}

/**
 * Save current session's distance to daily stats
 * Only saves the increment since last save to avoid double-counting
 */
void UtilityEngine::saveCurrentSessionStats() {
  // Calculate distance increment since last save (in steps)
  unsigned long incrementSteps = stats.getIncrementSteps();
  
  // Convert to millimeters
  float incrementMM = incrementSteps / STEPS_PER_MM;
  
  if (incrementMM <= 0) {
    debug("📊 No new distance to save (no increment since last save)");
    return;
  }
  
  // Save increment to daily stats
  incrementDailyStats(incrementMM);
  
  debug(String("💾 Session stats saved: +") + String(incrementMM, 1) + "mm (total session: " + String(stats.totalDistanceTraveled / STEPS_PER_MM, 1) + "mm)");
  
  // Mark as saved using StatsTracking method
  stats.markSaved();
}

/**
 * Reset total distance counter to zero
 * Saves current session stats before resetting
 */
void UtilityEngine::resetTotalDistance() {
  // Save any unsaved distance before resetting
  saveCurrentSessionStats();
  
  // Now reset counters using StatsTracking method
  stats.reset();
  info("🔄 Total distance counter reset to 0");
}

/**
 * Update effective max distance based on limit percent
 */
void UtilityEngine::updateEffectiveMaxDistance() {
  effectiveMaxDistanceMM = config.totalDistanceMM * (maxDistanceLimitPercent / 100.0);
  debug(String("📏 Effective max distance: ") + String(effectiveMaxDistanceMM, 1) + 
        " mm (" + String(maxDistanceLimitPercent, 0) + "% of " + 
        String(config.totalDistanceMM, 1) + " mm)");
}

/**
 * Increment daily statistics with distance traveled
 * Saves to /stats.json as array of {date, distanceMM} objects
 */
void UtilityEngine::incrementDailyStats(float distanceMM) {
  if (distanceMM <= 0) return;
  
  // Check if stats recording is disabled
  if (!statsRecordingEnabled) {
    debug("📊 Stats recording disabled - skipping save");
    return;
  }
  
  // Get current date (YYYY-MM-DD format)
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
  
  // Load existing stats
  JsonDocument statsDoc;
  if (LittleFS.exists("/stats.json")) {
    File file = LittleFS.open("/stats.json", "r");
    if (file) {
      deserializeJson(statsDoc, file);
      file.close();
    }
  }
  
  // Find or create today's entry
  JsonArray statsArray = statsDoc.as<JsonArray>();
  if (statsArray.isNull()) {
    statsArray = statsDoc.to<JsonArray>();
  }
  
  bool found = false;
  for (JsonObject entry : statsArray) {
    if (strcmp(entry["date"], dateStr) == 0) {
      float current = entry["distanceMM"] | 0.0;
      entry["distanceMM"] = current + distanceMM;
      found = true;
      break;
    }
  }
  
  if (!found) {
    JsonObject newEntry = statsArray.add<JsonObject>();
    newEntry["date"] = dateStr;
    newEntry["distanceMM"] = distanceMM;
  }
  
  // Save back to file
  File file = LittleFS.open("/stats.json", "w");
  if (!file) {
    error("Failed to write stats.json");
    return;
  }
  
  serializeJson(statsDoc, file);
  file.close();
  
  debug(String("📊 Stats: +") + String(distanceMM, 1) + "mm on " + String(dateStr));
}

/**
 * Get today's total distance from stats
 */
float UtilityEngine::getTodayDistance() {
  // Get current date
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
  
  // Load stats
  JsonDocument statsDoc;
  if (!LittleFS.exists("/stats.json")) {
    return 0.0;
  }
  
  File file = LittleFS.open("/stats.json", "r");
  if (!file) {
    return 0.0;
  }
  
  deserializeJson(statsDoc, file);
  file.close();
  
  // Find today's entry
  JsonArray statsArray = statsDoc.as<JsonArray>();
  for (JsonObject entry : statsArray) {
    if (strcmp(entry["date"], dateStr) == 0) {
      return entry["distanceMM"] | 0.0;
    }
  }
  
  return 0.0;
}



