// ============================================================================
// BASE MOVEMENT CONTROLLER - Implementation
// ============================================================================

#include "movement/BaseMovementController.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include "core/GlobalState.h"
#include "core/MovementMath.h"
#include "core/UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"
#include "movement/PursuitController.h"
#include "movement/ChaosController.h"
#include "movement/OscillationController.h"
#include "movement/SequenceExecutor.h"
#include "movement/CalibrationManager.h"

using enum SystemState;
using enum MovementType;
using enum SpeedEffect;
using enum SpeedCurve;
using enum ExecutionContext;

// ============================================================================
// ZONE EFFECT STATE - Owned by BaseMovementController (integrated)
// ============================================================================
constinit ZoneEffectConfig zoneEffect;
constinit ZoneEffectState zoneEffectState;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

BaseMovementControllerClass BaseMovement;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

BaseMovementControllerClass::BaseMovementControllerClass() = default;

// DRY helper: recalculate startStep/targetStep from current motion config
inline void BaseMovementControllerClass::recalcStepPositions() {
    startStep = MovementMath::mmToSteps(motion.startPositionMM);
    targetStep = MovementMath::mmToSteps(motion.startPositionMM + motion.targetDistanceMM);
}
// ============================================================================
// PARAMETER UPDATE METHODS
// ============================================================================

void BaseMovementControllerClass::setDistance(float distMM) {
    MutexGuard guard(motionMutex);
    if (!guard) {
        engine->warn("setDistance: mutex timeout");
        return;
    }
    
    // Limit distance to valid range
    if (motion.startPositionMM + distMM > config.totalDistanceMM) {
        distMM = config.totalDistanceMM - motion.startPositionMM;
    }
    
    if (config.currentState == STATE_RUNNING) {
        // Queue change for end of cycle
        if (!pendingMotion.hasChanges) {
            initPendingFromCurrent();
        }
        pendingMotion.distanceMM = distMM;
        pendingMotion.hasChanges = true;
    } else {
        // Apply immediately
        motion.targetDistanceMM = distMM;
        recalcStepPositions();
        calculateStepDelay();
    }
}

void BaseMovementControllerClass::setStartPosition(float startMM) {
    MutexGuard guard(motionMutex);
    if (!guard) {
        engine->warn("setStartPosition: mutex timeout");
        return;
    }
    
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
    
    bool wasRunning = (config.currentState == STATE_RUNNING);
    
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
        recalcStepPositions();
        calculateStepDelay();
        
        engine->debug(String("✓ Start position updated: ") + String(motion.startPositionMM) + " mm");
        
        // If distance was auto-adjusted, send immediate status update to sync UI
        if (distanceWasAdjusted) {
            sendStatus();
        }
    }
}

void BaseMovementControllerClass::setSpeedForward(float speedLevel) {
    setSpeedInternal(speedLevel, true);
}

void BaseMovementControllerClass::setSpeedBackward(float speedLevel) {
    setSpeedInternal(speedLevel, false);
}

