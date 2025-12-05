// ============================================================================
// VA-ET-VIENT CONTROLLER - Implementation
// ============================================================================

#include "movement/VaEtVientController.h"
#include "GlobalState.h"
#include "UtilityEngine.h"

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
// PRIVATE METHODS
// ============================================================================

void VaEtVientControllerClass::initPendingFromCurrent() {
    pendingMotion.startPositionMM = motion.startPositionMM;
    pendingMotion.distanceMM = motion.targetDistanceMM;
    pendingMotion.speedLevelForward = motion.speedLevelForward;
    pendingMotion.speedLevelBackward = motion.speedLevelBackward;
}
