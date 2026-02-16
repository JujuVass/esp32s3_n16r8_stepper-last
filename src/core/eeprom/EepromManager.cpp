// ============================================================================
// EEPROM MANAGER IMPLEMENTATION
// ============================================================================

#include "core/eeprom/EepromManager.h"
#include "core/UtilityEngine.h"  // For commitEEPROMWithRetry(), LogLevel enum

// EEPROM addresses
#define EEPROM_ADDR_LOGGING_ENABLED   0
#define EEPROM_ADDR_LOG_LEVEL         1
#define EEPROM_ADDR_STATS_ENABLED     2
#define EEPROM_ADDR_SENSORS_INVERTED  3
#define EEPROM_ADDR_CHECKSUM          127

// ============================================================================
// CONSTRUCTOR
// ============================================================================

EepromManager::EepromManager() {}

// ============================================================================
// LIFECYCLE
// ============================================================================

void EepromManager::begin(uint16_t size) {
  EEPROM.begin(size);
}

// ============================================================================
// CHECKSUM HELPERS
// ============================================================================

uint8_t EepromManager::calculateChecksum() const {
  uint8_t checksum = 0;
  for (int i = 0; i < 4; i++) {
    checksum ^= EEPROM.read(i);
  }
  return checksum;
}

bool EepromManager::verifyChecksum() const {
  uint8_t calculated = calculateChecksum();
  uint8_t stored = EEPROM.read(EEPROM_ADDR_CHECKSUM);
  return (calculated == stored);
}

void EepromManager::updateChecksum() {
  uint8_t checksum = calculateChecksum();
  EEPROM.write(EEPROM_ADDR_CHECKSUM, checksum);
}

// ============================================================================
// LOGGING PREFERENCES
// ============================================================================

void EepromManager::saveLoggingPreferences(bool enabled, uint8_t level) {
  EEPROM.write(EEPROM_ADDR_LOGGING_ENABLED, enabled ? 1 : 0);
  EEPROM.write(EEPROM_ADDR_LOG_LEVEL, level);

  updateChecksum();

  if (commitEEPROMWithRetry("Logging")) {
    Serial.println("[EepromManager] 💾 Logging preferences saved with checksum");
  }
}

void EepromManager::loadLoggingPreferences(bool& enabled, uint8_t& level) {
  uint8_t enabledByte = EEPROM.read(EEPROM_ADDR_LOGGING_ENABLED);
  uint8_t levelByte = EEPROM.read(EEPROM_ADDR_LOG_LEVEL);

  bool checksumValid = verifyChecksum();

  // Check if EEPROM is uninitialized (fresh ESP32) OR corrupted
  if (enabledByte == 0xFF || !checksumValid) {
    if (!checksumValid && enabledByte != 0xFF) {
      Serial.println("[EepromManager] ⚠️ EEPROM CORRUPTION DETECTED! Resetting to defaults");
    }

    // Defaults
    enabled = true;
    level = 2;  // LOG_INFO

    // Save defaults immediately
    saveLoggingPreferences(enabled, level);

    if (enabledByte == 0xFF) {
      Serial.println("[EepromManager] 🔧 First boot: initialized logging defaults");
    } else {
      Serial.println("[EepromManager] 🔧 EEPROM repaired: reset logging to defaults");
    }
  } else {
    enabled = (enabledByte == 1);

    // Validate log level range (0-3)
    if (levelByte <= 3) {
      level = levelByte;
    } else {
      Serial.println("[EepromManager] ⚠️ Invalid log level: " + String(levelByte) + " - using default");
      level = 2;  // LOG_INFO
    }

    Serial.print("[EepromManager] 📂 Logging: ");
    Serial.print(enabled ? "ENABLED" : "DISABLED");
    Serial.print(", Level: ");
    Serial.println(level);
  }
}

// ============================================================================
// STATS RECORDING
// ============================================================================

void EepromManager::saveStatsRecording(bool enabled) {
  EEPROM.write(EEPROM_ADDR_STATS_ENABLED, enabled ? 1 : 0);

  updateChecksum();
  commitEEPROMWithRetry("Stats");

  Serial.println(String("[EepromManager] 📊 Stats recording: ") + (enabled ? "ENABLED" : "DISABLED"));
}

void EepromManager::loadStatsRecording(bool& enabled) {
  uint8_t statsByte = EEPROM.read(EEPROM_ADDR_STATS_ENABLED);
  bool checksumValid = verifyChecksum();

  if (statsByte == 0xFF || !checksumValid) {
    if (!checksumValid && statsByte != 0xFF) {
      Serial.println("[EepromManager] ⚠️ Stats: checksum mismatch, resetting to default");
    }
    // First boot or corruption: enable by default
    enabled = true;
    saveStatsRecording(enabled);
    Serial.println(statsByte == 0xFF
      ? "[EepromManager] 🔧 First boot: stats recording enabled by default"
      : "[EepromManager] 🔧 Stats repaired: reset to default (enabled)");
  } else {
    enabled = (statsByte == 1);
    Serial.print("[EepromManager] 📂 Stats recording: ");
    Serial.println(enabled ? "ENABLED" : "DISABLED");
  }
}

// ============================================================================
// SENSORS INVERSION
// ============================================================================

void EepromManager::saveSensorsInverted(bool inverted) {
  EEPROM.write(EEPROM_ADDR_SENSORS_INVERTED, inverted ? 1 : 0);

  updateChecksum();
  commitEEPROMWithRetry("Sensors");

  Serial.println(String("[EepromManager] 🔄 Sensors mode: ") + (inverted ? "INVERTED" : "NORMAL"));
}

void EepromManager::loadSensorsInverted(bool& inverted) {
  uint8_t byte = EEPROM.read(EEPROM_ADDR_SENSORS_INVERTED);
  bool checksumValid = verifyChecksum();

  if (byte == 0xFF || !checksumValid) {
    if (!checksumValid && byte != 0xFF) {
      Serial.println("[EepromManager] ⚠️ Sensors: checksum mismatch, resetting to default");
    }
    // First boot or corruption: normal mode
    inverted = false;
    saveSensorsInverted(false);
    Serial.println(byte == 0xFF
      ? "[EepromManager] 🔧 First boot: sensors mode = NORMAL"
      : "[EepromManager] 🔧 Sensors repaired: reset to default (NORMAL)");
  } else {
    inverted = (byte == 1);
    Serial.print("[EepromManager] 📂 Sensors mode: ");
    Serial.println(inverted ? "INVERTED" : "NORMAL");
  }
}
