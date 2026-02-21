// ============================================================================
// UTILITY ENGINE IMPLEMENTATION
// ============================================================================
// Facade orchestration: constructor, initialization sequence, time utilities,
// status reporting, and Preferences/NVS bridge methods.
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

  // Initialize Preferences (NVS)
  _eeprom.begin(128);

  // Load logging preferences from NVS -> restore into Logger
  loadLoggingPreferences();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool UtilityEngine::initialize() {
  info("[UtilityEngine] Initializing...");

  // STEP 1: Mount filesystem
  _fs.mount();

  // STEP 2: Create /logs directory + initialize log file
  if (_fs.isReady()) {
    if (!_fs.directoryExists("/logs")) {
      info("[UtilityEngine] Creating /logs directory...");
      if (_fs.createDirectory("/logs")) {
        info("[UtilityEngine] /logs directory created");
      } else {
        warn("[UtilityEngine] Failed to create /logs - continuing anyway");
      }
    }
  } else {
    warn("[UtilityEngine] Log directory creation SKIPPED (filesystem not mounted)");
  }

  // Wait for NTP sync
  delay(1000);

  // STEP 3: Initialize Logger (log file)
  if (_fs.isReady()) {
    if (!_logger.initializeLogFile()) {
      info("[UtilityEngine] Log file initialization deferred (NTP not ready)");
    }
  } else {
    warn("[UtilityEngine] Log file init SKIPPED (filesystem not available - logs to Serial only)");
  }

  // STEP 4: Initialize StatsManager (load stats recording pref)
  _stats.initialize();

  // STEP 5: Restore sensors inversion from NVS → global state
  loadSensorsInverted();

  info(String("[UtilityEngine] Initialization complete (degraded mode = ") + String(!_fs.isReady() ? "YES" : "NO") + ")");
  return true;
}

void UtilityEngine::shutdown() {
  _logger.shutdown();
}

// ============================================================================
// NVS BRIDGE: LOGGING PREFERENCES
// ============================================================================

void UtilityEngine::loadLoggingPreferences() {
  bool enabled = true;
  uint8_t level = static_cast<uint8_t>(LogLevel::LOG_INFO);
  _eeprom.loadLoggingPreferences(enabled, level);
  _logger.restoreState(enabled, (LogLevel)level);
}

// ============================================================================
// NVS BRIDGE: SENSORS INVERSION
// ============================================================================

void UtilityEngine::loadSensorsInverted() {
  bool inverted = false;
  _eeprom.loadSensorsInverted(inverted);
  sensorsInverted = inverted;
}

void UtilityEngine::saveSensorsInverted() {
  _eeprom.saveSensorsInverted(sensorsInverted);
  info(String("Sensors mode: ") + (sensorsInverted ? "INVERTED" : "NORMAL") + " (saved to NVS)");
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

String UtilityEngine::getFormattedTime(const char* format) const {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  std::array<char, 64> buffer{};
  strftime(buffer.data(), buffer.size(), format, &timeinfo);
  return String(buffer.data());
}

bool UtilityEngine::isTimeSynchronized() const {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  return (timeinfo.tm_year > (2020 - 1900));
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

  constexpr std::array levelNames = {"ERROR", "WARN", "INFO", "DEBUG"};
  auto lvl = (int)status.currentLogLevel;

  // Use Serial directly for formatted status dump — this is intentionally
  // a diagnostic console output (not a log event)
  Serial.println("\n[UtilityEngine] ======================================");
  Serial.println("[UtilityEngine] STATUS REPORT");
  Serial.println("[UtilityEngine] ======================================");
  Serial.println(String("[UtilityEngine] Filesystem: ") + (status.filesystemReady ? "Ready" : "Failed"));
  Serial.println(String("[UtilityEngine] Log File: ") + (status.fileOpen ? "Open" : "Closed"));
  Serial.println(String("[UtilityEngine] Time Sync: ") + (status.timeSynced ? "Synchronized" : "Pending"));
  Serial.println(String("[UtilityEngine] WebSocket Clients: ") + String(status.connectedClients));
  Serial.println(String("[UtilityEngine] Disk Usage: ") + String(status.diskUsagePercent, 1) + "% (" +
                 String(status.usedBytes / 1024) + "KB / " + String(status.totalBytes / 1024 / 1024) + "MB)");
  Serial.println(String("[UtilityEngine] Log Level: ") + (lvl >= 0 && lvl <= 3 ? levelNames[lvl] : "UNKNOWN"));
  Serial.println(String("[UtilityEngine] Current Log: ") + status.currentLogFile);
  Serial.println("[UtilityEngine] ======================================\n");
}