void BaseMovementControllerClass::setSpeedInternal(float speedLevel, bool isForward) {
    MutexGuard guard(motionMutex);
    if (!guard) {
        engine->warn(isForward ? "setSpeedForward: mutex timeout" : "setSpeedBackward: mutex timeout");
        return;
    }
    
    const char* dirName = isForward ? "Forward" : "Backward";
    float& currentLevel = isForward ? motion.speedLevelForward : motion.speedLevelBackward;
    float& pendingLevel = isForward ? pendingMotion.speedLevelForward : pendingMotion.speedLevelBackward;
    float oldSpeedLevel = currentLevel;
    bool wasRunning = (config.currentState == STATE_RUNNING);
    
    if (wasRunning) {
        if (!pendingMotion.hasChanges) {
            initPendingFromCurrent();
        }
        pendingLevel = speedLevel;
        pendingMotion.hasChanges = true;
        
        engine->debug(String("⏳ ") + dirName + " speed queued: " + String(oldSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " → " + 
              String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(MovementMath::speedLevelToCPM(speedLevel), 0) + " c/min)");
    } else {
        currentLevel = speedLevel;
        engine->debug(String("✓ ") + dirName + " speed: " + String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(MovementMath::speedLevelToCPM(speedLevel), 0) + " c/min)");
        calculateStepDelay();
    }
}

// ============================================================================
// CALCULATION METHODS
// ============================================================================

void BaseMovementControllerClass::calculateStepDelay() {
    // Delegate core math to MovementMath (testable pure functions)
    stepDelayMicrosForward  = MovementMath::vaetStepDelay(motion.speedLevelForward,  motion.targetDistanceMM);
    stepDelayMicrosBackward = MovementMath::vaetStepDelay(motion.speedLevelBackward, motion.targetDistanceMM);
    
    // Early exit guard — bad input already handled by vaetStepDelay (returns 1000)
    if (motion.targetDistanceMM <= 0 || motion.speedLevelForward <= 0 || motion.speedLevelBackward <= 0) {
        return;
    }
    
    long stepsPerDirection = MovementMath::mmToSteps(motion.targetDistanceMM);
    
    // 🛡️ CRITICAL SAFETY: Log if stepsPerDirection was zero (vaetStepDelay already returned 1000)
    if (stepsPerDirection <= 0) {
        engine->error("⚠️ DIVISION BY ZERO PREVENTED! stepsPerDirection=" + String(stepsPerDirection) + 
              " (distance=" + String(motion.targetDistanceMM, 3) + "mm)");
        return;
    }
    
    // Warn if speeds were clamped to minimum (20µs)
    float cyclesPerMinuteForward  = MovementMath::speedLevelToCPM(motion.speedLevelForward);
    float cyclesPerMinuteBackward = MovementMath::speedLevelToCPM(motion.speedLevelBackward);
    
    if (stepDelayMicrosForward <= 20) {
        engine->warn("⚠️ Forward speed limited! Distance " + String(motion.targetDistanceMM, 0) + 
              "mm too long for speed " + String(motion.speedLevelForward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(cyclesPerMinuteForward, 0) + " c/min)");
    }
    if (stepDelayMicrosBackward <= 20) {
        engine->warn("⚠️ Backward speed limited! Distance " + String(motion.targetDistanceMM, 0) + 
              "mm too long for speed " + String(motion.speedLevelBackward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
              String(cyclesPerMinuteBackward, 0) + " c/min)");
    }
    
    // Diagnostics log (intermediate values recomputed for display only)
    float halfCycleForwardMs = (60000.0f / max(cyclesPerMinuteForward, 0.1f)) / 2.0f;
    float rawDelayForward = (halfCycleForwardMs * 1000.0f) / (float)stepsPerDirection;
    engine->info("⚙️ CALC: dist=" + String(motion.targetDistanceMM, 1) + "mm → " + 
          String(stepsPerDirection) + " steps | speed=" + String(motion.speedLevelForward, 1) + 
          " → " + String(cyclesPerMinuteForward, 0) + " c/min | halfCycle=" + 
          String(halfCycleForwardMs, 1) + "ms | rawDelay=" + String(rawDelayForward, 1) + 
          "µs → final=" + String(stepDelayMicrosForward) + "µs");
}

// ============================================================================
// ZONE EFFECT METHODS (Speed Effect + Special Effects)
// ============================================================================

int BaseMovementControllerClass::calculateAdjustedDelay(float currentPositionMM, float movementStartMM, 
                                                        float movementEndMM, int baseDelayMicros,
                                                        bool effectiveEnableStart, bool effectiveEnableEnd) {
    // If zone effects disabled or no speed effect, return base speed
    if (!zoneEffect.enabled || zoneEffect.speedEffect == SPEED_NONE) {
        return baseDelayMicros;
    }
    
    // Safety: protect against division by zero
    if (zoneEffect.zoneMM <= 0.0f) {
        return baseDelayMicros;
    }
    
    // Calculate distances relative to movement boundaries
    float distanceFromStart = abs(currentPositionMM - movementStartMM);
    float distanceFromEnd = abs(movementEndMM - currentPositionMM);
    
    float speedFactor = 1.0f;  // Default: normal speed
    
    // Check if in START zone
    if (effectiveEnableStart && distanceFromStart <= zoneEffect.zoneMM) {
        float zoneProgress = distanceFromStart / zoneEffect.zoneMM;
        speedFactor = MovementMath::zoneSpeedFactor(zoneEffect.speedEffect, zoneEffect.speedCurve,
                                                       zoneEffect.speedIntensity, zoneProgress);
    }
    
    // Check if in END zone
    if (effectiveEnableEnd && distanceFromEnd <= zoneEffect.zoneMM) {
        float zoneProgress = distanceFromEnd / zoneEffect.zoneMM;
        float endFactor = MovementMath::zoneSpeedFactor(zoneEffect.speedEffect, zoneEffect.speedCurve,
                                                           zoneEffect.speedIntensity, zoneProgress);
        
        // For decel: use max slowdown; for accel: use max speedup (min factor)
        if (zoneEffect.speedEffect == SPEED_DECEL) {
            speedFactor = max(speedFactor, endFactor);
        } else {
            speedFactor = min(speedFactor, endFactor);
        }
    }
    
    // Apply speed factor to base delay
    return (int)(baseDelayMicros * speedFactor);
}

// ============================================================================
// RANDOM TURNBACK LOGIC
// ============================================================================

void BaseMovementControllerClass::checkAndTriggerRandomTurnback(float distanceIntoZone, [[maybe_unused]] bool isEndZone) {
    // Only process if random turnback is enabled
    if (!zoneEffect.randomTurnbackEnabled) {
        return;
    }
    
    // Don't trigger turnback if we're currently pausing
    if (zoneEffectState.isPausing) {
        return;
    }
    
    // Check if we already have a pending turnback
    if (zoneEffectState.hasPendingTurnback) {
        // Check if we've reached the turnback point
        if (distanceIntoZone >= zoneEffectState.turnbackPointMM) {
            // Execute turnback - first trigger pause if enabled
            if (zoneEffect.endPauseEnabled) {
                triggerEndPause();
                if (engine->isDebugEnabled()) {
                    engine->debug("🔄⏸️ Random turnback + pause at " + String(distanceIntoZone, 1) + "mm");
                }
            } else {
                if (engine->isDebugEnabled()) {
                    engine->debug("🔄 Random turnback executed at " + String(distanceIntoZone, 1) + "mm into zone");
                }
            }
            movingForward = !movingForward;
            zoneEffectState.hasPendingTurnback = false;
            // Don't reset hasRolledForTurnback here - it will be reset when we exit the zone
        }
        return;
    }
    
    // If we already rolled the dice for this zone entry, don't roll again
    if (zoneEffectState.hasRolledForTurnback) {
        return;
    }
    
    // We just entered the zone - roll the dice ONCE
    if (distanceIntoZone < 2.0f) {  // Just entered (within first 2mm)
        // Mark that we've rolled for this zone entry
        zoneEffectState.hasRolledForTurnback = true;
        
        // Roll the dice
        int roll = random(100);
        if (roll < zoneEffect.turnbackChance) {
            // Decide to turn back at a random point in the zone
            // Don't turn back in the first 10% or last 10% of the zone
            float minTurnback = zoneEffect.zoneMM * 0.1f;
            float maxTurnback = zoneEffect.zoneMM * 0.9f;
            zoneEffectState.turnbackPointMM = minTurnback + (random(0, 1000) / 1000.0f) * (maxTurnback - minTurnback);
            zoneEffectState.hasPendingTurnback = true;
            if (engine->isDebugEnabled()) {
                engine->debug("🔄 Random turnback planned at " + String(zoneEffectState.turnbackPointMM, 1) + "mm (roll=" + String(roll) + " < " + String(zoneEffect.turnbackChance) + "%)");
            }
        } else {
            if (engine->isDebugEnabled()) {
                engine->debug("🎲 No turnback (roll=" + String(roll) + " >= " + String(zoneEffect.turnbackChance) + "%)");
            }
        }
    }
}

void BaseMovementControllerClass::resetRandomTurnback() {
    zoneEffectState.hasPendingTurnback = false;
    zoneEffectState.hasRolledForTurnback = false;  // Reset the roll flag too
    zoneEffectState.turnbackPointMM = 0.0f;
}

// ============================================================================
// END PAUSE LOGIC (like cycle pause)
// ============================================================================

bool BaseMovementControllerClass::checkAndHandleEndPause() {
    // If not pausing, nothing to do
    if (!zoneEffectState.isPausing) {
        return false;
    }
    
    // Check if pause duration has elapsed
    unsigned long elapsed = millis() - zoneEffectState.pauseStartMs;
    if (elapsed >= zoneEffectState.pauseDurationMs) {
        // Pause complete
        zoneEffectState.isPausing = false;
        if (engine->isDebugEnabled()) {
            engine->debug("⏸️ End pause complete (" + String(zoneEffectState.pauseDurationMs) + "ms)");
        }
        return false;
    }
    
    // Still pausing - don't step
    return true;
}

void BaseMovementControllerClass::triggerEndPause() {
    if (!zoneEffect.endPauseEnabled) {
        return;
    }
    
    // Calculate pause duration
    if (zoneEffect.endPauseIsRandom) {
        float minMs = zoneEffect.endPauseMinSec * 1000.0f;
        float maxMs = zoneEffect.endPauseMaxSec * 1000.0f;
        zoneEffectState.pauseDurationMs = (unsigned long)(minMs + (random(0, 1000) / 1000.0f) * (maxMs - minMs));
    } else {
        zoneEffectState.pauseDurationMs = (unsigned long)(zoneEffect.endPauseDurationSec * 1000.0f);
    }
    
    zoneEffectState.isPausing = true;
    zoneEffectState.pauseStartMs = millis();
    if (engine->isDebugEnabled()) {
        engine->debug("⏸️ End pause: " + String(zoneEffectState.pauseDurationMs) + "ms");
    }
}

// ============================================================================
// ZONE VALIDATION
// ============================================================================

void BaseMovementControllerClass::validateZoneEffect() {
    if (!zoneEffect.enabled) {
        return;  // No validation needed if disabled
    }
    
    // Get current movement amplitude
    float movementAmplitudeMM = motion.targetDistanceMM;
    
    if (movementAmplitudeMM <= 0) {
        engine->warn("⚠️ Cannot validate zone effect: no movement configured");
        return;
    }
    
    float maxAllowedZone;
    
    // If both zones enabled, each can use max 50% of movement amplitude
    if (zoneEffect.enableStart && zoneEffect.enableEnd) {
        maxAllowedZone = movementAmplitudeMM / 2.0f;
    } else {
        maxAllowedZone = movementAmplitudeMM;
    }
    
    // Enforce minimum zone size (10mm)
    if (zoneEffect.zoneMM < 0) {
        zoneEffect.zoneMM = 10.0f;
        engine->warn("⚠️ Negative zone detected, corrected to 10 mm");
    } else if (zoneEffect.zoneMM < 10.0f) {
        zoneEffect.zoneMM = 10.0f;
        engine->warn("⚠️ Zone increased to 10 mm (minimum)");
    }
    
    // Enforce maximum zone size
    if (zoneEffect.zoneMM > maxAllowedZone) {
        engine->warn("⚠️ Zone reduced from " + String(zoneEffect.zoneMM, 1) + " mm to " + 
              String(maxAllowedZone, 1) + " mm (max for amplitude of " + 
              String(movementAmplitudeMM, 1) + " mm)");
        
        zoneEffect.zoneMM = maxAllowedZone;
    }
    
    // Validate turnback chance
    if (zoneEffect.turnbackChance > 100) {
        zoneEffect.turnbackChance = 100;
    }
    
    // Validate pause durations
    if (zoneEffect.endPauseMinSec < 0.1f) zoneEffect.endPauseMinSec = 0.1f;
    if (zoneEffect.endPauseMaxSec < zoneEffect.endPauseMinSec) {
        zoneEffect.endPauseMaxSec = zoneEffect.endPauseMinSec + 0.5f;
    }
    if (zoneEffect.endPauseDurationSec < 0.1f) zoneEffect.endPauseDurationSec = 0.1f;
}

// ============================================================================
// PENDING CHANGES MANAGEMENT
// ============================================================================

void BaseMovementControllerClass::applyPendingChanges() {
    MutexGuard guard(motionMutex);
    if (!guard) {
        engine->warn("applyPendingChanges: mutex timeout");
        return;
    }
    
    if (!pendingMotion.hasChanges) return;
    
    if (engine->isDebugEnabled()) {
        engine->debug(String("🔄 Applying pending config: ") + String(pendingMotion.distanceMM, 1) + 
              "mm @ F" + String(pendingMotion.speedLevelForward, 1) + 
              "/B" + String(pendingMotion.speedLevelBackward, 1));
    }
    
    motion.startPositionMM = pendingMotion.startPositionMM;
    motion.targetDistanceMM = pendingMotion.distanceMM;
    motion.speedLevelForward = pendingMotion.speedLevelForward;
    motion.speedLevelBackward = pendingMotion.speedLevelBackward;
    pendingMotion.hasChanges = false;
    
    calculateStepDelay();
    recalcStepPositions();
}

void BaseMovementControllerClass::resetCycleTiming() {
    lastStartContactMillis = 0;
    cycleTimeMillis = 0;
    measuredCyclesPerMinute = 0;
    wasAtStart = false;
}

// ============================================================================
// MOVEMENT CONTROL
// ============================================================================

void BaseMovementControllerClass::togglePause() {
    MutexGuard guard(stateMutex);
    if (!guard) {
        engine->warn("togglePause: mutex timeout");
        return;
    }
    
    if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
        bool wasPaused = (config.currentState == STATE_PAUSED);
        
        // 💾 Save stats BEFORE toggling pause (save accumulated distance)
        if (!wasPaused) {
            // Going from RUNNING → PAUSED: save current session
            engine->saveCurrentSessionStats();
            engine->debug("💾 Stats saved before pause");
        }
        
        // Toggle state directly via config.currentState (single source of truth)
        config.currentState = wasPaused ? STATE_RUNNING : STATE_PAUSED;
        
        // Reset phase timer in oscillation mode to avoid phase jump on resume
        if (wasPaused && currentMovement == MOVEMENT_OSC) {
            oscillationState.lastPhaseUpdateMs = millis();
            engine->debug("🔄 Phase frozen after pause (avoids jerk)");
        }
        
        engine->info(config.currentState == STATE_PAUSED ? "Paused" : "Resumed");
    }
}

void BaseMovementControllerClass::stop() {
    MutexGuard guard(stateMutex);
    if (!guard) {
        engine->warn("stop: mutex timeout");
        return;
    }
    
    if (currentMovement == MOVEMENT_PURSUIT) {
        Pursuit.stop();  // Delegated to PursuitController
        // Keep motor enabled - HSS86 needs to stay synchronized
        
        // Save session stats before stopping
        engine->saveCurrentSessionStats();
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
        // Note: isPaused global removed - config.currentState is now single source of truth
        
        pendingMotion.hasChanges = false;
        
        // Save session stats before stopping
        engine->saveCurrentSessionStats();
    }
}

void BaseMovementControllerClass::start(float distMM, float speedLevel) {
    // Use both mutexes - motion config AND state change
    MutexGuard motionGuard(motionMutex);
    MutexGuard stateGuard(stateMutex);
    if (!motionGuard || !stateGuard) {
        engine->warn("start: mutex timeout");
        return;
    }
    
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
        Status.sendError("❌ Cannot start: System in ERROR state - Use 'Return to Start' or recalibrate");
        return;
    }
    
    if (config.currentState != STATE_READY && config.currentState != STATE_PAUSED && config.currentState != STATE_RUNNING) {
        return;
    }
    
    // Validate and limit distance if needed
    if (motion.startPositionMM + distMM > config.totalDistanceMM) {
        if (motion.startPositionMM >= config.totalDistanceMM) {
            Status.sendError("❌ ERROR: Start position exceeds maximum");
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
          String(speedLevel, 1) + " (" + String(MovementMath::speedLevelToCPM(speedLevel), 0) + " c/min)");
    
    calculateStepDelay();
    
    // CRITICAL: Initialize step timing BEFORE setting STATE_RUNNING
    lastStepMicros = micros();
    
    // Calculate step positions
    recalcStepPositions();
    
    // NOW set running state - lastStepMicros is properly initialized
    // config.currentState is single source of truth (no separate isPaused variable)
    config.currentState = STATE_RUNNING;
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
    stats.syncPosition(currentStep);
    
    // Reset speed measurement
    resetCycleTiming();
    
    // Reset PEND tracking (prevents false lag warnings at movement start)
    Motor.resetPendTracking();
    
    // Reset startStep reached flag
    // If we're already at or past startStep, mark it as reached
    hasReachedStartStep = (currentStep >= startStep);
    
    if (engine->isDebugEnabled()) {
        engine->debug("🚀 Starting movement: currentStep=" + String(currentStep) + 
              " startStep=" + String(startStep) + " targetStep=" + String(targetStep) + 
              " movingForward=" + String(movingForward ? "YES" : "NO"));
    }
}

void BaseMovementControllerClass::returnToStart() {
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
    // Use Calibration.returnToStart() for precise positioning
    // This ensures position 0 is IDENTICAL to calibration position 0
    // (contact + decontact + SAFETY_OFFSET_STEPS)
    // ============================================================================
    
    bool success = Calibration.returnToStart();
    
    if (!success) {
        // Error already logged by CalibrationManager
        return;
    }
    
    // Reset position variables (already done in returnToStart, but explicit here)
    currentStep = 0;
    config.minStep = 0;
    
    engine->info("✓ Return to start complete - Position synchronized with calibration");
    
    // Keep motor enabled - HSS86 needs to stay synchronized
    config.currentState = STATE_READY;
}

// ============================================================================
// MAIN LOOP PROCESSING
// ============================================================================

void BaseMovementControllerClass::process() {
    // Guard: Only process if running (STATE_PAUSED is handled via config.currentState)
    if (config.currentState != STATE_RUNNING) [[unlikely]] {
        return;
    }
    
    // Check if in cycle pause (between cycles)
    if (motionPauseState.isPausing) [[unlikely]] {
        unsigned long elapsedMs = millis() - motionPauseState.pauseStartMs;
        if (elapsedMs >= motionPauseState.currentPauseDuration) {
            // End of pause, resume movement
            motionPauseState.isPausing = false;
            movingForward = true;  // Resume forward direction
            engine->debug("▶️ End cycle pause VAET");
        }
        // During pause, don't step
        return;
    }
    
    // Check if in end pause (zone effect)
    if (checkAndHandleEndPause()) [[unlikely]] {
        return;  // Still pausing, don't step
    }
    
    // Calculate current step delay
    unsigned long currentMicros = micros();
    unsigned long currentDelay = movingForward ? stepDelayMicrosForward : stepDelayMicrosBackward;
    
    // Apply zone effects if enabled
    if (zoneEffect.enabled && hasReachedStartStep) {
        float currentPositionMM = MovementMath::stepsToMM(currentStep - startStep);
        
        // Mirror mode: swap enableStart/enableEnd on return trip
        // Affects SPATIAL zone effects (speed curve + random turnback)
        // Does NOT affect endpoint effects (endPause uses physical flags in doStep)
        bool effectiveEnableStart = zoneEffect.enableStart;
        bool effectiveEnableEnd = zoneEffect.enableEnd;
        if (zoneEffect.mirrorOnReturn && !movingForward) {
            effectiveEnableStart = zoneEffect.enableEnd;
            effectiveEnableEnd = zoneEffect.enableStart;
        }
        
        // CRITICAL: Direction matters for zones!
        float movementStartMM;
        float movementEndMM;
        if (movingForward) {
            movementStartMM = 0.0f;
            movementEndMM = motion.targetDistanceMM;
        } else {
            // Inverted for backward movement
            movementStartMM = motion.targetDistanceMM;
            movementEndMM = 0.0f;
        }
        
        // Calculate distance into each zone
        float distanceFromEnd = abs(movementEndMM - currentPositionMM);
        
        // Check for random turnback in START zone (when moving backward)
        // Uses effective (mirrored) flags - turnback is a spatial zone effect
        if (!movingForward && effectiveEnableStart && distanceFromEnd <= zoneEffect.zoneMM) {
            float distanceIntoZone = zoneEffect.zoneMM - distanceFromEnd;
            checkAndTriggerRandomTurnback(distanceIntoZone, false);
            // If pause was triggered, exit early
            if (zoneEffectState.isPausing) {
                return;
            }
        }
        
        // Check for random turnback in END zone (when moving forward)
        // Uses effective (mirrored) flags - turnback is a spatial zone effect
        if (movingForward && effectiveEnableEnd && distanceFromEnd <= zoneEffect.zoneMM) {
            float distanceIntoZone = zoneEffect.zoneMM - distanceFromEnd;
            checkAndTriggerRandomTurnback(distanceIntoZone, true);
            // If pause was triggered, exit early
            if (zoneEffectState.isPausing) {
                return;
            }
        }
        
        // Apply speed effect (decel or accel) - pass effective flags directly
        currentDelay = calculateAdjustedDelay(currentPositionMM, movementStartMM, movementEndMM, currentDelay, effectiveEnableStart, effectiveEnableEnd);
    }
    
    // Check if enough time has passed for next step
    if (currentMicros - lastStepMicros >= currentDelay) [[unlikely]] {
        lastStepMicros = currentMicros;
        doStep();
    }
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void BaseMovementControllerClass::initPendingFromCurrent() {
    pendingMotion.startPositionMM = motion.startPositionMM;
    pendingMotion.distanceMM = motion.targetDistanceMM;
    pendingMotion.speedLevelForward = motion.speedLevelForward;
    pendingMotion.speedLevelBackward = motion.speedLevelBackward;
}

// ============================================================================
// STEP EXECUTION
// ============================================================================

void BaseMovementControllerClass::doStep() {
    if (movingForward) {
        // ═══════════════════════════════════════════════════════════════════
        // MOVING FORWARD (startStep → targetStep)
        // ═══════════════════════════════════════════════════════════════════
        
        // Drift detection & correction (delegated to ContactSensors)
        if (Contacts.checkAndCorrectDriftEnd()) [[unlikely]] {
            movingForward = false;  // Reverse direction after correction
            resetRandomTurnback();  // Reset turnback state on direction change
            return;
        }
        
        // Hard drift check (critical error)
        if (!Contacts.checkHardDriftEnd()) [[unlikely]] {
            return;  // Error state, stop processing
        }
        
        // Check if reached target position
        if (currentStep + 1 > targetStep) [[unlikely]] {
            if (engine->isDebugEnabled()) {
                engine->debug("🎯 Reached targetStep=" + String(targetStep) + " (currentStep=" + 
                      String(currentStep) + ", pos=" + String(MovementMath::stepsToMM(currentStep), 1) + "mm)");
            }
            // Trigger end pause if enabled (at END extremity)
            // Note: end pause uses PHYSICAL zone flags (no mirror swap)
            if (zoneEffect.enabled && zoneEffect.endPauseEnabled && zoneEffect.enableEnd) {
                triggerEndPause();
            }
            movingForward = false;
            resetRandomTurnback();  // Reset turnback state on direction change
            return;
        }
        
        // Check if we've reached startStep for the first time (initial approach phase)
        if (!hasReachedStartStep && currentStep >= startStep) [[unlikely]] {
            hasReachedStartStep = true;
        }
        
        // Execute step
        Motor.setDirection(true);  // Forward
        Motor.step();
        currentStep = currentStep + 1;
        
        // Track distance
        stats.trackDelta(currentStep);
        
    } else {
        // ═══════════════════════════════════════════════════════════════════
        // MOVING BACKWARD (targetStep → startStep)
        // ═══════════════════════════════════════════════════════════════════
        
        // Drift detection & correction (delegated to ContactSensors)
        if (Contacts.checkAndCorrectDriftStart()) [[unlikely]] {
            return;  // Correction done, wait for next step
        }
        
        // Hard drift check (critical error)
        if (!Contacts.checkHardDriftStart()) [[unlikely]] {
            return;  // Error state, stop processing
        }
        
        // Reset wasAtStart flag when far from start
        if (currentStep > config.minStep + WASATSTART_THRESHOLD_STEPS) {
            wasAtStart = false;
        }
        
        // Execute step
        Motor.setDirection(false);  // Backward
        Motor.step();
        currentStep = currentStep - 1;
        
        // Track distance
        stats.trackDelta(currentStep);
        
        // Check if reached startStep (end of backward movement)
        // ONLY reverse if we've already been to startStep once (va-et-vient mode active)
        if (currentStep <= startStep && hasReachedStartStep) [[unlikely]] {
            if (engine->isDebugEnabled()) {
                engine->debug("🏠 Reached startStep=" + String(startStep) + " (currentStep=" + 
                      String(currentStep) + ", pos=" + String(MovementMath::stepsToMM(currentStep), 1) + "mm)");
            }
            // Trigger end pause if enabled (at START extremity)
            // Note: end pause uses PHYSICAL zone flags (no mirror swap)
            // Mirror only affects speed effect curve, not pause triggers
            if (zoneEffect.enabled && zoneEffect.endPauseEnabled && zoneEffect.enableStart) {
                triggerEndPause();
            }
            resetRandomTurnback();  // Reset turnback state on direction change
            processCycleCompletion();
        }
    }
}

void BaseMovementControllerClass::processCycleCompletion() {
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
    
}

bool BaseMovementControllerClass::handleCyclePause() {
    if (!motion.cyclePause.enabled) {
        return false;  // No pause, continue
    }
    
    motionPauseState.currentPauseDuration = motion.cyclePause.calculateDurationMs();
    
    // Start pause
    motionPauseState.isPausing = true;
    motionPauseState.pauseStartMs = millis();
    
    if (engine->isDebugEnabled()) {
        engine->debug("⏸️ Cycle pause VAET: " + String(motionPauseState.currentPauseDuration) + "ms");
    }
    
    return true;  // Pausing, don't reverse direction yet
}

void BaseMovementControllerClass::measureCycleTime() {
    if (wasAtStart) {
        return;  // Already measured this cycle
    }
    
    unsigned long currentMillis = millis();
    
    if (lastStartContactMillis > 0) {
        cycleTimeMillis = currentMillis - lastStartContactMillis;
        measuredCyclesPerMinute = 60000.0f / cycleTimeMillis;
        
        float avgTargetCPM = (MovementMath::speedLevelToCPM(motion.speedLevelForward) + 
                             MovementMath::speedLevelToCPM(motion.speedLevelBackward)) / 2.0f;
        float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0f;
        float diffPercent = ((measuredCyclesPerMinute - avgTargetCPM) / avgTargetCPM) * 100.0f;
        
        // Only log if difference is significant (> 15% after compensation)
        if (abs(diffPercent) > 15.0f && engine->isDebugEnabled()) {
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

// trackDistance() removed — callers use stats.trackDelta(currentStep) directly