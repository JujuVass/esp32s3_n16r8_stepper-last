/**
 * ============================================================================
 * SequenceExecutor.cpp - Sequence Execution Engine Implementation
 * ============================================================================
 * 
 * Extracted from stepper_controller_restructured.ino (~400 lines)
 * Manages sequence table execution: line advancement, timing, coordination.
 * 
 * @author Refactored from main file
 * @version 1.0
 */

#include "movement/SequenceExecutor.h"
#include "movement/SequenceTableManager.h"
#include "core/GlobalState.h"
#include "core/UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "movement/CalibrationManager.h"
#include "movement/ChaosController.h"
#include "movement/OscillationController.h"
#include "movement/BaseMovementController.h"

// ============================================================================
// SEQUENCER STATE - Owned by this module (Phase 4D migration)
// ============================================================================
SequenceExecutionState seqState;
MovementType currentMovement = MOVEMENT_VAET;  // Default: Va-et-vient

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

SequenceExecutor& SequenceExecutor::getInstance() {
    static SequenceExecutor instance;
    return instance;
}

// Global accessor
SequenceExecutor& SeqExecutor = SequenceExecutor::getInstance();

// ============================================================================
// INITIALIZATION
// ============================================================================

void SequenceExecutor::begin(WebSocketsServer* ws) {
    _webSocket = ws;
    engine->info("SequenceExecutor initialized");
}

// ============================================================================
// SEQUENCE CONTROL
// ============================================================================

void SequenceExecutor::start(bool loopMode) {
    // Check if any lines are enabled
    int enabledCount = 0;
    for (int i = 0; i < sequenceLineCount; i++) {
        if (sequenceTable[i].enabled) enabledCount++;
    }
    
    if (enabledCount == 0) {
        sendError("❌ Aucune ligne active à exécuter!");
        return;
    }
    
    if (config.currentState != STATE_READY) {
        sendError("❌ Système pas prêt (calibration requise?)");
        return;
    }
    
    // Initialize execution state
    seqState.isRunning = true;
    seqState.isLoopMode = loopMode;
    seqState.currentLineIndex = 0;
    seqState.currentCycleInLine = 0;
    seqState.isPaused = false;
    seqState.isWaitingPause = false;
    seqState.loopCount = 0;
    seqState.sequenceStartTime = millis();
    
    // Set execution context
    config.executionContext = CONTEXT_SEQUENCER;
    currentMovement = MOVEMENT_VAET;
    
    // Skip to first enabled line
    while (seqState.currentLineIndex < sequenceLineCount && 
           !sequenceTable[seqState.currentLineIndex].enabled) {
        seqState.currentLineIndex++;
    }
    
    engine->info(String("═══════════════════════════════════════════\n") +
          "▶️ SÉQUENCE DÉMARRÉE - Mode: " + (loopMode ? "BOUCLE INFINIE" : "LECTURE UNIQUE") + "\n" +
          "   isLoopMode = " + (seqState.isLoopMode ? "TRUE" : "FALSE") + "\n" +
          "   Lignes actives: " + String(enabledCount) + " / " + String(sequenceLineCount) + "\n" +
          "═══════════════════════════════════════════");
}

void SequenceExecutor::stop() {
    if (!seqState.isRunning) return;
    engine->debug("SequenceExecutor::stop() called - seqState.currentLineIndex=" + 
                  String(seqState.currentLineIndex) + " currentCycle=" + String(seqState.currentCycleInLine));
    
    seqState.isRunning = false;
    engine->debug("seqState.isRunning set to false in stop()");
    seqState.isPaused = false;
    seqState.isWaitingPause = false;
    
    // Reset execution context to standalone
    config.executionContext = CONTEXT_STANDALONE;
    
    // Stop current movement if any
    stopMovement();
    
    unsigned long elapsedSec = (millis() - seqState.sequenceStartTime) / 1000;
    
    String loopInfo = seqState.isLoopMode ? 
        String("\n   Boucles complétées: ") + String(seqState.loopCount) : "";
    
    engine->info(String("═══════════════════════════════════════════\n") +
          "⏹️ SÉQUENCE ARRÊTÉE\n" +
          "   Durée: " + String(elapsedSec) + "s" + loopInfo + "\n" +
          "═══════════════════════════════════════════");
}

