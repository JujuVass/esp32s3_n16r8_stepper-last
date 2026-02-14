/**
 * ============================================================================
 * ChaosController.cpp - Chaos Mode Movement Controller Implementation
 * ============================================================================
 * 
 * Implements random pattern generation and execution for chaos mode.
 * ~1100 lines of chaos mode logic extracted from main file.
 * 
 * @author Refactored from stepper_controller_restructured.ino
 * @version 1.0
 */

#include "movement/ChaosController.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include "core/UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "movement/SequenceExecutor.h"

// ============================================================================
// CHAOS STATE - Owned by this module
// ============================================================================
ChaosRuntimeConfig chaos;
ChaosExecutionState chaosState;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

ChaosController& ChaosController::getInstance() {
    static ChaosController instance;
    return instance;
}

// Global accessor
ChaosController& Chaos = ChaosController::getInstance();

// ============================================================================
// CONSTANTS (from Config.h, not redefined here)
// ============================================================================
// CHAOS_MAX_STEP_DELAY_MICROS, SPEED_COMPENSATION_FACTOR, HARD_DRIFT_TEST_ZONE_MM
// are already defined in Config.h

// Pattern names for logging
static const char* PATTERN_NAMES[] = {
    "ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", 
    "WAVE", "PENDULUM", "SPIRAL", "CALM", "BRUTE_FORCE", "LIBERATOR"
};

