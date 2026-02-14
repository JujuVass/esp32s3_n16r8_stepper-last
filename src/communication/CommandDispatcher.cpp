/**
 * ============================================================================
 * CommandDispatcher.cpp - WebSocket Command Routing Implementation
 * ============================================================================
 * 
 * Implements all WebSocket command handlers extracted from main file.
 * ~850 lines of command routing logic centralized here.
 * 
 * @author Refactored from stepper_controller_restructured.ino
 * @version 1.0
 */

#include "communication/CommandDispatcher.h"
#include "communication/StatusBroadcaster.h"
#include "communication/NetworkManager.h"
#include "core/UtilityEngine.h"
#include "movement/CalibrationManager.h"
#include "movement/SequenceTableManager.h"
#include "movement/OscillationController.h"
#include "movement/PursuitController.h"
#include "movement/ChaosController.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

CommandDispatcher& CommandDispatcher::getInstance() {
    static CommandDispatcher instance;
    return instance;
}

// Global accessor
CommandDispatcher& Dispatcher = CommandDispatcher::getInstance();

// ============================================================================
// INITIALIZATION
// ============================================================================

void CommandDispatcher::begin(WebSocketsServer* ws) {
    _webSocket = ws;
    engine->info("CommandDispatcher initialized");
}

// ============================================================================
// WEBSOCKET EVENT HANDLER
// ============================================================================

void CommandDispatcher::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    // Client connected
    if (type == WStype_CONNECTED) {
        IPAddress ip = _webSocket->remoteIP(num);
        engine->info(String("WebSocket client #") + String(num) + " connected from " + 
              String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]));
        // Don't send status automatically - let client request via getStatus
    }
    
    // Client disconnected
    if (type == WStype_DISCONNECTED) {
        engine->info(String("WebSocket client #") + String(num) + " disconnected");
        engine->saveCurrentSessionStats();
    }
    
    // Text message received
    if (type == WStype_TEXT) {
        String message = String((char*)payload);
        handleCommand(num, message);
    }
}

// ============================================================================
// MAIN COMMAND ROUTER
// ============================================================================

void CommandDispatcher::handleCommand(uint8_t clientNum, const String& message) {
    // Parse JSON
    JsonDocument doc;
    if (!parseJsonCommand(message, doc)) {
        return;  // Error already logged
    }
    
    const char* cmd = doc["cmd"];
    if (!cmd) {
        engine->warn("WebSocket command without 'cmd' field");
        return;
    }
    
    // Route to handlers - first match wins
    if (handleBasicCommands(cmd, doc)) return;
    if (handleConfigCommands(cmd, doc)) return;
    if (handleDecelZoneCommands(cmd, doc, message)) return;
    if (handleCyclePauseCommands(cmd, doc)) return;
    if (handlePursuitCommands(cmd, doc)) return;
    if (handleChaosCommands(cmd, doc, message)) return;
    if (handleOscillationCommands(cmd, doc, message)) return;
    if (handleSequencerCommands(cmd, doc, message)) return;
    
    // Unknown command
    engine->warn(String("Unknown command: ") + cmd);
}

// ============================================================================
// HELPER METHODS
// ============================================================================

bool CommandDispatcher::parseJsonCommand(const String& jsonStr, JsonDocument& doc) {
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        engine->error("JSON parse error: " + String(error.c_str()));
        Status.sendError("❌ Invalid JSON command: " + String(error.c_str()));
        return false;
    }
    
    return true;
}

bool CommandDispatcher::validateAndReport(bool isValid, const String& errorMsg) {
    if (!isValid && errorMsg.length() > 0) {
        Status.sendError(errorMsg);
        return false;
    }
    return isValid;
}

// ============================================================================
// HANDLER 1/8: BASIC COMMANDS
// ============================================================================

