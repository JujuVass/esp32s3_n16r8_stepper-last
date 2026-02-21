// ============================================================================
// LOGGER - Multi-Channel Structured Logging
// ============================================================================
// Multi-level logging (ERROR, WARN, INFO, DEBUG) with three output channels:
// - Serial console
// - WebSocket broadcast to connected clients
// - Buffered file output with circular log buffer
// Includes log file management (create, rotate).
// ============================================================================

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <array>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "core/Config.h"

// Forward declaration
class FileSystem;
enum class LogLevel : int;

// ============================================================================
// LOG BUFFER CONFIGURATION
// ============================================================================
// LOG_BUFFER_SIZE is defined in Config.h (constexpr int LOG_BUFFER_SIZE = 100)
constexpr const char* LOG_FILE_PATTERN = "/logs/log_";
constexpr const char* LOG_FILE_EXTENSION = ".txt";

// ============================================================================
// LOG ENTRY STRUCTURE
// ============================================================================
struct LogEntry {
  unsigned long timestamp = 0;  // millis() when log was created
  LogLevel level = (LogLevel)2;
  String message;

  LogEntry() = default;
};

// ============================================================================
// LOGGER CLASS
// ============================================================================
class Logger {
public:
  // ========================================================================
  // CONSTRUCTOR & LIFECYCLE
  // ========================================================================

  /**
   * @param ws Reference to WebSocketsServer for WS broadcast
   * @param fs Reference to FileSystem for log file operations
   */
  Logger(WebSocketsServer& ws, FileSystem& fs);

  /**
   * Initialize log file (creates /logs dir, opens session file)
   * Must be called after FileSystem::mount() and NTP sync
   * @return true if log file opened successfully
   */
  bool initializeLogFile();

  /**
   * Shutdown: flush + close log file
   */
  void shutdown();

  // ========================================================================
  // LOGGING INTERFACE
  // ========================================================================

  /**
   * Main logging function — outputs to Serial + WebSocket + File buffer
   * @param level Severity level
   * @param message Log message
   */
  void log(LogLevel level, const String& message);

  // Convenience methods
  void error(const String& message);
  void warn(const String& message);
  void info(const String& message);
  void debug(const String& message);

  /**
   * Flush log buffer to disk
   * @param forceFlush Force flush even during movement
   */
  void flushLogBuffer(bool forceFlush = false);

  // ========================================================================
  // LOG LEVEL MANAGEMENT
  // ========================================================================

  void setLogLevel(LogLevel level) { _currentLogLevel = level; }
  LogLevel getLogLevel() const { return _currentLogLevel; }

  /** Fast check for hot-path debug guards */
  bool isDebugEnabled() const { return _loggingEnabled && _currentLogLevel >= (LogLevel)3; }

  void setLoggingEnabled(bool enabled) { _loggingEnabled = enabled; }
  bool isLoggingEnabled() const { return _loggingEnabled; }

  // ========================================================================
  // LOG FILE INFO
  // ========================================================================

  String getCurrentLogFile() const { return _currentLogFileName; }

  // ========================================================================
  // STATE RESTORATION (called by UtilityEngine after NVS load)
  // ========================================================================

  /** Set initial state from NVS values (no save triggered) */
  void restoreState(bool enabled, LogLevel level) {
    _loggingEnabled = enabled;
    _currentLogLevel = level;
  }

private:
  WebSocketsServer& _ws;
  FileSystem& _fs;

  // Log file state
  File _logFile;
  String _currentLogFileName;

  // Logging state
  LogLevel _currentLogLevel;
  bool _loggingEnabled;

  // Circular log buffer (protected by _logMutex for cross-core safety)
  // Head/tail ring buffer: _head = next write position, _count = valid entries
  std::array<LogEntry, LOG_BUFFER_SIZE> _logBuffer;
  int _logBufferHead;            // Next write position (0..LOG_BUFFER_SIZE-1)
  int _logBufferCount;           // Number of valid entries (0..LOG_BUFFER_SIZE)
  unsigned long _lastLogFlush;
  SemaphoreHandle_t _logMutex;

  // ========================================================================
  // PRIVATE HELPERS
  // ========================================================================

  /** Generate log filename with session suffix */
  String generateLogFilename();

  /** Get log level prefix string ([ERROR], [WARN], etc.) */
  const char* getLevelPrefix(LogLevel level) const;

  /** Check if NTP time is synchronized */
  bool isTimeSynchronized() const;
};

#endif // LOGGER_H