// Debug helper - local to this module
static const char* executionContextName(ExecutionContext ctx) {
    switch(ctx) {
        case CONTEXT_STANDALONE: return "STANDALONE";
        case CONTEXT_SEQUENCER: return "SEQUENCER";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

inline void ChaosController::calculateLimits(float& minLimit, float& maxLimit) {
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    minLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
    maxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
}

inline float ChaosController::calculateMaxAmplitude(float minLimit, float maxLimit) {
    return min(
        chaos.centerPositionMM - minLimit,
        maxLimit - chaos.centerPositionMM
    );
}

inline bool ChaosController::forceDirectionAtLimits(float currentPos, float minLimit, float maxLimit, bool& movingFwd) {
    if (currentPos <= minLimit + 1.0) {
        movingFwd = true;
        return true;
    } else if (currentPos >= maxLimit - 1.0) {
        movingFwd = false;
        return true;
    }
    return false;
}

// ============================================================================
// LIMIT CHECKING
// ============================================================================

bool ChaosController::checkLimits() {
    if (currentMovement != MOVEMENT_CHAOS) return true;
    
    const float STEPS_PER_MM_INV = 1.0f / STEPS_PER_MM;
    float currentPosMM = currentStep * STEPS_PER_MM_INV;
    float nextPosMM = movingForward ? (currentStep + 1) * STEPS_PER_MM_INV 
                                     : (currentStep - 1) * STEPS_PER_MM_INV;
    
    float minChaosPositionMM = chaos.centerPositionMM - chaos.amplitudeMM;
    float maxChaosPositionMM = chaos.centerPositionMM + chaos.amplitudeMM;
    
    if (movingForward) {
        float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
        float effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
        
        if (nextPosMM > effectiveMaxLimit) {
            engine->warn(String("🛡️ CHAOS: Hit upper limit! Current: ") + 
                  String(currentPosMM, 1) + "mm | Limit: " + String(effectiveMaxLimit, 1) + "mm");
            targetStep = currentStep;
            movingForward = false;
            return false;
        }
        
        float distanceToEndLimitMM = config.totalDistanceMM - maxChaosPositionMM;
        if (distanceToEndLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
            if (Contacts.isEndActive()) {
                Status.sendError("❌ CHAOS: Contact END atteint - amplitude proche limite");
                config.currentState = STATE_ERROR;
                chaosState.isRunning = false;
                return false;
            }
        }
        
    } else {
        float effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
        
        if (nextPosMM < effectiveMinLimit) {
            engine->debug(String("🛡️ CHAOS: Hit lower limit! Current: ") + 
                  String(currentPosMM, 1) + "mm | Limit: " + String(effectiveMinLimit, 1) + "mm");
            targetStep = currentStep;
            movingForward = true;
            return false;
        }
        
        if (minChaosPositionMM <= HARD_DRIFT_TEST_ZONE_MM) {
            if (Contacts.isStartActive()) {
                Status.sendError("❌ CHAOS: Contact START atteint - amplitude proche limite");
                config.currentState = STATE_ERROR;
                chaosState.isRunning = false;
                return false;
            }
        }
    }
    
    return true;
}

// ============================================================================
// CHAOS STEP EXECUTION
// ============================================================================

void ChaosController::doStep() {
    if (movingForward) {
        // ═══════════════════════════════════════════════════════════════════
        // MOVING FORWARD
        // ═══════════════════════════════════════════════════════════════════
        
        // Drift detection (safety - shared with va-et-vient)
        if (Contacts.checkAndCorrectDriftEnd()) {
            movingForward = false;
            return;
        }
        if (!Contacts.checkHardDriftEnd()) {
            chaosState.isRunning = false;
            return;
        }
        
        // Chaos amplitude limits (specific to chaos mode)
        if (!checkLimits()) return;
        
        // Check if reached target
        if (currentStep + 1 > targetStep) {
            movingForward = false;
            return;
        }
        
        // Execute step
        Motor.setDirection(true);
        Motor.step();
        currentStep++;
        
        // Track distance using StatsTracking
        stats.trackDelta(currentStep);
        
    } else {
        // ═══════════════════════════════════════════════════════════════════
        // MOVING BACKWARD
        // ═══════════════════════════════════════════════════════════════════
        
        // Drift detection (safety - shared with va-et-vient)
        if (Contacts.checkAndCorrectDriftStart()) {
            return;
        }
        if (!Contacts.checkHardDriftStart()) {
            chaosState.isRunning = false;
            return;
        }
        
        // Chaos amplitude limits (specific to chaos mode)
        if (!checkLimits()) return;
        
        // Check if reached target
        if (currentStep - 1 < targetStep) {
            movingForward = true;
            return;
        }
        
        // Execute step
        Motor.setDirection(false);
        Motor.step();
        currentStep--;
        
        // Track distance using StatsTracking
        stats.trackDelta(currentStep);
    }
}

// ============================================================================
// STEP DELAY CALCULATION
// ============================================================================

void ChaosController::calculateStepDelay() {
    float mmPerSecond = chaosState.currentSpeedLevel * 10.0;
    float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
    
    if (stepsPerSecond > 0) {
        chaosState.stepDelay = (unsigned long)((1000000.0 / stepsPerSecond) / SPEED_COMPENSATION_FACTOR);
    } else {
        chaosState.stepDelay = 10000;
    }
    
    if (chaosState.stepDelay < 20) {
        chaosState.stepDelay = 20;
    }
    
    if (chaosState.stepDelay > CHAOS_MAX_STEP_DELAY_MICROS) {
        chaosState.stepDelay = CHAOS_MAX_STEP_DELAY_MICROS;
    }
}

// ============================================================================
// PATTERN HANDLERS
// ============================================================================

void ChaosController::handleZigzag(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                    float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = ZIGZAG_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.375);
    patternDuration = random(durationMin, durationMax);
    
    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    
    float currentPos = currentStep / (float)STEPS_PER_MM;
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float maxJump = maxPossibleAmplitude * jumpMultiplier;
    float targetOffset = (random(-100, 101) / 100.0) * maxJump;
    chaosState.targetPositionMM = constrain(
        currentPos + targetOffset,
        effectiveMinLimit,
        effectiveMaxLimit
    );
}

void ChaosController::handleSweep(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = SWEEP_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.7);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float sweepPercent = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * random(0, 101) / 100.0);
    chaosState.waveAmplitude = maxPossibleAmplitude * sweepPercent;
    
    chaosState.movingForward = random(2) == 0;
    chaosState.patternStartTime = millis();
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = chaos.centerPositionMM + chaosState.waveAmplitude;
    } else {
        chaosState.targetPositionMM = chaos.centerPositionMM - chaosState.waveAmplitude;
    }
    
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
}

void ChaosController::handlePulse(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = PULSE_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.53);
    patternDuration = random(durationMin, durationMax);
    
    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    
    chaosState.pulsePhase = false;
    chaosState.pulseCenterMM = currentStep / (float)STEPS_PER_MM;
    chaosState.patternStartTime = millis();
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float pulseOffset = (random(-100, 101) / 100.0) * maxPossibleAmplitude * jumpMultiplier;
    chaosState.targetPositionMM = constrain(
        chaos.centerPositionMM + pulseOffset,
        effectiveMinLimit,
        effectiveMaxLimit
    );
    
    engine->debug("💓 PULSE Phase 1 (OUT): from=" + String(chaosState.pulseCenterMM, 1) + 
          "mm → target=" + String(chaosState.targetPositionMM, 1) + 
          "mm (will return to " + String(chaosState.pulseCenterMM, 1) + "mm)");
}

void ChaosController::handleDrift(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = DRIFT_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.6);
    patternDuration = random(durationMin, durationMax);
    
    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    
    float currentPos = currentStep / (float)STEPS_PER_MM;
    float drift = (random(-100, 101) / 100.0) * maxPossibleAmplitude * jumpMultiplier;
    chaosState.targetPositionMM = constrain(currentPos + drift, effectiveMinLimit, effectiveMaxLimit);
}