bool CommandDispatcher::handleBasicCommands(const char* cmd, JsonDocument& doc) {
    if (strcmp(cmd, "calibrate") == 0) {
        engine->info("Command: Calibration (delegating to Core 1)");
        // Don't call Calibration.startCalibration() directly from Core 0!
        // Set flag to trigger calibration from motorTask (Core 1)
        requestCalibration = true;
        return true;
    }
    
    if (strcmp(cmd, "start") == 0) {
        float dist = doc["distance"] | motion.targetDistanceMM;
        float spd = doc["speed"] | motion.speedLevelForward;
        
        String errorMsg;
        if (!validateAndReport(Validators::motionRange(motion.startPositionMM, dist, errorMsg), errorMsg)) return true;
        if (!validateAndReport(Validators::speed(spd, errorMsg), errorMsg)) return true;
        
        engine->info("Command: Start movement (" + String(dist, 1) + "mm @ speed " + String(spd, 1) + ")");
        BaseMovement.start(dist, spd);  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "pause") == 0) {
        engine->debug("Command: Pause/Resume");
        BaseMovement.togglePause();  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "stop") == 0) {
        engine->info("Command: Stop");
        BaseMovement.stop();  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "getStatus") == 0) {
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "syncTime") == 0) {
        uint64_t epochMs = doc["time"] | (uint64_t)0;
        if (epochMs > 0) {
            Network.syncTimeFromClient(epochMs);
        }
        return true;
    }
    
    if (strcmp(cmd, "returnToStart") == 0) {
        engine->debug("Command: Return to start");
        BaseMovement.returnToStart();  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "resetTotalDistance") == 0) {
        engine->debug("Command: Reset total distance");
        engine->resetTotalDistance();
        return true;
    }
    
    if (strcmp(cmd, "saveStats") == 0) {
        engine->debug("Command: Save stats");
        engine->saveCurrentSessionStats();
        return true;
    }
    
    if (strcmp(cmd, "setStatsRecording") == 0) {
        bool enabled = doc["enabled"] | true;
        if(!enabled) {
            engine->saveCurrentSessionStats();
        }
        engine->resetTotalDistance();
        engine->setStatsRecordingEnabled(enabled);
        sendStatus();  // Update UI with new state
        return true;
    }
    
    if (strcmp(cmd, "setMaxDistanceLimit") == 0) {
        float percent = doc["percent"] | 100.0;
        
        if (percent < 50.0 || percent > 100.0) {
            Status.sendError("⚠️ Limit must be between 50% and 100% (received: " + String(percent, 0) + "%)");
            return true;
        }
        
        if (config.currentState != STATE_READY) {
            Status.sendError("⚠️ Cannot change limit - System must be in READY state");
            return true;
        }
        
        maxDistanceLimitPercent = percent;
        engine->updateEffectiveMaxDistance();
        
        engine->info(String("✅ Limite course: ") + String(percent, 0) + "% (" + 
              String(effectiveMaxDistanceMM, 1) + " mm / " + 
              String(config.totalDistanceMM, 1) + " mm)");
        
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "setSensorsInverted") == 0) {
        bool inverted = doc["inverted"] | false;
        
        // Allow change only when stopped (READY, INIT, or ERROR states)
        if (config.currentState == STATE_RUNNING || config.currentState == STATE_CALIBRATING) {
            Status.sendError("⚠️ Stop movement before changing sensor mode");
            return true;
        }
        
        sensorsInverted = inverted;
        engine->saveSensorsInverted();
        
        // Force recalibration (physical positions have changed meaning)
        config.currentState = STATE_INIT;
        
        engine->info(String("🔄 Sensor mode: ") + (inverted ? "INVERTED (START↔END)" : "NORMAL"));
        engine->warn("⚠️ Recalibration required after sensor mode change");
        
        sendStatus();
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 2/8: CONFIG COMMANDS
// ============================================================================

bool CommandDispatcher::handleConfigCommands(const char* cmd, JsonDocument& doc) {
    if (strcmp(cmd, "setDistance") == 0) {
        float dist = doc["distance"] | 0.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::distance(dist, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set distance (" + String(dist, 1) + "mm)");
        BaseMovement.setDistance(dist);  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "setStartPosition") == 0) {
        float startPos = doc["startPosition"] | 0.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::position(startPos, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set start position (" + String(startPos, 1) + "mm)");
        BaseMovement.setStartPosition(startPos);  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "setSpeedForward") == 0) {
        float spd = doc["speed"] | 5.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::speed(spd, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set forward speed (" + String(spd, 1) + ")");
        BaseMovement.setSpeedForward(spd);  // Direct call to BaseMovement singleton
        return true;
    }
    
    if (strcmp(cmd, "setSpeedBackward") == 0) {
        float spd = doc["speed"] | 5.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::speed(spd, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set backward speed (" + String(spd, 1) + ")");
        BaseMovement.setSpeedBackward(spd);  // Direct call to BaseMovement singleton
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 3/8: ZONE EFFECT COMMANDS (Speed + Special Effects)
// ============================================================================

bool CommandDispatcher::handleDecelZoneCommands(const char* cmd, JsonDocument& doc, const String& message) {
    if (strcmp(cmd, "setDecelZone") == 0 || strcmp(cmd, "setZoneEffect") == 0) {
        // === Zone Settings ===
        zoneEffect.enabled = doc["enabled"] | false;
        zoneEffect.enableStart = doc["enableStart"] | zoneEffect.enableStart;
        zoneEffect.enableEnd = doc["enableEnd"] | zoneEffect.enableEnd;
        if (doc["mirrorOnReturn"].is<bool>()) {
            zoneEffect.mirrorOnReturn = doc["mirrorOnReturn"].as<bool>();
        }
        
        float zoneMM = doc["zoneMM"] | zoneEffect.zoneMM;
        if (zoneMM > 0) zoneEffect.zoneMM = zoneMM;
        
        // === Speed Effect ===
        // Use is<T>() to handle value 0 correctly (containsKey is deprecated)
        if (doc["speedEffect"].is<int>()) {
            int speedEffectValue = doc["speedEffect"].as<int>();
            if (speedEffectValue >= 0 && speedEffectValue <= 2) {
                zoneEffect.speedEffect = (SpeedEffect)speedEffectValue;
            }
        }
        
        // Legacy: "mode" maps to speedCurve, "effectPercent" maps to speedIntensity
        // Use is<T>() to handle value 0 correctly (containsKey is deprecated)
        if (doc["speedCurve"].is<int>()) {
            int curveValue = doc["speedCurve"].as<int>();
            if (curveValue >= 0 && curveValue <= 3) {
                zoneEffect.speedCurve = (SpeedCurve)curveValue;
            }
        } else if (doc["mode"].is<int>()) {
            int curveValue = doc["mode"].as<int>();
            if (curveValue >= 0 && curveValue <= 3) {
                zoneEffect.speedCurve = (SpeedCurve)curveValue;
            }
        }
        
        float intensity = doc["speedIntensity"] | doc["effectPercent"] | zoneEffect.speedIntensity;
        if (intensity >= 0 && intensity <= 100) {
            zoneEffect.speedIntensity = intensity;
        }
        
        // === Random Turnback ===
        if (doc["randomTurnbackEnabled"].is<bool>()) {
            zoneEffect.randomTurnbackEnabled = doc["randomTurnbackEnabled"].as<bool>();
        }
        int turnbackChance = doc["turnbackChance"] | zoneEffect.turnbackChance;
        if (turnbackChance >= 0 && turnbackChance <= 100) {
            zoneEffect.turnbackChance = turnbackChance;
        }
        
        // === End Pause ===
        if (doc["endPauseEnabled"].is<bool>()) {
            zoneEffect.endPauseEnabled = doc["endPauseEnabled"].as<bool>();
        }
        if (doc["endPauseIsRandom"].is<bool>()) {
            zoneEffect.endPauseIsRandom = doc["endPauseIsRandom"].as<bool>();
        }
        float endPauseDuration = doc["endPauseDurationSec"] | zoneEffect.endPauseDurationSec;
        if (endPauseDuration >= 0.1) zoneEffect.endPauseDurationSec = endPauseDuration;
        
        float endPauseMin = doc["endPauseMinSec"] | zoneEffect.endPauseMinSec;
        float endPauseMax = doc["endPauseMaxSec"] | zoneEffect.endPauseMaxSec;
        if (endPauseMin >= 0.1) zoneEffect.endPauseMinSec = endPauseMin;
        if (endPauseMax >= endPauseMin) zoneEffect.endPauseMaxSec = endPauseMax;
        
        // Validate zone
        BaseMovement.validateZoneEffect();
        
        // Build debug log
        String speedEffectNames[] = {"NONE", "DECEL", "ACCEL"};
        String curveNames[] = {"LINEAR", "SINE", "TRI_INV", "SINE_INV"};
        
        String zones = "";
        if (zoneEffect.enableStart) zones += "START ";
        if (zoneEffect.enableEnd) zones += "END";
        if (zoneEffect.mirrorOnReturn) zones += " MIRROR";
        
        engine->debug("✅ Zone Effect: " + String(zoneEffect.enabled ? "ON" : "OFF") + 
              (zoneEffect.enabled ? " | zones=" + zones + 
               " | speed=" + speedEffectNames[zoneEffect.speedEffect] + 
               " " + curveNames[zoneEffect.speedCurve] + " " + String(zoneEffect.speedIntensity, 0) + "%" +
               " | zone=" + String(zoneEffect.zoneMM, 1) + "mm" +
               (zoneEffect.randomTurnbackEnabled ? " | turnback=" + String(zoneEffect.turnbackChance) + "%" : "") +
               (zoneEffect.endPauseEnabled ? " | pause=" + String(zoneEffect.endPauseDurationSec, 1) + "s" : "")
               : ""));
        
        sendStatus();
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 4/8: CYCLE PAUSE COMMANDS
// ============================================================================

bool CommandDispatcher::handleCyclePauseCommands(const char* cmd, JsonDocument& doc) {
    if (strcmp(cmd, "updateCyclePause") == 0) {
        bool enabled = doc["enabled"] | false;
        bool isRandom = doc["isRandom"] | false;
        float pauseDurationSec = doc["pauseDurationSec"] | 1.5;
        float minPauseSec = doc["minPauseSec"] | 1.5;
        float maxPauseSec = doc["maxPauseSec"] | 5.0;
        
        if (minPauseSec < 0.1) minPauseSec = 0.1;
        if (maxPauseSec < minPauseSec) maxPauseSec = minPauseSec + 0.5;
        if (pauseDurationSec < 0.1) pauseDurationSec = 0.1;
        
        motion.cyclePause.enabled = enabled;
        motion.cyclePause.isRandom = isRandom;
        motion.cyclePause.pauseDurationSec = pauseDurationSec;
        motion.cyclePause.minPauseSec = minPauseSec;
        motion.cyclePause.maxPauseSec = maxPauseSec;
        
        engine->info(String("⏸️ Cycle pause VAET: ") + (enabled ? "enabled" : "disabled") +
              " | Mode: " + (isRandom ? "random" : "fixed") +
              " | " + (isRandom ? String(minPauseSec, 1) + "-" + String(maxPauseSec, 1) + "s" : String(pauseDurationSec, 1) + "s"));
        
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "updateCyclePauseOsc") == 0) {
        bool enabled = doc["enabled"] | false;
        bool isRandom = doc["isRandom"] | false;
        float pauseDurationSec = doc["pauseDurationSec"] | 1.5;
        float minPauseSec = doc["minPauseSec"] | 1.5;
        float maxPauseSec = doc["maxPauseSec"] | 5.0;
        
        if (minPauseSec < 0.1) minPauseSec = 0.1;
        if (maxPauseSec < minPauseSec) maxPauseSec = minPauseSec + 0.5;
        if (pauseDurationSec < 0.1) pauseDurationSec = 0.1;
        
        oscillation.cyclePause.enabled = enabled;
        oscillation.cyclePause.isRandom = isRandom;
        oscillation.cyclePause.pauseDurationSec = pauseDurationSec;
        oscillation.cyclePause.minPauseSec = minPauseSec;
        oscillation.cyclePause.maxPauseSec = maxPauseSec;
        
        engine->info(String("⏸️ Cycle pause OSC: ") + (enabled ? "enabled" : "disabled") +
              " | Mode: " + (isRandom ? "random" : "fixed") +
              " | " + (isRandom ? String(minPauseSec, 1) + "-" + String(maxPauseSec, 1) + "s" : String(pauseDurationSec, 1) + "s"));
        
        sendStatus();
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 5/8: PURSUIT COMMANDS
// ============================================================================

bool CommandDispatcher::handlePursuitCommands(const char* cmd, JsonDocument& doc) {
    if (strcmp(cmd, "enablePursuitMode") == 0) {
        if (config.currentState == STATE_CALIBRATING) {
            Status.sendError("⚠️ Cannot enable Pursuit mode: calibration in progress");
            return true;
        }
        if (config.currentState == STATE_ERROR) {
            Status.sendError("⚠️ Cannot enable Pursuit mode: system in error state");
            return true;
        }
        
        if (seqState.isRunning) {
            SeqExecutor.stop();
        }
        
        currentMovement = MOVEMENT_PURSUIT;
        config.executionContext = CONTEXT_STANDALONE;
        
        // Protected state change (Core 0 → Core 1 safety)
        {
            MutexGuard guard(stateMutex);
            if (guard && config.currentState == STATE_RUNNING) {
                config.currentState = STATE_READY;
            }
        }
        
        engine->debug("✅ Pursuit mode enabled");
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "pursuitMove") == 0) {
        if (currentMovement != MOVEMENT_PURSUIT) {
            engine->warn("pursuitMove ignored: not in PURSUIT mode");
            return true;
        }
        if (config.currentState == STATE_CALIBRATING) {
            engine->warn("pursuitMove ignored: calibration in progress");
            return true;
        }
        
        float targetPos = doc["targetPosition"] | 0.0;
        float maxSpd = doc["maxSpeed"] | 10.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::speed(maxSpd, errorMsg), errorMsg)) return true;
        
        Pursuit.move(targetPos, maxSpd);
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 6/8: CHAOS COMMANDS
// ============================================================================

bool CommandDispatcher::handleChaosCommands(const char* cmd, JsonDocument& doc, const String& message) {
    if (strcmp(cmd, "startChaos") == 0) {
        if (config.currentState == STATE_CALIBRATING) {
            Status.sendError("⚠️ Cannot start Chaos mode: calibration in progress");
            return true;
        }
        if (config.currentState == STATE_ERROR) {
            Status.sendError("⚠️ Cannot start Chaos mode: system in error state");
            return true;
        }
        
        float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
        chaos.centerPositionMM = doc["centerPositionMM"] | (effectiveMax / 2.0);
        chaos.amplitudeMM = doc["amplitudeMM"] | 50.0;
        chaos.maxSpeedLevel = doc["maxSpeedLevel"] | 10.0;
        chaos.crazinessPercent = doc["crazinessPercent"] | 50.0;
        // Don't use | operator for durationSeconds as 0 means infinite (falsy but valid)
        if (!doc["durationSeconds"].isNull()) {
            chaos.durationSeconds = doc["durationSeconds"].as<int>();
        } else {
            chaos.durationSeconds = 60;  // Default: 60 seconds
        }
        chaos.seed = doc["seed"] | (int)millis();
        
        String errorMsg;
        if (!validateAndReport(Validators::chaosParams(chaos.centerPositionMM, chaos.amplitudeMM, 
            chaos.maxSpeedLevel, chaos.crazinessPercent, errorMsg), errorMsg)) {
            return true;
        }
        
        // Parse patterns array
        JsonArray patternsArray = doc["patternsEnabled"];
        if (patternsArray) {
            int idx = 0;
            for (JsonVariant v : patternsArray) {
                if (idx < 11) {
                    chaos.patternsEnabled[idx] = v.as<bool>();
                    idx++;
                }
            }
        }
        
        engine->info("🌪️ Starting Chaos mode: center=" + String(chaos.centerPositionMM, 1) + 
              "mm, amplitude=±" + String(chaos.amplitudeMM, 1) + "mm, speed=" + 
              String(chaos.maxSpeedLevel, 1) + ", craziness=" + String(chaos.crazinessPercent, 0) + "%");
        
        Chaos.start();
        return true;
    }
    
    if (strcmp(cmd, "stopChaos") == 0) {
        Chaos.stop();
        return true;
    }
    
    if (strcmp(cmd, "setChaosConfig") == 0) {
        chaos.centerPositionMM = doc["centerPositionMM"] | chaos.centerPositionMM;
        chaos.amplitudeMM = doc["amplitudeMM"] | chaos.amplitudeMM;
        chaos.maxSpeedLevel = doc["maxSpeedLevel"] | chaos.maxSpeedLevel;
        chaos.crazinessPercent = doc["crazinessPercent"] | chaos.crazinessPercent;
        // Don't use | operator for durationSeconds as 0 means infinite (falsy but valid)
        if (!doc["durationSeconds"].isNull()) {
            chaos.durationSeconds = doc["durationSeconds"].as<int>();
        }
        
        String errorMsg;
        if (!validateAndReport(Validators::chaosParams(chaos.centerPositionMM, chaos.amplitudeMM,
            chaos.maxSpeedLevel, chaos.crazinessPercent, errorMsg), errorMsg)) {
            return true;
        }
        
        if (!chaosState.isRunning) {
            chaos.seed = doc["seed"] | chaos.seed;
        }
        
        JsonArray patternsArray = doc["patternsEnabled"];
        if (patternsArray) {
            int idx = 0;
            for (JsonVariant v : patternsArray) {
                if (idx < 11) {
                    chaos.patternsEnabled[idx] = v.as<bool>();
                    idx++;
                }
            }
        }
        
        engine->debug("✅ Chaos config: center=" + String(chaos.centerPositionMM, 1) + "mm | amp=±" + 
              String(chaos.amplitudeMM, 1) + "mm | speed=" + String(chaos.maxSpeedLevel, 1) + 
              "/" + String(MAX_SPEED_LEVEL, 0) + " | craziness=" + String(chaos.crazinessPercent, 0) + 
              "% | duration=" + String(chaos.durationSeconds) + "s" + 
              (chaosState.isRunning ? " | ✓ Applied live" : " | seed=" + String(chaos.seed)));
        
        if (chaosState.isRunning) {
            // Reset duration timer when applying config live (avoid immediate stop if new duration < elapsed)
            chaosState.startTime = millis();
            chaosState.nextPatternChangeTime = millis();
        }
        
        sendStatus();
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 7/8: OSCILLATION COMMANDS
// ============================================================================

bool CommandDispatcher::handleOscillationCommands(const char* cmd, JsonDocument& doc, const String& message) {
    if (strcmp(cmd, "setOscillation") == 0) {
        float oldCenter = oscillation.centerPositionMM;
        float oldAmplitude = oscillation.amplitudeMM;
        float oldFrequency = oscillation.frequencyHz;
        OscillationWaveform oldWaveform = oscillation.waveform;
        
        oscillation.centerPositionMM = doc["centerPositionMM"] | oscillation.centerPositionMM;
        oscillation.amplitudeMM = doc["amplitudeMM"] | oscillation.amplitudeMM;
        oscillation.waveform = (OscillationWaveform)(doc["waveform"] | (int)oscillation.waveform);
        oscillation.frequencyHz = doc["frequencyHz"] | oscillation.frequencyHz;
        
        oscillation.enableRampIn = doc["enableRampIn"] | oscillation.enableRampIn;
        // Don't use | for rampDurationMs as 0 means disabled (falsy but valid)
        if (!doc["rampInDurationMs"].isNull()) {
            oscillation.rampInDurationMs = doc["rampInDurationMs"].as<int>();
        }
        oscillation.enableRampOut = doc["enableRampOut"] | oscillation.enableRampOut;
        if (!doc["rampOutDurationMs"].isNull()) {
            oscillation.rampOutDurationMs = doc["rampOutDurationMs"].as<int>();
        }
        
        // Don't use | for cycleCount as 0 means infinite (falsy but valid)
        if (!doc["cycleCount"].isNull()) {
            oscillation.cycleCount = doc["cycleCount"].as<int>();
        }
        oscillation.returnToCenter = doc["returnToCenter"] | oscillation.returnToCenter;
        
        // Cycle pause parameters
        if (doc["cyclePauseEnabled"].is<bool>()) {
            oscillation.cyclePause.enabled = doc["cyclePauseEnabled"];
            oscillation.cyclePause.isRandom = doc["cyclePauseIsRandom"] | false;
            oscillation.cyclePause.pauseDurationSec = doc["cyclePauseDurationSec"] | 0.0f;
            oscillation.cyclePause.minPauseSec = doc["cyclePauseMinSec"] | 0.5f;
            oscillation.cyclePause.maxPauseSec = doc["cyclePauseMaxSec"] | 3.0f;
        }
        
        bool paramsChanged = (oldCenter != oscillation.centerPositionMM || 
                              oldAmplitude != oscillation.amplitudeMM ||
                              oldFrequency != oscillation.frequencyHz ||
                              oldWaveform != oscillation.waveform);
        
        bool isOscRunning = (currentMovement == MOVEMENT_OSC && config.currentState == STATE_RUNNING);
        
        if (paramsChanged) {
            engine->debug("📝 OSC config: center=" + String(oscillation.centerPositionMM, 1) + 
                  "mm | amp=" + String(oscillation.amplitudeMM, 1) + 
                  "mm | freq=" + String(oscillation.frequencyHz, 3) + "Hz" +
                  (isOscRunning ? " | ⚡ Live update" : ""));
            
            // Smooth center transition during active oscillation
            if (isOscRunning && oldCenter != oscillation.centerPositionMM) {
                oscillationState.isCenterTransitioning = true;
                oscillationState.centerTransitionStartMs = millis();
                oscillationState.oldCenterMM = oldCenter;
                oscillationState.targetCenterMM = oscillation.centerPositionMM;
            }
            
            // Smooth amplitude transition
            if (isOscRunning && oldAmplitude != oscillation.amplitudeMM) {
                oscillationState.isAmplitudeTransitioning = true;
                oscillationState.amplitudeTransitionStartMs = millis();
                oscillationState.oldAmplitudeMM = oldAmplitude;
                oscillationState.targetAmplitudeMM = oscillation.amplitudeMM;
            }
            
            // Disable ramps for immediate effect
            if (isOscRunning) {
                oscillationState.isRampingIn = false;
                oscillationState.isRampingOut = false;
            }
        }
        
        String errorMsg;
        if (!validateAndReport(Validators::oscillationParams(oscillation.centerPositionMM, 
            oscillation.amplitudeMM, oscillation.frequencyHz, errorMsg), errorMsg)) {
            return true;
        }
        
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "setCyclePause") == 0) {
        const char* mode = doc["mode"];
        
        if (mode && strcmp(mode, "simple") == 0) {
            motion.cyclePause.enabled = doc["enabled"] | false;
            motion.cyclePause.isRandom = doc["isRandom"] | false;
            motion.cyclePause.pauseDurationSec = doc["durationSec"] | 0.0f;
            
            float minSec = doc["minSec"] | 0.5f;
            float maxSec = doc["maxSec"] | 3.0f;
            
            if (minSec > maxSec) {
                float temp = minSec;
                minSec = maxSec;
                maxSec = temp;
            }
            
            motion.cyclePause.minPauseSec = min(minSec, 60.0f);
            motion.cyclePause.maxPauseSec = min(maxSec, 60.0f);
            
        } else if (mode && strcmp(mode, "oscillation") == 0) {
            oscillation.cyclePause.enabled = doc["enabled"] | false;
            oscillation.cyclePause.isRandom = doc["isRandom"] | false;
            oscillation.cyclePause.pauseDurationSec = doc["durationSec"] | 0.0f;
            
            float minSec = doc["minSec"] | 0.5f;
            float maxSec = doc["maxSec"] | 3.0f;
            
            if (minSec > maxSec) {
                float temp = minSec;
                minSec = maxSec;
                maxSec = temp;
            }
            
            oscillation.cyclePause.minPauseSec = min(minSec, 60.0f);
            oscillation.cyclePause.maxPauseSec = min(maxSec, 60.0f);
        }
        
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "startOscillation") == 0) {
        if (config.currentState == STATE_INIT || config.currentState == STATE_CALIBRATING) {
            Status.sendError("⚠️ Calibration required before starting oscillation");
            return true;
        }
        
        if (seqState.isRunning) {
            SeqExecutor.stop();
        }
        
        if (config.currentState == STATE_RUNNING) {
            stopMovement();
        }
        
        Osc.start();
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "stopOscillation") == 0) {
        if (currentMovement == MOVEMENT_OSC) {
            stopMovement();
            currentMovement = MOVEMENT_VAET;
        }
        
        if (seqState.isRunning) {
            SeqExecutor.stop();
        }
        
        sendStatus();
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 8/8: SEQUENCER COMMANDS
// ============================================================================

bool CommandDispatcher::handleSequencerCommands(const char* cmd, JsonDocument& doc, const String& message) {
    
    // ========================================================================
    // SEQUENCE TABLE MANAGEMENT (CRUD)
    // ========================================================================
    
    if (strcmp(cmd, "addSequenceLine") == 0) {
        SequenceLine newLine = SeqTable.parseFromJson(doc);
        
        String validationError = SeqTable.validatePhysics(newLine);
        if (validationError.length() > 0) {
            Status.sendError("❌ Invalid line: " + validationError);
            return true;
        }
        
        String errorMsg;
        if (newLine.movementType == MOVEMENT_VAET) {
            if (!validateAndReport(Validators::speed(newLine.speedForward, errorMsg), errorMsg)) return true;
            if (!validateAndReport(Validators::speed(newLine.speedBackward, errorMsg), errorMsg)) return true;
        } else if (newLine.movementType == MOVEMENT_OSC) {
            if (newLine.oscFrequencyHz <= 0 || newLine.oscFrequencyHz > 10.0) {
                Status.sendError("❌ Frequency must be 0.01-10 Hz");
                return true;
            }
        } else if (newLine.movementType == MOVEMENT_CHAOS) {
            if (!validateAndReport(Validators::speed(newLine.chaosMaxSpeedLevel, errorMsg), errorMsg)) return true;
            if (newLine.chaosDurationSeconds < 1 || newLine.chaosDurationSeconds > 3600) {
                Status.sendError("❌ Duration must be 1-3600 seconds");
                return true;
            }
        }
        
        if (newLine.cycleCount < 1 || newLine.cycleCount > 9999) {
            Status.sendError("❌ Cycle count must be 1-9999 (received: " + String(newLine.cycleCount) + ")");
            return true;
        }
        
        SeqTable.addLine(newLine);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "deleteSequenceLine") == 0) {
        int lineId = doc["lineId"] | -1;
        if (lineId < 0) {
            Status.sendError("❌ Invalid Line ID");
            return true;
        }
        SeqTable.deleteLine(lineId);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "updateSequenceLine") == 0) {
        int lineId = doc["lineId"] | -1;
        SequenceLine updatedLine = SeqTable.parseFromJson(doc);
        
        String validationError = SeqTable.validatePhysics(updatedLine);
        if (validationError.length() > 0) {
            Status.sendError("❌ Invalid line: " + validationError);
            return true;
        }
        
        SeqTable.updateLine(lineId, updatedLine);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "moveSequenceLine") == 0) {
        int lineId = doc["lineId"] | -1;
        int direction = doc["direction"] | 0;
        SeqTable.moveLine(lineId, direction);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "reorderSequenceLine") == 0) {
        int lineId = doc["lineId"] | -1;
        int newIndex = doc["newIndex"] | -1;
        SeqTable.reorderLine(lineId, newIndex);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "duplicateSequenceLine") == 0) {
        int lineId = doc["lineId"] | -1;
        SeqTable.duplicateLine(lineId);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "toggleSequenceLine") == 0) {
        int lineId = doc["lineId"] | -1;
        bool enabled = doc["enabled"] | false;
        SeqTable.toggleLine(lineId, enabled);
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "clearSequence") == 0) {
        SeqTable.clear();
        SeqTable.broadcast();
        return true;
    }
    
    if (strcmp(cmd, "getSequenceTable") == 0) {
        SeqTable.broadcast();
        return true;
    }
    
    // ========================================================================
    // SEQUENCE EXECUTION CONTROL
    // ========================================================================
    
    if (strcmp(cmd, "startSequence") == 0) {
        SeqExecutor.start(false);
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "loopSequence") == 0) {
        SeqExecutor.start(true);
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "stopSequence") == 0) {
        SeqExecutor.stop();
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "toggleSequencePause") == 0) {
        SeqExecutor.togglePause();
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "skipSequenceLine") == 0) {
        SeqExecutor.skipToNextLine();
        SeqExecutor.sendStatus();
        return true;
    }
    
    // ========================================================================
    // IMPORT/EXPORT
    // ========================================================================
    
    if (strcmp(cmd, "exportSequence") == 0) {
        SeqTable.sendJsonResponse("exportData", SeqTable.exportToJson());
        return true;
    }
    
    if (strcmp(cmd, "toggleDebug") == 0) {
        if (engine) {
            LogLevel current = engine->getLogLevel();
            LogLevel next = (current == LOG_DEBUG) ? LOG_INFO : LOG_DEBUG;
            engine->setLogLevel(next);
            engine->info(String("Log level set to: ") + (next == LOG_DEBUG ? "DEBUG" : "INFO"));
        }
        return true;
    }
    
    if (strcmp(cmd, "importSequence") == 0) {
        int dataStart = message.indexOf("\"jsonData\":\"");
        if (dataStart > 0) {
            dataStart += 12;
            int dataEnd = message.lastIndexOf("\"}");
            
            if (dataEnd > dataStart) {
                String jsonData = message.substring(dataStart, dataEnd);
                
                jsonData.replace("\\\"", "\"");
                jsonData.replace("\\n", "");
                jsonData.replace("\\r", "");
                jsonData.replace("\\t", "");
                
                SeqTable.importFromJson(jsonData);
                SeqTable.broadcast();
            } else {
                Status.sendError("❌ JSON parsing error: invalid dataEnd");
            }
        } else {
            Status.sendError("❌ JSON parsing error: jsonData field not found");
        }
        return true;
    }
    
    // ========================================================================
    // STATS ON-DEMAND
    // ========================================================================
    
    if (strcmp(cmd, "requestStats") == 0) {
        bool enable = doc["enable"] | false;
        statsRequested = enable;

        
        engine->debug(String("📊 Stats tracking: ") + (enable ? "ENABLED" : "DISABLED"));
        
        if (enable) {
            engine->saveCurrentSessionStats();
            sendStatus();
        }
        
        return true;
    }
    
    return false;
}
