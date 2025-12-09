/**
 * ============================================================================
 * StatusBroadcaster.cpp - WebSocket Status Broadcasting Implementation
 * ============================================================================
 * 
 * Extracted from stepper_controller_restructured.ino (~300 lines)
 * Handles mode-specific JSON construction and status broadcasting.
 * 
 * @author Refactored from main file
 * @version 1.0
 */

#include "communication/StatusBroadcaster.h"
#include "communication/NetworkManager.h"
#include "movement/ChaosController.h"
#include "movement/OscillationController.h"
#include "movement/PursuitController.h"
#include "movement/BaseMovementController.h"
#include "movement/SequenceExecutor.h"
#include "core/UtilityEngine.h"
#include <WiFi.h>

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

StatusBroadcaster& StatusBroadcaster::getInstance() {
    static StatusBroadcaster instance;
    return instance;
}

// Global accessor
StatusBroadcaster& Status = StatusBroadcaster::getInstance();

// ============================================================================
// INITIALIZATION
// ============================================================================

void StatusBroadcaster::begin(WebSocketsServer* ws) {
    _webSocket = ws;
    engine->info("StatusBroadcaster initialized");
}

// ============================================================================
// MAIN BROADCAST METHOD
// ============================================================================

void StatusBroadcaster::send() {
    // Only broadcast if clients are connected (early exit optimization)
    if (_webSocket == nullptr) {
        engine->debug("⚠️ sendStatus: _webSocket is NULL!");
        return;
    }
    if (_webSocket->connectedClients() == 0) {
        //engine->debug("⚠️ sendStatus: No clients connected!");
        return;
    }
    
    // ============================================================================
    // COMMON FIELDS (all modes)
    // ============================================================================
    
    // Calculate derived values
    float positionMM = currentStep / STEPS_PER_MM;
    float totalTraveledMM = totalDistanceTraveled / STEPS_PER_MM;
    
    // Validation state - canStart controls UI visibility (tabs shown after calibration)
    bool canStart = (config.totalDistanceMM > 0);  // Show UI after calibration
    String errorMessage = "";
    if (!canStart) {
        errorMessage = "Recalibration nécessaire";
    }
    
    bool canCalibrate = (config.currentState == STATE_READY || 
                         config.currentState == STATE_INIT || 
                         config.currentState == STATE_ERROR);
    
    // Use ArduinoJson for efficient JSON construction
    JsonDocument doc;
    
    // Root level fields (ALWAYS sent regardless of mode)
    doc["state"] = (int)config.currentState;
    doc["currentStep"] = currentStep;
    doc["positionMM"] = serialized(String(positionMM, 2));
    doc["totalDistMM"] = serialized(String(config.totalDistanceMM, 2));
    doc["maxDistLimitPercent"] = serialized(String(maxDistanceLimitPercent, 0));
    doc["effectiveMaxDistMM"] = serialized(String(effectiveMaxDistanceMM, 2));
    doc["isPaused"] = (config.currentState == STATE_PAUSED);  // Derived from single source of truth
    doc["totalTraveled"] = serialized(String(totalTraveledMM, 2));
    doc["canStart"] = canStart;
    doc["canCalibrate"] = canCalibrate;
    doc["errorMessage"] = errorMessage;
    doc["movementType"] = (int)currentMovement;
    doc["executionContext"] = (int)config.executionContext;
    doc["operationMode"] = (int)currentMovement;  // Legacy
    doc["pursuitActive"] = pursuit.isMoving;
    doc["statsRecordingEnabled"] = engine->isStatsRecordingEnabled();  // Stats recording preference
    
    // ============================================================================
    // MODE-SPECIFIC FIELDS
    // ============================================================================
    
    if (currentMovement == MOVEMENT_VAET || currentMovement == MOVEMENT_PURSUIT) {
        addVaEtVientFields(doc);
    }
    else if (currentMovement == MOVEMENT_OSC) {
        addOscillationFields(doc);
    }
    else if (currentMovement == MOVEMENT_CHAOS) {
        addChaosFields(doc);
    }
    
    // ============================================================================
    // SYSTEM STATS (On-Demand)
    // ============================================================================
    
    if (statsRequested) {
        addSystemStats(doc);
    }
    
    // Serialize to String and broadcast
    String output;
    serializeJson(doc, output);
    _webSocket->broadcastTXT(output);
}