void ChaosController::handleBurst(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = BURST_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.5);
    patternDuration = random(durationMin, durationMax);
    
    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    
    float currentPos = currentStep / (float)STEPS_PER_MM;
    float maxJump = maxPossibleAmplitude * jumpMultiplier;
    float burstOffset = (random(-100, 101) / 100.0) * maxJump;
    chaosState.targetPositionMM = constrain(currentPos + burstOffset, effectiveMinLimit, effectiveMaxLimit);
}

void ChaosController::handleWave(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                  float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = WAVE_CONFIG;
    const ChaosSinusoidalExt& sin_cfg = WAVE_SIN;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.33);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * random(0, 101) / 100.0);
    chaosState.waveFrequency = sin_cfg.cyclesOverDuration / (patternDuration / 1000.0);
    chaosState.patternStartTime = millis();
    chaosState.targetPositionMM = chaos.centerPositionMM;
    
    engine->debug("🌊 WAVE: amplitude=" + String(chaosState.waveAmplitude, 1) + 
          "mm, freq=" + String(chaosState.waveFrequency, 3) + 
          "Hz, duration=" + String(patternDuration) + "ms");
}

void ChaosController::handlePendulum(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                      float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = PENDULUM_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.4);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * random(0, 101) / 100.0);
    chaosState.waveAmplitude = maxPossibleAmplitude * jumpMultiplier;
    chaosState.movingForward = true;
    chaosState.patternStartTime = millis();
    
    chaosState.targetPositionMM = constrain(
        chaos.centerPositionMM + chaosState.waveAmplitude,
        effectiveMinLimit,
        effectiveMaxLimit
    );
}

void ChaosController::handleSpiral(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                    float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = SPIRAL_CONFIG;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.5);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    chaosState.spiralRadius = maxPossibleAmplitude * cfg.amplitudeJumpMin;
    chaosState.movingForward = random(2) == 0;
    chaosState.patternStartTime = millis();
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(
            chaos.centerPositionMM + chaosState.spiralRadius,
            effectiveMinLimit,
            effectiveMaxLimit
        );
    } else {
        chaosState.targetPositionMM = constrain(
            chaos.centerPositionMM - chaosState.spiralRadius,
            effectiveMinLimit,
            effectiveMaxLimit
        );
    }
}

void ChaosController::handleCalm(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                  float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = CALM_CONFIG;
    const ChaosSinusoidalExt& sin_cfg = CALM_SIN;
    const ChaosPauseExt& pause_cfg = CALM_PAUSE;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.667);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float amplitudeRange = cfg.amplitudeJumpMax - cfg.amplitudeJumpMin;
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + amplitudeRange * craziness);
    
    chaosState.waveFrequency = sin_cfg.frequencyMin + ((sin_cfg.frequencyMax - sin_cfg.frequencyMin) * random(0, 101) / 100.0);
    chaosState.pauseDuration = pause_cfg.pauseMin + (unsigned long)((pause_cfg.pauseMax - pause_cfg.pauseMin) * (1.0 - craziness));
    chaosState.isInPatternPause = false;
    chaosState.patternStartTime = millis();
    chaosState.targetPositionMM = chaos.centerPositionMM;
    chaosState.lastCalmSineValue = 0.0;  // Reset sine tracking for fresh CALM start
}

void ChaosController::handleBruteForce(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                        float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = BRUTE_FORCE_CONFIG;
    const ChaosMultiPhaseExt& multi_cfg = BRUTE_FORCE_MULTI;
    const ChaosDirectionExt& dir_cfg = BRUTE_FORCE_DIR;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.75);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    
    int forwardChance = dir_cfg.forwardChanceMin - (int)((dir_cfg.forwardChanceMin - dir_cfg.forwardChanceMax) * craziness);
    chaosState.movingForward = random(100) < forwardChance;
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(
            chaos.centerPositionMM + chaosState.waveAmplitude,
            effectiveMinLimit,
            effectiveMaxLimit
        );
    } else {
        chaosState.targetPositionMM = constrain(
            chaos.centerPositionMM - chaosState.waveAmplitude,
            effectiveMinLimit,
            effectiveMaxLimit
        );
    }
    
    chaosState.pauseDuration = multi_cfg.pauseMin + (unsigned long)((multi_cfg.pauseMax - multi_cfg.pauseMin) * (1.0 - craziness));
    chaosState.brutePhase = 0;
    chaosState.patternStartTime = millis();
}

