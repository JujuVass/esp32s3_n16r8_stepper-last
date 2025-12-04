/**
 * ============================================================================
 * SEQUENCE TABLE MANAGER IMPLEMENTATION
 * ============================================================================
 * Handles sequence table CRUD operations, import/export, validation.
 * 
 * @author Modular Refactoring
 * @version 1.0
 */

#include "sequencer/SequenceTableManager.h"
#include <WebSocketsServer.h>

// ============================================================================
// EXTERNAL VARIABLES (defined in main)
// ============================================================================
extern SequenceLine sequenceTable[];
extern int sequenceLineCount;
extern int nextLineId;

// ============================================================================
// FORWARD DECLARATIONS (functions in main)
// ============================================================================
void sendError(String message);
bool parseJsonCommand(const String& jsonStr, JsonDocument& doc);

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

SequenceTableManager& SeqTable = SequenceTableManager::getInstance();

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SequenceTableManager::SequenceTableManager() {
  // engine is a global pointer (extern UtilityEngine* engine)
  // Note: sequenceTable, sequenceLineCount, nextLineId are in main
}

// ============================================================================
// CRUD OPERATIONS
// ============================================================================

int SequenceTableManager::addLine(SequenceLine newLine) {
  if (sequenceLineCount >= MAX_SEQUENCE_LINES) {
    sendError("❌ Séquenceur plein! Max 20 lignes");
    return -1;
  }
  
  newLine.lineId = nextLineId++;
  sequenceTable[sequenceLineCount] = newLine;
  sequenceLineCount++;
  
  engine->info("✅ Ligne ajoutée: ID=" + String(newLine.lineId) + " | Pos:" + 
        String(newLine.startPositionMM, 1) + "mm, Dist:" + String(newLine.distanceMM, 1) + "mm");
  
  return newLine.lineId;
}

bool SequenceTableManager::deleteLine(int lineId) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) {
    sendError("❌ Ligne non trouvée");
    return false;
  }
  
  // Shift lines down
  for (int i = idx; i < sequenceLineCount - 1; i++) {
    sequenceTable[i] = sequenceTable[i + 1];
  }
  
  sequenceLineCount--;
  engine->info("🗑️ Ligne supprimée: ID=" + String(lineId));
  
  return true;
}

bool SequenceTableManager::updateLine(int lineId, SequenceLine updatedLine) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) {
    sendError("❌ Ligne non trouvée");
    return false;
  }
  
  updatedLine.lineId = lineId;  // Keep original ID
  sequenceTable[idx] = updatedLine;
  
  engine->info("✏️ Ligne mise à jour: ID=" + String(lineId));
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
  
  engine->info(String("↕️ Ligne déplacée: ID=") + String(lineId) + " | " + 
        String(idx + 1) + " → " + String(newIdx + 1));
  
  return true;
}

