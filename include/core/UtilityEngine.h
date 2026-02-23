// ============================================================================
// UTILITY ENGINE - System Services Facade
// ============================================================================
// Lightweight facade that coordinates four focused sub-systems:
//   - Logger         (core/logger/)      — multi-channel structured logging
//   - FileSystem     (core/filesystem/)   — LittleFS wrapper + JSON helpers
//   - StatsManager   (core/stats/)        — daily distance statistics
//   - EepromManager  (core/eeprom/)       — NVS persistence (Preferences API)
//
// Public methods forward inline to sub-objects, providing a single
// access point for all system services.
// ============================================================================

#ifndef UTILITY_ENGINE_H
#define UTILITY_ENGINE_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include "Types.h"  // For SystemState, ExecutionContext enums

// Sub-object headers
#include "core/logger/Logger.h"
#include "core/filesystem/FileSystem.h"
#include "core/stats/StatsManager.h"
#include "core/eeprom/EepromManager.h"

// ============================================================================
// LOG LEVEL ENUM
// ============================================================================
enum class LogLevel : int {
  LOG_ERROR = 0,
  LOG_WARNING = 1,
  LOG_INFO = 2,
  LOG_DEBUG = 3
};

// ============================================================================
// SYSTEM CONFIGURATION STRUCT
// ============================================================================
// Single Source of Truth (SSoT) for fields that are NOT duplicated as globals.
// Runtime-volatile state (motion, oscillation, chaos, pursuit, etc.) lives
// in dedicated globals declared in GlobalState.h and module headers.
//
// Fields kept here: core system state, calibration results, contact persistence,
// and sequencer ID counter. All other runtime state uses globals directly.
// ============================================================================
struct SystemConfig {

  // ========================================================================
  // SYSTEM STATE (SSoT - used by all modules via config.currentState)
  // volatile: both fields are read/written from both cores
  // ========================================================================
  volatile SystemState currentState = SystemState::STATE_INIT;         // Current operation state (INIT, RUNNING, PAUSED, etc.)
  volatile ExecutionContext executionContext = ExecutionContext::CONTEXT_STANDALONE; // Execution context (STANDALONE vs SEQUENCER)

  // ========================================================================
  // CALIBRATION RESULTS (SSoT - written by CalibrationManager)
  // ========================================================================
  long minStep = 0;                      // Minimum position (after START contact)
  long maxStep = 0;                      // Maximum position (before END contact)
  float totalDistanceMM = 0;             // Total travel distance (mm)

  // ========================================================================
  // CONTACT SENSING PERSISTENCE
  // ========================================================================
  int lastStartContactState = HIGH;      // Last read START contact state
  int lastEndContactState = HIGH;        // Last read END contact state
  bool currentMotorDirection = HIGH;     // Current motor direction (HIGH/LOW)

  // ========================================================================
  // SEQUENCER (SSoT - auto-increment ID, used by SequenceTableManager)
  // ========================================================================
  int nextLineId = 1;                    // Auto-increment ID counter

  SystemConfig() = default;
};

// ============================================================================
// UTILITY ENGINE CLASS
// ============================================================================
class UtilityEngine { // NOSONAR(cpp:S1448) Facade pattern — all methods are one-line forwarders

public:
  // ========================================================================
  // CONSTRUCTOR & LIFECYCLE
  // ========================================================================

  explicit UtilityEngine(AsyncWebSocket& webSocket);

  /**
   * Full initialization sequence:
   * 1. EEPROM → 2. FileSystem → 3. Logger → 4. StatsManager
   */
  bool initialize();

  /** Cleanup before shutdown or OTA update */
  void shutdown();

  // ========================================================================
  // LOGGING FACADE (inline forwarding — zero overhead)
  // ========================================================================

  void log(LogLevel level, const String& message)  { _logger.log(level, message); }
  void error(const String& message)                { _logger.error(message); }
  void warn(const String& message)                 { _logger.warn(message); }
  void info(const String& message)                 { _logger.info(message); }
  void debug(const String& message)                { _logger.debug(message); }
  void flushLogBuffer(bool forceFlush = false)      { _logger.flushLogBuffer(forceFlush); }
  void setLogLevel(LogLevel level)                  { _logger.setLogLevel(level); }
  LogLevel getLogLevel() const                      { return _logger.getLogLevel(); }
  bool isDebugEnabled() const                       { return _logger.isDebugEnabled(); }
  void setLoggingEnabled(bool enabled)              { _logger.setLoggingEnabled(enabled); }
  bool isLoggingEnabled() const                     { return _logger.isLoggingEnabled(); }
  String getCurrentLogFile() const                  { return _logger.getCurrentLogFile(); }