void ChaosController::handleLiberator(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                       float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = LIBERATOR_CONFIG;
    const ChaosMultiPhaseExt& multi_cfg = LIBERATOR_MULTI;
    const ChaosDirectionExt& dir_cfg = LIBERATOR_DIR;
    
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
    
    unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
    unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.75);
    patternDuration = random(durationMin, durationMax);
    
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    
    int forwardChance = dir_cfg.forwardChanceMin - (int)((dir_cfg.forwardChanceMin - dir_cfg.forwardChanceMax) * craziness);
    chaosState.movingForward = random(100) < forwardChance;
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(
            chaos.centerPositionMM + chaosState.waveAmplitude,
            effectiveMinLimit,
            effectiveMaxLimit
        );
    } else {
        chaosState.targetPositionMM = constrain(
            chaos.centerPositionMM - chaosState.waveAmplitude,
            effectiveMinLimit,
            effectiveMaxLimit
        );
    }
    
    chaosState.pauseDuration = multi_cfg.pauseMin + (unsigned long)((multi_cfg.pauseMax - multi_cfg.pauseMin) * (1.0 - craziness));
    chaosState.liberatorPhase = 0;
    chaosState.patternStartTime = millis();
}

// ============================================================================
// PATTERN GENERATION
// ============================================================================