void SequenceExecutor::togglePause() {
    if (!seqState.isRunning) return;
    
    seqState.isPaused = !seqState.isPaused;
    
    if (seqState.isPaused) {
        // Pause current movement (config.currentState is single source of truth)
        config.currentState = STATE_PAUSED;
        engine->info("⏸️ Séquence en pause");
    } else {
        // Resume movement
        config.currentState = STATE_RUNNING;
        engine->info("▶️ Séquence reprise");
    }
}

void SequenceExecutor::skipToNextLine() {
    if (!seqState.isRunning) return;
    
    // Force current line to complete
    seqState.currentCycleInLine = sequenceTable[seqState.currentLineIndex].cycleCount;
    stopMovement();
    
    engine->info("⏭️ Ligne suivante...");
}

// ============================================================================
// STATUS BROADCASTING
// ============================================================================

void SequenceExecutor::sendStatus() {
    // Only broadcast if clients are connected (early exit)
    if (_webSocket == nullptr || _webSocket->connectedClients() == 0) return;
    
    JsonDocument doc;
    
    doc["type"] = "sequenceStatus";
    doc["isRunning"] = seqState.isRunning;
    doc["isLoopMode"] = seqState.isLoopMode;
    doc["isPaused"] = seqState.isPaused;
    doc["currentLineIndex"] = seqState.currentLineIndex;
    doc["currentLineNumber"] = seqState.currentLineIndex + 1;
    doc["totalLines"] = sequenceLineCount;
    
    // Display real cycle info: for OSC, show internal oscillation cycles
    int displayCycle = seqState.currentCycleInLine + 1;  // +1 because cycles are 1-indexed for display
    if (seqState.isRunning && seqState.currentLineIndex < sequenceLineCount) {
        SequenceLine* line = &sequenceTable[seqState.currentLineIndex];
        if (line->movementType == MOVEMENT_OSC) {
            displayCycle = oscillationState.completedCycles + 1;  // Show oscillation's internal cycle count (1-indexed)
        } else if (line->movementType == MOVEMENT_CHAOS) {
            displayCycle = 1;  // Chaos is always 1 cycle (duration-based)
        }
    }
    doc["currentCycle"] = displayCycle;
    doc["loopCount"] = seqState.loopCount;
    
    // Pause remaining time
    if (seqState.isWaitingPause) {
        long remaining = seqState.pauseEndTime - millis();
        doc["pauseRemaining"] = remaining > 0 ? remaining : 0;
    } else {
        doc["pauseRemaining"] = 0;
    }
    
    String output;
    serializeJson(doc, output);
    _webSocket->broadcastTXT(output);
}

// ============================================================================
// COMPLETION HANDLER
// ============================================================================

void SequenceExecutor::onMovementComplete() {
    if (config.executionContext == CONTEXT_SEQUENCER) {
        // Running from sequencer: increment cycle counter
        seqState.currentCycleInLine++;
        config.currentState = STATE_READY;  // Signal sequencer that cycle is complete
        
        engine->info("✅ Cycle terminé - retour séquenceur");
        sendStatus();
    } else {
        // Standalone mode: movement is complete, return to ready state
        config.currentState = STATE_READY;
        
        // Auto-increment daily statistics with distance traveled
        if (motion.targetDistanceMM > 0) {
            engine->incrementDailyStats(motion.targetDistanceMM);
        }
        
        engine->info("✅ Mouvement terminé (STANDALONE)");
    }
}

// ============================================================================
// POSITIONING HELPER
// ============================================================================