bool SequenceTableManager::toggleLine(int lineId, bool enabled) {
  int idx = findLineIndex(lineId);
  
  if (idx == -1) return false;
  
  sequenceTable[idx].enabled = enabled;
  engine->info(String(enabled ? "✓" : "✗") + " Ligne ID=" + String(lineId) + 
        (enabled ? " activée" : " désactivée"));
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
  nextLineId = 1;
  engine->info("🗑️ Tableau vidé");
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
  line.decelStartEnabled = obj["decelStartEnabled"] | false;
  line.decelEndEnabled = obj["decelEndEnabled"] | false;
  line.decelZoneMM = obj["decelZoneMM"] | 50.0;
  line.decelEffectPercent = obj["decelEffectPercent"] | 50.0;
  line.decelMode = (DecelMode)(obj["decelMode"] | 0);
  
  // VA-ET-VIENT cycle pause
  line.vaetCyclePauseEnabled = obj["vaetCyclePauseEnabled"] | false;
  line.vaetCyclePauseIsRandom = obj["vaetCyclePauseIsRandom"] | false;
  line.vaetCyclePauseDurationSec = obj["vaetCyclePauseDurationSec"] | 0.0;
  line.vaetCyclePauseMinSec = obj["vaetCyclePauseMinSec"] | 0.5;
  line.vaetCyclePauseMaxSec = obj["vaetCyclePauseMaxSec"] | 3.0;
  
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
  
  // OSCILLATION cycle pause
  line.oscCyclePauseEnabled = obj["oscCyclePauseEnabled"] | false;
  line.oscCyclePauseIsRandom = obj["oscCyclePauseIsRandom"] | false;
  line.oscCyclePauseDurationSec = obj["oscCyclePauseDurationSec"] | 0.0;
  line.oscCyclePauseMinSec = obj["oscCyclePauseMinSec"] | 0.5;
  line.oscCyclePauseMaxSec = obj["oscCyclePauseMaxSec"] | 3.0;
  
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
      if (idx < 11) line.chaosPatternsEnabled[idx++] = val.as<bool>();
    }
  } else {
    // Default: all patterns enabled
    for (int i = 0; i < 11; i++) {
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
    lineObj["decelStartEnabled"] = line->decelStartEnabled;
    lineObj["decelEndEnabled"] = line->decelEndEnabled;
    lineObj["decelZoneMM"] = serialized(String(line->decelZoneMM, 1));
    lineObj["decelEffectPercent"] = serialized(String(line->decelEffectPercent, 0));
    lineObj["decelMode"] = (int)line->decelMode;
    
    // VA-ET-VIENT cycle pause
    lineObj["vaetCyclePauseEnabled"] = line->vaetCyclePauseEnabled;
    lineObj["vaetCyclePauseIsRandom"] = line->vaetCyclePauseIsRandom;
    lineObj["vaetCyclePauseDurationSec"] = serialized(String(line->vaetCyclePauseDurationSec, 1));
    lineObj["vaetCyclePauseMinSec"] = serialized(String(line->vaetCyclePauseMinSec, 1));
    lineObj["vaetCyclePauseMaxSec"] = serialized(String(line->vaetCyclePauseMaxSec, 1));
    
    // OSCILLATION fields
    lineObj["oscCenterPositionMM"] = serialized(String(line->oscCenterPositionMM, 1));
    lineObj["oscAmplitudeMM"] = serialized(String(line->oscAmplitudeMM, 1));
    lineObj["oscWaveform"] = (int)line->oscWaveform;
    lineObj["oscFrequencyHz"] = serialized(String(line->oscFrequencyHz, 3));
    lineObj["oscEnableRampIn"] = line->oscEnableRampIn;
    lineObj["oscEnableRampOut"] = line->oscEnableRampOut;
    lineObj["oscRampInDurationMs"] = serialized(String(line->oscRampInDurationMs, 0));
    lineObj["oscRampOutDurationMs"] = serialized(String(line->oscRampOutDurationMs, 0));
    
    // OSCILLATION cycle pause
    lineObj["oscCyclePauseEnabled"] = line->oscCyclePauseEnabled;
    lineObj["oscCyclePauseIsRandom"] = line->oscCyclePauseIsRandom;
    lineObj["oscCyclePauseDurationSec"] = serialized(String(line->oscCyclePauseDurationSec, 1));
    lineObj["oscCyclePauseMinSec"] = serialized(String(line->oscCyclePauseMinSec, 1));
    lineObj["oscCyclePauseMaxSec"] = serialized(String(line->oscCyclePauseMaxSec, 1));
    
    // CHAOS fields
    lineObj["chaosCenterPositionMM"] = serialized(String(line->chaosCenterPositionMM, 1));
    lineObj["chaosAmplitudeMM"] = serialized(String(line->chaosAmplitudeMM, 1));
    lineObj["chaosMaxSpeedLevel"] = serialized(String(line->chaosMaxSpeedLevel, 1));
    lineObj["chaosCrazinessPercent"] = serialized(String(line->chaosCrazinessPercent, 1));
    lineObj["chaosDurationSeconds"] = line->chaosDurationSeconds;
    lineObj["chaosSeed"] = line->chaosSeed;
    
    JsonArray patternsArray = lineObj["chaosPatternsEnabled"].to<JsonArray>();
    for (int p = 0; p < 11; p++) {
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
  engine->debug(String("📤 JSON reçu (") + String(jsonData.length()) + " chars): " + 
        jsonData.substring(0, min(200, (int)jsonData.length())));
  
  // Clear existing table
  clear();
  
  JsonDocument importDoc;
  if (!parseJsonCommand(jsonData, importDoc)) {
    return -1;
  }
  
  // Validate sequenceLineCount
  int importLineCount = importDoc["sequenceLineCount"] | 0;
  if (importLineCount <= 0 || importLineCount > MAX_SEQUENCE_LINES) {
    sendError("❌ JSON invalide ou trop de lignes");
    return -1;
  }
  
  // Validate lines array
  if (!importDoc["lines"].is<JsonArray>()) {
    sendError("❌ Array 'lines' non trouvé ou invalide");
    return -1;
  }
  
  JsonArray linesArray = importDoc["lines"].as<JsonArray>();
  engine->info(String("📥 Import: ") + String(importLineCount) + " lignes");
  
  int maxLineId = 0;
  int importedCount = 0;
  
  for (JsonObject lineObj : linesArray) {
    if (sequenceLineCount >= MAX_SEQUENCE_LINES) {
      engine->warn("⚠️ Table pleine, arrêt import");
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
  
  nextLineId = maxLineId + 1;
  
  engine->info(String("✅ ") + String(importedCount) + " lignes importées");
  engine->info(String("📢 nextLineId mis à jour: ") + String(nextLineId));
  
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
