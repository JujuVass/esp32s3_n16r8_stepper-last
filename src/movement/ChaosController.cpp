/**
 * ============================================================================
 * ChaosController.cpp - Chaos Mode Movement Controller Implementation
 * ============================================================================
 *
 * Implements random pattern generation and execution for chaos mode.
 */

#include "movement/ChaosController.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include "core/UtilityEngine.h"
#include "core/MovementMath.h"
#include "core/Validators.h"
#include "hardware/MotorDriver.h"
#include "movement/SequenceExecutor.h"

using enum ChaosPattern;
using enum SystemState;
using enum MovementType;
using enum ExecutionContext;

// ============================================================================
// CHAOS STATE - Owned by this module
// ============================================================================
constinit ChaosRuntimeConfig chaos;
constinit ChaosExecutionState chaosState;

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

// Pattern names for logging (shared via header)
const char* const CHAOS_PATTERN_NAMES[] = {
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
    float maxAllowed = Validators::getMaxAllowedMM();
    minLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
    maxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
}

inline float ChaosController::calculateMaxAmplitude(float minLimit, float maxLimit) {
    return min(
        chaos.centerPositionMM - minLimit,
        maxLimit - chaos.centerPositionMM
    );
}

// DRY helper: set target position based on movingForward direction
inline void ChaosController::setDirectionalTarget(float amplitude, float minLimit, float maxLimit) {
    if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(chaos.centerPositionMM + amplitude, minLimit, maxLimit);
    } else {
        chaosState.targetPositionMM = constrain(chaos.centerPositionMM - amplitude, minLimit, maxLimit);
    }
}

// DRY helper: set targetPositionMM + sync targetStep atomically
inline void ChaosController::setTargetMM(float mm) {
    chaosState.targetPositionMM = mm;
    targetStep = MovementMath::mmToSteps(mm);
}

// DRY helper: random jump pattern (Zigzag/Drift/Burst share identical logic)
inline void ChaosController::handleRandomJump(const ChaosBaseConfig& cfg, float durationMaxFactor,
                                               float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                               float& speedMultiplier, unsigned long& patternDuration) {
    calcSpeedAndDuration(cfg, craziness, durationMaxFactor, speedMultiplier, patternDuration);

    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float currentPos = MovementMath::stepsToMM(currentStep);
    float maxJump = maxPossibleAmplitude * jumpMultiplier;
    float targetOffset = (static_cast<float>(random(-100, 101)) / 100.0f) * maxJump;
    chaosState.targetPositionMM = constrain(currentPos + targetOffset, effectiveMinLimit, effectiveMaxLimit);
}

// ============================================================================
// LIMIT CHECKING
// ============================================================================

bool ChaosController::checkLimits() {
    if (currentMovement != MOVEMENT_CHAOS) return true;

    float currentPosMM = MovementMath::stepsToMM(currentStep);
    float nextPosMM = movingForward ? MovementMath::stepsToMM(currentStep + 1)
                                     : MovementMath::stepsToMM(currentStep - 1);

    float minChaosPositionMM = chaos.centerPositionMM - chaos.amplitudeMM;
    float maxChaosPositionMM = chaos.centerPositionMM + chaos.amplitudeMM;

    if (movingForward) {
        float maxAllowed = Validators::getMaxAllowedMM();
        if (auto effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed); nextPosMM > effectiveMaxLimit) {
            engine->warn(String("🛡️ CHAOS: Hit upper limit! Current: ") +
                  String(currentPosMM, 1) + "mm | Limit: " + String(effectiveMaxLimit, 1) + "mm");
            targetStep = currentStep;
            movingForward = false;
            return false;
        }

        float distanceToEndLimitMM = config.totalDistanceMM - maxChaosPositionMM;
        if (distanceToEndLimitMM <= HARD_DRIFT_TEST_ZONE_MM && Contacts.isEndActive()) {
            Status.sendError("❌ CHAOS: END contact triggered - amplitude near limit");
            config.currentState = STATE_ERROR;
            chaosState.isRunning = false;
            return false;
        }

    } else {
        if (auto effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f); nextPosMM < effectiveMinLimit) {
            if (engine->isDebugEnabled()) {
                engine->debug(String("🛡️ CHAOS: Hit lower limit! Current: ") +
                      String(currentPosMM, 1) + "mm | Limit: " + String(effectiveMinLimit, 1) + "mm");
            }
            targetStep = currentStep;
            movingForward = true;
            return false;
        }

        if (minChaosPositionMM <= HARD_DRIFT_TEST_ZONE_MM && Contacts.isStartActive()) {
            Status.sendError("❌ CHAOS: START contact triggered - amplitude near limit");
            config.currentState = STATE_ERROR;
            chaosState.isRunning = false;
            return false;
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
        currentStep = currentStep + 1;

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
        currentStep = currentStep - 1;

        // Track distance using StatsTracking
        stats.trackDelta(currentStep);
    }
}

