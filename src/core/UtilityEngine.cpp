// ============================================================================
// UTILITY ENGINE IMPLEMENTATION
// ============================================================================
// Facade orchestration: constructor, initialization sequence, time utilities,
// status reporting, and EEPROM bridge methods.
// ============================================================================

#include "core/UtilityEngine.h"
#include "core/GlobalState.h"  // For sensorsInverted, effectiveMaxDistanceMM, etc.
#include <time.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

UtilityEngine::UtilityEngine(WebSocketsServer& webSocketServer)
  : _ws(webSocketServer),
    _fs(),
    _eeprom(),
    _logger(webSocketServer, _fs),
    _stats(_fs, _eeprom) {

  // Initialize EEPROM
  _eeprom.begin(128);

  // Load logging preferences from EEPROM -> restore into Logger
  loadLoggingPreferences();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool UtilityEngine::initialize() {
  Serial.println("\n[UtilityEngine] Initializing...");

  // STEP 1: Mount filesystem
  _fs.mount();

  // STEP 2: Create /logs directory + initialize log file
  if (_fs.isReady()) {
    if (!_fs.directoryExists("/logs")) {
      Serial.println("[UtilityEngine] Creating /logs directory...");
      if (_fs.createDirectory("/logs")) {
        Serial.println("[UtilityEngine] /logs directory created");
      } else {
        Serial.println("[UtilityEngine] Failed to create /logs - continuing anyway");
      }
    }
  } else {
    Serial.println("[UtilityEngine] Log directory creation SKIPPED (filesystem not mounted)");
  }

  // Wait for NTP sync
  delay(1000);

  // STEP 3: Initialize Logger (log file)
  if (_fs.isReady()) {
    if (!_logger.initializeLogFile()) {
      Serial.println("[UtilityEngine] Log file initialization deferred (NTP not ready)");
    }
  } else {
    Serial.println("[UtilityEngine] Log file init SKIPPED (filesystem not available - logs to Serial only)");
  }

  // STEP 4: Initialize StatsManager (load stats recording pref)
  _stats.initialize();

  // STEP 5: Restore sensors inversion from EEPROM → global state
  loadSensorsInverted();

  Serial.println(String("[UtilityEngine] Initialization complete (degraded mode = ") + String(!_fs.isReady() ? "YES" : "NO") + ")");
  return true;
}

void UtilityEngine::shutdown() {
  _logger.shutdown();
}

// ============================================================================
// EEPROM BRIDGE: LOGGING PREFERENCES
// ============================================================================

void UtilityEngine::loadLoggingPreferences() {
  bool enabled = true;
  uint8_t level = LOG_INFO;
  _eeprom.loadLoggingPreferences(enabled, level);
  _logger.restoreState(enabled, (LogLevel)level);
}

// ============================================================================
// EEPROM BRIDGE: SENSORS INVERSION
// ============================================================================

void UtilityEngine::loadSensorsInverted() {
  bool inverted = false;
  _eeprom.loadSensorsInverted(inverted);
  sensorsInverted = inverted;
}

void UtilityEngine::saveSensorsInverted() {
  _eeprom.saveSensorsInverted(sensorsInverted);
  info(String("Sensors mode: ") + (sensorsInverted ? "INVERTED" : "NORMAL") + " (saved to EEPROM)");
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

String UtilityEngine::getFormattedTime(const char* format) const {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[64];
  strftime(buffer, sizeof(buffer), format, timeinfo);
  return String(buffer);
}

bool UtilityEngine::isTimeSynchronized() const {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  return (timeinfo->tm_year > (2020 - 1900));
}

// ============================================================================
// STATUS REPORTING
// ============================================================================

UtilityEngine::EngineStatus UtilityEngine::getStatus() const {
  EngineStatus status;
  status.filesystemReady = _fs.isReady();
  status.fileOpen = !_logger.getCurrentLogFile().isEmpty();
  status.timeSynced = isTimeSynchronized();
  status.connectedClients = _ws.connectedClients();
  status.totalBytes = _fs.getTotalBytes();
  status.usedBytes = _fs.getUsedBytes();
  status.diskUsagePercent = _fs.getDiskUsagePercent();
  status.currentLogLevel = _logger.getLogLevel();
  status.currentLogFile = _logger.getCurrentLogFile();
  return status;
}

void UtilityEngine::printStatus() const {
  EngineStatus status = getStatus();

  Serial.println("\n[UtilityEngine] ======================================");
  Serial.println("[UtilityEngine] STATUS REPORT");
  Serial.println("[UtilityEngine] ======================================");
  Serial.print("[UtilityEngine] Filesystem: ");
  Serial.println(status.filesystemReady ? "Ready" : "Failed");
  Serial.print("[UtilityEngine] Log File: ");
  Serial.println(status.fileOpen ? "Open" : "Closed");
  Serial.print("[UtilityEngine] Time Sync: ");
  Serial.println(status.timeSynced ? "Synchronized" : "Pending");
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
  const char* levelNames[] = {"ERROR", "WARN", "INFO", "DEBUG"};
  int lvl = (int)status.currentLogLevel;
  Serial.println(lvl >= 0 && lvl <= 3 ? levelNames[lvl] : "UNKNOWN");
  Serial.print("[UtilityEngine] Current Log: ");
  Serial.println(status.currentLogFile);
  Serial.println("[UtilityEngine] ======================================\n");
}
