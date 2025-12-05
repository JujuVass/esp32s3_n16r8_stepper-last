// ============================================================================
// VA-ET-VIENT CONTROLLER - Implementation
// ============================================================================

#include "movement/VaEtVientController.h"
#include "GlobalState.h"
#include "UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"
#include "movement/PursuitController.h"
#include "sequencer/SequenceExecutor.h"
#include "controllers/CalibrationManager.h"

// External functions from main
extern bool returnToStartContact();
extern const char* movementTypeName(int type);

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

VaEtVientControllerClass VaEtVient;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

VaEtVientControllerClass::VaEtVientControllerClass() :
    lastStartContactMillis(0),
    cycleTimeMillis(0),
    measuredCyclesPerMinute(0),
    wasAtStart(false)
{
    // motion, pendingMotion, pauseState use default constructors from Types.h
}

// ============================================================================
// PARAMETER UPDATE METHODS
// ============================================================================

void VaEtVientControllerClass::setDistance(float distMM) {
    // Limit distance to valid range
    if (motion.startPositionMM + distMM > config.totalDistanceMM) {
        distMM = config.totalDistanceMM - motion.startPositionMM;
    }
    
    if (config.currentState == STATE_RUNNING && !isPaused) {
        // Queue change for end of cycle
        if (!pendingMotion.hasChanges) {
            initPendingFromCurrent();
        }
        pendingMotion.distanceMM = distMM;
        pendingMotion.hasChanges = true;
    } else {
        // Apply immediately
        motion.targetDistanceMM = distMM;
        startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
        targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
        calculateStepDelay();
    }
}

void VaEtVientControllerClass::setStartPosition(float startMM) {
    if (startMM < 0) startMM = 0;
    if (startMM > config.totalDistanceMM) {
        startMM = config.totalDistanceMM;
        engine->warn(String("⚠️ Start position limited to ") + String(startMM, 1) + " mm (maximum)");
    }
    
    // Validate start position + distance don't exceed maximum
    bool distanceWasAdjusted = false;
    if (startMM + motion.targetDistanceMM > config.totalDistanceMM) {
        motion.targetDistanceMM = config.totalDistanceMM - startMM;
        distanceWasAdjusted = true;
        engine->warn(String("⚠️ Distance auto-adjusted to ") + String(motion.targetDistanceMM, 1) + " mm to fit within maximum");
    }
    
    bool wasRunning = (config.currentState == STATE_RUNNING && !isPaused);
    
    if (wasRunning) {
        // Queue change for end of cycle
        if (!pendingMotion.hasChanges) {
            initPendingFromCurrent();
        }
        pendingMotion.startPositionMM = startMM;
        pendingMotion.distanceMM = motion.targetDistanceMM;
        pendingMotion.hasChanges = true;
        
        engine->debug(String("⏳ Start position queued: ") + String(startMM) + " mm (will apply at end of cycle)");
    } else {
        // Apply immediately
        motion.startPositionMM = startMM;
        startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
        targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
        calculateStepDelay();
        
        engine->debug(String("✓ Start position updated: ") + String(motion.startPositionMM) + " mm");
        
        // If distance was auto-adjusted, send immediate status update to sync UI
        if (distanceWasAdjusted) {
            sendStatus();
        }
    }
}

