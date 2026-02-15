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

// Forward declaration
class FileSystem;
enum LogLevel : int;

// ============================================================================
// LOG BUFFER CONFIGURATION
// ============================================================================
#define LOG_BUFFER_SIZE 100
#define LOG_FILE_PATTERN "/logs/log_"
#define LOG_FILE_EXTENSION ".txt"

// ============================================================================
// LOG ENTRY STRUCTURE
// ============================================================================
struct LogEntry {
  unsigned long timestamp;  // millis() when log was created
  LogLevel level;
  String message;
  bool valid;

  LogEntry() : timestamp(0), level((LogLevel)2), message(""), valid(false) {}
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
  // STATE RESTORATION (called by UtilityEngine after EEPROM load)
  // ========================================================================

  /** Set initial state from EEPROM values (no save triggered) */
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

  // Circular log buffer
  LogEntry _logBuffer[LOG_BUFFER_SIZE];
  int _logBufferWriteIndex;
  unsigned long _lastLogFlush;

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