void ChaosController::generatePattern() {
    // Build list of enabled patterns with weights
    int enabledPatterns[CHAOS_PATTERN_COUNT];
    int weights[CHAOS_PATTERN_COUNT] = {12, 12, 8, 8, 5, 10, 12, 8, 15, 10, 10};
    int totalWeight = 0;
    int enabledCount = 0;
    
    for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
        if (chaos.patternsEnabled[i]) {
            enabledPatterns[enabledCount] = i;
            totalWeight += weights[i];
            enabledCount++;
        }
    }
    
    if (enabledCount == 0) {
        engine->warn("⚠️ No patterns enabled, enabling all");
        for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
            chaos.patternsEnabled[i] = true;
            enabledPatterns[i] = i;
            totalWeight += weights[i];
        }
        enabledCount = CHAOS_PATTERN_COUNT;
    }
    
    float effectiveMinLimit, effectiveMaxLimit;
    calculateLimits(effectiveMinLimit, effectiveMaxLimit);
    
    // Weighted random selection
    int roll = random(totalWeight);
    int cumulative = 0;
    
    for (int i = 0; i < enabledCount; i++) {
        int patternIndex = enabledPatterns[i];
        cumulative += weights[patternIndex];
        if (roll < cumulative) {
            chaosState.currentPattern = (ChaosPattern)patternIndex;
            break;
        }
    }
    
    float craziness = chaos.crazinessPercent / 100.0;
    float speedMultiplier = 1.0;
    unsigned long patternDuration = 2000;
    
    // Dispatch to pattern handler
    switch (chaosState.currentPattern) {
        case CHAOS_ZIGZAG:
            handleZigzag(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_SWEEP:
            handleSweep(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_PULSE:
            handlePulse(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_DRIFT:
            handleDrift(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_BURST:
            handleBurst(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_WAVE:
            handleWave(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_PENDULUM:
            handlePendulum(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_SPIRAL:
            handleSpiral(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_CALM:
            handleCalm(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_BRUTE_FORCE:
            handleBruteForce(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
        case CHAOS_LIBERATOR:
            handleLiberator(craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
            break;
    }
    
    // Apply speed and timing
    chaosState.currentSpeedLevel = speedMultiplier * chaos.maxSpeedLevel;
    chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
    calculateStepDelay();
    
    chaosState.nextPatternChangeTime = millis() + patternDuration;
    chaosState.patternsExecuted++;
    
    // Clamp target
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    
    // Debug output
    float currentPos = currentStep / (float)STEPS_PER_MM;
    engine->debug(String("🎲 Chaos #") + String(chaosState.patternsExecuted) + ": " + 
          PATTERN_NAMES[chaosState.currentPattern] + 
          " | Config: center=" + String(chaos.centerPositionMM, 1) + 
          "mm amplitude=" + String(chaos.amplitudeMM, 1) + "mm" +
          " | Current: " + String(currentPos, 1) + "mm" +
          " | Target: " + String(chaosState.targetPositionMM, 1) + "mm" +
          " | Limits: [" + String(effectiveMinLimit, 1) + " - " + String(effectiveMaxLimit, 1) + "]" +
          " | Speed: " + String(chaosState.currentSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) +
          " | Duration: " + String(patternDuration) + "ms");
}

// ============================================================================
// CONTINUOUS PATTERN PROCESSING
// ============================================================================

void ChaosController::processWave(float effectiveMinLimit, float effectiveMaxLimit) {
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    float t = elapsed / 1000.0;
    
    float sineValue = sin(2.0 * PI * chaosState.waveFrequency * t);
    chaosState.targetPositionMM = chaos.centerPositionMM + (chaosState.waveAmplitude * sineValue);
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
}

void ChaosController::processCalm(float effectiveMinLimit, float effectiveMaxLimit) {
    if (chaosState.isInPatternPause) {
        unsigned long pauseElapsed = millis() - chaosState.pauseStartTime;
        if (pauseElapsed >= chaosState.pauseDuration) {
            chaosState.isInPatternPause = false;
            chaosState.patternStartTime = millis();
            engine->debug("😮 CALM: pause complete, resuming breathing");
        }
        return;
    }
    
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    float t = elapsed / 1000.0;
    
    float sineValue = sin(2.0 * PI * chaosState.waveFrequency * t);
    chaosState.targetPositionMM = chaos.centerPositionMM + (chaosState.waveAmplitude * sineValue);
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    // Random pause at peaks
    bool crossedThreshold = (abs(chaosState.lastCalmSineValue) <= CALM_PAUSE.pauseTriggerThreshold && 
                            abs(sineValue) > CALM_PAUSE.pauseTriggerThreshold);
    chaosState.lastCalmSineValue = sineValue;
    
    if (crossedThreshold && random(10000) < (int)(CALM_PAUSE.pauseChancePercent * 100)) {
        chaosState.isInPatternPause = true;
        chaosState.pauseStartTime = millis();
        chaosState.pauseDuration = random(CALM_PAUSE.pauseMin, CALM_PAUSE.pauseMax);
        engine->debug("😌 CALM: entering pause for " + String(chaosState.pauseDuration) + "ms");
    }
}

// ============================================================================
// AT-TARGET HANDLERS
// ============================================================================

void ChaosController::handlePulseAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    float currentPos = currentStep / (float)STEPS_PER_MM;
    
    if (!chaosState.pulsePhase) {
        chaosState.pulsePhase = true;
        chaosState.targetPositionMM = chaosState.pulseCenterMM;
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("💓 PULSE Phase 2 (RETURN): from=" + String(currentPos, 1) + 
              "mm → return to " + String(chaosState.pulseCenterMM, 1) + "mm");
    } else {
        unsigned long elapsed = millis() - chaosState.patternStartTime;
        const unsigned long MIN_PATTERN_DURATION = 150;
        
        if (elapsed >= MIN_PATTERN_DURATION) {
            chaosState.nextPatternChangeTime = millis();
            engine->debug("💓 PULSE complete after " + String(elapsed) + "ms → force new pattern");
        }
    }
}

void ChaosController::handlePendulumAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    chaosState.movingForward = !chaosState.movingForward;
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = chaos.centerPositionMM + chaosState.waveAmplitude;
    } else {
        chaosState.targetPositionMM = chaos.centerPositionMM - chaosState.waveAmplitude;
    }
    
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    engine->debug("⚖️ PENDULUM alternate: dir=" + String(chaosState.movingForward ? "UP" : "DOWN") + 
          " target=" + String(chaosState.targetPositionMM, 1) + "mm");
}

void ChaosController::handleSpiralAtTarget(float effectiveMinLimit, float effectiveMaxLimit, float maxPossibleAmplitude) {
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    unsigned long duration = chaosState.nextPatternChangeTime - chaosState.patternStartTime;
    float progress = constrain((float)elapsed / (float)duration, 0.0f, 1.0f);
    
    float currentRadius;
    if (progress < 0.5) {
        currentRadius = maxPossibleAmplitude * (0.1 + 0.9 * (progress * 2.0));
    } else {
        currentRadius = maxPossibleAmplitude * (1.0 - 0.9 * ((progress - 0.5) * 2.0));
    }
    
    chaosState.movingForward = !chaosState.movingForward;
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = chaos.centerPositionMM + currentRadius;
    } else {
        chaosState.targetPositionMM = chaos.centerPositionMM - currentRadius;
    }
    
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    engine->debug("🌀 SPIRAL: progress=" + String(progress * 100, 0) + "%" +
          " radius=" + String(currentRadius, 1) + "mm" +
          " dir=" + String(chaosState.movingForward ? "UP" : "DOWN") +
          " target=" + String(chaosState.targetPositionMM, 1) + "mm");
}

void ChaosController::handleSweepAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    chaosState.movingForward = !chaosState.movingForward;
    
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = chaos.centerPositionMM + chaosState.waveAmplitude;
    } else {
        chaosState.targetPositionMM = chaos.centerPositionMM - chaosState.waveAmplitude;
    }
    
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    engine->debug("🌊 SWEEP alternate: " + String(chaosState.movingForward ? "UP" : "DOWN") +
          " target=" + String(chaosState.targetPositionMM, 1) + "mm");
}

void ChaosController::handleBruteForceAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    float craziness = chaos.crazinessPercent / 100.0;
    float amplitude = chaosState.waveAmplitude;
    
    if (chaosState.brutePhase == 0) {
        chaosState.brutePhase = 1;
        chaosState.movingForward = !chaosState.movingForward;
        
        chaosState.currentSpeedLevel = (0.01 + 0.09 * craziness) * chaos.maxSpeedLevel;
        chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
        calculateStepDelay();
        
        if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM + amplitude, effectiveMinLimit, effectiveMaxLimit);
        } else {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM - amplitude, effectiveMinLimit, effectiveMaxLimit);
        }
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("🔨 BRUTE_FORCE Phase 1 (slow out): speed=" + String(chaosState.currentSpeedLevel, 1) +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
              
    } else if (chaosState.brutePhase == 1) {
        chaosState.brutePhase = 2;
        chaosState.isInPatternPause = true;
        chaosState.pauseStartTime = millis();
        
        engine->debug("🔨 BRUTE_FORCE Phase 2 (pause): " + String(chaosState.pauseDuration) + "ms");
        
    } else {
        chaosState.brutePhase = 0;
        chaosState.isInPatternPause = false;
        chaosState.movingForward = !chaosState.movingForward;
        
        chaosState.currentSpeedLevel = (0.7 + 0.3 * craziness) * chaos.maxSpeedLevel;
        chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
        calculateStepDelay();
        
        if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM + amplitude, effectiveMinLimit, effectiveMaxLimit);
        } else {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM - amplitude, effectiveMinLimit, effectiveMaxLimit);
        }
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("🔨 BRUTE_FORCE Phase 0 (fast in): speed=" + String(chaosState.currentSpeedLevel, 1) +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
    }
}

