// ============================================================================
// STATS MANAGER - Distance Statistics Tracking
// ============================================================================
// Daily distance statistics management:
// - Increment daily stats (saves to /stats.json)
// - Session stats save (with mutex protection)
// - Distance reset
// - Effective max distance calculation
// ============================================================================

#ifndef STATS_MANAGER_H
#define STATS_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Forward declarations
class FileSystem;
class EepromManager;

class StatsManager {
public:
  // ========================================================================
  // CONSTRUCTOR
  // ========================================================================

  /**
   * @param fs  Reference to FileSystem (for /stats.json read/write)
   * @param eeprom Reference to EepromManager (for stats recording pref)
   */
  StatsManager(FileSystem& fs, EepromManager& eeprom);

  /**
   * Initialize: load stats recording preference from EEPROM
   */
  void initialize();

  // ========================================================================
  // STATS RECORDING PREFERENCE
  // ========================================================================

  /**
   * Enable/disable stats recording (persisted to EEPROM)
   */
  void setStatsRecordingEnabled(bool enabled);

  /** Check if stats recording is enabled */
  bool isStatsRecordingEnabled() const { return _statsRecordingEnabled; }

  // ========================================================================
  // SESSION STATS
  // ========================================================================

  /**
   * Save current session's distance to daily stats
   * Only saves the increment since last save (avoids double-counting)
   * Thread-safe: uses statsMutex
   */
  void saveCurrentSessionStats();

  /**
   * Reset total distance counter to zero
   * Saves current session stats first, then resets
   * Thread-safe: uses statsMutex
   */
  void resetTotalDistance();

  /**
   * Update effective max distance based on limit percent
   * effectiveMaxDistanceMM = totalDistanceMM * (percent / 100)
   */
  void updateEffectiveMaxDistance();

  // ========================================================================
  // DAILY STATS
  // ========================================================================

  /**
   * Increment daily statistics with distance traveled
   * Saves to /stats.json as array of {date, distanceMM}
   * @param distanceMM Distance to add in millimeters
   */
  void incrementDailyStats(float distanceMM);

  /**
   * Get today's total distance from /stats.json
   * @return Distance in mm, 0 if no data
   */
  float getTodayDistance();

private:
  FileSystem& _fs;
  EepromManager& _eeprom;
  bool _statsRecordingEnabled;
};

#endif // STATS_MANAGER_H