// ============================================================================
// STEP DELAY CALCULATION
// ============================================================================

void ChaosController::calculateStepDelay() {
    chaosState.stepDelay = MovementMath::chaosStepDelay(chaosState.currentSpeedLevel);
}

// ============================================================================
// PATTERN HANDLERS
// ============================================================================

// DRY helper: compute speed multiplier + pattern duration from base config
inline void ChaosController::calcSpeedAndDuration(const ChaosBaseConfig& cfg, float craziness,
                                                   float durationMaxFactor,
                                                   float& speedMultiplier, unsigned long& patternDuration) {
    float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
    float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
    speedMultiplier = (static_cast<float>(random(100)) / 100.0f) * (speedMax - speedMin) + speedMin;

    unsigned long durationMin;
    unsigned long durationMax;
    MovementMath::safeDurationCalc(cfg, craziness, durationMaxFactor, durationMin, durationMax);
    patternDuration = random(durationMin, durationMax);
}

void ChaosController::handleZigzag(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                    float& speedMultiplier, unsigned long& patternDuration) {
    handleRandomJump(ZIGZAG_CONFIG, 0.375f, craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
}

void ChaosController::handleSweep(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = SWEEP_CONFIG;
    calcSpeedAndDuration(cfg, craziness, 0.7f, speedMultiplier, patternDuration);

    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float sweepPercent = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * static_cast<float>(random(0, 101)) / 100.0f);
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
    calcSpeedAndDuration(cfg, craziness, 0.53f, speedMultiplier, patternDuration);

    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);

    chaosState.pulsePhase = false;
    chaosState.pulseCenterMM = MovementMath::stepsToMM(currentStep);
    chaosState.patternStartTime = millis();

    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float pulseOffset = (static_cast<float>(random(-100, 101)) / 100.0f) * maxPossibleAmplitude * jumpMultiplier;
    chaosState.targetPositionMM = constrain(
        chaos.centerPositionMM + pulseOffset,
        effectiveMinLimit,
        effectiveMaxLimit
    );

    if (engine->isDebugEnabled()) {
        engine->debug("💓 PULSE Phase 1 (OUT): from=" + String(chaosState.pulseCenterMM, 1) +
              "mm → target=" + String(chaosState.targetPositionMM, 1) +
              "mm (will return to " + String(chaosState.pulseCenterMM, 1) + "mm)");
    }
}

