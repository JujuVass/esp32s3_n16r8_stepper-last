// ============================================================================
// UTILITY ENGINE IMPLEMENTATION
// ============================================================================

#include "core/UtilityEngine.h"
#include "core/Types.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <math.h>

// EEPROM addresses for preferences
#define EEPROM_SIZE 128   // Increased: 0-2 for logging/stats, 3-100 for WiFi config
#define EEPROM_ADDR_LOGGING_ENABLED 0
#define EEPROM_ADDR_LOG_LEVEL 1
#define EEPROM_ADDR_STATS_ENABLED 2  // Stats recording enabled (1=enabled, 0=disabled)

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
  
  // Mount LittleFS
  Serial.println("[UtilityEngine] Mounting LittleFS...");
  if (!LittleFS.begin(true)) {  // true = format if mount fails
    Serial.println("[UtilityEngine] ❌ LittleFS mount FAILED!");
    littleFsMounted = false;
    return false;
  }
  
  littleFsMounted = true;
  Serial.println("[UtilityEngine] ✅ LittleFS mounted successfully");
  
  // Create logs directory if needed
  if (!directoryExists("/logs")) {
    if (!createDirectory("/logs")) {
      Serial.println("[UtilityEngine] ⚠️ Failed to create /logs directory");
    }
  }
  
  // Clean up epoch date log files (created before NTP sync)
  Serial.println("[UtilityEngine] Cleaning up epoch log files...");
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
  
  // Wait for NTP sync (should be done by main firmware)
  delay(1000);
  
  // Initialize log file
  if (!initializeLogFile()) {
    Serial.println("[UtilityEngine] ⚠️ Log file initialization deferred (NTP not ready)");
    // Not fatal - will retry on first log message
  }
  
  Serial.println("[UtilityEngine] ✅ Initialization complete");
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
  
  globalLogFile.flush();
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
  if (!file) return false;
  
  size_t written = file.print(data);
  file.close();
  
  return written == data.length();
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
  
  // CRITICAL: Flush before close to ensure data is written to flash
  file.flush();
  file.close();
  
  if (bytesWritten == 0) {
    error("Failed to write JSON to: " + path);
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
  // EXTRACT MOTION CONFIG
  // ========================================================================
  if (doc["motion"].is<JsonObject>()) {
    JsonObject motionObj = doc["motion"];
    config.motion.startPositionMM = motionObj["startPositionMM"] | 0.0;
    config.motion.targetDistanceMM = motionObj["targetDistanceMM"] | 50.0;
    config.motion.speedLevelForward = motionObj["speedLevelForward"] | 5.0;
    config.motion.speedLevelBackward = motionObj["speedLevelBackward"] | 5.0;
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
  // EXTRACT DECELERATION ZONE
  // ========================================================================
  if (doc["decelZone"].is<JsonObject>()) {
    JsonObject decelObj = doc["decelZone"];
    config.decelZone.enabled = decelObj["enabled"] | false;
    config.decelZone.zoneMM = decelObj["zoneMM"] | 50.0;
    config.decelZone.effectPercent = decelObj["effectPercent"] | 75.0;
    config.decelZone.mode = (DecelMode)(decelObj["mode"] | (int)DECEL_SINE);
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
  // SAVE MOTION CONFIG
  // ========================================================================
  JsonObject motionObj = doc["motion"].to<JsonObject>();
  motionObj["startPositionMM"] = config.motion.startPositionMM;
  motionObj["targetDistanceMM"] = config.motion.targetDistanceMM;
  motionObj["speedLevelForward"] = config.motion.speedLevelForward;
  motionObj["speedLevelBackward"] = config.motion.speedLevelBackward;
  
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
  // SAVE DECELERATION ZONE
  // ========================================================================
  JsonObject decelObj = doc["decelZone"].to<JsonObject>();
  decelObj["enabled"] = config.decelZone.enabled;
  decelObj["zoneMM"] = config.decelZone.zoneMM;
  decelObj["effectPercent"] = config.decelZone.effectPercent;
  decelObj["mode"] = (int)config.decelZone.mode;
  
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
 */
void UtilityEngine::saveLoggingPreferences() {
  EEPROM.write(EEPROM_ADDR_LOGGING_ENABLED, loggingEnabled ? 1 : 0);
  EEPROM.write(EEPROM_ADDR_LOG_LEVEL, (uint8_t)currentLogLevel);
  EEPROM.commit();
  
  Serial.println("[UtilityEngine] 💾 Logging preferences saved to EEPROM");
}

/**
 * Load logging preferences from EEPROM
 * Reads saved preferences or uses defaults if uninitialized (0xFF)
 */
void UtilityEngine::loadLoggingPreferences() {
  uint8_t enabledByte = EEPROM.read(EEPROM_ADDR_LOGGING_ENABLED);
  uint8_t levelByte = EEPROM.read(EEPROM_ADDR_LOG_LEVEL);
  
  // Check if EEPROM is uninitialized (fresh ESP32)
  if (enabledByte == 0xFF) {
    // First boot: use defaults and save them
    loggingEnabled = true;
    currentLogLevel = LOG_INFO;
    saveLoggingPreferences();
    Serial.println("[UtilityEngine] 🔧 First boot: initialized EEPROM with default logging preferences");
  } else {
    // Load saved preferences
    loggingEnabled = (enabledByte == 1);
    
    // Validate log level (0-3 range)
    if (levelByte <= LOG_DEBUG) {
      currentLogLevel = (LogLevel)levelByte;
    } else {
      currentLogLevel = LOG_INFO;  // Fallback to default
    }
    
    Serial.print("[UtilityEngine] 📂 Loaded logging preferences from EEPROM: ");
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
    EEPROM.commit();
    Serial.println("[UtilityEngine] 🔧 First boot: stats recording enabled by default");
  } else {
    statsRecordingEnabled = (statsByte == 1);
    Serial.print("[UtilityEngine] 📂 Stats recording: ");
    Serial.println(statsRecordingEnabled ? "ENABLED" : "DISABLED");
  }
}

// ============================================================================
// STATS RECORDING PREFERENCE
// ============================================================================

/**
 * Enable/disable stats recording (saved in EEPROM)
 */
void UtilityEngine::setStatsRecordingEnabled(bool enabled) {
  statsRecordingEnabled = enabled;
  EEPROM.write(EEPROM_ADDR_STATS_ENABLED, enabled ? 1 : 0);
  EEPROM.commit();
  
  info(String("📊 Stats recording: ") + (enabled ? "ENABLED" : "DISABLED") + " (saved to EEPROM)");
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