void SequenceExecutor::positionForNextLine() {
    if (!seqState.isRunning) return;
    if (seqState.currentLineIndex >= sequenceLineCount) return;
    
    SequenceLine* currentLine = &sequenceTable[seqState.currentLineIndex];
    
    float targetPositionMM = 0;
    bool needsPositioning = true;
    
    // Determine target position based on current line's movement type
    switch (currentLine->movementType) {
        case MOVEMENT_VAET:
            targetPositionMM = currentLine->startPositionMM;
            break;
            
        case MOVEMENT_OSC:
            // Start at center position - oscillation always starts from center
            targetPositionMM = currentLine->oscCenterPositionMM;
            break;
            
        case MOVEMENT_CHAOS:
            // Start at center of chaos zone
            targetPositionMM = currentLine->chaosCenterPositionMM;
            break;
            
        case MOVEMENT_CALIBRATION:
            // Calibration will handle positioning itself
            needsPositioning = false;
            break;
            
        default:
            needsPositioning = false;
            break;
    }
    
    if (!needsPositioning) return;
    
    // Validate target position against effective limits
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    if (targetPositionMM < 0) {
        engine->warn("⚠️ Position cible négative (" + String(targetPositionMM, 1) + "mm) - ajustée à 0mm");
        targetPositionMM = 0;
    }
    if (targetPositionMM > maxAllowed) {
        engine->warn("⚠️ Position cible (" + String(targetPositionMM, 1) + "mm) dépasse limite (" + String(maxAllowed, 1) + "mm) - ajustée");
        targetPositionMM = maxAllowed;
    }
    
    // Calculate current position
    float currentPosMM = currentStep / (float)STEPS_PER_MM;
    
    // Only move if we're not already at target (tolerance: 1mm)
    if (abs(currentPosMM - targetPositionMM) > 1.0) {
        engine->info("🎯 Repositionnement: " + String(currentPosMM, 1) + "mm → " + String(targetPositionMM, 1) + "mm");
        
        // CRITICAL: Stop previous movement completely before repositioning
        chaosState.isRunning = false;
        oscillationState.isRampingIn = false;
        oscillationState.isRampingOut = false;
        currentMovement = MOVEMENT_VAET;  // Force back to VAET
        
        long targetStepPos = (long)(targetPositionMM * STEPS_PER_MM);
        
        // Set direction for repositioning
        bool moveForward = (targetStepPos > currentStep);
        Motor.setDirection(moveForward);
        
        // CRITICAL: Keep motor ENABLED during repositioning - HSS loses position if disabled!
        // Blocking move to target position at moderate speed (990 µs = speed 5.0)
        unsigned long positioningStart = millis();
        unsigned long lastStepTime = micros();
        const unsigned long stepDelay = 990;  // Same speed as normal VAET (5.0)
        
        while (currentStep != targetStepPos && (millis() - positioningStart < 30000)) {  // 30s timeout
            unsigned long now = micros();
            if (now - lastStepTime >= stepDelay) {
                // Simple step without calling doStep() to avoid triggering cycle detection
                if (moveForward) {
                    Motor.step();
                    currentStep++;
                } else {
                    Motor.step();
                    currentStep--;
                }
                lastStepTime = now;
            }
            yield();  // Allow ESP32 to handle other tasks (WebSocket, etc.)
            if (_webSocket) _webSocket->loop();  // Keep WebSocket alive
        }
        
        // Repositioning complete - restore state
        config.currentState = STATE_READY;
        
        if (currentStep != targetStepPos) {
            engine->warn("⚠️ Timeout repositionnement - position: " + String(currentStep / (float)STEPS_PER_MM, 1) + "mm");
        } else {
            engine->info("✅ Repositionnement terminé");
        }
    }
}

// ============================================================================
// SEQUENCE END HANDLER
// ============================================================================

