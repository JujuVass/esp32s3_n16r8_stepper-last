/**
 * ============================================================================
 * SEQUENCE TABLE MANAGER IMPLEMENTATION
 * ============================================================================
 * Handles sequence table CRUD operations, import/export, validation.
 */

#include "movement/SequenceTableManager.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include <WebSocketsServer.h>

// ============================================================================
// SEQUENCE DATA - Owned by this module
// ============================================================================
SequenceLine sequenceTable[MAX_SEQUENCE_LINES];
int sequenceLineCount = 0;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

SequenceTableManager& SeqTable = SequenceTableManager::getInstance();

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SequenceTableManager::SequenceTableManager() {
  // engine is a global pointer (extern UtilityEngine* engine)
}

// ============================================================================
// CRUD OPERATIONS
// ============================================================================

int SequenceTableManager::addLine(const SequenceLine& newLine) {
  if (sequenceLineCount >= MAX_SEQUENCE_LINES) {
    Status.sendError("❌ Sequencer full! Max 20 lines");
    return -1;
  }
  
  sequenceTable[sequenceLineCount] = newLine;
  sequenceTable[sequenceLineCount].lineId = config.nextLineId++;  // Assign ID after copy
  int assignedId = sequenceTable[sequenceLineCount].lineId;
  sequenceLineCount++;
  
  engine->info("✅ Line added: ID=" + String(assignedId) + " | Pos:" + 
        String(newLine.startPositionMM, 1) + "mm, Dist:" + String(newLine.distanceMM, 1) + "mm");
  
  return assignedId;
}

bool SequenceTableManager::deleteLine(int lineId) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) {
    Status.sendError("❌ Line not found");
    return false;
  }
  
  // Shift lines down
  for (int i = idx; i < sequenceLineCount - 1; i++) {
    sequenceTable[i] = sequenceTable[i + 1];
  }
  
  sequenceLineCount--;
  engine->info("🗑️ Line deleted: ID=" + String(lineId));
  
  return true;
}

bool SequenceTableManager::updateLine(int lineId, const SequenceLine& updatedLine) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) {
    Status.sendError("❌ Line not found");
    return false;
  }
  
  sequenceTable[idx] = updatedLine;
  sequenceTable[idx].lineId = lineId;  // Keep original ID
  
  engine->info("✏️ Line updated: ID=" + String(lineId));
  return true;
}

bool SequenceTableManager::moveLine(int lineId, int direction) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) return false;
  
  int newIdx = idx + direction;
  if (newIdx < 0 || newIdx >= sequenceLineCount) {
    return false;  // Out of bounds
  }
  
  // Swap lines
  SequenceLine temp = sequenceTable[idx];
  sequenceTable[idx] = sequenceTable[newIdx];
  sequenceTable[newIdx] = temp;
  
  engine->info(String("↕️ Line moved: ID=") + String(lineId) + " | " + 
        String(idx + 1) + " → " + String(newIdx + 1));
  
  return true;
}

bool SequenceTableManager::reorderLine(int lineId, int newIndex) {
  int oldIndex = findLineIndex(lineId);
  
  if (oldIndex == -1) return false;
  if (newIndex < 0 || newIndex >= sequenceLineCount) return false;
  if (oldIndex == newIndex) return true;  // Already at target
  
  // Store the line to move
  SequenceLine lineToMove = sequenceTable[oldIndex];
  
  // Shift lines to fill the gap
  if (oldIndex < newIndex) {
    // Moving down: shift lines up
    for (int i = oldIndex; i < newIndex; i++) {
      sequenceTable[i] = sequenceTable[i + 1];
    }
  } else {
    // Moving up: shift lines down
    for (int i = oldIndex; i > newIndex; i--) {
      sequenceTable[i] = sequenceTable[i - 1];
    }
  }
  
  // Place the line at new position
  sequenceTable[newIndex] = lineToMove;
  
  engine->info(String("🔄 Line reordered: ID=") + String(lineId) + " | " + 
        String(oldIndex + 1) + " → " + String(newIndex + 1));
  
  return true;
}

bool SequenceTableManager::toggleLine(int lineId, bool enabled) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) return false;
  
  sequenceTable[idx].enabled = enabled;
  engine->info(String(enabled ? "✓" : "✗") + " Line ID=" + String(lineId) + 
        (enabled ? " enabled" : " disabled"));
  return true;
}

int SequenceTableManager::duplicateLine(int lineId) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) return -1;
  
  SequenceLine duplicate = sequenceTable[idx];
  return addLine(duplicate);
}