void VaEtVientControllerClass::setSpeedForward(float speedLevel) {
    float oldSpeedLevel = motion.speedLevelForward;
    bool wasRunning = (config.currentState == STATE_RUNNING && !isPaused);
    
    if (wasRunning) {
        // Queue change for end of cycle
        if (!pendingMotion.hasChanges) {
            initPendingFromCurrent();
        }
        pendingMotion.speedLevelForward = speedLevel;
        pendingMotion.hasChanges = true;
        
        engine->debug(String("⏳ Forward speed queued: ") + String(oldSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " → " + 
              String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
    } else {
        motion.speedLevelForward = speedLevel;
        engine->debug(String("✓ Forward speed: ") + String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
        calculateStepDelay();
    }
}

void VaEtVientControllerClass::setSpeedBackward(float speedLevel) {
    float oldSpeedLevel = motion.speedLevelBackward;
    bool wasRunning = (config.currentState == STATE_RUNNING && !isPaused);
    
    if (wasRunning) {
        // Queue change for end of cycle
        if (!pendingMotion.hasChanges) {
            initPendingFromCurrent();
        }
        pendingMotion.speedLevelBackward = speedLevel;
        pendingMotion.hasChanges = true;
        
        engine->debug(String("⏳ Backward speed queued: ") + String(oldSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " → " + 
              String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
    } else {
        motion.speedLevelBackward = speedLevel;
        engine->debug(String("✓ Backward speed: ") + String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
        calculateStepDelay();
    }
}

void VaEtVientControllerClass::setCyclePause(bool enabled, float durationSec, 
                                              bool isRandom, float minSec, float maxSec) {
    motion.cyclePause.enabled = enabled;
    motion.cyclePause.pauseDurationSec = durationSec;
    motion.cyclePause.isRandom = isRandom;
    motion.cyclePause.minPauseSec = minSec;
    motion.cyclePause.maxPauseSec = maxSec;
    
    if (enabled) {
        if (isRandom) {
            engine->debug(String("⏸️ Cycle pause enabled: random ") + String(minSec, 1) + "-" + String(maxSec, 1) + "s");
        } else {
            engine->debug(String("⏸️ Cycle pause enabled: fixed ") + String(durationSec, 1) + "s");
        }
    } else {
        engine->debug("⏸️ Cycle pause disabled");
    }
}

// ============================================================================
// CALCULATION METHODS
// ============================================================================

void VaEtVientControllerClass::calculateStepDelay() {
    // FIXED CYCLE TIME - speed adapts to distance
    if (motion.targetDistanceMM <= 0 || motion.speedLevelForward <= 0 || motion.speedLevelBackward <= 0) {
        stepDelayMicrosForward = 1000;
        stepDelayMicrosBackward = 1000;
        return;
    }
    
    // Convert speed levels to cycles/min
    float cyclesPerMinuteForward = speedLevelToCyclesPerMin(motion.speedLevelForward);
    float cyclesPerMinuteBackward = speedLevelToCyclesPerMin(motion.speedLevelBackward);
    
    // Safety: prevent division by zero
    if (cyclesPerMinuteForward <= 0.1) cyclesPerMinuteForward = 0.1;
    if (cyclesPerMinuteBackward <= 0.1) cyclesPerMinuteBackward = 0.1;
    
    long stepsPerDirection = (long)(motion.targetDistanceMM * STEPS_PER_MM);
    
    // Calculate forward delay with compensation for system overhead
    float halfCycleForwardMs = (60000.0 / cyclesPerMinuteForward) / 2.0;
    float rawDelayForward = (halfCycleForwardMs * 1000.0) / (float)stepsPerDirection;
    float delayForward = (rawDelayForward - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    
    // Calculate backward delay (can be different) with compensation
    float halfCycleBackwardMs = (60000.0 / cyclesPerMinuteBackward) / 2.0;
    float rawDelayBackward = (halfCycleBackwardMs * 1000.0) / (float)stepsPerDirection;
    float delayBackward = (rawDelayBackward - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    
    stepDelayMicrosForward = (unsigned long)delayForward;
    stepDelayMicrosBackward = (unsigned long)delayBackward;
    
    // Minimum delay for HSS86 safety (50kHz max = 20µs period)
    if (stepDelayMicrosForward < 20) {
        stepDelayMicrosForward = 20;
        engine->warn("⚠️ Forward speed limited! Distance " + String(motion.targetDistanceMM, 0) + 
              "mm too long for speed " + String(motion.speedLevelForward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(cyclesPerMinuteForward, 0) + " c/min)");
    }
    if (stepDelayMicrosBackward < 20) {
        stepDelayMicrosBackward = 20;
        engine->warn("⚠️ Backward speed limited! Distance " + String(motion.targetDistanceMM, 0) + 
              "mm too long for speed " + String(motion.speedLevelBackward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(cyclesPerMinuteBackward, 0) + " c/min)");
    }
    
    float avgCyclesPerMin = (cyclesPerMinuteForward + cyclesPerMinuteBackward) / 2.0;
    float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0;
    float totalCycleTime = (60000.0 / cyclesPerMinuteForward / 2.0) + (60000.0 / cyclesPerMinuteBackward / 2.0);
    
    engine->debug("⚙️  " + String(stepsPerDirection) + " steps × 2 directions | Forward: " + 
          String(motion.speedLevelForward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(cyclesPerMinuteForward, 0) + " c/min, " + 
          String(stepDelayMicrosForward) + " µs) | Backward: " + String(motion.speedLevelBackward, 1) + 
          "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(cyclesPerMinuteBackward, 0) + " c/min, " + String(stepDelayMicrosBackward) + 
          " µs) | Avg: " + String(avgSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(avgCyclesPerMin, 0) + 
          " c/min) | Total: " + String((int)totalCycleTime) + " ms");
}

float VaEtVientControllerClass::speedLevelToCyclesPerMin(float speedLevel) {
    // Convert 0-20 scale to cycles/min (0-200)
    float cpm = speedLevel * 10.0;
    
    // Safety limits
    if (cpm < 0) cpm = 0;
    if (cpm > MAX_SPEED_LEVEL * 10.0) cpm = MAX_SPEED_LEVEL * 10.0;
    
    return cpm;
}

float VaEtVientControllerClass::cyclesPerMinToSpeedLevel(float cpm) {
    return cpm / 10.0;
}

// ============================================================================
// PENDING CHANGES MANAGEMENT
// ============================================================================

void VaEtVientControllerClass::applyPendingChanges() {
    if (!pendingMotion.hasChanges) return;
    
    engine->debug(String("🔄 Applying pending config: ") + String(pendingMotion.distanceMM, 1) + 
          "mm @ F" + String(pendingMotion.speedLevelForward, 1) + 
          "/B" + String(pendingMotion.speedLevelBackward, 1));
    
    motion.startPositionMM = pendingMotion.startPositionMM;
    motion.targetDistanceMM = pendingMotion.distanceMM;
    motion.speedLevelForward = pendingMotion.speedLevelForward;
    motion.speedLevelBackward = pendingMotion.speedLevelBackward;
    pendingMotion.hasChanges = false;
    
    calculateStepDelay();
    startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
    targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
}

void VaEtVientControllerClass::resetCycleTiming() {
    lastStartContactMillis = 0;
    cycleTimeMillis = 0;
    measuredCyclesPerMinute = 0;
    wasAtStart = false;
}

// ============================================================================
// MOVEMENT CONTROL (Phase 2)
// ============================================================================

void VaEtVientControllerClass::togglePause() {
    if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
        // 💾 Save stats BEFORE toggling pause (save accumulated distance)
        if (!isPaused) {
            // Going from RUNNING → PAUSED: save current session
            saveCurrentSessionStats();
            engine->debug("💾 Stats saved before pause");
        }
        
        isPaused = !isPaused;
        config.currentState = isPaused ? STATE_PAUSED : STATE_RUNNING;
        
        // 🆕 CORRECTION: Reset timer en mode oscillation pour éviter le saut de phase lors de la reprise
        if (!isPaused && currentMovement == MOVEMENT_OSC) {
            oscillationState.lastPhaseUpdateMs = millis();
            engine->debug("🔄 Phase gelée après pause (évite à-coup)");
        }
        
        engine->info(isPaused ? "Paused" : "Resumed");
    }
}

void VaEtVientControllerClass::stop() {
    if (currentMovement == MOVEMENT_PURSUIT) {
        Pursuit.stop();  // Delegated to PursuitController
        // Keep motor enabled - HSS86 needs to stay synchronized
        
        // Save session stats before stopping
        saveCurrentSessionStats();
        return;
    }
    
    // Stop oscillation if running (important for sequence stop)
    if (currentMovement == MOVEMENT_OSC) {
        currentMovement = MOVEMENT_VAET;  // Reset to default mode
        engine->debug("🌊 Oscillation stopped by stop()");
    }
    
    // Stop chaos if running (important for sequence stop)
    if (chaosState.isRunning) {
        chaosState.isRunning = false;
        engine->debug("⚡ Chaos stopped by stop()");
    }
    
    // Reset pause states
    motionPauseState.isPausing = false;
    oscPauseState.isPausing = false;
    
    // Stop simple mode
    if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
        // CRITICAL: Keep motor enabled to maintain HSS86 synchronization
        // Disabling and re-enabling causes step loss with startPosition > 0
        
        config.currentState = STATE_READY;
        isPaused = false;
        
        pendingMotion.hasChanges = false;
        
        // Save session stats before stopping
        saveCurrentSessionStats();
    }
}

void VaEtVientControllerClass::start(float distMM, float speedLevel) {
    // ✅ Stop sequence if running (user manually starts simple mode)
    if (seqState.isRunning) {
        engine->debug("start(): stopping sequence because user manually started movement");
        SeqExecutor.stop();
    }
    
    // Auto-calibrate if not yet done
    if (config.totalDistanceMM == 0) {
        engine->warn("Not calibrated - auto-calibrating...");
        Calibration.startCalibration();
        if (config.totalDistanceMM == 0) return;
    }
    
    // Block start if in error state
    if (config.currentState == STATE_ERROR) {
        sendError("❌ Impossible de démarrer: Système en état ERREUR - Utilisez 'Retour Départ' ou recalibrez");
        return;
    }
    
    if (config.currentState != STATE_READY && config.currentState != STATE_PAUSED && config.currentState != STATE_RUNNING) {
        return;
    }
    
    // Validate and limit distance if needed
    if (motion.startPositionMM + distMM > config.totalDistanceMM) {
        if (motion.startPositionMM >= config.totalDistanceMM) {
            sendError("❌ ERROR: Position de départ dépasse le maximum");
            return;
        }
        distMM = config.totalDistanceMM - motion.startPositionMM;
    }
    
    // If already running, queue changes for next cycle
    if (config.currentState == STATE_RUNNING) {
        pendingMotion.startPositionMM = motion.startPositionMM;
        pendingMotion.distanceMM = distMM;
        pendingMotion.speedLevelForward = speedLevel;
        pendingMotion.speedLevelBackward = speedLevel;
        pendingMotion.hasChanges = true;
        return;
    }
    
    motion.targetDistanceMM = distMM;
    motion.speedLevelForward = speedLevel;
    motion.speedLevelBackward = speedLevel;
    
    engine->info("▶ Start movement: " + String(distMM, 1) + " mm @ speed " + 
          String(speedLevel, 1) + " (" + String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
    
    calculateStepDelay();
    
    // CRITICAL: Initialize step timing BEFORE setting STATE_RUNNING
    lastStepMicros = micros();
    
    // Calculate step positions
    startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
    targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
    
    // NOW set running state - lastStepMicros is properly initialized
    config.currentState = STATE_RUNNING;
    isPaused = false;
    currentMovement = MOVEMENT_VAET;  // Reset to Simple mode (va-et-vient)
    
    // Determine starting direction based on current position
    if (currentStep <= startStep) {
        movingForward = true;  // Need to go forward to start position
    } else if (currentStep >= targetStep) {
        movingForward = false;  // Already past target, go backward
    } else {
        movingForward = true;  // Continue forward to target
    }
    
    // Initialize PIN_DIR based on starting direction
    Motor.setDirection(movingForward);
    
    // Initialize distance tracking
    lastStepForDistance = currentStep;
    
    // Reset speed measurement
    resetCycleTiming();
    
    // Reset startStep reached flag
    // If we're already at or past startStep, mark it as reached
    hasReachedStartStep = (currentStep >= startStep);
    
    engine->debug("🚀 Starting movement: currentStep=" + String(currentStep) + 
          " startStep=" + String(startStep) + " targetStep=" + String(targetStep) + 
          " movingForward=" + String(movingForward ? "YES" : "NO"));
}

void VaEtVientControllerClass::returnToStart() {
    engine->info("🔄 Returning to start...");
    
    if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
        stop();
        delay(100);
    }
    
    // Allow returnToStart even from ERROR state (recovery mechanism)
    if (config.currentState == STATE_ERROR) {
        engine->info("   → Recovering from ERROR state");
    }
    
    Motor.enable();  // Enable motor
    config.currentState = STATE_CALIBRATING;
    sendStatus();  // Show calibration overlay
    delay(50);
    
    // ============================================================================
    // Use returnToStartContact() for precise positioning
    // This ensures position 0 is IDENTICAL to calibration position 0
    // (contact + decontact + SAFETY_OFFSET_STEPS)
    // ============================================================================
    
    bool success = ::returnToStartContact();  // Call global function from main
    
    if (!success) {
        // Error already logged by returnToStartContact()
        return;
    }
    
    // Reset position variables (already done in returnToStartContact, but explicit here)
    currentStep = 0;
    config.minStep = 0;
    
    engine->info("✓ Return to start complete - Position synchronized with calibration");
    
    // Keep motor enabled - HSS86 needs to stay synchronized
    config.currentState = STATE_READY;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void VaEtVientControllerClass::initPendingFromCurrent() {
    pendingMotion.startPositionMM = motion.startPositionMM;
    pendingMotion.distanceMM = motion.targetDistanceMM;
    pendingMotion.speedLevelForward = motion.speedLevelForward;
    pendingMotion.speedLevelBackward = motion.speedLevelBackward;
}

// ============================================================================
// STEP EXECUTION (Phase 3)
// ============================================================================

void VaEtVientControllerClass::doStep() {
    if (movingForward) {
        // ═══════════════════════════════════════════════════════════════════
        // MOVING FORWARD (startStep → targetStep)
        // ═══════════════════════════════════════════════════════════════════
        
        // Drift detection & correction (delegated to ContactSensors)
        if (Contacts.checkAndCorrectDriftEnd()) {
            movingForward = false;  // Reverse direction after correction
            return;
        }
        
        // Hard drift check (critical error)
        if (!Contacts.checkHardDriftEnd()) {
            return;  // Error state, stop processing
        }
        
        // Check if reached target position
        if (currentStep + 1 > targetStep) {
            movingForward = false;
            return;
        }
        
        // Check if we've reached startStep for the first time (initial approach phase)
        if (!hasReachedStartStep && currentStep >= startStep) {
            hasReachedStartStep = true;
        }
        
        // Execute step
        Motor.setDirection(true);  // Forward
        Motor.step();
        currentStep++;
        
        // Track distance
        trackDistance();
        
    } else {
        // ═══════════════════════════════════════════════════════════════════
        // MOVING BACKWARD (targetStep → startStep)
        // ═══════════════════════════════════════════════════════════════════
        
        // Drift detection & correction (delegated to ContactSensors)
        if (Contacts.checkAndCorrectDriftStart()) {
            return;  // Correction done, wait for next step
        }
        
        // Hard drift check (critical error)
        if (!Contacts.checkHardDriftStart()) {
            return;  // Error state, stop processing
        }
        
        // Reset wasAtStart flag when far from start
        if (currentStep > config.minStep + WASATSTART_THRESHOLD_STEPS) {
            wasAtStart = false;
        }
        
        // Execute step
        Motor.setDirection(false);  // Backward
        Motor.step();
        currentStep--;
        
        // Track distance
        trackDistance();
        
        // Check if reached startStep (end of backward movement)
        // ONLY reverse if we've already been to startStep once (va-et-vient mode active)
        if (currentStep <= startStep && hasReachedStartStep) {
            processCycleCompletion();
        }
    }
}

void VaEtVientControllerClass::processCycleCompletion() {
    // Apply pending changes at end of cycle BEFORE reversing direction
    applyPendingChanges();
    
    // Handle cycle pause if enabled
    if (handleCyclePause()) {
        return;  // Pausing, don't reverse yet
    }
    
    // Reverse direction for next cycle
    movingForward = true;
    
    // Sequencer callback if in sequencer context
    if (config.executionContext == CONTEXT_SEQUENCER) {
        SeqExecutor.onMovementComplete();
    }
    
    // Measure cycle timing
    measureCycleTime();
    
    // Prepare for next forward movement
    Motor.setDirection(true);
    
    engine->debug(String("🔄 End of backward cycle - State: ") + String(config.currentState) + 
          " | Movement: " + movementTypeName(currentMovement) + 
          " | movingForward: " + String(movingForward) + 
          " | seqState.isRunning: " + String(seqState.isRunning));
}

bool VaEtVientControllerClass::handleCyclePause() {
    if (!motion.cyclePause.enabled) {
        return false;  // No pause, continue
    }
    
    // Calculate pause duration
    if (motion.cyclePause.isRandom) {
        // Random mode: pick value between min and max
        // Safety: ensure min ≤ max (defense in depth)
        float minVal = min(motion.cyclePause.minPauseSec, motion.cyclePause.maxPauseSec);
        float maxVal = max(motion.cyclePause.minPauseSec, motion.cyclePause.maxPauseSec);
        float range = maxVal - minVal;
        float randomOffset = (float)random(0, 10000) / 10000.0;  // 0.0 to 1.0
        float pauseSec = minVal + (randomOffset * range);
        motionPauseState.currentPauseDuration = (unsigned long)(pauseSec * 1000);
    } else {
        // Fixed mode
        motionPauseState.currentPauseDuration = (unsigned long)(motion.cyclePause.pauseDurationSec * 1000);
    }
    
    // Start pause
    motionPauseState.isPausing = true;
    motionPauseState.pauseStartMs = millis();
    
    engine->debug("⏸️ Pause cycle VAET: " + String(motionPauseState.currentPauseDuration) + "ms");
    
    return true;  // Pausing, don't reverse direction yet
}

void VaEtVientControllerClass::measureCycleTime() {
    if (wasAtStart) {
        return;  // Already measured this cycle
    }
    
    unsigned long currentMillis = millis();
    
    if (lastStartContactMillis > 0) {
        cycleTimeMillis = currentMillis - lastStartContactMillis;
        measuredCyclesPerMinute = 60000.0 / cycleTimeMillis;
        
        float avgTargetCPM = (speedLevelToCyclesPerMin(motion.speedLevelForward) + 
                             speedLevelToCyclesPerMin(motion.speedLevelBackward)) / 2.0;
        float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0;
        float diffPercent = ((measuredCyclesPerMinute - avgTargetCPM) / avgTargetCPM) * 100.0;
        
        // Only log if difference is significant (> 15% after compensation)
        if (abs(diffPercent) > 15.0) {
            engine->debug(String("⏱️  Cycle timing: ") + String(cycleTimeMillis) + 
                  " ms | Target: " + String(avgSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
                  String(avgTargetCPM, 0) + " c/min) | Actual: " + 
                  String(measuredCyclesPerMinute, 1) + " c/min | ⚠️ Diff: " + 
                  String(diffPercent, 1) + " %");
        }
    }
    
    lastStartContactMillis = currentMillis;
    wasAtStart = true;
}

void VaEtVientControllerClass::trackDistance() {
    long delta = abs(currentStep - lastStepForDistance);
    if (delta > 0) {
        totalDistanceTraveled += delta;
        lastStepForDistance = currentStep;
    }
}