bool SequenceExecutor::checkAndHandleSequenceEnd() {
    // Move to next line and reset cycle counter
    seqState.currentLineIndex++;
    seqState.currentCycleInLine = 0;
    
    engine->debug("🔍 checkAndHandleSequenceEnd: lineIndex=" + String(seqState.currentLineIndex) + 
                  " / lineCount=" + String(sequenceLineCount) + 
                  " | isLoopMode=" + String(seqState.isLoopMode));
    
    // Check if sequence is complete
    if (seqState.currentLineIndex >= sequenceLineCount) {
        
        // Loop mode: restart sequence
        if (seqState.isLoopMode) {
            seqState.currentLineIndex = 0;
            seqState.loopCount++;
            
            engine->info("───────────────────────────────────────────");
            engine->info("🔁 Boucle #" + String(seqState.loopCount) + " terminée - Redémarrage...");
            engine->info("───────────────────────────────────────────");
        }
        // Single read mode: stop
        else {
            unsigned long elapsedSec = (millis() - seqState.sequenceStartTime) / 1000;
            
            engine->info("═══════════════════════════════════════════");
            engine->info("✅ SÉQUENCE TERMINÉE (LECTURE UNIQUE)!");
            engine->info("   Lignes exécutées: " + String(sequenceLineCount));
            engine->info("   Durée totale: " + String(elapsedSec) + "s");
            engine->info("   Mode: " + String(seqState.isLoopMode ? "BOUCLE" : "UNIQUE"));
            engine->info("═══════════════════════════════════════════");
            
            // NEW ARCHITECTURE: Auto-return to 0.0mm and full cleanup
            seqState.isRunning = false;
            config.executionContext = CONTEXT_STANDALONE;
            
            // Return to START contact (position 0.0mm) if not already there
            if (currentStep != 0) {
                engine->info("🏠 Retour automatique au contact START...");
                long startReturnStep = currentStep;
                
                Motor.setDirection(false);  // Backward to START
                unsigned long returnStart = millis();
                unsigned long lastStepTime = micros();
                const unsigned long stepDelay = 990;  // Speed 5.0
                
                while (currentStep > 0 && (millis() - returnStart < 30000)) {
                    unsigned long now = micros();
                    if (now - lastStepTime >= stepDelay) {
                        Motor.step();
                        currentStep--;
                        lastStepTime = now;
                    }
                    yield();  // Allow ESP32 to handle other tasks
                    if (_webSocket) _webSocket->loop();  // Keep WebSocket alive
                }
                
                float returnedMM = (startReturnStep - currentStep) / STEPS_PER_MM;
                engine->info("✓ Retour terminé: " + String(returnedMM, 1) + "mm → Position 0.0mm");
            }
            
            // Full cleanup of variables
            currentStep = 0;
            startStep = 0;
            targetStep = 0;
            movingForward = true;
            hasReachedStartStep = false;
            config.currentState = STATE_READY;
            
            engine->info("✓ Système prêt pour le prochain cycle");
            sendStatus();           // Send sequenceStatus (isRunning=false)
            ::sendStatus();         // Send global status (canStart=true) to re-enable buttons
            return false;  // Sequence ended
        }
    }
    
    // Skip disabled lines
    while (seqState.currentLineIndex < sequenceLineCount && 
           !sequenceTable[seqState.currentLineIndex].enabled) {
        engine->debug("⏭️ Skipping disabled line " + String(seqState.currentLineIndex));
        seqState.currentLineIndex++;
    }
    
    engine->debug("🔍 After skip: lineIndex=" + String(seqState.currentLineIndex) + 
                  " / lineCount=" + String(sequenceLineCount));
    
    // Check again if we've reached the end after skipping
    if (seqState.currentLineIndex >= sequenceLineCount) {
        engine->info("🎯 End condition met after skip - triggering sequence end");
        if (seqState.isLoopMode) {
            seqState.currentLineIndex = 0;
            seqState.loopCount++;
            engine->info("🔁 Boucle #" + String(seqState.loopCount) + " - Redémarrage...");
        } else {
            seqState.isRunning = false;
            engine->info("📡 Setting isRunning=false, sending status updates...");
            
            // CRITICAL: Stop motor and reset execution context
            stopMovement();
            config.executionContext = CONTEXT_STANDALONE;
            config.currentState = STATE_READY;
            
            engine->info("📡 Sending sequenceStatus (isRunning=false)...");
            sendStatus();           // Send sequenceStatus (isRunning=false)
            engine->info("📡 Sending global status (canStart=true)...");
            ::sendStatus();         // Send global status (canStart=true) to re-enable buttons
            engine->info("✅ Both status messages sent!");
            return false;  // Sequence ended
        }
    }
    
    return true;  // Sequence continues
}

// ============================================================================
// LINE STARTERS
// ============================================================================