void SequenceTableManager::clear() {
  sequenceLineCount = 0;
  config.nextLineId = 1;
  engine->info("🗑️ Table cleared");
}

// ============================================================================
// HELPER
// ============================================================================

int SequenceTableManager::findLineIndex(int lineId) {
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].lineId == lineId) {
      return i;
    }
  }
  return -1;
}

// ============================================================================
// VALIDATION
// ============================================================================

String SequenceTableManager::validatePhysics(const SequenceLine& line) {
  float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  switch (line.movementType) {
    case MOVEMENT_VAET: {
      // VA-ET-VIENT validation
      float endPosition = line.startPositionMM + line.distanceMM;
      
      if (line.startPositionMM < 0) {
        return "Start position negative";
      }
      if (line.startPositionMM > effectiveMax) {
        return "Start position (" + String(line.startPositionMM, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (endPosition > effectiveMax) {
        return "End position (" + String(endPosition, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (line.distanceMM <= 0) {
        return "Distance must be positive";
      }
      break;
    }
    
    case MOVEMENT_OSC: {
      // OSCILLATION validation
      float minPos = line.oscCenterPositionMM - line.oscAmplitudeMM;
      float maxPos = line.oscCenterPositionMM + line.oscAmplitudeMM;
      
      if (minPos < 0) {
        return "Oscillation min position (" + String(minPos, 1) + "mm) is negative";
      }
      if (maxPos > effectiveMax) {
        return "Oscillation max position (" + String(maxPos, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (line.oscAmplitudeMM <= 0) {
        return "Amplitude must be positive";
      }
      break;
    }
    
    case MOVEMENT_CHAOS: {
      // CHAOS validation
      float minPos = line.chaosCenterPositionMM - line.chaosAmplitudeMM;
      float maxPos = line.chaosCenterPositionMM + line.chaosAmplitudeMM;
      
      if (minPos < 0) {
        return "Chaos min position (" + String(minPos, 1) + "mm) is negative";
      }
      if (maxPos > effectiveMax) {
        return "Chaos max position (" + String(maxPos, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (line.chaosAmplitudeMM <= 0) {
        return "Amplitude must be positive";
      }
      break;
    }
    
    default:
      break;
  }
  
  return "";  // Valid
}

// ============================================================================
// JSON PARSING
// ============================================================================

SequenceLine SequenceTableManager::parseFromJson(JsonVariantConst obj) {
  SequenceLine line;
  
  // Common fields
  line.enabled = obj["enabled"] | true;
  line.movementType = (MovementType)(obj["movementType"] | 0);
  
  // Cycle count: always 1 for CALIBRATION, else from JSON
  if (line.movementType == MOVEMENT_CALIBRATION) {
    line.cycleCount = 1;
  } else {
    line.cycleCount = obj["cycleCount"] | 1;
  }
  
  // Pause after movement
  line.pauseAfterMs = obj["pauseAfterMs"] | 0;
  
  // VA-ET-VIENT fields
  line.startPositionMM = obj["startPositionMM"] | 0.0;
  line.distanceMM = obj["distanceMM"] | 100.0;
  line.speedForward = obj["speedForward"] | 5.0;
  line.speedBackward = obj["speedBackward"] | 5.0;
  
  // VA-ET-VIENT zone effects (embedded ZoneEffectConfig)
  JsonVariantConst ze = obj["vaetZoneEffect"];
  if (!ze.isNull()) {
    // New format: vaetZoneEffect object
    line.vaetZoneEffect.enabled = ze["enabled"] | false;
    line.vaetZoneEffect.enableStart = ze["enableStart"] | true;
    line.vaetZoneEffect.enableEnd = ze["enableEnd"] | true;
    line.vaetZoneEffect.mirrorOnReturn = ze["mirrorOnReturn"] | false;
    line.vaetZoneEffect.zoneMM = ze["zoneMM"] | 50.0;
    line.vaetZoneEffect.speedEffect = (SpeedEffect)(ze["speedEffect"] | 1);
    line.vaetZoneEffect.speedCurve = (SpeedCurve)(ze["speedCurve"] | 0);
    line.vaetZoneEffect.speedIntensity = ze["speedIntensity"] | 75.0;
    line.vaetZoneEffect.randomTurnbackEnabled = ze["randomTurnbackEnabled"] | false;
    line.vaetZoneEffect.turnbackChance = ze["turnbackChance"] | 30;
    line.vaetZoneEffect.endPauseEnabled = ze["endPauseEnabled"] | false;
    line.vaetZoneEffect.endPauseIsRandom = ze["endPauseIsRandom"] | false;
    line.vaetZoneEffect.endPauseDurationSec = ze["endPauseDurationSec"] | 1.0;
    line.vaetZoneEffect.endPauseMinSec = ze["endPauseMinSec"] | 0.5;
    line.vaetZoneEffect.endPauseMaxSec = ze["endPauseMaxSec"] | 2.0;
  } else {
    // Legacy format: decel* fields - migrate to new format
    bool decelStartEnabled = obj["decelStartEnabled"] | false;
    bool decelEndEnabled = obj["decelEndEnabled"] | false;
    float decelZoneMM = obj["decelZoneMM"] | 50.0;
    float decelEffectPercent = obj["decelEffectPercent"] | 50.0;
    int decelMode = obj["decelMode"] | 0;
    
    // Convert to ZoneEffectConfig
    line.vaetZoneEffect.enabled = (decelStartEnabled || decelEndEnabled);
    line.vaetZoneEffect.enableStart = decelStartEnabled;
    line.vaetZoneEffect.enableEnd = decelEndEnabled;
    line.vaetZoneEffect.zoneMM = decelZoneMM;
    line.vaetZoneEffect.speedEffect = SPEED_DECEL;
    line.vaetZoneEffect.speedCurve = (SpeedCurve)decelMode;
    line.vaetZoneEffect.speedIntensity = decelEffectPercent;
    // New features disabled by default for legacy imports
    line.vaetZoneEffect.randomTurnbackEnabled = false;
    line.vaetZoneEffect.endPauseEnabled = false;
  }
  
  // VA-ET-VIENT cycle pause (DRY: uses CyclePauseConfig struct)
  line.vaetCyclePause.enabled = obj["vaetCyclePauseEnabled"] | false;
  line.vaetCyclePause.isRandom = obj["vaetCyclePauseIsRandom"] | false;
  line.vaetCyclePause.pauseDurationSec = obj["vaetCyclePauseDurationSec"] | 0.0;
  line.vaetCyclePause.minPauseSec = obj["vaetCyclePauseMinSec"] | 0.5;
  line.vaetCyclePause.maxPauseSec = obj["vaetCyclePauseMaxSec"] | 3.0;
  
  // OSCILLATION fields
  float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  line.oscCenterPositionMM = obj["oscCenterPositionMM"] | (effectiveMax / 2.0);
  line.oscAmplitudeMM = obj["oscAmplitudeMM"] | 50.0;
  line.oscWaveform = (OscillationWaveform)(obj["oscWaveform"] | 0);
  line.oscFrequencyHz = obj["oscFrequencyHz"] | 1.0;
  line.oscEnableRampIn = obj["oscEnableRampIn"] | false;
  line.oscEnableRampOut = obj["oscEnableRampOut"] | false;
  line.oscRampInDurationMs = obj["oscRampInDurationMs"] | 1000.0;
  line.oscRampOutDurationMs = obj["oscRampOutDurationMs"] | 1000.0;
  
  // OSCILLATION cycle pause (DRY: uses CyclePauseConfig struct)
  line.oscCyclePause.enabled = obj["oscCyclePauseEnabled"] | false;
  line.oscCyclePause.isRandom = obj["oscCyclePauseIsRandom"] | false;
  line.oscCyclePause.pauseDurationSec = obj["oscCyclePauseDurationSec"] | 0.0;
  line.oscCyclePause.minPauseSec = obj["oscCyclePauseMinSec"] | 0.5;
  line.oscCyclePause.maxPauseSec = obj["oscCyclePauseMaxSec"] | 3.0;
  
  // CHAOS fields
  line.chaosCenterPositionMM = obj["chaosCenterPositionMM"] | (effectiveMax / 2.0);
  line.chaosAmplitudeMM = obj["chaosAmplitudeMM"] | 50.0;
  line.chaosMaxSpeedLevel = obj["chaosMaxSpeedLevel"] | 10.0;
  line.chaosCrazinessPercent = obj["chaosCrazinessPercent"] | 50.0;
  line.chaosDurationSeconds = obj["chaosDurationSeconds"] | 30UL;
  line.chaosSeed = obj["chaosSeed"] | 0UL;
  
  // Parse patterns array
  JsonVariantConst patternsVar = obj["chaosPatternsEnabled"];
  if (patternsVar.is<JsonArrayConst>()) {
    JsonArrayConst patterns = patternsVar.as<JsonArrayConst>();
    int idx = 0;
    for (JsonVariantConst val : patterns) {
      if (idx < CHAOS_PATTERN_COUNT) line.chaosPatternsEnabled[idx++] = val.as<bool>();
    }
  } else {
    // Default: all patterns enabled
    for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
      line.chaosPatternsEnabled[i] = true;
    }
  }
  
  return line;
}

// ============================================================================
// JSON EXPORT
// ============================================================================

String SequenceTableManager::exportToJson() {
  JsonDocument doc;
  
  doc["version"] = "2.0";
  doc["sequenceLineCount"] = sequenceLineCount;
  
  JsonArray linesArray = doc["lines"].to<JsonArray>();
  
  for (int i = 0; i < sequenceLineCount; i++) {
    SequenceLine* line = &sequenceTable[i];
    
    JsonObject lineObj = linesArray.add<JsonObject>();
    
    // Common fields
    lineObj["lineId"] = line->lineId;
    lineObj["enabled"] = line->enabled;
    lineObj["movementType"] = (int)line->movementType;
    
    // VA-ET-VIENT fields
    lineObj["startPositionMM"] = serialized(String(line->startPositionMM, 1));
    lineObj["distanceMM"] = serialized(String(line->distanceMM, 1));
    lineObj["speedForward"] = serialized(String(line->speedForward, 1));
    lineObj["speedBackward"] = serialized(String(line->speedBackward, 1));
    
    // VA-ET-VIENT zone effects (embedded ZoneEffectConfig)
    JsonObject ze = lineObj["vaetZoneEffect"].to<JsonObject>();
    ze["enabled"] = line->vaetZoneEffect.enabled;
    ze["enableStart"] = line->vaetZoneEffect.enableStart;
    ze["enableEnd"] = line->vaetZoneEffect.enableEnd;
    ze["mirrorOnReturn"] = line->vaetZoneEffect.mirrorOnReturn;
    ze["zoneMM"] = serialized(String(line->vaetZoneEffect.zoneMM, 1));
    ze["speedEffect"] = (int)line->vaetZoneEffect.speedEffect;
    ze["speedCurve"] = (int)line->vaetZoneEffect.speedCurve;
    ze["speedIntensity"] = serialized(String(line->vaetZoneEffect.speedIntensity, 0));
    ze["randomTurnbackEnabled"] = line->vaetZoneEffect.randomTurnbackEnabled;
    ze["turnbackChance"] = line->vaetZoneEffect.turnbackChance;
    ze["endPauseEnabled"] = line->vaetZoneEffect.endPauseEnabled;
    ze["endPauseIsRandom"] = line->vaetZoneEffect.endPauseIsRandom;
    ze["endPauseDurationSec"] = serialized(String(line->vaetZoneEffect.endPauseDurationSec, 1));
    ze["endPauseMinSec"] = serialized(String(line->vaetZoneEffect.endPauseMinSec, 1));
    ze["endPauseMaxSec"] = serialized(String(line->vaetZoneEffect.endPauseMaxSec, 1));
    
    // VA-ET-VIENT cycle pause (JSON keys unchanged for front-end compatibility)
    lineObj["vaetCyclePauseEnabled"] = line->vaetCyclePause.enabled;
    lineObj["vaetCyclePauseIsRandom"] = line->vaetCyclePause.isRandom;
    lineObj["vaetCyclePauseDurationSec"] = serialized(String(line->vaetCyclePause.pauseDurationSec, 1));
    lineObj["vaetCyclePauseMinSec"] = serialized(String(line->vaetCyclePause.minPauseSec, 1));
    lineObj["vaetCyclePauseMaxSec"] = serialized(String(line->vaetCyclePause.maxPauseSec, 1));
    
    // OSCILLATION fields
    lineObj["oscCenterPositionMM"] = serialized(String(line->oscCenterPositionMM, 1));
    lineObj["oscAmplitudeMM"] = serialized(String(line->oscAmplitudeMM, 1));
    lineObj["oscWaveform"] = (int)line->oscWaveform;
    lineObj["oscFrequencyHz"] = serialized(String(line->oscFrequencyHz, 3));
    lineObj["oscEnableRampIn"] = line->oscEnableRampIn;
    lineObj["oscEnableRampOut"] = line->oscEnableRampOut;
    lineObj["oscRampInDurationMs"] = serialized(String(line->oscRampInDurationMs, 0));
    lineObj["oscRampOutDurationMs"] = serialized(String(line->oscRampOutDurationMs, 0));
    
    // OSCILLATION cycle pause (JSON keys unchanged for front-end compatibility)
    lineObj["oscCyclePauseEnabled"] = line->oscCyclePause.enabled;
    lineObj["oscCyclePauseIsRandom"] = line->oscCyclePause.isRandom;
    lineObj["oscCyclePauseDurationSec"] = serialized(String(line->oscCyclePause.pauseDurationSec, 1));
    lineObj["oscCyclePauseMinSec"] = serialized(String(line->oscCyclePause.minPauseSec, 1));
    lineObj["oscCyclePauseMaxSec"] = serialized(String(line->oscCyclePause.maxPauseSec, 1));
    
    // CHAOS fields
    lineObj["chaosCenterPositionMM"] = serialized(String(line->chaosCenterPositionMM, 1));
    lineObj["chaosAmplitudeMM"] = serialized(String(line->chaosAmplitudeMM, 1));
    lineObj["chaosMaxSpeedLevel"] = serialized(String(line->chaosMaxSpeedLevel, 1));
    lineObj["chaosCrazinessPercent"] = serialized(String(line->chaosCrazinessPercent, 1));
    lineObj["chaosDurationSeconds"] = line->chaosDurationSeconds;
    lineObj["chaosSeed"] = line->chaosSeed;
    
    JsonArray patternsArray = lineObj["chaosPatternsEnabled"].to<JsonArray>();
    for (int p = 0; p < CHAOS_PATTERN_COUNT; p++) {
      patternsArray.add(line->chaosPatternsEnabled[p]);
    }
    
    // COMMON fields
    lineObj["cycleCount"] = line->cycleCount;
    lineObj["pauseAfterMs"] = line->pauseAfterMs;
  }
  
  String output;
  serializeJson(doc, output);
  return output;
}

// ============================================================================
// JSON IMPORT
// ============================================================================

int SequenceTableManager::importFromJson(String jsonData) {
  engine->debug(String("📤 JSON received (") + String(jsonData.length()) + " chars): " + 
        jsonData.substring(0, min(200, (int)jsonData.length())));
  
  // Clear existing table
  clear();
  
  JsonDocument importDoc;
  DeserializationError error = deserializeJson(importDoc, jsonData);
  if (error) {
    engine->error("JSON parse error: " + String(error.c_str()));
    Status.sendError("❌ Invalid JSON: " + String(error.c_str()));
    return -1;
  }
  
  // Validate sequenceLineCount
  int importLineCount = importDoc["sequenceLineCount"] | 0;
  if (importLineCount <= 0 || importLineCount > MAX_SEQUENCE_LINES) {
    Status.sendError("❌ Invalid JSON or too many lines");
    return -1;
  }
  
  // Validate lines array
  if (!importDoc["lines"].is<JsonArray>()) {
    Status.sendError("❌ 'lines' array not found or invalid");
    return -1;
  }
  
  JsonArray linesArray = importDoc["lines"].as<JsonArray>();
  engine->info(String("📥 Import: ") + String(importLineCount) + " lines");
  
  int maxLineId = 0;
  int importedCount = 0;
  
  for (JsonObject lineObj : linesArray) {
    if (sequenceLineCount >= MAX_SEQUENCE_LINES) {
      engine->warn("⚠️ Table full, stopping import");
      break;
    }
    
    SequenceLine newLine = parseFromJson(lineObj);
    newLine.lineId = lineObj["lineId"] | 0;
    
    if (newLine.lineId > maxLineId) {
      maxLineId = newLine.lineId;
    }
    
    sequenceTable[sequenceLineCount] = newLine;
    sequenceLineCount++;
    importedCount++;
  }
  
  config.nextLineId = maxLineId + 1;
  
  engine->info(String("✅ ") + String(importedCount) + " lines imported");
  engine->info(String("📢 nextLineId updated: ") + String(config.nextLineId));
  
  return importedCount;
}

// ============================================================================
// BROADCASTING
// ============================================================================

void SequenceTableManager::sendJsonResponse(const char* type, const String& data) {
  if (webSocket.connectedClients() > 0) {
    String response = "{\"type\":\"" + String(type) + "\",\"data\":" + data + "}";
    webSocket.broadcastTXT(response);
  }
}

void SequenceTableManager::broadcast() {
  sendJsonResponse("sequenceTable", exportToJson());
}
