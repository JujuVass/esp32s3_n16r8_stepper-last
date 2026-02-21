// ============================================================================
// LOGGER IMPLEMENTATION
// ============================================================================

#include "core/logger/Logger.h"
#include "core/filesystem/FileSystem.h"
#include "core/UtilityEngine.h"  // For LogLevel enum values
#include <time.h>

// WebSocket mutex — declared in GlobalState.h, defined in StepperController.cpp
// Used here to guard broadcastTXT from cross-core races
extern SemaphoreHandle_t wsMutex;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

Logger::Logger(WebSocketsServer& ws, FileSystem& fs)
  : _ws(ws),
    _fs(fs),
    _currentLogLevel(LogLevel::LOG_INFO),
    _loggingEnabled(true),
    _logBufferHead(0),
    _logBufferCount(0),
    _lastLogFlush(0),
    _logMutex(nullptr) {
  _logMutex = xSemaphoreCreateMutex();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool Logger::initializeLogFile() {
  if (!_fs.isReady()) return false;

  if (!isTimeSynchronized()) {
    Serial.println("[Logger] ⏳ NTP not synced yet - cannot create log file");
    return false;
  }

  // Create /logs directory if needed
  if (!_fs.directoryExists("/logs")) {
    Serial.println("[Logger] 📁 Creating /logs directory...");
    _fs.createDirectory("/logs");
  }

  // Clean up epoch date log files (created before NTP sync)
  // Direct LittleFS: intentional (directory iteration not supported by FileSystem wrapper)
  File logsDir = LittleFS.open("/logs");
  if (logsDir && logsDir.isDirectory()) {
    for (File logFile = logsDir.openNextFile(); logFile; logFile = logsDir.openNextFile()) {
      auto fileName = String(logFile.name());
      if (fileName.indexOf("1970") < 0) continue;
      String fullPath = "/logs/" + fileName;
      logFile.close();
      if (LittleFS.remove(fullPath)) {
        Serial.println("[Logger] 🗑️ Removed epoch file: " + fullPath);
      }
    }
  }

  _currentLogFileName = generateLogFilename();

  // Direct LittleFS: intentional (append mode not supported by FileSystem wrapper)
  _logFile = LittleFS.open(_currentLogFileName, "a");
  if (!_logFile) {
    Serial.println("[Logger] ❌ Failed to open log file: " + _currentLogFileName);
    return false;
  }

  Serial.println("[Logger] ✅ Log file opened: " + _currentLogFileName);

  // Write session header
  std::array<char, 30> ts{};
  time_t now = time(nullptr);
  struct tm tmstruct;
  localtime_r(&now, &tmstruct);
  strftime(ts.data(), ts.size(), "%Y-%m-%d %H:%M:%S", &tmstruct);

  _logFile.println("");
  _logFile.println("========================================");
  _logFile.print("SESSION START: ");
  _logFile.println(ts.data());
  _logFile.println("========================================");
  _logFile.flush();

  return true;
}

void Logger::shutdown() {
  if (_logFile) {
    flushLogBuffer(true);
    _logFile.println("========================================");
    _logFile.println("SESSION ENDING - Engine shutdown");
    _logFile.println("========================================");
    _logFile.close();
  }
}

// ============================================================================
// LOGGING
// ============================================================================

void Logger::log(LogLevel level, const String& message) {
  if (!_loggingEnabled) return;
  if (level > _currentLogLevel) return;

  const char* prefix = getLevelPrefix(level);

  // 1. Serial output (always)
  Serial.print(prefix);
  Serial.println(message);

  // 2. WebSocket broadcast (if clients connected)
  // Guard with wsMutex to prevent cross-core race (Core 1 motor logging vs Core 0 network)
  if (_ws.connectedClients() > 0 && wsMutex != nullptr) {
    if (xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      static constexpr std::array levelNames = {"ERROR", "WARN", "INFO", "DEBUG"};
      auto levelIdx = static_cast<int>(level);
      auto levelName = (levelIdx >= 0 && levelIdx <= 3) ? levelNames[levelIdx] : "INFO";

      JsonDocument doc;
      doc["type"] = "log";
      doc["level"] = levelName;
      doc["message"] = message;

      String payload;
      serializeJson(doc, payload);
      _ws.broadcastTXT(payload);
      xSemaphoreGiveRecursive(wsMutex);
    }
    // If mutex not available, skip WS broadcast (Serial + ring buffer still capture it)
  }

  // 3. Buffer for async file write (if filesystem ready)
  if (_fs.isReady()) {
    // Initialize log file if not yet open (delayed for NTP sync)
    if (!_logFile && isTimeSynchronized()) {
      initializeLogFile();
    }

    if (_logFile && _logMutex && xSemaphoreTake(_logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      _logBuffer[_logBufferHead].timestamp = millis();
      _logBuffer[_logBufferHead].level = level;
      _logBuffer[_logBufferHead].message = String(prefix) + message;

      _logBufferHead = (_logBufferHead + 1) % LOG_BUFFER_SIZE;
      if (_logBufferCount < LOG_BUFFER_SIZE) _logBufferCount++;
      xSemaphoreGive(_logMutex);
    }
  }
}

void Logger::error(const String& message) { log(LogLevel::LOG_ERROR, message); }
void Logger::warn(const String& message)  { log(LogLevel::LOG_WARNING, message); }
void Logger::info(const String& message)  { log(LogLevel::LOG_INFO, message); }
void Logger::debug(const String& message) { log(LogLevel::LOG_DEBUG, message); }

// ============================================================================
// FLUSH BUFFER
// ============================================================================

void Logger::flushLogBuffer(bool forceFlush) {
  if (!_logFile || !_fs.isReady() || !_logMutex) return;

  unsigned long now = millis();

  // Take mutex to safely read buffer (short hold: count + copy)
  if (xSemaphoreTake(_logMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  int validEntries = _logBufferCount;

  float bufferUsagePercent = (static_cast<float>(validEntries) * 100.0f) / LOG_BUFFER_SIZE;

  // CRITICAL: Force flush if buffer is 80% full
  bool shouldForce = (bufferUsagePercent >= 80.0f);

  // Flush every 5 seconds OR if forced/critical
  if (!forceFlush && !shouldForce && now - _lastLogFlush < 5000) {
    xSemaphoreGive(_logMutex);
    return;
  }

  if (validEntries == 0) {
    _lastLogFlush = now;
    xSemaphoreGive(_logMutex);
    return;
  }

  // Copy only valid entries under mutex (O(count) not O(buffer_size))
  // Oldest entry is at (_logBufferHead - _logBufferCount + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE
  int tail = (_logBufferHead - _logBufferCount + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
  std::array<LogEntry, LOG_BUFFER_SIZE> localBuffer;
  int localCount = validEntries;
  for (int i = 0; i < validEntries; i++) {
    int idx = (tail + i) % LOG_BUFFER_SIZE;
    localBuffer[i].timestamp = _logBuffer[idx].timestamp;
    localBuffer[i].level = _logBuffer[idx].level;
    localBuffer[i].message = _logBuffer[idx].message;
    _logBuffer[idx].message = "";
  }
  _logBufferCount = 0;
  xSemaphoreGive(_logMutex);

  // Get current time for timestamps
  time_t currentTime = time(nullptr);
  struct tm tmstruct;
  localtime_r(&currentTime, &tmstruct);
  bool timeValid = (tmstruct.tm_year > (2020 - 1900));

  // Write all valid entries in one batch (no mutex needed - local copy)
  for (int i = 0; i < localCount; i++) {
      if (timeValid) {
        std::array<char, 30> ts{};
        time_t logTime = currentTime - ((now - localBuffer[i].timestamp) / 1000);
        struct tm logTm;
        localtime_r(&logTime, &logTm);
        strftime(ts.data(), ts.size(), "%Y-%m-%d %H:%M:%S", &logTm);
        _logFile.print("[");
        _logFile.print(ts.data());
        _logFile.print("] ");
      } else {
        _logFile.print("[T+");
        _logFile.print(localBuffer[i].timestamp / 1000);
        _logFile.print("s] ");
      }

      _logFile.println(localBuffer[i].message);
  }

  // 🛡️ PROTECTION: Flush AND verify file is still valid
  if (_logFile) {
    _logFile.flush();
    if (!_logFile) {
      Serial.println("[Logger] ⚠️ Log file corrupted during flush - reinitializing...");
      initializeLogFile();
    }
  }

  _lastLogFlush = now;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

String Logger::generateLogFilename() {
  time_t now = time(nullptr);
  struct tm tmstruct;
  localtime_r(&now, &tmstruct);

  std::array<char, 20> dateBuf{};
  strftime(dateBuf.data(), dateBuf.size(), "%Y%m%d", &tmstruct);
  auto dateStr = String(dateBuf.data());

  // Find max suffix by scanning /logs directory
  int maxSuffix = -1;

  // Direct LittleFS: intentional (directory iteration not supported by FileSystem wrapper)
  if (auto scanDir = LittleFS.open("/logs"); scanDir) {
    const String prefix = "log_" + dateStr + "_";
    for (File file = scanDir.openNextFile(); file; file = scanDir.openNextFile()) {
      auto fileName = String(file.name());
      if (!fileName.startsWith(prefix) || !fileName.endsWith(".txt")) continue;
      String suffixStr = fileName.substring(prefix.length(), fileName.length() - 4);
      int suffix = suffixStr.toInt();
      if (suffix > maxSuffix) maxSuffix = suffix;
    }
    scanDir.close();
  }

  return String(LOG_FILE_PATTERN) + dateStr + "_" + String(maxSuffix + 1) + LOG_FILE_EXTENSION;
}

const char* Logger::getLevelPrefix(LogLevel level) const {
  using enum LogLevel;
  switch (level) {
    case LOG_ERROR:   return "[ERROR] ";
    case LOG_WARNING: return "[WARN]  ";
    case LOG_INFO:    return "[INFO]  ";
    case LOG_DEBUG:   return "[DEBUG] ";
    default:          return "[LOG]   ";
  }
}

bool Logger::isTimeSynchronized() const {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  return (timeinfo.tm_year > (2020 - 1900));
}