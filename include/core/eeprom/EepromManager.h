// ============================================================================
// EEPROM MANAGER - Persistent Preferences Storage (NVS via Preferences API)
// ============================================================================
// Key-value persistent storage for system preferences:
// - Logging preferences (enabled + level)
// - Stats recording preference
// - Sensor inversion preference
//
// Uses ESP32 Preferences (NVS) — no manual checksums needed.
// ============================================================================

#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

// Forward declaration to avoid circular include
enum LogLevel : int;

class EepromManager {
public:
  // ========================================================================
  // LIFECYCLE
  // ========================================================================

  EepromManager();

  /**
   * Initialize Preferences (call once in setup)
   * @param size Ignored — kept for API compatibility
   */
  void begin(uint16_t size = 128);

  /**
   * Close NVS namespace handle
   * Call during shutdown to release resources
   */
  void end();

  // ========================================================================
  // LOGGING PREFERENCES
  // ========================================================================

  /**
   * Save logging preferences
   * @param enabled  Master logging switch
   * @param level    Current log level (0-3)
   */
  void saveLoggingPreferences(bool enabled, uint8_t level);

  /**
   * Load logging preferences
   * @param[out] enabled  Will be set to saved value (or default true)
   * @param[out] level    Will be set to saved value (or default LOG_INFO=2)
   */
  void loadLoggingPreferences(bool& enabled, uint8_t& level);

  // ========================================================================
  // STATS RECORDING PREFERENCE
  // ========================================================================

  /**
   * Save stats recording preference
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
  Preferences _prefs;
  static constexpr const char* NVS_NAMESPACE = "stepper_cfg";
};

#endif // EEPROM_MANAGER_H