void ChaosController::handleDrift(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    handleRandomJump(DRIFT_CONFIG, 0.6f, craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
}

void ChaosController::handleBurst(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                   float& speedMultiplier, unsigned long& patternDuration) {
    handleRandomJump(BURST_CONFIG, 0.5f, craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
}

void ChaosController::handleWave(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                  float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = WAVE_CONFIG;
    const ChaosSinusoidalExt& sin_cfg = WAVE_SIN;
    calcSpeedAndDuration(cfg, craziness, 0.33f, speedMultiplier, patternDuration);

    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * static_cast<float>(random(0, 101)) / 100.0f);
    chaosState.waveFrequency = sin_cfg.cyclesOverDuration / (static_cast<float>(patternDuration) / 1000.0f);
    chaosState.patternStartTime = millis();
    chaosState.targetPositionMM = chaos.centerPositionMM;

    if (engine->isDebugEnabled()) {
        engine->debug("🌊 WAVE: amplitude=" + String(chaosState.waveAmplitude, 1) +
              "mm, freq=" + String(chaosState.waveFrequency, 3) +
              "Hz, duration=" + String(patternDuration) + "ms");
    }
}

void ChaosController::handlePendulum(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                      float& speedMultiplier, unsigned long& patternDuration) {
    const ChaosBaseConfig& cfg = PENDULUM_CONFIG;
    calcSpeedAndDuration(cfg, craziness, 0.4f, speedMultiplier, patternDuration);

    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * static_cast<float>(random(0, 101)) / 100.0f);
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
    calcSpeedAndDuration(cfg, craziness, 0.5f, speedMultiplier, patternDuration);

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
    calcSpeedAndDuration(cfg, craziness, 0.667f, speedMultiplier, patternDuration);

    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    float amplitudeRange = cfg.amplitudeJumpMax - cfg.amplitudeJumpMin;
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + amplitudeRange * craziness);

    chaosState.waveFrequency = sin_cfg.frequencyMin + ((sin_cfg.frequencyMax - sin_cfg.frequencyMin) * static_cast<float>(random(0, 101)) / 100.0f);
    chaosState.pauseDuration = pause_cfg.pauseMin + (unsigned long)(static_cast<float>(pause_cfg.pauseMax - pause_cfg.pauseMin) * (1.0f - craziness));
    chaosState.isInPatternPause = false;
    chaosState.patternStartTime = millis();
    chaosState.targetPositionMM = chaos.centerPositionMM;
    chaosState.lastCalmSineValue = 0.0f;  // Reset sine tracking for fresh CALM start
}

// DRY helper for multi-phase patterns (BruteForce and Liberator share identical logic)
void ChaosController::handleMultiPhase(const ChaosBaseConfig& cfg, const ChaosMultiPhaseExt& multi_cfg,
                                        const ChaosDirectionExt& dir_cfg, float craziness,
                                        float effectiveMinLimit, float effectiveMaxLimit,
                                        float& speedMultiplier, unsigned long& patternDuration) {
    calcSpeedAndDuration(cfg, craziness, 0.75f, speedMultiplier, patternDuration);

    float maxPossibleAmplitude = calculateMaxAmplitude(effectiveMinLimit, effectiveMaxLimit);
    chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);

    int forwardChance = dir_cfg.forwardChanceMin - static_cast<int>(static_cast<float>(dir_cfg.forwardChanceMin - dir_cfg.forwardChanceMax) * craziness);
    chaosState.movingForward = random(100) < forwardChance;

    setDirectionalTarget(chaosState.waveAmplitude, effectiveMinLimit, effectiveMaxLimit);

    chaosState.pauseDuration = multi_cfg.pauseMin + (unsigned long)(static_cast<float>(multi_cfg.pauseMax - multi_cfg.pauseMin) * (1.0f - craziness));
    chaosState.patternStartTime = millis();
}

