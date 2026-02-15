// ============================================================================
// EEPROM MANAGER - Persistent Preferences Storage
// ============================================================================
// EEPROM read/write operations for persistent preferences:
// - Logging preferences (enabled + level)
// - Stats recording preference
// - Sensor inversion preference
// - Checksum integrity protection
//
// EEPROM Layout:
//   Byte 0:   loggingEnabled (0=disabled, 1=enabled)
//   Byte 1:   currentLogLevel (0-3: ERROR, WARN, INFO, DEBUG)
//   Byte 2:   statsRecordingEnabled (0=disabled, 1=enabled)
//   Byte 3:   sensorsInverted (0=normal, 1=inverted)
//   Byte 4-126: (reserved / WiFi config)
//   Byte 127: XOR checksum of bytes 0-3
// ============================================================================

#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include <Arduino.h>
#include <EEPROM.h>

// Forward declaration to avoid circular include
enum LogLevel : int;

class EepromManager {
public:
  // ========================================================================
  // LIFECYCLE
  // ========================================================================

  EepromManager();

  /**
   * Initialize EEPROM (call once in setup)
   * @param size EEPROM allocation size in bytes
   */
  void begin(uint16_t size = 128);

  // ========================================================================
  // LOGGING PREFERENCES
  // ========================================================================

  /**
   * Save logging preferences to EEPROM (with checksum)
   * @param enabled  Master logging switch
   * @param level    Current log level (0-3)
   */
  void saveLoggingPreferences(bool enabled, uint8_t level);

  /**
   * Load logging preferences from EEPROM
   * @param[out] enabled  Will be set to saved value (or default true)
   * @param[out] level    Will be set to saved value (or default LOG_INFO=2)
   */
  void loadLoggingPreferences(bool& enabled, uint8_t& level);

  // ========================================================================
  // STATS RECORDING PREFERENCE
  // ========================================================================

  /**
   * Save stats recording preference (with checksum)
   * @param enabled true = stats are recorded
   */
  void saveStatsRecording(bool enabled);

  /**
   * Load stats recording preference
   * @param[out] enabled Will be set to saved value (or default true)
   */
  void loadStatsRecording(bool& enabled);

  // ========================================================================
  // SENSORS INVERSION PREFERENCE
  // ========================================================================

  /** Save sensors inversion preference */
  void saveSensorsInverted(bool inverted);

  /**
   * Load sensors inversion preference
   * @param[out] inverted Will be set to saved value (or default false)
   */
  void loadSensorsInverted(bool& inverted);

private:
  /** Calculate XOR checksum of critical data bytes */
  uint8_t calculateChecksum() const;

  /** Verify stored checksum matches calculated */
  bool verifyChecksum() const;

  /** Update checksum after any write */
  void updateChecksum();
};

#endif // EEPROM_MANAGER_H
