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
#include "UtilityEngine.h"
#include "controllers/CalibrationManager.h"
#include "hardware/MotorDriver.h"
#include "sequencer/SequenceTableManager.h"

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
        saveCurrentSessionStats();
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
        sendError("❌ Commande JSON invalide: " + String(error.c_str()));
        return false;
    }
    
    return true;
}

bool CommandDispatcher::validateAndReport(bool isValid, const String& errorMsg) {
    if (!isValid && errorMsg.length() > 0) {
        sendError(errorMsg);
        return false;
    }
    return isValid;
}

// ============================================================================
// HANDLER 1/8: BASIC COMMANDS
// ============================================================================

bool CommandDispatcher::handleBasicCommands(const char* cmd, JsonDocument& doc) {
    if (strcmp(cmd, "calibrate") == 0) {
        engine->info("Command: Calibration");
        Calibration.startCalibration();
        return true;
    }
    
    if (strcmp(cmd, "start") == 0) {
        float dist = doc["distance"] | motion.targetDistanceMM;
        float spd = doc["speed"] | motion.speedLevelForward;
        
        String errorMsg;
        if (!validateAndReport(Validators::motionRange(motion.startPositionMM, dist, errorMsg), errorMsg)) return true;
        if (!validateAndReport(Validators::speed(spd, errorMsg), errorMsg)) return true;
        
        engine->info("Command: Start movement (" + String(dist, 1) + "mm @ speed " + String(spd, 1) + ")");
        startMovement(dist, spd);
        return true;
    }
    
    if (strcmp(cmd, "pause") == 0) {
        engine->debug("Command: Pause/Resume");
        togglePause();
        return true;
    }
    
    if (strcmp(cmd, "stop") == 0) {
        engine->info("Command: Stop");
        stopMovement();
        return true;
    }
    
    if (strcmp(cmd, "getStatus") == 0) {
        sendStatus();
        return true;
    }
    
    if (strcmp(cmd, "returnToStart") == 0) {
        engine->debug("Command: Return to start");
        returnToStart();
        return true;
    }
    
    if (strcmp(cmd, "resetTotalDistance") == 0) {
        engine->debug("Command: Reset total distance");
        resetTotalDistance();
        return true;
    }
    
    if (strcmp(cmd, "saveStats") == 0) {
        engine->debug("Command: Save stats");
        saveCurrentSessionStats();
        return true;
    }
    
    if (strcmp(cmd, "setMaxDistanceLimit") == 0) {
        float percent = doc["percent"] | 100.0;
        
        if (percent < 50.0 || percent > 100.0) {
            sendError("⚠️ Limite doit être entre 50% et 100% (reçu: " + String(percent, 0) + "%)");
            return true;
        }
        
        if (config.currentState != STATE_READY) {
            sendError("⚠️ Modification limite impossible - Système doit être en état PRÊT");
            return true;
        }
        
        maxDistanceLimitPercent = percent;
        updateEffectiveMaxDistance();
        
        engine->info(String("✅ Limite course: ") + String(percent, 0) + "% (" + 
              String(effectiveMaxDistanceMM, 1) + " mm / " + 
              String(config.totalDistanceMM, 1) + " mm)");
        
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
        setDistance(dist);
        return true;
    }
    
    if (strcmp(cmd, "setStartPosition") == 0) {
        float startPos = doc["startPosition"] | 0.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::position(startPos, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set start position (" + String(startPos, 1) + "mm)");
        setStartPosition(startPos);
        return true;
    }
    
    if (strcmp(cmd, "setSpeedForward") == 0) {
        float spd = doc["speed"] | 5.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::speed(spd, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set forward speed (" + String(spd, 1) + ")");
        setSpeedForward(spd);
        return true;
    }
    
    if (strcmp(cmd, "setSpeedBackward") == 0) {
        float spd = doc["speed"] | 5.0;
        
        String errorMsg;
        if (!validateAndReport(Validators::speed(spd, errorMsg), errorMsg)) return true;
        
        engine->debug("Command: Set backward speed (" + String(spd, 1) + ")");
        setSpeedBackward(spd);
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 3/8: DECEL ZONE COMMANDS
// ============================================================================

bool CommandDispatcher::handleDecelZoneCommands(const char* cmd, JsonDocument& doc, const String& message) {
    if (message.indexOf("\"cmd\":\"setDecelZone\"") > 0) {
        decelZone.enabled = message.indexOf("\"enabled\":true") > 0;
        decelZone.enableStart = doc["enableStart"] | decelZone.enableStart;
        decelZone.enableEnd = doc["enableEnd"] | decelZone.enableEnd;
        
        float zoneMM = doc["zoneMM"] | decelZone.zoneMM;
        float effectPercent = doc["effectPercent"] | decelZone.effectPercent;
        int modeValue = doc["mode"] | (int)decelZone.mode;
        
        if (zoneMM > 0) decelZone.zoneMM = zoneMM;
        if (effectPercent >= 0 && effectPercent <= 100) decelZone.effectPercent = effectPercent;
        if (modeValue >= 0 && modeValue <= 3) decelZone.mode = (DecelMode)modeValue;
        
        validateDecelZone();
        
        String zones = "";
        if (decelZone.enableStart) zones += "START ";
        if (decelZone.enableEnd) zones += "END";
        engine->debug("✅ Decel config: " + String(decelZone.enabled ? "ON" : "OFF") + 
              (decelZone.enabled ? " | zones=" + zones + "| size=" + String(decelZone.zoneMM, 1) + 
               "mm | effect=" + String(decelZone.effectPercent, 0) + "%" : ""));
        
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
        
        engine->info(String("⏸️ Pause cycle VAET: ") + (enabled ? "activée" : "désactivée") +
              " | Mode: " + (isRandom ? "aléatoire" : "fixe") +
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
        
        engine->info(String("⏸️ Pause cycle OSC: ") + (enabled ? "activée" : "désactivée") +
              " | Mode: " + (isRandom ? "aléatoire" : "fixe") +
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
            sendError("⚠️ Impossible d'activer le mode Pursuit: calibration en cours");
            return true;
        }
        if (config.currentState == STATE_ERROR) {
            sendError("⚠️ Impossible d'activer le mode Pursuit: système en état erreur");
            return true;
        }
        
        if (seqState.isRunning) {
            SeqExecutor.stop();
        }
        
        currentMovement = MOVEMENT_PURSUIT;
        config.executionContext = CONTEXT_STANDALONE;
        
        if (config.currentState == STATE_RUNNING) {
            config.currentState = STATE_READY;
        }
        
        engine->debug("✅ Mode Pursuit activé");
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
        
        pursuitMove(targetPos, maxSpd);
        return true;
    }
    
    return false;
}

// ============================================================================
// HANDLER 6/8: CHAOS COMMANDS
// ============================================================================

bool CommandDispatcher::handleChaosCommands(const char* cmd, JsonDocument& doc, const String& message) {
    if (message.indexOf("\"cmd\":\"startChaos\"") > 0) {
        if (config.currentState == STATE_CALIBRATING) {
            sendError("⚠️ Impossible de démarrer le mode Chaos: calibration en cours");
            return true;
        }
        if (config.currentState == STATE_ERROR) {
            sendError("⚠️ Impossible de démarrer le mode Chaos: système en état erreur");
            return true;
        }
        
        float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
        chaos.centerPositionMM = doc["centerPositionMM"] | (effectiveMax / 2.0);
        chaos.amplitudeMM = doc["amplitudeMM"] | 50.0;
        chaos.maxSpeedLevel = doc["maxSpeedLevel"] | 10.0;
        chaos.crazinessPercent = doc["crazinessPercent"] | 50.0;
        chaos.durationSeconds = doc["durationSeconds"] | 60;
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
        
        startChaos();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"stopChaos\"") > 0) {
        stopChaos();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"setChaosConfig\"") > 0) {
        chaos.centerPositionMM = doc["centerPositionMM"] | chaos.centerPositionMM;
        chaos.amplitudeMM = doc["amplitudeMM"] | chaos.amplitudeMM;
        chaos.maxSpeedLevel = doc["maxSpeedLevel"] | chaos.maxSpeedLevel;
        chaos.crazinessPercent = doc["crazinessPercent"] | chaos.crazinessPercent;
        chaos.durationSeconds = doc["durationSeconds"] | chaos.durationSeconds;
        
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
    if (message.indexOf("\"cmd\":\"setOscillation\"") > 0) {
        float oldCenter = oscillation.centerPositionMM;
        float oldAmplitude = oscillation.amplitudeMM;
        float oldFrequency = oscillation.frequencyHz;
        OscillationWaveform oldWaveform = oscillation.waveform;
        
        oscillation.centerPositionMM = doc["centerPositionMM"] | oscillation.centerPositionMM;
        oscillation.amplitudeMM = doc["amplitudeMM"] | oscillation.amplitudeMM;
        oscillation.waveform = (OscillationWaveform)(doc["waveform"] | (int)oscillation.waveform);
        oscillation.frequencyHz = doc["frequencyHz"] | oscillation.frequencyHz;
        
        oscillation.enableRampIn = doc["enableRampIn"] | oscillation.enableRampIn;
        oscillation.rampInDurationMs = doc["rampInDurationMs"] | oscillation.rampInDurationMs;
        oscillation.enableRampOut = doc["enableRampOut"] | oscillation.enableRampOut;
        oscillation.rampOutDurationMs = doc["rampOutDurationMs"] | oscillation.rampOutDurationMs;
        
        oscillation.cycleCount = doc["cycleCount"] | oscillation.cycleCount;
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
        
        if (paramsChanged) {
            engine->debug("📝 OSC config: center=" + String(oscillation.centerPositionMM, 1) + 
                  "mm | amp=" + String(oscillation.amplitudeMM, 1) + 
                  "mm | freq=" + String(oscillation.frequencyHz, 3) + "Hz" +
                  (currentMovement == MOVEMENT_OSC && !isPaused ? " | ⚡ Live update" : ""));
            
            // Smooth center transition during active oscillation
            if (currentMovement == MOVEMENT_OSC && !isPaused && oldCenter != oscillation.centerPositionMM) {
                oscillationState.isCenterTransitioning = true;
                oscillationState.centerTransitionStartMs = millis();
                oscillationState.oldCenterMM = oldCenter;
                oscillationState.targetCenterMM = oscillation.centerPositionMM;
            }
            
            // Smooth amplitude transition
            if (currentMovement == MOVEMENT_OSC && !isPaused && oldAmplitude != oscillation.amplitudeMM) {
                oscillationState.isAmplitudeTransitioning = true;
                oscillationState.amplitudeTransitionStartMs = millis();
                oscillationState.oldAmplitudeMM = oldAmplitude;
                oscillationState.targetAmplitudeMM = oscillation.amplitudeMM;
            }
            
            // Disable ramps for immediate effect
            if (currentMovement == MOVEMENT_OSC && !isPaused) {
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
    
    if (message.indexOf("\"cmd\":\"setCyclePause\"") > 0) {
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
    
    if (message.indexOf("\"cmd\":\"startOscillation\"") > 0) {
        if (config.currentState == STATE_INIT || config.currentState == STATE_CALIBRATING) {
            sendError("⚠️ Calibration requise avant de démarrer l'oscillation");
            return true;
        }
        
        if (seqState.isRunning) {
            SeqExecutor.stop();
        }
        
        if (config.currentState == STATE_RUNNING) {
            stopMovement();
        }
        
        startOscillation();
        sendStatus();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"stopOscillation\"") > 0) {
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
    
    if (message.indexOf("\"cmd\":\"addSequenceLine\"") > 0) {
        SequenceLine newLine = SeqTable.parseFromJson(doc);
        
        String validationError = SeqTable.validatePhysics(newLine);
        if (validationError.length() > 0) {
            sendError("❌ Ligne invalide : " + validationError);
            return true;
        }
        
        String errorMsg;
        if (newLine.movementType == MOVEMENT_VAET) {
            if (!validateAndReport(Validators::speed(newLine.speedForward, errorMsg), errorMsg)) return true;
            if (!validateAndReport(Validators::speed(newLine.speedBackward, errorMsg), errorMsg)) return true;
        } else if (newLine.movementType == MOVEMENT_OSC) {
            if (newLine.oscFrequencyHz <= 0 || newLine.oscFrequencyHz > 10.0) {
                sendError("❌ Frequency doit être 0.01-10 Hz");
                return true;
            }
        } else if (newLine.movementType == MOVEMENT_CHAOS) {
            if (!validateAndReport(Validators::speed(newLine.chaosMaxSpeedLevel, errorMsg), errorMsg)) return true;
            if (newLine.chaosDurationSeconds < 1 || newLine.chaosDurationSeconds > 3600) {
                sendError("❌ Duration doit être 1-3600 secondes");
                return true;
            }
        }
        
        if (newLine.cycleCount < 1 || newLine.cycleCount > 9999) {
            sendError("❌ Cycle count doit être 1-9999 (reçu: " + String(newLine.cycleCount) + ")");
            return true;
        }
        
        SeqTable.addLine(newLine);
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"deleteSequenceLine\"") > 0) {
        int lineId = doc["lineId"] | -1;
        if (lineId < 0) {
            sendError("❌ Line ID invalide");
            return true;
        }
        SeqTable.deleteLine(lineId);
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"updateSequenceLine\"") > 0) {
        int lineId = doc["lineId"] | -1;
        SequenceLine updatedLine = SeqTable.parseFromJson(doc);
        
        String validationError = SeqTable.validatePhysics(updatedLine);
        if (validationError.length() > 0) {
            sendError("❌ Ligne invalide : " + validationError);
            return true;
        }
        
        SeqTable.updateLine(lineId, updatedLine);
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"moveSequenceLine\"") > 0) {
        int lineId = doc["lineId"] | -1;
        int direction = doc["direction"] | 0;
        SeqTable.moveLine(lineId, direction);
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"duplicateSequenceLine\"") > 0) {
        int lineId = doc["lineId"] | -1;
        SeqTable.duplicateLine(lineId);
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"toggleSequenceLine\"") > 0) {
        int lineId = doc["lineId"] | -1;
        bool enabled = doc["enabled"] | false;
        SeqTable.toggleLine(lineId, enabled);
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"clearSequence\"") > 0) {
        SeqTable.clear();
        SeqTable.broadcast();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"getSequenceTable\"") > 0) {
        SeqTable.broadcast();
        return true;
    }
    
    // ========================================================================
    // SEQUENCE EXECUTION CONTROL
    // ========================================================================
    
    if (message.indexOf("\"cmd\":\"startSequence\"") > 0) {
        SeqExecutor.start(false);
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"loopSequence\"") > 0) {
        SeqExecutor.start(true);
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"stopSequence\"") > 0) {
        SeqExecutor.stop();
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"toggleSequencePause\"") > 0) {
        SeqExecutor.togglePause();
        SeqExecutor.sendStatus();
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"skipSequenceLine\"") > 0) {
        SeqExecutor.skipToNextLine();
        SeqExecutor.sendStatus();
        return true;
    }
    
    // ========================================================================
    // IMPORT/EXPORT
    // ========================================================================
    
    if (message.indexOf("\"cmd\":\"exportSequence\"") > 0) {
        SeqTable.sendJsonResponse("exportData", SeqTable.exportToJson());
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"toggleDebug\"") > 0) {
        if (engine) {
            LogLevel current = engine->getLogLevel();
            LogLevel next = (current == LOG_DEBUG) ? LOG_INFO : LOG_DEBUG;
            engine->setLogLevel(next);
            engine->info(String("Log level set to: ") + (next == LOG_DEBUG ? "DEBUG" : "INFO"));
        }
        return true;
    }
    
    if (message.indexOf("\"cmd\":\"importSequence\"") > 0) {
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
                sendError("❌ Erreur parsing JSON: dataEnd invalide");
            }
        } else {
            sendError("❌ Erreur parsing JSON: champ jsonData introuvable");
        }
        return true;
    }
    
    // ========================================================================
    // STATS ON-DEMAND
    // ========================================================================
    
    if (message.indexOf("\"cmd\":\"requestStats\"") > 0) {
        bool enable = doc["enable"] | false;
        statsRequested = enable;
        lastStatsRequestTime = millis();
        
        engine->debug(String("📊 Stats tracking: ") + (enable ? "ENABLED" : "DISABLED"));
        
        if (enable) {
            saveCurrentSessionStats();
            sendStatus();
        }
        
        return true;
    }
    
    return false;
}