// ============================================================================
// VA-ET-VIENT / PURSUIT MODE FIELDS
// ============================================================================

void StatusBroadcaster::addVaEtVientFields(JsonDocument& doc) {
    // Motion-specific derived values
    float cyclesPerMinForward = BaseMovement.speedLevelToCyclesPerMin(motion.speedLevelForward);
    float cyclesPerMinBackward = BaseMovement.speedLevelToCyclesPerMin(motion.speedLevelBackward);
    float avgCyclesPerMin = (cyclesPerMinForward + cyclesPerMinBackward) / 2.0;
    float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0;
    
    // Motion object (nested)
    JsonObject motionObj = doc["motion"].to<JsonObject>();
    motionObj["startPositionMM"] = serialized(String(motion.startPositionMM, 2));
    motionObj["targetDistanceMM"] = serialized(String(motion.targetDistanceMM, 2));
    motionObj["speedLevelForward"] = serialized(String(motion.speedLevelForward, 1));
    motionObj["speedLevelBackward"] = serialized(String(motion.speedLevelBackward, 1));
    
    // Cycle pause config & state
    JsonObject pauseObj = motionObj["cyclePause"].to<JsonObject>();
    pauseObj["enabled"] = motion.cyclePause.enabled;
    pauseObj["isRandom"] = motion.cyclePause.isRandom;
    pauseObj["pauseDurationSec"] = serialized(String(motion.cyclePause.pauseDurationSec, 1));
    pauseObj["minPauseSec"] = serialized(String(motion.cyclePause.minPauseSec, 1));
    pauseObj["maxPauseSec"] = serialized(String(motion.cyclePause.maxPauseSec, 1));
    pauseObj["isPausing"] = motionPauseState.isPausing;
    
    // Calculate remaining time (server-side)
    if (motionPauseState.isPausing) {
        unsigned long elapsedMs = millis() - motionPauseState.pauseStartMs;
        long remainingMs = (long)motionPauseState.currentPauseDuration - (long)elapsedMs;
        pauseObj["remainingMs"] = max(0L, remainingMs);
    } else {
        pauseObj["remainingMs"] = 0;
    }
    
    // Legacy fields (backward compatibility)
    doc["startPositionMM"] = serialized(String(motion.startPositionMM, 2));
    doc["targetDistMM"] = serialized(String(motion.targetDistanceMM, 2));
    doc["targetDistanceMM"] = serialized(String(motion.targetDistanceMM, 2));
    doc["speedLevelForward"] = serialized(String(motion.speedLevelForward, 1));
    doc["speedLevelBackward"] = serialized(String(motion.speedLevelBackward, 1));
    doc["speedLevelAvg"] = serialized(String(avgSpeedLevel, 1));
    doc["cyclesPerMinForward"] = serialized(String(cyclesPerMinForward, 1));
    doc["cyclesPerMinBackward"] = serialized(String(cyclesPerMinBackward, 1));
    doc["cyclesPerMinAvg"] = serialized(String(avgCyclesPerMin, 1));
    
    // Pending motion
    doc["hasPending"] = pendingMotion.hasChanges;
    if (pendingMotion.hasChanges) {
        JsonObject pendingObj = doc["pendingMotion"].to<JsonObject>();
        pendingObj["startPositionMM"] = serialized(String(pendingMotion.startPositionMM, 2));
        pendingObj["distanceMM"] = serialized(String(pendingMotion.distanceMM, 2));
        pendingObj["speedLevelForward"] = serialized(String(pendingMotion.speedLevelForward, 1));
        pendingObj["speedLevelBackward"] = serialized(String(pendingMotion.speedLevelBackward, 1));
        
        // Legacy pending fields
        doc["pendingStartPos"] = serialized(String(pendingMotion.startPositionMM, 2));
        doc["pendingDist"] = serialized(String(pendingMotion.distanceMM, 2));
        doc["pendingSpeedLevelForward"] = serialized(String(pendingMotion.speedLevelForward, 1));
        doc["pendingSpeedLevelBackward"] = serialized(String(pendingMotion.speedLevelBackward, 1));
    } else {
        doc["hasPending"] = false;
    }
    
    // Deceleration zone (always send, even if disabled, for UI sync)
    JsonObject decelObj = doc["decelZone"].to<JsonObject>();
    decelObj["enabled"] = decelZone.enabled;
    decelObj["enableStart"] = decelZone.enableStart;
    decelObj["enableEnd"] = decelZone.enableEnd;
    decelObj["zoneMM"] = serialized(String(decelZone.zoneMM, 1));
    decelObj["effectPercent"] = serialized(String(decelZone.effectPercent, 0));
    decelObj["mode"] = (int)decelZone.mode;
}

