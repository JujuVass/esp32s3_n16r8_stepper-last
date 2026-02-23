/**
 * ============================================================================
 * StatusBroadcaster.cpp - WebSocket Status Broadcasting Implementation
 * ============================================================================
 *
 * Handles mode-specific JSON construction and status broadcasting.
 */

#include "communication/StatusBroadcaster.h"
#include "communication/NetworkManager.h"
#include "core/MovementMath.h"
#include "movement/ChaosController.h"
#include "movement/OscillationController.h"
#include "movement/PursuitController.h"
#include "movement/BaseMovementController.h"
#include "movement/SequenceExecutor.h"
#include "core/UtilityEngine.h"
#include <WiFi.h>

using enum SystemState;
using enum MovementType;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

StatusBroadcaster& StatusBroadcaster::getInstance() {
    static StatusBroadcaster instance; // NOSONAR(cpp:S6018)
    return instance;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void StatusBroadcaster::begin(AsyncWebSocket* ws) {
    _webSocket = ws;
    engine->info("StatusBroadcaster initialized");
}

// ============================================================================
// ADAPTIVE BROADCAST RATE
// ============================================================================

unsigned long StatusBroadcaster::getAdaptiveBroadcastInterval() const {
    // File upload: minimal broadcast to reduce TCP contention
    if (isUploadActive()) {
        return STATUS_UPLOAD_INTERVAL_MS;     // 2000ms (0.5 Hz) - reduce load during upload
    }

    // Pursuit mode gets highest priority for fast feedback
    if (currentMovement == MOVEMENT_PURSUIT) {
        return STATUS_PURSUIT_INTERVAL_MS;       // 50ms (20 Hz) - real-time gauge tracking
    }

    switch (config.currentState) {
        case STATE_RUNNING:
        case STATE_PAUSED:
            return STATUS_UPDATE_INTERVAL_MS;    // 100ms (10 Hz) - real-time feedback
        case STATE_CALIBRATING:
            return STATUS_CALIB_INTERVAL_MS;     // 200ms (5 Hz) - position matters
        default:
            return STATUS_IDLE_INTERVAL_MS;      // 1000ms (1 Hz) - keep-alive only
    }
}

// ============================================================================
// MAIN BROADCAST METHOD
// ============================================================================

void StatusBroadcaster::send() {
    // Start timing for performance monitoring
    unsigned long startMicros = micros();

    // Only broadcast if clients are connected (early exit optimization)
    if (_webSocket == nullptr) {
        engine->debug("⚠️ sendStatus: _webSocket is NULL!");
        return;
    }
    if (_webSocket->count() == 0) {
        return;
    }

    // ============================================================================
    // UPLOAD MODE: Lightweight payload (just state + updating flag)
    // ============================================================================
    bool uploading = isUploadActive();

    // Detect upload→normal transition: force next broadcast (hash dedup would skip it)
    static bool wasUploading = false;
    if (wasUploading && !uploading) {
        _lastBroadcastHash = 0;  // Invalidate hash so client gets the "updating gone" message
    }
    wasUploading = uploading;

    if (uploading) {
        float uploadPosMM = MovementMath::stepsToMM(currentStep);
        JsonDocument doc;
        doc["state"] = (int)config.currentState;
        doc["updating"] = true;
        doc["positionMM"] = serialized(String(uploadPosMM, 2));
        doc["currentStep"] = currentStep;
        doc["ip"] = StepperNetwork.getIPAddress();

        String output;
        serializeJson(doc, output);
        _webSocket->textAll(output);
        return;
    }

    // ============================================================================
    // COMMON FIELDS (all modes)
    // ============================================================================

    // Calculate derived values
    float positionMM = MovementMath::stepsToMM(currentStep);
    float totalTraveledMM = MovementMath::stepsToMM(stats.totalDistanceTraveled);

    // Validation state - canStart controls UI visibility (tabs shown after calibration)
    bool canStart = (config.totalDistanceMM > 0);  // Show UI after calibration
    String errorMessage = "";
    if (!canStart) {
        errorMessage = "Recalibration required";
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
    doc["pursuitActive"] = pursuit.isMoving;
    doc["statsRecordingEnabled"] = engine->isStatsRecordingEnabled();  // Stats recording preference
    doc["sensorsInverted"] = sensorsInverted;  // Sensors inversion mode
    doc["ip"] = StepperNetwork.getIPAddress();  // Cached IP for WebSocket reconnection

    // StepperNetwork mode info
    doc["networkMode"] = (int)StepperNetwork.getMode();  // 0=AP_SETUP, 1=STA_AP, 2=AP_DIRECT
    doc["apClients"] = StepperNetwork.getAPClientCount();  // Number of AP clients
    doc["wdState"] = (int)StepperNetwork.getWatchdogState();  // Watchdog: 0=healthy, 1=soft, 2=hard, 3=reboot

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

    // Check for JSON overflow (heap fragmentation could truncate document)
    if (doc.overflowed()) {
        engine->warn("⚠️ JSON doc overflowed - status broadcast skipped");
        return;
    }

    // Serialize to String and broadcast
    String output;
    serializeJson(doc, output);

    // Hash-based deduplication: skip broadcast if payload is identical to last one
    // Uses FNV-1a hash (fast, good distribution, no library needed)
    uint32_t hash = 2166136261u;  // FNV offset basis
    const char* data = output.c_str();
    size_t len = output.length();
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;  // FNV prime
    }

    // Always send if stats were requested (on-demand, don't skip)
    if (hash == _lastBroadcastHash && !statsRequested) {
        return;  // Identical payload, skip broadcast
    }

    _lastBroadcastHash = hash;

    _webSocket->textAll(output);

    // Performance monitoring: warn if broadcast took too long (can cause step loss)
    unsigned long elapsedMicros = micros() - startMicros;
    if (elapsedMicros > BROADCAST_SLOW_THRESHOLD_US) {
        engine->warn("⚠️ SLOW BROADCAST: " + String(elapsedMicros / 1000.0, 1) + "ms");
    }
}

// ============================================================================
// ERROR BROADCASTING
// ============================================================================

void StatusBroadcaster::sendError(const String& message) {
    // Use structured logging
    engine->error(message);

    // Send to all WebSocket clients (only if clients connected)
    if (_webSocket != nullptr && _webSocket->count() > 0) {
        JsonDocument doc;
        doc["type"] = "error";
        doc["message"] = message;  // ArduinoJson handles escaping automatically

        String json;
        serializeJson(doc, json);
        _webSocket->textAll(json);
    }
}

// ============================================================================
// VA-ET-VIENT / PURSUIT MODE FIELDS
// ============================================================================

void StatusBroadcaster::addVaEtVientFields(JsonDocument& doc) {
    // Motion-specific derived values
    float cyclesPerMinForward = MovementMath::speedLevelToCPM(motion.speedLevelForward);
    float cyclesPerMinBackward = MovementMath::speedLevelToCPM(motion.speedLevelBackward);

    // Motion object (nested)
    JsonObject motionObj = doc["motion"].to<JsonObject>();
    motionObj["startPositionMM"] = serialized(String(motion.startPositionMM, 2));
    motionObj["targetDistanceMM"] = serialized(String(motion.targetDistanceMM, 2));
    motionObj["speedLevelForward"] = serialized(String(motion.speedLevelForward, 1));
    motionObj["speedLevelBackward"] = serialized(String(motion.speedLevelBackward, 1));
    motionObj["cyclesPerMinForward"] = serialized(String(cyclesPerMinForward, 1));
    motionObj["cyclesPerMinBackward"] = serialized(String(cyclesPerMinBackward, 1));

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

    // Pending motion
    doc["hasPending"] = pendingMotion.hasChanges;
    if (pendingMotion.hasChanges) {
        JsonObject pendingObj = doc["pendingMotion"].to<JsonObject>();
        pendingObj["startPositionMM"] = serialized(String(pendingMotion.startPositionMM, 2));
        pendingObj["distanceMM"] = serialized(String(pendingMotion.distanceMM, 2));
        pendingObj["speedLevelForward"] = serialized(String(pendingMotion.speedLevelForward, 1));
        pendingObj["speedLevelBackward"] = serialized(String(pendingMotion.speedLevelBackward, 1));
    } else {
        doc["hasPending"] = false;
    }

    // Zone effects (always send, even if disabled, for UI sync)
    // Keep "decelZone" key for backward compatibility with existing frontend
    JsonObject zoneObj = doc["decelZone"].to<JsonObject>();
    zoneObj["enabled"] = zoneEffect.enabled;
    zoneObj["enableStart"] = zoneEffect.enableStart;
    zoneObj["enableEnd"] = zoneEffect.enableEnd;
    zoneObj["mirrorOnReturn"] = zoneEffect.mirrorOnReturn;
    zoneObj["zoneMM"] = serialized(String(zoneEffect.zoneMM, 1));

    // Speed effect
    zoneObj["speedEffect"] = (int)zoneEffect.speedEffect;
    zoneObj["speedCurve"] = (int)zoneEffect.speedCurve;
    zoneObj["speedIntensity"] = serialized(String(zoneEffect.speedIntensity, 0));

    // Random turnback
    zoneObj["randomTurnbackEnabled"] = zoneEffect.randomTurnbackEnabled;
    zoneObj["turnbackChance"] = zoneEffect.turnbackChance;

    // End pause
    zoneObj["endPauseEnabled"] = zoneEffect.endPauseEnabled;
    zoneObj["endPauseIsRandom"] = zoneEffect.endPauseIsRandom;
    zoneObj["endPauseDurationSec"] = serialized(String(zoneEffect.endPauseDurationSec, 1));
    zoneObj["endPauseMinSec"] = serialized(String(zoneEffect.endPauseMinSec, 1));
    zoneObj["endPauseMaxSec"] = serialized(String(zoneEffect.endPauseMaxSec, 1));
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

    // Effective frequency (capped by hardware speed limit) — DRY: uses shared helper
    float effectiveFrequencyHz = MovementMath::effectiveFrequency(
        oscillation.frequencyHz, oscillation.amplitudeMM);
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
    for (bool enabled : chaos.patternsEnabled) {
        patternsArray.add(enabled);
    }

    // Chaos state
    JsonObject chaosStateObj = doc["chaosState"].to<JsonObject>();
    chaosStateObj["isRunning"] = chaosState.isRunning;
    chaosStateObj["currentPattern"] = (int)chaosState.currentPattern;

    chaosStateObj["patternName"] = CHAOS_PATTERN_NAMES[static_cast<int>(chaosState.currentPattern)];

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

    // StepperNetwork info (IP addresses, hostname)
    systemObj["ipSta"] = WiFi.localIP().toString();
    systemObj["ipAp"] = WiFi.softAPIP().toString();
    systemObj["hostname"] = String(otaHostname);
    systemObj["ssid"] = WiFi.SSID();
    systemObj["apClients"] = WiFi.softAPgetStationNum();
    systemObj["apMode"] = StepperNetwork.isAPMode();
    systemObj["staMode"] = StepperNetwork.isSTAMode();
}

// ============================================================================
// UPLOAD STATUS CHECK
// ============================================================================

bool StatusBroadcaster::isUploadActive() const {
    if (lastUploadActivityTime == 0) return false;
    return (millis() - lastUploadActivityTime) < UPLOAD_ACTIVITY_TIMEOUT_MS;
}