void ChaosController::handleBruteForce(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                        float& speedMultiplier, unsigned long& patternDuration) {
    handleMultiPhase(BRUTE_FORCE_CONFIG, BRUTE_FORCE_MULTI, BRUTE_FORCE_DIR,
                     craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
    chaosState.brutePhase = 0;
}

void ChaosController::handleLiberator(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                                       float& speedMultiplier, unsigned long& patternDuration) {
    handleMultiPhase(LIBERATOR_CONFIG, LIBERATOR_MULTI, LIBERATOR_DIR,
                     craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);
    chaosState.liberatorPhase = 0;
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

    float effectiveMinLimit;
    float effectiveMaxLimit;
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

    float craziness = chaos.crazinessPercent / 100.0f;
    float speedMultiplier = 1.0f;
    unsigned long patternDuration = 2000;

    // Dispatch to pattern handler via table
    using PatternHandler = void (ChaosController::*)(float, float, float, float&, unsigned long&);
    static const PatternHandler PATTERN_HANDLERS[] = {
        &ChaosController::handleZigzag,     // CHAOS_ZIGZAG = 0
        &ChaosController::handleSweep,      // CHAOS_SWEEP = 1
        &ChaosController::handlePulse,      // CHAOS_PULSE = 2
        &ChaosController::handleDrift,      // CHAOS_DRIFT = 3
        &ChaosController::handleBurst,      // CHAOS_BURST = 4
        &ChaosController::handleWave,       // CHAOS_WAVE = 5
        &ChaosController::handlePendulum,   // CHAOS_PENDULUM = 6
        &ChaosController::handleSpiral,     // CHAOS_SPIRAL = 7
        &ChaosController::handleCalm,       // CHAOS_CALM = 8
        &ChaosController::handleBruteForce, // CHAOS_BRUTE_FORCE = 9
        &ChaosController::handleLiberator,  // CHAOS_LIBERATOR = 10
    };
    (this->*PATTERN_HANDLERS[static_cast<int>(chaosState.currentPattern)])(
        craziness, effectiveMinLimit, effectiveMaxLimit, speedMultiplier, patternDuration);

    // Apply speed and timing
    chaosState.currentSpeedLevel = speedMultiplier * chaos.maxSpeedLevel;
    chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
    calculateStepDelay();

    chaosState.nextPatternChangeTime = millis() + patternDuration;
    chaosState.patternsExecuted++;

    // Clamp target and sync targetStep
    setTargetMM(constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit));

    // Debug output
    if (engine->isDebugEnabled()) {
        float currentPos = MovementMath::stepsToMM(currentStep);
        engine->debug(String("🎲 Chaos #") + String(chaosState.patternsExecuted) + ": " +
              CHAOS_PATTERN_NAMES[static_cast<int>(chaosState.currentPattern)] +
              " | Config: center=" + String(chaos.centerPositionMM, 1) +
              "mm amplitude=" + String(chaos.amplitudeMM, 1) + "mm" +
              " | Current: " + String(currentPos, 1) + "mm" +
              " | Target: " + String(chaosState.targetPositionMM, 1) + "mm" +
              " | Limits: [" + String(effectiveMinLimit, 1) + " - " + String(effectiveMaxLimit, 1) + "]" +
              " | Speed: " + String(chaosState.currentSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) +
              " | Duration: " + String(patternDuration) + "ms");
    }
}

// ============================================================================
// CONTINUOUS PATTERN PROCESSING
// ============================================================================

void ChaosController::processWave(float effectiveMinLimit, float effectiveMaxLimit) {
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    float t = static_cast<float>(elapsed) / 1000.0f;

    float sineValue = sinf(2.0f * PI_F * chaosState.waveFrequency * t);
    chaosState.targetPositionMM = chaos.centerPositionMM + (chaosState.waveAmplitude * sineValue);
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);
}

void ChaosController::processCalm(float effectiveMinLimit, float effectiveMaxLimit) {
    if (chaosState.isInPatternPause) {
        if (auto pauseElapsed = millis() - chaosState.pauseStartTime; pauseElapsed >= chaosState.pauseDuration) {
            chaosState.isInPatternPause = false;
            chaosState.patternStartTime = millis();
            engine->debug("😮 CALM: pause complete, resuming breathing");
        }
        return;
    }

    unsigned long elapsed = millis() - chaosState.patternStartTime;
    float t = static_cast<float>(elapsed) / 1000.0f;

    float sineValue = sinf(2.0f * PI_F * chaosState.waveFrequency * t);
    chaosState.targetPositionMM = chaos.centerPositionMM + (chaosState.waveAmplitude * sineValue);
    chaosState.targetPositionMM = constrain(chaosState.targetPositionMM, effectiveMinLimit, effectiveMaxLimit);
    targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);

    // Random pause at peaks
    bool crossedThreshold = (abs(chaosState.lastCalmSineValue) <= CALM_PAUSE.pauseTriggerThreshold &&
                            abs(sineValue) > CALM_PAUSE.pauseTriggerThreshold);
    chaosState.lastCalmSineValue = sineValue;

    if (crossedThreshold && random(10000) < (int)(CALM_PAUSE.pauseChancePercent * 100)) {
        chaosState.isInPatternPause = true;
        chaosState.pauseStartTime = millis();
        chaosState.pauseDuration = random(CALM_PAUSE.pauseMin, CALM_PAUSE.pauseMax);
        if (engine->isDebugEnabled()) {
            engine->debug("😌 CALM: entering pause for " + String(chaosState.pauseDuration) + "ms");
        }
    }
}