void SequenceExecutor::startVaEtVientLine(SequenceLine* line) {
    // Apply line parameters to motion configuration
    motion.startPositionMM = line->startPositionMM;
    motion.targetDistanceMM = line->distanceMM;
    
    // Apply speed levels directly (0.0-MAX_SPEED_LEVEL scale)
    motion.speedLevelForward = line->speedForward;
    motion.speedLevelBackward = line->speedBackward;
    
    // Clamp to valid range [1, MAX_SPEED_LEVEL]
    if (motion.speedLevelForward < 1.0) motion.speedLevelForward = 1.0;
    if (motion.speedLevelForward > MAX_SPEED_LEVEL) motion.speedLevelForward = MAX_SPEED_LEVEL;
    if (motion.speedLevelBackward < 1.0) motion.speedLevelBackward = 1.0;
    if (motion.speedLevelBackward > MAX_SPEED_LEVEL) motion.speedLevelBackward = MAX_SPEED_LEVEL;
    
    // Apply deceleration configuration
    decelZone.enabled = line->decelStartEnabled || line->decelEndEnabled;
    decelZone.enableStart = line->decelStartEnabled;
    decelZone.enableEnd = line->decelEndEnabled;
    decelZone.zoneMM = line->decelZoneMM;
    decelZone.effectPercent = line->decelEffectPercent;
    decelZone.mode = line->decelMode;
    
    // Apply cycle pause configuration from sequence line
    motion.cyclePause.enabled = line->vaetCyclePauseEnabled;
    motion.cyclePause.isRandom = line->vaetCyclePauseIsRandom;
    motion.cyclePause.pauseDurationSec = line->vaetCyclePauseDurationSec;
    motion.cyclePause.minPauseSec = line->vaetCyclePauseMinSec;
    motion.cyclePause.maxPauseSec = line->vaetCyclePauseMaxSec;
    
    // Validate configuration
    BaseMovement.validateDecelZone();  // Integrated into BaseMovementController
    
    // Calculate step delays and start movement
    BaseMovement.calculateStepDelay();
    
    Motor.enable();
    lastStepMicros = micros();
    
    // Calculate step positions
    startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
    targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
    
    // Set running state (config.currentState is single source of truth)
    config.currentState = STATE_RUNNING;
    
    // Set movement type (config.executionContext already == CONTEXT_SEQUENCER)
    currentMovement = MOVEMENT_VAET;
    
    // Determine starting direction
    if (currentStep <= startStep) {
        movingForward = true;
    } else if (currentStep >= targetStep) {
        movingForward = false;
    } else {
        movingForward = true;
    }
    
    Motor.setDirection(movingForward);
    
    stats.syncPosition(currentStep);
    lastStartContactMillis = 0;
    cycleTimeMillis = 0;
    measuredCyclesPerMinute = 0;
    wasAtStart = false;
    hasReachedStartStep = (currentStep >= startStep);
    
    seqState.lineStartTime = millis();
    
    engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 🔄 VA-ET-VIENT | Cycle " + String(seqState.currentCycleInLine + 1) + "/" + String(line->cycleCount) + 
          " | " + String(line->startPositionMM, 1) + "mm → " + 
          String(line->startPositionMM + line->distanceMM, 1) + "mm | Speed: " + 
          String(motion.speedLevelForward, 1) + "/" + String(motion.speedLevelBackward, 1));
    
    sendStatus();
}

