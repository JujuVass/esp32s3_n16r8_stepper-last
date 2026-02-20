/**
 * ============================================================================
 * SequenceExecutor.cpp - Sequence Execution Engine Implementation
 * ============================================================================
 * 
 * Manages sequence table execution: line advancement, timing, coordination.
 */

#include "movement/SequenceExecutor.h"
#include "movement/SequenceTableManager.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include "core/GlobalState.h"
#include "core/UtilityEngine.h"
#include "core/Validators.h"
#include "hardware/MotorDriver.h"
#include "movement/CalibrationManager.h"
#include "movement/ChaosController.h"
#include "movement/OscillationController.h"
#include "movement/BaseMovementController.h"

using enum MovementType;
using enum SystemState;
using enum ExecutionContext;
using enum OscillationWaveform;

// ============================================================================
// SEQUENCER STATE - Owned by this module
// ============================================================================
constinit SequenceExecutionState seqState;
volatile MovementType currentMovement = MOVEMENT_VAET;  // Default: Va-et-vient

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
        Status.sendError("❌ No active lines to execute!");
        return;
    }
    
    if (config.currentState != STATE_READY) {
        Status.sendError("❌ System not ready (calibration required?)");
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
          "▶️ SEQUENCE STARTED - Mode: " + (loopMode ? "INFINITE LOOP" : "SINGLE PLAY") + "\n" +
          "   isLoopMode = " + (seqState.isLoopMode ? "TRUE" : "FALSE") + "\n" +
          "   Active lines: " + String(enabledCount) + " / " + String(sequenceLineCount) + "\n" +
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
        String("\n   Loops completed: ") + String(seqState.loopCount) : "";
    
    engine->info(String("═══════════════════════════════════════════\n") +
          "⏹️ SEQUENCE STOPPED\n" +
          "   Duration: " + String(elapsedSec) + "s" + loopInfo + "\n" +
          "═══════════════════════════════════════════");
}

void SequenceExecutor::togglePause() {
    if (!seqState.isRunning) return;
    
    seqState.isPaused = !seqState.isPaused;
    
    if (seqState.isPaused) {
        // Pause current movement (config.currentState is single source of truth)
        config.currentState = STATE_PAUSED;
        engine->info("⏸️ Sequence paused");
    } else {
        // Resume movement
        config.currentState = STATE_RUNNING;
        engine->info("▶️ Sequence resumed");
    }
}

void SequenceExecutor::skipToNextLine() {
    if (!seqState.isRunning) return;
    
    // Force current line to complete
    seqState.currentCycleInLine = sequenceTable[seqState.currentLineIndex].cycleCount;
    stopMovement();
    
    engine->info("⏭️ Next line...");
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
        
        engine->info("✅ Cycle complete - returning to sequencer");
        sendStatus();
    } else {
        // Standalone mode: movement is complete, return to ready state
        config.currentState = STATE_READY;
        
        // Note: Daily stats are saved via saveCurrentSessionStats() called in stop()
        // No need for incrementDailyStats() here - it would cause double-counting
        
        engine->info("✅ Movement complete (STANDALONE)");
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
    float maxAllowed = Validators::getMaxAllowedMM();
    if (targetPositionMM < 0) {
        engine->warn("⚠️ Target position negative (" + String(targetPositionMM, 1) + "mm) - adjusted to 0mm");
        targetPositionMM = 0;
    }
    if (targetPositionMM > maxAllowed) {
        engine->warn("⚠️ Target position (" + String(targetPositionMM, 1) + "mm) exceeds limit (" + String(maxAllowed, 1) + "mm) - adjusted");
        targetPositionMM = maxAllowed;
    }
    
    // Calculate current position
    float currentPosMM = currentStep / (float)STEPS_PER_MM;
    
    // Only move if we're not already at target (tolerance: 1mm)
    if (abs(currentPosMM - targetPositionMM) > 1.0) {
    engine->info("🎯 Repositioning: " + String(currentPosMM, 1) + "mm → " + String(targetPositionMM, 1) + "mm");
        
        // CRITICAL: Stop previous movement completely before repositioning
        {
            MutexGuard guard(motionMutex);
            chaosState.isRunning = false;
            oscillationState.isRampingIn = false;
            oscillationState.isRampingOut = false;
            currentMovement = MOVEMENT_VAET;  // Force back to VAET
        }
        
        long targetStepPos = (long)(targetPositionMM * STEPS_PER_MM);
        
        // Blocking move to target position (D4: uses shared helper)
        if (!blockingMoveToStep(targetStepPos)) {
            engine->warn("⚠️ Repositioning timeout - position: " + String(currentStep / (float)STEPS_PER_MM, 1) + "mm");
        } else {
            engine->info("✅ Repositioning complete");
        }
    }
}