// ============================================================================
// AT-TARGET HANDLERS
// ============================================================================

void ChaosController::handlePulseAtTarget([[maybe_unused]] float effectiveMinLimit, [[maybe_unused]] float effectiveMaxLimit) {
    float currentPos = MovementMath::stepsToMM(currentStep);

    if (!chaosState.pulsePhase) {
        chaosState.pulsePhase = true;
        setTargetMM(chaosState.pulseCenterMM);

        if (engine->isDebugEnabled()) {
            engine->debug("💓 PULSE Phase 2 (RETURN): from=" + String(currentPos, 1) +
                  "mm → return to " + String(chaosState.pulseCenterMM, 1) + "mm");
        }
    } else {
        unsigned long elapsed = millis() - chaosState.patternStartTime;

        if (elapsed >= CHAOS_MIN_PATTERN_DURATION_MS) {
            chaosState.nextPatternChangeTime = millis();
            if (engine->isDebugEnabled()) {
                engine->debug("💓 PULSE complete after " + String(elapsed) + "ms → force new pattern");
            }
        }
    }
}

void ChaosController::handlePendulumAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    chaosState.movingForward = !chaosState.movingForward;
    setDirectionalTarget(chaosState.waveAmplitude, effectiveMinLimit, effectiveMaxLimit);
    targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);

    if (engine->isDebugEnabled()) {
        engine->debug("⚖️ PENDULUM alternate: dir=" + String(chaosState.movingForward ? "UP" : "DOWN") +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
    }
}

void ChaosController::handleSpiralAtTarget(float effectiveMinLimit, float effectiveMaxLimit, float maxPossibleAmplitude) {
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    unsigned long duration = chaosState.nextPatternChangeTime - chaosState.patternStartTime;
    float progress = constrain((float)elapsed / (float)duration, 0.0f, 1.0f);

    float currentRadius;
    if (progress < 0.5f) {
        currentRadius = maxPossibleAmplitude * (0.1f + 0.9f * (progress * 2.0f));
    } else {
        currentRadius = maxPossibleAmplitude * (1.0f - 0.9f * ((progress - 0.5f) * 2.0f));
    }

    chaosState.movingForward = !chaosState.movingForward;
    setDirectionalTarget(currentRadius, effectiveMinLimit, effectiveMaxLimit);
    targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);

    if (engine->isDebugEnabled()) {
        engine->debug("🌀 SPIRAL: progress=" + String(progress * 100, 0) + "%" +
              " radius=" + String(currentRadius, 1) + "mm" +
              " dir=" + String(chaosState.movingForward ? "UP" : "DOWN") +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
    }
}

void ChaosController::handleSweepAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    chaosState.movingForward = !chaosState.movingForward;
    setDirectionalTarget(chaosState.waveAmplitude, effectiveMinLimit, effectiveMaxLimit);
    targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);

    if (engine->isDebugEnabled()) {
        engine->debug("🌊 SWEEP alternate: " + String(chaosState.movingForward ? "UP" : "DOWN") +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
    }
}