// ============================================================================
// OSCILLATION MODE FIELDS
// ============================================================================

void StatusBroadcaster::addOscillationFields(JsonDocument& doc) {
    // Oscillation config
    JsonObject oscObj = doc["oscillation"].to<JsonObject>();
    oscObj["centerPositionMM"] = serialized(String(oscillation.centerPositionMM, 2));
    oscObj["amplitudeMM"] = serialized(String(oscillation.amplitudeMM, 2));
    oscObj["waveform"] = (int)oscillation.waveform;
    oscObj["frequencyHz"] = serialized(String(oscillation.frequencyHz, 3));
    
    // Calculate effective frequency (capped if exceeds max speed)
    float effectiveFrequencyHz = oscillation.frequencyHz;
    if (oscillation.amplitudeMM > 0.0) {
        float maxAllowedFreq = OSC_MAX_SPEED_MM_S / (2.0 * PI * oscillation.amplitudeMM);
        if (oscillation.frequencyHz > maxAllowedFreq) {
            effectiveFrequencyHz = maxAllowedFreq;
        }
    }
    oscObj["effectiveFrequencyHz"] = serialized(String(effectiveFrequencyHz, 3));
    oscObj["actualSpeedMMS"] = serialized(String(actualOscillationSpeedMMS, 1));
    oscObj["enableRampIn"] = oscillation.enableRampIn;
    oscObj["rampInDurationMs"] = serialized(String(oscillation.rampInDurationMs, 0));
    oscObj["enableRampOut"] = oscillation.enableRampOut;
    oscObj["rampOutDurationMs"] = serialized(String(oscillation.rampOutDurationMs, 0));
    oscObj["cycleCount"] = oscillation.cycleCount;
    oscObj["returnToCenter"] = oscillation.returnToCenter;
    
    // Cycle pause config & state
    JsonObject oscPauseObj = oscObj["cyclePause"].to<JsonObject>();
    oscPauseObj["enabled"] = oscillation.cyclePause.enabled;
    oscPauseObj["isRandom"] = oscillation.cyclePause.isRandom;
    oscPauseObj["pauseDurationSec"] = serialized(String(oscillation.cyclePause.pauseDurationSec, 1));
    oscPauseObj["minPauseSec"] = serialized(String(oscillation.cyclePause.minPauseSec, 1));
    oscPauseObj["maxPauseSec"] = serialized(String(oscillation.cyclePause.maxPauseSec, 1));
    oscPauseObj["isPausing"] = oscPauseState.isPausing;
    
    // Calculate remaining time (server-side)
    if (oscPauseState.isPausing) {
        unsigned long elapsedMs = millis() - oscPauseState.pauseStartMs;
        long remainingMs = (long)oscPauseState.currentPauseDuration - (long)elapsedMs;
        oscPauseObj["remainingMs"] = max(0L, remainingMs);
    } else {
        oscPauseObj["remainingMs"] = 0;
    }
    
    // Oscillation state (minimal if no ramping, full if ramping)
    JsonObject oscStateObj = doc["oscillationState"].to<JsonObject>();
    oscStateObj["completedCycles"] = oscillationState.completedCycles;  // Always needed
    
    if (oscillation.enableRampIn || oscillation.enableRampOut) {
        // Full state if ramping enabled
        oscStateObj["currentAmplitude"] = serialized(String(oscillationState.currentAmplitude, 2));
        oscStateObj["isRampingIn"] = oscillationState.isRampingIn;
        oscStateObj["isRampingOut"] = oscillationState.isRampingOut;
    }
}