void SequenceExecutor::startOscillationLine(SequenceLine* line) {
    // Copy oscillation parameters from sequence line to oscillation config
    oscillation.centerPositionMM = line->oscCenterPositionMM;
    oscillation.amplitudeMM = line->oscAmplitudeMM;
    oscillation.waveform = line->oscWaveform;
    oscillation.frequencyHz = line->oscFrequencyHz;
    oscillation.enableRampIn = line->oscEnableRampIn;
    oscillation.enableRampOut = line->oscEnableRampOut;
    oscillation.rampInDurationMs = line->oscRampInDurationMs;
    oscillation.rampOutDurationMs = line->oscRampOutDurationMs;
    oscillation.cycleCount = line->cycleCount;  // Oscillation will execute THIS many cycles
    oscillation.returnToCenter = false;  // Don't return to center in sequencer (continue to next line)
    
    // Apply cycle pause configuration from sequence line
    oscillation.cyclePause.enabled = line->oscCyclePauseEnabled;
    oscillation.cyclePause.isRandom = line->oscCyclePauseIsRandom;
    oscillation.cyclePause.pauseDurationSec = line->oscCyclePauseDurationSec;
    oscillation.cyclePause.minPauseSec = line->oscCyclePauseMinSec;
    oscillation.cyclePause.maxPauseSec = line->oscCyclePauseMaxSec;
    
    seqState.lineStartTime = millis();
    
    // Start oscillation (will set currentMovement = MOVEMENT_OSC)
    Osc.start();
    
    // Skip initial positioning - positionForNextLine() already moved us to start position
    oscillationState.isInitialPositioning = false;
    
    // Skip ramp-in too - we're already positioned, no need to ramp amplitude from 0
    oscillationState.isRampingIn = false;
    
    // Calculate where we are in the wave cycle based on current position
    float currentPosMM = currentStep / (float)STEPS_PER_MM;
    float relativePos = (currentPosMM - oscillation.centerPositionMM) / oscillation.amplitudeMM;
    
    // Clamp to [-1, 1] range (in case positioning wasn't perfect)
    if (relativePos < -1.0) relativePos = -1.0;
    if (relativePos > 1.0) relativePos = 1.0;
    
    // Calculate initial phase based on waveform type
    float initialPhase = 0.0;
    if (line->oscWaveform == OSC_SINE) {
        // For sine: waveValue = -cos(phase * 2π)
        initialPhase = acos(-relativePos) / (2.0 * PI);
    } else if (line->oscWaveform == OSC_TRIANGLE || line->oscWaveform == OSC_SQUARE) {
        // For triangle/square, approximate phase from position
        initialPhase = (relativePos + 1.0) / 4.0;  // Maps [-1,+1] to [0, 0.5]
    }
    
    oscillationState.accumulatedPhase = initialPhase;
    oscillationState.lastPhaseUpdateMs = millis();
    
    engine->debug("📍 Oscillation démarre depuis position actuelle: " + String(currentPosMM, 1) + 
          "mm (phase initiale: " + String(initialPhase, 3) + ", relativePos: " + String(relativePos, 2) + ")");
    
    String waveformName = "SINE";
    if (line->oscWaveform == OSC_TRIANGLE) waveformName = "TRIANGLE";
    if (line->oscWaveform == OSC_SQUARE) waveformName = "SQUARE";
    
    engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 〰️ OSCILLATION (" + String(line->cycleCount) + " cycles internes)" +
          " | Centre: " + String(line->oscCenterPositionMM, 1) + "mm | Amp: ±" + 
          String(line->oscAmplitudeMM, 1) + "mm | " + waveformName + " @ " + 
          String(line->oscFrequencyHz, 2) + " Hz");
    
    sendStatus();
}

void SequenceExecutor::startChaosLine(SequenceLine* line) {
    // Copy chaos parameters from sequence line to chaos config
    chaos.centerPositionMM = line->chaosCenterPositionMM;
    chaos.amplitudeMM = line->chaosAmplitudeMM;
    chaos.maxSpeedLevel = line->chaosMaxSpeedLevel;
    chaos.crazinessPercent = line->chaosCrazinessPercent;
    chaos.durationSeconds = line->chaosDurationSeconds;
    chaos.seed = line->chaosSeed;
    
    // Copy patterns enabled array (11 patterns)
    for (int i = 0; i < 11; i++) {
        chaos.patternsEnabled[i] = line->chaosPatternsEnabled[i];
    }
    
    seqState.lineStartTime = millis();
    
    // Start chaos mode (delegated to ChaosController module)
    Chaos.start();
    
    engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 🌀 CHAOS | Cycle " + String(seqState.currentCycleInLine + 1) + "/" + String(line->cycleCount) + 
          " | Durée: " + String(line->chaosDurationSeconds) + "s | Centre: " + 
          String(line->chaosCenterPositionMM, 1) + "mm ±" + 
          String(line->chaosAmplitudeMM, 1) + "mm | Speed: " + 
          String(line->chaosMaxSpeedLevel, 1) + " | Madness: " + 
          String(line->chaosCrazinessPercent, 0) + "%");
    
    sendStatus();
}