void ChaosController::handleLiberatorAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    float craziness = chaos.crazinessPercent / 100.0;
    float amplitude = chaosState.waveAmplitude;
    
    if (chaosState.liberatorPhase == 0) {
        chaosState.liberatorPhase = 1;
        chaosState.movingForward = !chaosState.movingForward;
        
        chaosState.currentSpeedLevel = (0.7 + 0.3 * craziness) * chaos.maxSpeedLevel;
        chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
        calculateStepDelay();
        
        if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM + amplitude, effectiveMinLimit, effectiveMaxLimit);
        } else {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM - amplitude, effectiveMinLimit, effectiveMaxLimit);
        }
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("🔓 LIBERATOR Phase 1 (fast out): speed=" + String(chaosState.currentSpeedLevel, 1) +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
              
    } else if (chaosState.liberatorPhase == 1) {
        chaosState.liberatorPhase = 2;
        chaosState.isInPatternPause = true;
        chaosState.pauseStartTime = millis();
        
        engine->debug("🔓 LIBERATOR Phase 2 (pause): " + String(chaosState.pauseDuration) + "ms");
        
    } else {
        chaosState.liberatorPhase = 0;
        chaosState.isInPatternPause = false;
        chaosState.movingForward = !chaosState.movingForward;
        
        chaosState.currentSpeedLevel = (0.01 + 0.09 * craziness) * chaos.maxSpeedLevel;
        chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
        calculateStepDelay();
        
        if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM + amplitude, effectiveMinLimit, effectiveMaxLimit);
        } else {
            chaosState.targetPositionMM = constrain(chaos.centerPositionMM - amplitude, effectiveMinLimit, effectiveMaxLimit);
        }
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("🔓 LIBERATOR Phase 0 (slow in): speed=" + String(chaosState.currentSpeedLevel, 1) +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
    }
}

void ChaosController::handleDiscreteAtTarget() {
    if (chaosState.currentPattern == CHAOS_WAVE) {
        return;  // WAVE uses continuous calculation
    }
    
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    const unsigned long MIN_PATTERN_DURATION = 150;
    
    if (elapsed >= MIN_PATTERN_DURATION) {
        chaosState.nextPatternChangeTime = millis();
        engine->debug("🎯 Discrete pattern " + String(PATTERN_NAMES[chaosState.currentPattern]) + 
              " reached target after " + String(elapsed) + "ms → force new pattern");
    }
}

// ============================================================================
// MAIN PROCESS LOOP
// ============================================================================

