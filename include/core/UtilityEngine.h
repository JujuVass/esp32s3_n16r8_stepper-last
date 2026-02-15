// ============================================================================
// UTILITY ENGINE - System Services Facade
// ============================================================================
// Lightweight facade that coordinates four focused sub-systems:
//   - Logger         (core/logger/)      — multi-channel structured logging
//   - FileSystem     (core/filesystem/)   — LittleFS wrapper + JSON helpers
//   - StatsManager   (core/stats/)        — daily distance statistics
//   - EepromManager  (core/eeprom/)       — EEPROM persistence
//
// Public methods forward inline to sub-objects, providing a single
// access point for all system services.
// ============================================================================

#ifndef UTILITY_ENGINE_H
#define UTILITY_ENGINE_H

#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
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
enum LogLevel : int {
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
  volatile SystemState currentState;         // Current operation state (INIT, RUNNING, PAUSED, etc.)
  volatile ExecutionContext executionContext; // Execution context (STANDALONE vs SEQUENCER)
  
  // ========================================================================
  // CALIBRATION RESULTS (SSoT - written by CalibrationManager)
  // ========================================================================
  long minStep;                      // Minimum position (after START contact)
  long maxStep;                      // Maximum position (before END contact)
  float totalDistanceMM;             // Total travel distance (mm)
  
  // ========================================================================
  // CONTACT SENSING PERSISTENCE
  // ========================================================================
  int lastStartContactState;         // Last read START contact state
  int lastEndContactState;           // Last read END contact state
  bool currentMotorDirection;        // Current motor direction (HIGH/LOW)
  
  // ========================================================================
  // SEQUENCER (SSoT - auto-increment ID, used by SequenceTableManager)
  // ========================================================================
  int nextLineId;                    // Auto-increment ID counter
  
  // ========================================================================
  // CONSTRUCTOR - Initialize all fields to defaults
  // ========================================================================
  SystemConfig() :
    currentState(STATE_INIT),
    executionContext(CONTEXT_STANDALONE),
    minStep(0),
    maxStep(0),
    totalDistanceMM(0),
    lastStartContactState(HIGH),
    lastEndContactState(HIGH),
    currentMotorDirection(HIGH),
    nextLineId(1) {}
};

// ============================================================================
// UTILITY ENGINE CLASS
// ============================================================================
class UtilityEngine {
  
public:
  // ========================================================================
  // CONSTRUCTOR & LIFECYCLE
  // ========================================================================
  
  UtilityEngine(WebSocketsServer& webSocketServer);
  
  /**
   * Full initialization sequence:
   * 1. EEPROM → 2. FileSystem → 3. Logger → 4. StatsManager
   */
  bool initialize();
  
  /** Cleanup before shutdown or OTA update */
  void shutdown();
  
  // ========================================================================
  // SUB-OBJECT ACCESS (for callers that need direct access)
  // ========================================================================
  
  Logger&        logger()       { return _logger; }
  FileSystem&    fs()           { return _fs; }
  StatsManager&  statsManager() { return _stats; }
  EepromManager& eeprom()       { return _eeprom; }
  
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
  // EEPROM FACADE (logging preferences — bridges Logger + EepromManager)
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
  // WEBSOCKET FACADE
  // ========================================================================
  
  uint8_t getConnectedClients() const   { return _ws.connectedClients(); }
  bool hasConnectedClients() const      { return getConnectedClients() > 0; }
  
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
  // SENSORS EEPROM FACADE
  // ========================================================================
  
  void loadSensorsInverted();  // Implemented in .cpp (bridges EEPROM → global)
  void saveSensorsInverted();  // Implemented in .cpp (bridges global → EEPROM)
  
  // ========================================================================
  // TIME UTILITIES (kept here — tiny, no dedicated class needed)
  // ========================================================================
  
  unsigned long getUptimeSeconds() const { return millis() / 1000; }
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
  
  WebSocketsServer& _ws;
  FileSystem     _fs;
  EepromManager  _eeprom;
  Logger         _logger;
  StatsManager   _stats;
  
}; // class UtilityEngine

// ============================================================================
// GLOBAL ENGINE POINTER (defined in main.ino, accessible everywhere)
// ============================================================================
extern UtilityEngine* engine;

// ============================================================================
// EEPROM COMMIT HELPER (free function, usable by any module)
// ============================================================================
/**
 * Commit EEPROM with retry and exponential backoff
 * @param context Label for log messages (e.g., "Logging", "WiFi", "Stats")
 * @param maxRetries Maximum retry attempts (default: 3)
 * @return true if commit succeeded, false after all retries exhausted
 */
inline bool commitEEPROMWithRetry(const char* context, int maxRetries = 3) {
  bool committed = false;
  for (int attempt = 0; attempt < maxRetries && !committed; attempt++) {
    if (attempt > 0) {
      Serial.println(String("[EEPROM] ⚠️ ") + context + " commit retry #" + String(attempt));
    }
    committed = EEPROM.commit();
    if (!committed) delay(50 * (attempt + 1));
  }
  if (!committed) {
    Serial.println(String("[EEPROM] ❌ ") + context + " commit failed after " + String(maxRetries) + " retries!");
  }
  return committed;
}

#endif // UTILITY_ENGINE_H
