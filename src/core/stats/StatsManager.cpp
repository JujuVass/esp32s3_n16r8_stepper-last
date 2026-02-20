// ============================================================================
// STATS MANAGER IMPLEMENTATION
// ============================================================================

#include "core/stats/StatsManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/eeprom/EepromManager.h"
#include "core/Config.h"        // For STEPS_PER_MM
#include "core/MovementMath.h"  // For mmToSteps/stepsToMM
#include "core/GlobalState.h"   // For stats (StatsTracking), statsMutex, effectiveMaxDistanceMM, etc.
#include "core/UtilityEngine.h" // For engine->info/debug/error logging
#include <time.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

StatsManager::StatsManager(FileSystem& fs, EepromManager& eeprom)
  : _fs(fs),
    _eeprom(eeprom),
    _statsRecordingEnabled(true) {}

// ============================================================================
// INITIALIZE
// ============================================================================

void StatsManager::initialize() {
  _eeprom.loadStatsRecording(_statsRecordingEnabled);
}

// ============================================================================
// STATS RECORDING PREFERENCE
// ============================================================================

void StatsManager::setStatsRecordingEnabled(bool enabled) {
  _statsRecordingEnabled = enabled;
  _eeprom.saveStatsRecording(enabled);
  if (engine) {
    engine->info(String("📊 Stats recording: ") + (enabled ? "ENABLED" : "DISABLED") + " (saved to NVS)");
  }
}

// ============================================================================
// SESSION STATS
// ============================================================================

void StatsManager::saveCurrentSessionStats() {
  // Protect compound stats read (getIncrementSteps + markSaved) from Core 1 trackDelta
  MutexGuard guard(statsMutex);

  // Calculate distance increment since last save (in steps)
  unsigned long incrementSteps = stats.getIncrementSteps();

  // Convert to millimeters
  float incrementMM = MovementMath::stepsToMM(incrementSteps);

  if (incrementMM <= 0) {
    if (engine) engine->debug("📊 No new distance to save (no increment since last save)");
    return;
  }

  // Save increment to daily stats
  incrementDailyStats(incrementMM);

  if (engine) {
    engine->debug(String("💾 Session stats saved: +") + String(incrementMM, 1) +
      "mm (total session: " + String(MovementMath::stepsToMM(stats.totalDistanceTraveled), 1) + "mm)");
  }

  // Mark as saved
  stats.markSaved();
}

void StatsManager::resetTotalDistance() {
  // Save any unsaved distance before resetting
  saveCurrentSessionStats();

  // Reset counters (protected from Core 1 trackDelta)
  {
    MutexGuard guard(statsMutex);
    stats.reset();
  }
  if (engine) engine->info("🔄 Total distance counter reset to 0");
}

void StatsManager::updateEffectiveMaxDistance() {
  effectiveMaxDistanceMM = config.totalDistanceMM * (maxDistanceLimitPercent / 100.0f);
  if (engine) {
    engine->debug(String("📏 Effective max distance: ") + String(effectiveMaxDistanceMM, 1) +
      " mm (" + String(maxDistanceLimitPercent, 0) + "% of " +
      String(config.totalDistanceMM, 1) + " mm)");
  }
}

// ============================================================================
// DAILY STATS
// ============================================================================

void StatsManager::incrementDailyStats(float distanceMM) {
  if (distanceMM <= 0) return;

  // Check if stats recording is disabled
  if (!_statsRecordingEnabled) {
    if (engine) engine->debug("📊 Stats recording disabled - skipping save");
    return;
  }

  // Guard against NTP not synced yet (would record under "1970-01-01")
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year <= (2020 - 1900)) {
    if (engine) engine->debug("📊 NTP not synced - deferring stats save");
    return;
  }
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);

  // Load existing stats
  JsonDocument statsDoc;
  _fs.loadJsonFile("/stats.json", statsDoc);

  // Find or create today's entry
  JsonArray statsArray = statsDoc.as<JsonArray>();
  if (statsArray.isNull()) {
    statsArray = statsDoc.to<JsonArray>();
  }

  bool found = false;
  for (JsonObject entry : statsArray) {
    if (strcmp(entry["date"], dateStr) == 0) {
      float current = entry["distanceMM"] | 0.0f;
      entry["distanceMM"] = current + distanceMM;
      found = true;
      break;
    }
  }

  if (!found) {
    JsonObject newEntry = statsArray.add<JsonObject>();
    newEntry["date"] = dateStr;
    newEntry["distanceMM"] = distanceMM;
  }

  // Save back to file
  if (!_fs.saveJsonFile("/stats.json", statsDoc)) {
    if (engine) engine->error("Failed to write stats.json");
    return;
  }

  if (engine) engine->debug(String("📊 Stats: +") + String(distanceMM, 1) + "mm on " + String(dateStr));
}

float StatsManager::getTodayDistance() {
  // Get current date
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);

  // Load stats
  JsonDocument statsDoc;
  if (!_fs.loadJsonFile("/stats.json", statsDoc)) {
    return 0.0f;
  }

  // Find today's entry
  JsonArray statsArray = statsDoc.as<JsonArray>();
  for (JsonObject entry : statsArray) {
    if (strcmp(entry["date"], dateStr) == 0) {
      return entry["distanceMM"] | 0.0f;
    }
  }

  return 0.0f;
}