// ============================================================================
// BLOCKING MOVE HELPER (D4: shared by positionForNextLine + completeSequence)
// ============================================================================

bool SequenceExecutor::blockingMoveToStep(long targetStepPos, unsigned long timeoutMs) {
    if (currentStep == targetStepPos) return true;
    
    bool moveForward = (targetStepPos > currentStep);
    Motor.setDirection(moveForward);
    
    unsigned long moveStart = millis();
    unsigned long lastStepTime = micros();
    unsigned long lastWsService = millis();
    unsigned long lastStatusUpdate = millis();
    const unsigned long stepDelay = POSITIONING_STEP_DELAY_MICROS;
    
    // Cooperative flag: networkTask will skip webSocket/server during blocking move
    blockingMoveInProgress = true;
    
    while (currentStep != targetStepPos && (millis() - moveStart < timeoutMs)) {
        unsigned long now = micros();
        if (now - lastStepTime >= stepDelay) {
            Motor.step();
            if (moveForward) {
                currentStep = currentStep + 1;
            } else {
                currentStep = currentStep - 1;
            }
            lastStepTime = now;
        }
        yield();
        
        unsigned long nowMs = millis();
        if (nowMs - lastWsService >= BLOCKING_MOVE_WS_SERVICE_MS) {
            if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (_webSocket) _webSocket->loop();
                server.handleClient();
                xSemaphoreGive(wsMutex);
            }
            lastWsService = nowMs;
        }
        if (nowMs - lastStatusUpdate >= BLOCKING_MOVE_STATUS_INTERVAL_MS) {
            if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                Status.send();
                xSemaphoreGive(wsMutex);
            }
            lastStatusUpdate = nowMs;
        }
    }
    
    blockingMoveInProgress = false;  // Resume normal networkTask operation
    
    return (currentStep == targetStepPos);
}

// ============================================================================
// SEQUENCE COMPLETION (B4: unified end-of-sequence logic)
// ============================================================================