void SequenceExecutor::startCalibrationLine(SequenceLine* line) {
    engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 📏 CALIBRATION | Lancement calibration complète...");
    
    seqState.lineStartTime = millis();
    
    // Start full calibration
    Calibration.startCalibration();
    
    // Note: onMovementComplete() will be called when calibration finishes
    sendStatus();
}

// ============================================================================
// MAIN PROCESSING LOOP
// ============================================================================

void SequenceExecutor::process() {
    if (!seqState.isRunning || seqState.isPaused) {
        return;
    }
    
    // Handle pause between lines (temporization)
    if (seqState.isWaitingPause) {
        if (millis() >= seqState.pauseEndTime) {
            seqState.isWaitingPause = false;
            
            // Use helper to advance to next line (DRY)
            if (!checkAndHandleSequenceEnd()) {
                return;  // Sequence ended
            }
            
            // Continue to start the next line's first cycle
        } else {
            // Still waiting, send status update every 500ms
            static unsigned long lastStatusSend = 0;
            if (millis() - lastStatusSend > SEQUENCE_STATUS_UPDATE_MS) {
                sendStatus();
                lastStatusSend = millis();
            }
            return;
        }
    }
    
    // Check if current movement is complete and we can start next action
    if (config.currentState == STATE_READY && !seqState.isWaitingPause) {
        
        SequenceLine* currentLine = &sequenceTable[seqState.currentLineIndex];
        
        // Determine effective cycle count for sequencer
        // OSC and CHAOS manage their own internal cycles, so sequencer sees them as 1 cycle
        int effectiveCycleCount = currentLine->cycleCount;
        if (currentLine->movementType == MOVEMENT_OSC || currentLine->movementType == MOVEMENT_CHAOS) {
            effectiveCycleCount = 1;  // They handle cycles internally
        }
        
        // Check if all cycles for current line are complete
        if (seqState.currentCycleInLine >= effectiveCycleCount) {
            // Line complete - all cycles executed
            engine->debug("🏁 Line complete: cycle " + String(seqState.currentCycleInLine) + 
                          " >= " + String(effectiveCycleCount) + " (effectiveCycles)");
            
            // Apply pause after line if configured
            if (currentLine->pauseAfterMs > 0) {
                seqState.isWaitingPause = true;
                seqState.pauseEndTime = millis() + currentLine->pauseAfterMs;
                
                engine->info("⏸️ Pause ligne: " + String(currentLine->pauseAfterMs / 1000.0, 1) + "s");
                
                sendStatus();
                return;
            }
            
            // Move to next line
            if (!checkAndHandleSequenceEnd()) {
                return;  // Sequence ended
            }
            
            // Reload current line after moving to next
            currentLine = &sequenceTable[seqState.currentLineIndex];
        }
        // Execute next cycle for current line
        else {
            // Load current line configuration
            currentLine = &sequenceTable[seqState.currentLineIndex];
            
            // POSITIONING: Move to start position on first cycle of each line
            if (seqState.currentCycleInLine == 0) {
                positionForNextLine();
            }
            
            // Start appropriate movement type
            switch (currentLine->movementType) {
                case MOVEMENT_VAET:
                    startVaEtVientLine(currentLine);
                    break;
                    
                case MOVEMENT_OSC:
                    startOscillationLine(currentLine);
                    break;
                    
                case MOVEMENT_CHAOS:
                    startChaosLine(currentLine);
                    break;
                    
                case MOVEMENT_CALIBRATION:
                    startCalibrationLine(currentLine);
                    break;
                    
                default:
                    engine->warn("⚠️ Type de mouvement inconnu: " + String(currentLine->movementType));
                    seqState.currentCycleInLine++;  // Skip this line
                    break;
            }
        }
    }
}