  // ========================================================================
  // PREFERENCES FACADE (logging preferences — bridges Logger + EepromManager)
  // ========================================================================

  void saveLoggingPreferences() {
    _eeprom.saveLoggingPreferences(_logger.isLoggingEnabled(), (uint8_t)_logger.getLogLevel());
  }
  void loadLoggingPreferences();  // Implemented in .cpp (loads + restores Logger state)

  // ========================================================================
  // FILESYSTEM FACADE
  // ========================================================================

  bool isFilesystemReady() const                            { return _fs.isReady(); }
  bool fileExists(const String& path) const                 { return _fs.fileExists(path); }
  bool directoryExists(const String& path) const            { return _fs.directoryExists(path); }
  String readFileAsString(const String& path, size_t maxSize = 1024 * 1024) { return _fs.readFileAsString(path, maxSize); }
  bool writeFileAsString(const String& path, const String& data) { return _fs.writeFileAsString(path, data); }
  bool deleteFile(const String& path)                       { return _fs.deleteFile(path); }
  bool createDirectory(const String& path)                  { return _fs.createDirectory(path); }
  uint32_t getTotalBytes() const                            { return _fs.getTotalBytes(); }
  uint32_t getUsedBytes() const                             { return _fs.getUsedBytes(); }
  uint32_t getAvailableBytes() const                        { return _fs.getAvailableBytes(); }
  float getDiskUsagePercent() const                         { return _fs.getDiskUsagePercent(); }
  bool loadJsonFile(const String& path, JsonDocument& doc)  { return _fs.loadJsonFile(path, doc); }
  bool saveJsonFile(const String& path, const JsonDocument& doc) { return _fs.saveJsonFile(path, doc); }

  // ========================================================================
  // STATISTICS FACADE
  // ========================================================================

  void incrementDailyStats(float distanceMM)  { _stats.incrementDailyStats(distanceMM); }
  float getTodayDistance()                     { return _stats.getTodayDistance(); }
  void setStatsRecordingEnabled(bool enabled)  { _stats.setStatsRecordingEnabled(enabled); }
  bool isStatsRecordingEnabled() const         { return _stats.isStatsRecordingEnabled(); }
  void saveCurrentSessionStats()               { _stats.saveCurrentSessionStats(); }
  void resetTotalDistance()                     { _stats.resetTotalDistance(); }
  void updateEffectiveMaxDistance()             { _stats.updateEffectiveMaxDistance(); }

  // ========================================================================
  // SENSORS NVS FACADE
  // ========================================================================

  void loadSensorsInverted();  // Implemented in .cpp (bridges NVS → global)
  void saveSensorsInverted();  // Implemented in .cpp (bridges global → NVS)

  // ========================================================================
  // TIME UTILITIES (kept here — tiny, no dedicated class needed)
  // ========================================================================

  String getFormattedTime(const char* format = "%Y-%m-%d %H:%M:%S") const;
  bool isTimeSynchronized() const;

  // ========================================================================
  // STATE INSPECTION
  // ========================================================================

  struct EngineStatus {
    bool filesystemReady;
    bool fileOpen;
    bool timeSynced;
    uint8_t connectedClients;
    uint32_t totalBytes;
    uint32_t usedBytes;
    float diskUsagePercent;
    LogLevel currentLogLevel;
    String currentLogFile;
  };

  EngineStatus getStatus() const;
  void printStatus() const;

private:
  // ========================================================================
  // SUB-OBJECTS (owned)
  // ========================================================================

  AsyncWebSocket& _ws;
  FileSystem     _fs;
  EepromManager  _eeprom;
  Logger         _logger;
  StatsManager   _stats;

}; // class UtilityEngine

// ============================================================================
// GLOBAL ENGINE POINTER (defined in main.ino, accessible everywhere)
// ============================================================================
extern UtilityEngine* engine;



#endif // UTILITY_ENGINE_H