void SequenceExecutor::completeSequence(bool autoReturnToStart) {
    unsigned long elapsedSec = (millis() - seqState.sequenceStartTime) / 1000;
    
    engine->info("═══════════════════════════════════════════");
    engine->info("✅ SEQUENCE COMPLETE (SINGLE PLAY)!");
    engine->info("   Lines executed: " + String(sequenceLineCount));
    engine->info("   Total duration: " + String(elapsedSec) + "s");
    engine->info("═══════════════════════════════════════════");
    
    seqState.isRunning = false;
    config.executionContext = CONTEXT_STANDALONE;
    
    // Stop current movement
    stopMovement();
    
    // Auto-return to START (position 0.0mm) if requested and not already there
    if (autoReturnToStart && currentStep != 0) {
        engine->info("🏠 Auto-return to START contact...");
        float startPosMM = currentStep / (float)STEPS_PER_MM;
        
        if (blockingMoveToStep(0)) {
            engine->info("✓ Return complete: " + String(startPosMM, 1) + "mm → Position 0.0mm");
        } else {
            engine->warn("⚠️ Return timeout at " + String(currentStep / (float)STEPS_PER_MM, 1) + "mm - position NOT reset");
        }
    }
    
    // Full cleanup - only reset position if actually at physical zero
    if (currentStep == 0) {
        startStep = 0;
        targetStep = 0;
    }
    movingForward = true;
    hasReachedStartStep = false;
    config.currentState = STATE_READY;
    
    engine->info("✓ System ready for next cycle");
    sendStatus();
    ::sendStatus();
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
            engine->info("🔁 Loop #" + String(seqState.loopCount) + " complete - Restarting...");
            engine->info("───────────────────────────────────────────");
        }
        // Single play mode: stop with auto-return (B4: unified path)
        else {
            completeSequence(true);  // Auto-return to start
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
    
    // Check again if we've reached the end after skipping (B4: same unified path)
    if (seqState.currentLineIndex >= sequenceLineCount) {
        engine->info("🎯 End condition met after skip - triggering sequence end");
        if (seqState.isLoopMode) {
            seqState.currentLineIndex = 0;
            seqState.loopCount++;
            engine->info("🔁 Loop #" + String(seqState.loopCount) + " - Restarting...");
        } else {
            completeSequence(true);  // Auto-return to start (was missing before!)
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
    
    // Copy zone effect configuration from sequence line (DRY: embedded ZoneEffectConfig)
    // Runtime state is automatically clean via separate ZoneEffectState
    zoneEffect = line->vaetZoneEffect;
    zoneEffectState = ZoneEffectState();  // Reset all runtime state
    
    // Apply cycle pause configuration from sequence line (DRY: direct struct copy)
    motion.cyclePause = line->vaetCyclePause;
    
    // Validate configuration
    BaseMovement.validateZoneEffect();
    
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
    
    engine->info(String("▶️ Line ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
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
    
    // Apply cycle pause configuration from sequence line (DRY: direct struct copy)
    oscillation.cyclePause = line->oscCyclePause;
    
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
    
    engine->debug("📍 Oscillation starts from current position: " + String(currentPosMM, 1) + 
          "mm (initial phase: " + String(initialPhase, 3) + ", relativePos: " + String(relativePos, 2) + ")");
    
    String waveformName = "SINE";
    if (line->oscWaveform == OSC_TRIANGLE) waveformName = "TRIANGLE";
    if (line->oscWaveform == OSC_SQUARE) waveformName = "SQUARE";
    
    engine->info(String("▶️ Line ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 〰️ OSCILLATION (" + String(line->cycleCount) + " internal cycles)" +
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
    
    // Copy patterns enabled array
    for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
        chaos.patternsEnabled[i] = line->chaosPatternsEnabled[i];
    }
    
    seqState.lineStartTime = millis();
    
    // Start chaos mode (delegated to ChaosController module)
    Chaos.start();
    
    engine->info(String("▶️ Line ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 🌀 CHAOS | Cycle " + String(seqState.currentCycleInLine + 1) + "/" + String(line->cycleCount) + 
          " | Duration: " + String(line->chaosDurationSeconds) + "s | Centre: " + 
          String(line->chaosCenterPositionMM, 1) + "mm ±" + 
          String(line->chaosAmplitudeMM, 1) + "mm | Speed: " + 
          String(line->chaosMaxSpeedLevel, 1) + " | Madness: " + 
          String(line->chaosCrazinessPercent, 0) + "%");
    
    sendStatus();
}

void SequenceExecutor::startCalibrationLine(SequenceLine* line) {
    engine->info(String("▶️ Line ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
          " | 📏 CALIBRATION | Starting full calibration...");
    
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
            if (millis() - _lastPauseStatusSend > SEQUENCE_STATUS_UPDATE_MS) {
                sendStatus();
                _lastPauseStatusSend = millis();
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
                
                engine->info("⏸️ Line pause: " + String(currentLine->pauseAfterMs / 1000.0, 1) + "s");
                
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
                    engine->warn("⚠️ Unknown movement type: " + String(static_cast<int>(currentLine->movementType)));
                    seqState.currentCycleInLine++;  // Skip this line
                    break;
            }
        }
    }
}
