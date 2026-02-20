// ============================================================================
// EEPROM MANAGER IMPLEMENTATION (NVS via Preferences API)
// ============================================================================

#include "core/eeprom/EepromManager.h"
#include "core/UtilityEngine.h"

// Forward declaration
extern UtilityEngine* engine;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

EepromManager::EepromManager() {}

// ============================================================================
// LIFECYCLE
// ============================================================================

void EepromManager::begin(uint16_t size) {
  // Open NVS namespace in read-write mode
  _prefs.begin(NVS_NAMESPACE, false);
  // Serial only here — Logger not ready yet (we're inside UtilityEngine constructor)
  Serial.println("[EepromManager] Preferences (NVS) initialized");
}

void EepromManager::end() {
  _prefs.end();
}

// ============================================================================
// LOGGING PREFERENCES
// ============================================================================

void EepromManager::saveLoggingPreferences(bool enabled, uint8_t level) {
  _prefs.putBool("logEnabled", enabled);
  _prefs.putUChar("logLevel", level);
}

void EepromManager::loadLoggingPreferences(bool& enabled, uint8_t& level) {
  // Preferences returns the default (2nd arg) if key doesn't exist yet
  enabled = _prefs.getBool("logEnabled", true);
  level = _prefs.getUChar("logLevel", 2);  // LOG_INFO

  // Validate log level range (0-3)
  if (level > 3) {
    if (engine) engine->warn("Invalid log level: " + String(level) + " - using default");
    else Serial.println("[EepromManager] Invalid log level: " + String(level) + " - using default");
    level = 2;  // LOG_INFO
    _prefs.putUChar("logLevel", level);
  }

  // Serial only — called during UtilityEngine constructor before engine is set
  Serial.print("[EepromManager] Logging: ");
  Serial.print(enabled ? "ENABLED" : "DISABLED");
  Serial.print(", Level: ");
  Serial.println(level);
}

// ============================================================================
// STATS RECORDING
// ============================================================================

void EepromManager::saveStatsRecording(bool enabled) {
  _prefs.putBool("statsEnabled", enabled);
}

void EepromManager::loadStatsRecording(bool& enabled) {
  enabled = _prefs.getBool("statsEnabled", true);  // Default: enabled
  if (engine) engine->info(String("Stats recording loaded: ") + (enabled ? "ENABLED" : "DISABLED"));
  else Serial.println(String("[EepromManager] Stats recording: ") + (enabled ? "ENABLED" : "DISABLED"));
}

// ============================================================================
// SENSORS INVERSION
// ============================================================================

void EepromManager::saveSensorsInverted(bool inverted) {
  _prefs.putBool("sensInverted", inverted);
}

void EepromManager::loadSensorsInverted(bool& inverted) {
  inverted = _prefs.getBool("sensInverted", false);  // Default: normal
  if (engine) engine->info(String("Sensors mode loaded: ") + (inverted ? "INVERTED" : "NORMAL"));
  else Serial.println(String("[EepromManager] Sensors mode: ") + (inverted ? "INVERTED" : "NORMAL"));
}