// DRY helper for multi-phase AtTarget logic (BruteForce and Liberator)
// speedCoeffPhase0/Phase2 = {base, scale} for speed = (base + scale*craziness) * maxSpeed
void ChaosController::handleMultiPhaseAtTarget(float effectiveMinLimit, float effectiveMaxLimit,
                                                uint8_t& phase, float speedBase0, float speedScale0,
                                                float speedBase2, float speedScale2,
                                                const char* emoji, const char* name) {
    float craziness = chaos.crazinessPercent / 100.0f;
    float amplitude = chaosState.waveAmplitude;

    if (phase == 0) {
        phase = 1;
        chaosState.movingForward = !chaosState.movingForward;

        chaosState.currentSpeedLevel = (speedBase0 + speedScale0 * craziness) * chaos.maxSpeedLevel;
        chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
        calculateStepDelay();

        setDirectionalTarget(amplitude, effectiveMinLimit, effectiveMaxLimit);
        targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);

        if (engine->isDebugEnabled()) {
            engine->debug(String(emoji) + " " + name + " Phase 1: speed=" + String(chaosState.currentSpeedLevel, 1) +
                  " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        }

    } else if (phase == 1) {
        phase = 2;
        chaosState.isInPatternPause = true;
        chaosState.pauseStartTime = millis();

        if (engine->isDebugEnabled()) {
            engine->debug(String(emoji) + " " + name + " Phase 2 (pause): " + String(chaosState.pauseDuration) + "ms");
        }

    } else {
        phase = 0;
        chaosState.isInPatternPause = false;
        chaosState.movingForward = !chaosState.movingForward;

        chaosState.currentSpeedLevel = (speedBase2 + speedScale2 * craziness) * chaos.maxSpeedLevel;
        chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0f, (float)MAX_SPEED_LEVEL);
        calculateStepDelay();

        setDirectionalTarget(amplitude, effectiveMinLimit, effectiveMaxLimit);
        targetStep = MovementMath::mmToSteps(chaosState.targetPositionMM);

        if (engine->isDebugEnabled()) {
            engine->debug(String(emoji) + " " + name + " Phase 0: speed=" + String(chaosState.currentSpeedLevel, 1) +
                  " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        }
    }
}

void ChaosController::handleBruteForceAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    // BruteForce: Phase 0=slow out (0.01+0.09), Phase 2=fast in (0.7+0.3)
    handleMultiPhaseAtTarget(effectiveMinLimit, effectiveMaxLimit,
                             chaosState.brutePhase, 0.01f, 0.09f, 0.7f, 0.3f, "🔨", "BRUTE_FORCE");
}

void ChaosController::handleLiberatorAtTarget(float effectiveMinLimit, float effectiveMaxLimit) {
    // Liberator: Phase 0=fast out (0.7+0.3), Phase 2=slow in (0.01+0.09)
    handleMultiPhaseAtTarget(effectiveMinLimit, effectiveMaxLimit,
                             chaosState.liberatorPhase, 0.7f, 0.3f, 0.01f, 0.09f, "🔓", "LIBERATOR");
}

void ChaosController::handleDiscreteAtTarget() {
    if (chaosState.currentPattern == CHAOS_WAVE) {
        return;  // WAVE uses continuous calculation
    }

    unsigned long elapsed = millis() - chaosState.patternStartTime;

    if (elapsed >= CHAOS_MIN_PATTERN_DURATION_MS) {
        chaosState.nextPatternChangeTime = millis();
        if (engine->isDebugEnabled()) {
            engine->debug("🎯 Discrete pattern " + String(CHAOS_PATTERN_NAMES[static_cast<int>(chaosState.currentPattern)]) +
                  " reached target after " + String(elapsed) + "ms → force new pattern");
        }
    }
}

// ============================================================================
// MAIN PROCESS LOOP
// ============================================================================

void ChaosController::process() {
    if (!chaosState.isRunning) [[unlikely]] return;

    // Handle pause for multi-phase patterns (internal pattern pause, not user pause)
    if (chaosState.isInPatternPause &&
        (chaosState.currentPattern == CHAOS_BRUTE_FORCE ||
         chaosState.currentPattern == CHAOS_LIBERATOR)) [[unlikely]] {
        unsigned long pauseElapsed = millis() - chaosState.pauseStartTime;
        if (pauseElapsed >= chaosState.pauseDuration) {
            chaosState.isInPatternPause = false;
            if (engine->isDebugEnabled()) {
                String patternName = (chaosState.currentPattern == CHAOS_BRUTE_FORCE) ? "BRUTE_FORCE" : "LIBERATOR";
                engine->debug(String(chaosState.currentPattern == CHAOS_BRUTE_FORCE ? "🔨" : "🔓") + " " +
                      patternName + " pause complete, resuming");
            }
        } else {
            return;
        }
    }

    // Check duration limit
    if (chaos.durationSeconds > 0) {
        unsigned long elapsed = (millis() - chaosState.startTime) / 1000;
        if (elapsed >= chaos.durationSeconds) [[unlikely]] {
            engine->info("⏱️ Chaos duration complete: " + String(elapsed) + "s");
            if (engine->isDebugEnabled()) {
                engine->debug(String("processChaosExecution(): config.executionContext=") +
                      executionContextName(config.executionContext) + " seqState.isRunning=" + String(seqState.isRunning));
            }

            if (config.executionContext == CONTEXT_SEQUENCER) {
                chaosState.isRunning = false;
                SeqExecutor.onMovementComplete();
            } else {
                stop();
            }
            return;
        }
    }

    // Check for new pattern (generatePattern now syncs targetStep via setTargetMM)
    if (millis() >= chaosState.nextPatternChangeTime) [[unlikely]] {
        generatePattern();
        calculateStepDelay();

        if (engine->isDebugEnabled()) {
            engine->debug(String("🎲 Pattern: ") + CHAOS_PATTERN_NAMES[static_cast<int>(chaosState.currentPattern)] +
                  " | Speed: " + String(chaosState.currentSpeedLevel, 1) +
                  "/" + String(MAX_SPEED_LEVEL, 0) + " | Delay: " + String(chaosState.stepDelay) + " µs/step");
        }
    }

    // Calculate limits
    float maxAllowed = Validators::getMaxAllowedMM();
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
    if (auto isAtTarget = (abs(currentStep - targetStep) <= 2); isAtTarget) {
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
    if (currentStep != targetStep) [[likely]] {
        unsigned long currentMicros = micros();
        if (currentMicros - chaosState.lastStepMicros >= chaosState.stepDelay) [[unlikely]] {
            chaosState.lastStepMicros = currentMicros;
            doStep();

            float currentPos = MovementMath::stepsToMM(currentStep);
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

    float maxAllowed = Validators::getMaxAllowedMM();

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
    chaosState.minReachedMM = MovementMath::stepsToMM(currentStep);
    chaosState.maxReachedMM = chaosState.minReachedMM;
    chaosState.patternsExecuted = 0;
    chaosState.lastStepMicros = micros();

    // Move to center if needed
    float currentPosMM = MovementMath::stepsToMM(currentStep);
    if (abs(currentPosMM - chaos.centerPositionMM) > 1.0f) {
        engine->info("🎯 Moving to center: " + String(chaos.centerPositionMM, 1) + " mm");
        targetStep = MovementMath::mmToSteps(chaos.centerPositionMM);

        Motor.enable();

        if (!SeqExecutor.blockingMoveToStep(targetStep)) {
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

    // Generate first pattern (generatePattern now syncs targetStep via setTargetMM)
    generatePattern();
    calculateStepDelay();

    if (engine->isDebugEnabled()) {
        engine->debug(String("🎲 Pattern: ") + CHAOS_PATTERN_NAMES[static_cast<int>(chaosState.currentPattern)] +
              " | Speed: " + String(chaosState.currentSpeedLevel, 1) +
              "/" + String(MAX_SPEED_LEVEL, 0) + " | Delay: " + String(chaosState.stepDelay) + " µs/step");
    }

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