void ChaosController::process() {
    if (!chaosState.isRunning) return;
    
    // Handle pause for multi-phase patterns (internal pattern pause, not user pause)
    if (chaosState.isInPatternPause && 
        (chaosState.currentPattern == CHAOS_BRUTE_FORCE || 
         chaosState.currentPattern == CHAOS_LIBERATOR)) {
        unsigned long pauseElapsed = millis() - chaosState.pauseStartTime;
        if (pauseElapsed >= chaosState.pauseDuration) {
            chaosState.isInPatternPause = false;
            String patternName = (chaosState.currentPattern == CHAOS_BRUTE_FORCE) ? "BRUTE_FORCE" : "LIBERATOR";
            engine->debug(String(chaosState.currentPattern == CHAOS_BRUTE_FORCE ? "🔨" : "🔓") + " " + 
                  patternName + " pause complete, resuming");
        } else {
            return;
        }
    }
    
    // Check duration limit
    if (chaos.durationSeconds > 0) {
        unsigned long elapsed = (millis() - chaosState.startTime) / 1000;
        if (elapsed >= chaos.durationSeconds) {
            engine->info("⏱️ Chaos duration complete: " + String(elapsed) + "s");
            engine->debug(String("processChaosExecution(): config.executionContext=") + 
                  executionContextName(config.executionContext) + " seqState.isRunning=" + String(seqState.isRunning));
            
            if (config.executionContext == CONTEXT_SEQUENCER) {
                chaosState.isRunning = false;
                SeqExecutor.onMovementComplete();
            } else {
                stop();
            }
            return;
        }
    }
    
    // Check for new pattern
    if (millis() >= chaosState.nextPatternChangeTime) {
        generatePattern();
        calculateStepDelay();
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug(String("🎲 Pattern: ") + PATTERN_NAMES[chaosState.currentPattern] + 
              " | Speed: " + String(chaosState.currentSpeedLevel, 1) + 
              "/" + String(MAX_SPEED_LEVEL, 0) + " | Delay: " + String(chaosState.stepDelay) + " µs/step");
    }
    
    // Calculate limits
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    float effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
    float effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    
    // Handle continuous patterns
    if (chaosState.currentPattern == CHAOS_WAVE) {
        processWave(effectiveMinLimit, effectiveMaxLimit);
    }
    
    if (chaosState.currentPattern == CHAOS_CALM) {
        processCalm(effectiveMinLimit, effectiveMaxLimit);
        if (chaosState.isInPatternPause) return;
    }
    
    // Check if at target
    bool isAtTarget = (abs(currentStep - targetStep) <= 2);
    
    if (isAtTarget) {
        switch (chaosState.currentPattern) {
            case CHAOS_PULSE:
                handlePulseAtTarget(effectiveMinLimit, effectiveMaxLimit);
                break;
            case CHAOS_PENDULUM:
                handlePendulumAtTarget(effectiveMinLimit, effectiveMaxLimit);
                break;
            case CHAOS_SPIRAL:
                handleSpiralAtTarget(effectiveMinLimit, effectiveMaxLimit, maxPossibleAmplitude);
                break;
            case CHAOS_SWEEP:
                handleSweepAtTarget(effectiveMinLimit, effectiveMaxLimit);
                break;
            case CHAOS_CALM:
                // Handled in processCalm
                break;
            case CHAOS_BRUTE_FORCE:
                handleBruteForceAtTarget(effectiveMinLimit, effectiveMaxLimit);
                break;
            case CHAOS_LIBERATOR:
                handleLiberatorAtTarget(effectiveMinLimit, effectiveMaxLimit);
                break;
            default:
                handleDiscreteAtTarget();
                break;
        }
    }
    
    // Execute movement
    if (currentStep != targetStep) {
        unsigned long currentMicros = micros();
        if (currentMicros - chaosState.lastStepMicros >= chaosState.stepDelay) {
            chaosState.lastStepMicros = currentMicros;
            doStep();
            
            float currentPos = currentStep / (float)STEPS_PER_MM;
            if (currentPos < chaosState.minReachedMM) {
                chaosState.minReachedMM = currentPos;
            }
            if (currentPos > chaosState.maxReachedMM) {
                chaosState.maxReachedMM = currentPos;
            }
        }
    }
}

// ============================================================================
// START / STOP
// ============================================================================