// ============================================================================
// CHAOS MODE FIELDS
// ============================================================================

void StatusBroadcaster::addChaosFields(JsonDocument& doc) {
    // Chaos config
    JsonObject chaosObj = doc["chaos"].to<JsonObject>();
    chaosObj["centerPositionMM"] = serialized(String(chaos.centerPositionMM, 2));
    chaosObj["amplitudeMM"] = serialized(String(chaos.amplitudeMM, 2));
    chaosObj["maxSpeedLevel"] = serialized(String(chaos.maxSpeedLevel, 1));
    chaosObj["crazinessPercent"] = serialized(String(chaos.crazinessPercent, 0));
    chaosObj["durationSeconds"] = chaos.durationSeconds;
    chaosObj["seed"] = chaos.seed;
    
    JsonArray patternsArray = chaosObj["patternsEnabled"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        patternsArray.add(chaos.patternsEnabled[i]);
    }
    
    // Chaos state
    JsonObject chaosStateObj = doc["chaosState"].to<JsonObject>();
    chaosStateObj["isRunning"] = chaosState.isRunning;
    chaosStateObj["currentPattern"] = (int)chaosState.currentPattern;
    
    const char* patternNames[] = {"ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", "WAVE", "PENDULUM", "SPIRAL", "BREATHING", "BRUTE_FORCE", "LIBERATOR"};
    chaosStateObj["patternName"] = patternNames[chaosState.currentPattern];
    
    chaosStateObj["targetPositionMM"] = serialized(String(chaosState.targetPositionMM, 2));
    chaosStateObj["currentSpeedLevel"] = serialized(String(chaosState.currentSpeedLevel, 1));
    chaosStateObj["minReachedMM"] = serialized(String(chaosState.minReachedMM, 2));
    chaosStateObj["maxReachedMM"] = serialized(String(chaosState.maxReachedMM, 2));
    chaosStateObj["patternsExecuted"] = chaosState.patternsExecuted;
    
    if (chaosState.isRunning && chaos.durationSeconds > 0) {
        unsigned long elapsed = (millis() - chaosState.startTime) / 1000;
        chaosStateObj["elapsedSeconds"] = elapsed;
    }
}

// ============================================================================
// SYSTEM STATS (ON-DEMAND)
// ============================================================================

void StatusBroadcaster::addSystemStats(JsonDocument& doc) {
    JsonObject systemObj = doc["system"].to<JsonObject>();
    systemObj["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    systemObj["heapTotal"] = ESP.getHeapSize();
    systemObj["heapFree"] = ESP.getFreeHeap();
    systemObj["heapUsedPercent"] = serialized(String(100.0 - (100.0 * ESP.getFreeHeap() / ESP.getHeapSize()), 1));
    systemObj["psramTotal"] = ESP.getPsramSize();
    systemObj["psramFree"] = ESP.getFreePsram();
    systemObj["psramUsedPercent"] = serialized(String(100.0 - (100.0 * ESP.getFreePsram() / ESP.getPsramSize()), 1));
    systemObj["wifiRssi"] = WiFi.RSSI();
    systemObj["temperatureC"] = serialized(String(temperatureRead(), 1));
    systemObj["uptimeSeconds"] = millis() / 1000;
    
    // Network info (IP addresses, hostname)
    systemObj["ipSta"] = WiFi.localIP().toString();
    systemObj["ipAp"] = WiFi.softAPIP().toString();
    systemObj["hostname"] = String(otaHostname);
    systemObj["ssid"] = WiFi.SSID();
    systemObj["apClients"] = WiFi.softAPgetStationNum();
    systemObj["apMode"] = Network.isAPMode();
    systemObj["staMode"] = Network.isSTAMode();
}
