// ============================================================================
// LOGGER IMPLEMENTATION
// ============================================================================

#include "core/logger/Logger.h"
#include "core/filesystem/FileSystem.h"
#include "core/UtilityEngine.h"  // For LogLevel enum values
#include <time.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

Logger::Logger(WebSocketsServer& ws, FileSystem& fs)
  : _ws(ws),
    _fs(fs),
    _currentLogLevel(LOG_INFO),
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
    File logFile = logsDir.openNextFile();
    while (logFile) {
      String fileName = String(logFile.name());
      if (fileName.indexOf("1970") >= 0) {
        String fullPath = "/logs/" + fileName;
        logFile.close();
        if (LittleFS.remove(fullPath)) {
          Serial.println("[Logger] 🗑️ Removed epoch file: " + fullPath);
        }
        logFile = logsDir.openNextFile();
      } else {
        logFile = logsDir.openNextFile();
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
  char ts[30];
  time_t now = time(nullptr);
  struct tm* tmstruct = localtime(&now);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmstruct);

  _logFile.println("");
  _logFile.println("========================================");
  _logFile.print("SESSION START: ");
  _logFile.println(ts);
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
  if (_ws.connectedClients() > 0) {
    // Pre-sized buffer avoids heap allocation for typical log messages
    static const char* levelNames[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    const char* levelName = (level >= 0 && level <= 3) ? levelNames[level] : "INFO";
    
    JsonDocument doc;
    doc["type"] = "log";
    doc["level"] = levelName;
    doc["message"] = message;

    String payload;
    serializeJson(doc, payload);
    _ws.broadcastTXT(payload);
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

void Logger::error(const String& message) { log(LOG_ERROR, message); }
void Logger::warn(const String& message)  { log(LOG_WARNING, message); }
void Logger::info(const String& message)  { log(LOG_INFO, message); }
void Logger::debug(const String& message) { log(LOG_DEBUG, message); }

// ============================================================================
// FLUSH BUFFER
// ============================================================================

void Logger::flushLogBuffer(bool forceFlush) {
  if (!_logFile || !_fs.isReady() || !_logMutex) return;

  unsigned long now = millis();

  // Take mutex to safely read buffer (short hold: count + copy)
  if (xSemaphoreTake(_logMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  int validEntries = _logBufferCount;

  float bufferUsagePercent = (validEntries * 100.0) / LOG_BUFFER_SIZE;

  // CRITICAL: Force flush if buffer is 80% full
  bool shouldForce = (bufferUsagePercent >= 80.0);

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
  LogEntry localBuffer[LOG_BUFFER_SIZE];
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
  struct tm* tmstruct = localtime(&currentTime);
  bool timeValid = (tmstruct->tm_year > (2020 - 1900));

  // Write all valid entries in one batch (no mutex needed - local copy)
  for (int i = 0; i < localCount; i++) {
    {
      if (timeValid) {
        char ts[30];
        time_t logTime = currentTime - ((now - localBuffer[i].timestamp) / 1000);
        struct tm* logTm = localtime(&logTime);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", logTm);
        _logFile.print("[");
        _logFile.print(ts);
        _logFile.print("] ");
      } else {
        _logFile.print("[T+");
        _logFile.print(localBuffer[i].timestamp / 1000);
        _logFile.print("s] ");
      }

      _logFile.println(localBuffer[i].message);
    }
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
  struct tm* tmstruct = localtime(&now);

  char dateBuf[20];
  strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", tmstruct);
  String dateStr = String(dateBuf);

  // Find max suffix by scanning /logs directory
  int maxSuffix = -1;

  // Direct LittleFS: intentional (directory iteration not supported by FileSystem wrapper)
  File scanDir = LittleFS.open("/logs");
  if (scanDir) {
    File file = scanDir.openNextFile();
    while (file) {
      String fileName = String(file.name());
      String prefix = "log_" + dateStr + "_";

      if (fileName.startsWith(prefix) && fileName.endsWith(".txt")) {
        String suffixStr = fileName.substring(
          prefix.length(),
          fileName.length() - 4
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

const char* Logger::getLevelPrefix(LogLevel level) const {
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
  struct tm* timeinfo = localtime(&now);
  return (timeinfo->tm_year > (2020 - 1900));
}