void ChaosController::start() {
    MutexGuard guard(stateMutex);
    if (!guard) {
        engine->warn("ChaosController::start: mutex timeout");
        return;
    }
    
    if (config.currentState != STATE_READY && config.currentState != STATE_PAUSED) {
        engine->error("❌ Cannot start chaos: system not ready");
        return;
    }
    
    if (seqState.isRunning && config.executionContext != CONTEXT_SEQUENCER) {
        engine->debug("startChaos(): stopping sequence because chaos started outside of sequencer");
        SeqExecutor.stop();
    }
    
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    
    if (chaos.amplitudeMM <= 0 || chaos.amplitudeMM > maxAllowed) {
        engine->error("❌ Invalid amplitude: " + String(chaos.amplitudeMM) + " mm (max: " + String(maxAllowed, 1) + " mm)");
        return;
    }
    
    if (chaos.centerPositionMM < chaos.amplitudeMM || 
        chaos.centerPositionMM > maxAllowed - chaos.amplitudeMM) {
        engine->error("❌ Invalid center: " + String(chaos.centerPositionMM) + 
          " mm (amplitude: " + String(chaos.amplitudeMM) + " mm, max: " + String(maxAllowed, 1) + " mm)");
        return;
    }
    
    // Initialize random
    if (chaos.seed == 0) {
        randomSeed(micros());
    } else {
        randomSeed(chaos.seed);
    }
    
    // Initialize state
    chaosState.isRunning = true;
    chaosState.startTime = millis();
    chaosState.minReachedMM = currentStep / (float)STEPS_PER_MM;
    chaosState.maxReachedMM = chaosState.minReachedMM;
    chaosState.patternsExecuted = 0;
    chaosState.lastStepMicros = micros();
    
    // Move to center if needed
    float currentPosMM = currentStep / (float)STEPS_PER_MM;
    if (abs(currentPosMM - chaos.centerPositionMM) > 1.0) {
        engine->info("🎯 Moving to center: " + String(chaos.centerPositionMM, 1) + " mm");
        targetStep = (long)(chaos.centerPositionMM * STEPS_PER_MM);
        
        Motor.enable();
        bool moveForward = (targetStep > currentStep);
        Motor.setDirection(moveForward);
        
        // Use calibrated speed (speed 5.0 ≈ 990µs) instead of fixed 500µs
        const unsigned long stepDelay = 990;
        unsigned long positioningStart = millis();
        unsigned long lastWsService = millis();
        unsigned long lastStatusUpdate = millis();
        unsigned long lastStepTime = micros();
        
        while (currentStep != targetStep && (millis() - positioningStart < 30000)) {
            unsigned long nowMicros = micros();
            if (nowMicros - lastStepTime >= stepDelay) {
                if (moveForward) {
                    Motor.step();
                    currentStep++;
                } else {
                    Motor.step();
                    currentStep--;
                }
                lastStepTime = nowMicros;
            }
            
            // Service network + send status feedback every 250ms
            unsigned long nowMs = millis();
            if (nowMs - lastWsService >= 10) {
                webSocket.loop();
                server.handleClient();
                lastWsService = nowMs;
            }
            if (nowMs - lastStatusUpdate >= 250) {
                Status.send();
                lastStatusUpdate = nowMs;
            }
            yield();
        }
        
        if (currentStep != targetStep) {
            engine->warn("⚠️ Timeout during center positioning");
            engine->error("❌ Chaos mode aborted - failed to reach center position");
            
            chaosState.isRunning = false;
            config.currentState = STATE_READY;
            currentMovement = MOVEMENT_VAET;
            Motor.disable();
            
            Status.sendError("Cannot reach center - timeout after 30s. Check that the motor can move freely.");
            Status.send();
            return;
        }
    }
    
    // Generate first pattern
    generatePattern();
    calculateStepDelay();
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    engine->debug(String("🎲 Pattern: ") + PATTERN_NAMES[chaosState.currentPattern] + 
          " | Speed: " + String(chaosState.currentSpeedLevel, 1) + 
          "/" + String(MAX_SPEED_LEVEL, 0) + " | Delay: " + String(chaosState.stepDelay) + " µs/step");
    
    // Set running state (config.currentState is single source of truth for user pause)
    config.currentState = STATE_RUNNING;
    currentMovement = MOVEMENT_CHAOS;
    
    Motor.enable();
    
    engine->info("🎲 Chaos mode started");
    engine->info("   Centre: " + String(chaos.centerPositionMM, 1) + " mm");
    engine->info("   Amplitude: ±" + String(chaos.amplitudeMM, 1) + " mm");
    engine->info("   Max speed: " + String(chaos.maxSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0));
    engine->info("   Craziness: " + String(chaos.crazinessPercent, 0) + " %");
    if (chaos.durationSeconds > 0) {
        engine->info("   Duration: " + String(chaos.durationSeconds) + " s");
    } else {
        engine->info("   Duration: INFINITE");
    }
    if (chaos.seed > 0) {
        engine->info("   Seed: " + String(chaos.seed));
    }
}

void ChaosController::stop() {
    MutexGuard guard(stateMutex);
    if (!guard) {
        engine->warn("ChaosController::stop: mutex timeout");
        return;
    }
    
    if (!chaosState.isRunning) return;
    
    chaosState.isRunning = false;
    
    engine->info(String("🛑 Chaos mode stopped:\n") +
          "   Patterns executed: " + String(chaosState.patternsExecuted) + "\n" +
          "   Min position: " + String(chaosState.minReachedMM, 1) + " mm\n" +
          "   Max position: " + String(chaosState.maxReachedMM, 1) + " mm\n" +
          "   Range covered: " + String(chaosState.maxReachedMM - chaosState.minReachedMM, 1) + " mm");
    
    config.currentState = STATE_READY;
    currentMovement = MOVEMENT_VAET;
    // Note: config.currentState = STATE_READY already stops movement (no need for separate isPaused)
}
