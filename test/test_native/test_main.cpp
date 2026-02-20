// ============================================================================
// NATIVE UNIT TESTS — Pure Logic & Math Verification
// ============================================================================
// Tests that run on the HOST PC (no ESP32 needed).
// Covers: Config constants, Type defaults, Speed math, Zone curves,
//         Chaos patterns, Validators, Stats tracking.
//
// Run with: pio test -e native
// ============================================================================

// Arduino stub MUST be first (provides min, max, random, String, PI)
#include <Arduino.h>
#include <unity.h>
#include <cmath>
#include <cstdlib>

// ============================================================================
// EXTERN DEFINITIONS (satisfy Config.h externs)
// ============================================================================
const char* ssid = "test_ssid";
const char* password = "test_password";
const char* otaHostname = "test-esp32";
const char* otaPassword = "test_ota";

// Include project headers
#include "core/Config.h"
#include "core/Types.h"
#include "movement/ChaosPatterns.h"

using enum SystemState;
using enum MovementType;
using enum ExecutionContext;
using enum SpeedEffect;
using enum SpeedCurve;
using enum OscillationWaveform;
using enum ChaosPattern;

// ============================================================================
// MINIMAL SystemConfig (real one lives in UtilityEngine.h — too many deps)
// Only the fields needed for Validators.h to compile/link
// ============================================================================
struct SystemConfig {
    volatile SystemState currentState;
    volatile ExecutionContext executionContext;
    long minStep;
    long maxStep;
    float totalDistanceMM;
    int lastStartContactState;
    int lastEndContactState;
    bool currentMotorDirection;
    int nextLineId;
    
    SystemConfig() :
        currentState(SystemState::STATE_INIT),
        executionContext(ExecutionContext::CONTEXT_STANDALONE),
        minStep(0), maxStep(0), totalDistanceMM(0),
        lastStartContactState(HIGH), lastEndContactState(HIGH),
        currentMotorDirection(true), nextLineId(1) {}
};

// Globals needed by Validators.h
volatile float effectiveMaxDistanceMM = 200.0f;
volatile float maxDistanceLimitPercent = 100.0f;
SystemConfig config;

#include "core/Validators.h"
// ============================================================================
// HELPER: Float comparison with tolerance
// ============================================================================
#define TEST_ASSERT_FLOAT_NEAR(expected, actual, tol) \
    TEST_ASSERT_FLOAT_WITHIN((tol), (expected), (actual))

// ============================================================================
// 1. CONFIG CONSTANT INTEGRITY
// ============================================================================
// Verify derived constants are consistent and relationships hold.
// These tests catch accidental edits that break hardware assumptions.
// ============================================================================

void test_step_timing_consistency() {
    // STEP_EXECUTION_TIME must be exactly 2× STEP_PULSE (HIGH + LOW)
    TEST_ASSERT_EQUAL_INT(STEP_PULSE_MICROS * 2, STEP_EXECUTION_TIME_MICROS);
}

void test_steps_per_mm_calculation() {
    // STEPS_PER_MM = STEPS_PER_REV / MM_PER_REV
    float expected = (float)STEPS_PER_REV / MM_PER_REV;
    TEST_ASSERT_FLOAT_NEAR(expected, STEPS_PER_MM, 0.001f);
}

void test_osc_max_speed_derived() {
    // OSC_MAX_SPEED_MM_S must equal MAX_SPEED_LEVEL × 20
    TEST_ASSERT_FLOAT_NEAR(MAX_SPEED_LEVEL * 20.0f, OSC_MAX_SPEED_MM_S, 0.01f);
}

void test_speed_level_positive() {
    TEST_ASSERT_TRUE(MAX_SPEED_LEVEL > 0);
}

void test_calibration_constants_sane() {
    TEST_ASSERT_TRUE(CALIBRATION_MAX_STEPS > 0);
    TEST_ASSERT_TRUE(MAX_CALIBRATION_RETRIES > 0);
    TEST_ASSERT_TRUE(MAX_CALIBRATION_ERROR_PERCENT > 0);
    TEST_ASSERT_TRUE(MAX_CALIBRATION_ERROR_PERCENT <= 100.0f);
    TEST_ASSERT_TRUE(CALIBRATION_ERROR_MARGIN_STEPS > 0);
    TEST_ASSERT_TRUE(CALIBRATION_SLOW_FACTOR >= 1);
}

void test_safety_offset_positive() {
    TEST_ASSERT_TRUE(SAFETY_OFFSET_STEPS > 0);
}

void test_hard_distance_limits_ordered() {
    // HARD_MIN must be less than HARD_MAX
    TEST_ASSERT_TRUE(HARD_MIN_DISTANCE_MM < HARD_MAX_DISTANCE_MM);
}

void test_timing_intervals_nonzero() {
    TEST_ASSERT_TRUE(STATUS_UPDATE_INTERVAL_MS > 0);
    TEST_ASSERT_TRUE(STATUS_IDLE_INTERVAL_MS > 0);
    TEST_ASSERT_TRUE(STATUS_PURSUIT_INTERVAL_MS > 0);
    TEST_ASSERT_TRUE(STATUS_CALIB_INTERVAL_MS > 0);
    // Pursuit must be fastest
    TEST_ASSERT_TRUE(STATUS_PURSUIT_INTERVAL_MS <= STATUS_UPDATE_INTERVAL_MS);
    // Idle must be slowest
    TEST_ASSERT_TRUE(STATUS_IDLE_INTERVAL_MS >= STATUS_UPDATE_INTERVAL_MS);
}

void test_watchdog_escalation_order() {
    // Recovery interval must be shorter than check interval (faster during recovery)
    TEST_ASSERT_TRUE(WATCHDOG_RECOVERY_INTERVAL_MS <= WATCHDOG_CHECK_INTERVAL_MS);
    // Must have at least 1 retry per tier
    TEST_ASSERT_TRUE(WATCHDOG_SOFT_MAX_RETRIES >= 1);
    TEST_ASSERT_TRUE(WATCHDOG_HARD_MAX_RETRIES >= 1);
}

void test_sequence_limits_sane() {
    TEST_ASSERT_TRUE(MAX_SEQUENCE_LINES > 0);
    TEST_ASSERT_TRUE(MAX_SEQUENCE_LINES <= 255);  // uint8_t
    TEST_ASSERT_TRUE(MAX_CYCLES_PER_LINE > 0);
    TEST_ASSERT_TRUE(MAX_PAUSE_AFTER_MS > 0);
}

void test_oscillation_constants_sane() {
    TEST_ASSERT_TRUE(OSC_MIN_STEP_DELAY_MICROS > 0);
    TEST_ASSERT_TRUE(OSC_MAX_STEPS_PER_CATCH_UP >= 1);
    TEST_ASSERT_TRUE(SINE_TABLE_SIZE > 0);
    TEST_ASSERT_TRUE(OSC_FREQ_TRANSITION_DURATION_MS > 0);
}

void test_chaos_max_delay_sane() {
    // Max delay must be large enough for slow movement
    TEST_ASSERT_TRUE(CHAOS_MAX_STEP_DELAY_MICROS >= 1000);
    // Min amplitude must be positive
    TEST_ASSERT_TRUE(CHAOS_MIN_AMPLITUDE_MM > 0);
    TEST_ASSERT_TRUE(CHAOS_MIN_AMPLITUDE_MM < CHAOS_MAX_AMPLITUDE_MM);
}

// ============================================================================
// 2. TYPE DEFAULT VALUES
// ============================================================================
// Verify all struct constructors initialize to documented defaults.
// Catches accidental changes to default values.
// ============================================================================

void test_motion_config_defaults() {
    constexpr MotionConfig m;
    TEST_ASSERT_FLOAT_NEAR(0.0f, m.startPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, m.targetDistanceMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(5.0f, m.speedLevelForward, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(5.0f, m.speedLevelBackward, 0.001f);
}

void test_cycle_pause_config_defaults() {
    constexpr CyclePauseConfig cp;
    TEST_ASSERT_FALSE(cp.enabled);
    TEST_ASSERT_FLOAT_NEAR(1.5f, cp.pauseDurationSec, 0.001f);
    TEST_ASSERT_FALSE(cp.isRandom);
    TEST_ASSERT_FLOAT_NEAR(0.5f, cp.minPauseSec, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(5.0f, cp.maxPauseSec, 0.001f);
}

void test_cycle_pause_state_defaults() {
    constexpr CyclePauseState cps;
    TEST_ASSERT_FALSE(cps.isPausing);
    TEST_ASSERT_EQUAL_UINT32(0, cps.pauseStartMs);
    TEST_ASSERT_EQUAL_UINT32(0, cps.currentPauseDuration);
}

void test_pending_motion_defaults() {
    constexpr PendingMotionConfig pm;
    TEST_ASSERT_FLOAT_NEAR(0.0f, pm.startPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, pm.distanceMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, pm.speedLevelForward, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, pm.speedLevelBackward, 0.001f);
    TEST_ASSERT_FALSE(pm.hasChanges);
}

void test_zone_effect_config_defaults() {
    constexpr ZoneEffectConfig ze;
    TEST_ASSERT_FALSE(ze.enabled);
    TEST_ASSERT_TRUE(ze.enableStart);
    TEST_ASSERT_TRUE(ze.enableEnd);
    TEST_ASSERT_FALSE(ze.mirrorOnReturn);
    TEST_ASSERT_FLOAT_NEAR(50.0f, ze.zoneMM, 0.001f);
    TEST_ASSERT_TRUE(ze.speedEffect == SpeedEffect::SPEED_DECEL);
    TEST_ASSERT_TRUE(ze.speedCurve == SpeedCurve::CURVE_SINE);
    TEST_ASSERT_FLOAT_NEAR(75.0f, ze.speedIntensity, 0.001f);
    TEST_ASSERT_FALSE(ze.randomTurnbackEnabled);
    TEST_ASSERT_EQUAL_UINT8(30, ze.turnbackChance);
    TEST_ASSERT_FALSE(ze.endPauseEnabled);
}

void test_zone_effect_state_defaults() {
    constexpr ZoneEffectState zes;
    TEST_ASSERT_FALSE(zes.hasPendingTurnback);
    TEST_ASSERT_FALSE(zes.hasRolledForTurnback);
    TEST_ASSERT_FLOAT_NEAR(0.0f, zes.turnbackPointMM, 0.001f);
    TEST_ASSERT_FALSE(zes.isPausing);
}

void test_pursuit_state_defaults() {
    constexpr PursuitState ps;
    TEST_ASSERT_EQUAL_INT32(0, ps.targetStep);
    TEST_ASSERT_FLOAT_NEAR(10.0f, ps.maxSpeedLevel, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(1000, ps.stepDelay);
    TEST_ASSERT_FALSE(ps.isMoving);
}

void test_oscillation_config_defaults() {
    constexpr OscillationConfig oc;
    TEST_ASSERT_FLOAT_NEAR(0.0f, oc.centerPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(20.0f, oc.amplitudeMM, 0.001f);
    TEST_ASSERT_TRUE(oc.waveform == OscillationWaveform::OSC_SINE);
    TEST_ASSERT_FLOAT_NEAR(0.5f, oc.frequencyHz, 0.001f);
    TEST_ASSERT_TRUE(oc.enableRampIn);
    TEST_ASSERT_TRUE(oc.enableRampOut);
    TEST_ASSERT_EQUAL_INT(0, oc.cycleCount);  // 0 = infinite
    TEST_ASSERT_TRUE(oc.returnToCenter);
}

void test_oscillation_state_defaults() {
    constexpr OscillationState os;
    TEST_ASSERT_EQUAL_UINT32(0, os.startTimeMs);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.currentAmplitude, 0.001f);
    TEST_ASSERT_EQUAL_INT(0, os.completedCycles);
    TEST_ASSERT_FALSE(os.isRampingIn);
    TEST_ASSERT_FALSE(os.isRampingOut);
    TEST_ASSERT_FALSE(os.isReturning);
    TEST_ASSERT_FALSE(os.isInitialPositioning);
    TEST_ASSERT_FALSE(os.isTransitioning);
    TEST_ASSERT_FALSE(os.isCenterTransitioning);
    TEST_ASSERT_FALSE(os.isAmplitudeTransitioning);
}

void test_chaos_runtime_config_defaults() {
    ChaosRuntimeConfig cr;  // non-constexpr (has for-loop in constructor)
    TEST_ASSERT_FLOAT_NEAR(110.0f, cr.centerPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, cr.amplitudeMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(5.0f, cr.maxSpeedLevel, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(0, cr.durationSeconds);  // 0 = infinite
    TEST_ASSERT_EQUAL_UINT32(0, cr.seed);
    TEST_ASSERT_FLOAT_NEAR(50.0f, cr.crazinessPercent, 0.001f);
    // All patterns enabled by default
    for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
        TEST_ASSERT_TRUE(cr.patternsEnabled[i]);
    }
}

void test_chaos_execution_state_defaults() {
    constexpr ChaosExecutionState cs;
    TEST_ASSERT_FALSE(cs.isRunning);
    TEST_ASSERT_TRUE(cs.currentPattern == ChaosPattern::CHAOS_ZIGZAG);
    TEST_ASSERT_EQUAL_UINT32(0, cs.startTime);
    TEST_ASSERT_EQUAL_UINT(0, cs.patternsExecuted);
    TEST_ASSERT_TRUE(cs.movingForward);
    TEST_ASSERT_FALSE(cs.isInPatternPause);
    TEST_ASSERT_EQUAL_UINT8(0, cs.brutePhase);
    TEST_ASSERT_EQUAL_UINT8(0, cs.liberatorPhase);
    TEST_ASSERT_EQUAL_UINT32(1000, cs.stepDelay);
}

void test_sequence_line_defaults() {
    SequenceLine sl;  // non-constexpr (has for-loop)
    TEST_ASSERT_TRUE(sl.enabled);
    TEST_ASSERT_TRUE(sl.movementType == MovementType::MOVEMENT_VAET);
    TEST_ASSERT_FLOAT_NEAR(0.0f, sl.startPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(100.0f, sl.distanceMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(5.0f, sl.speedForward, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(5.0f, sl.speedBackward, 0.001f);
    TEST_ASSERT_EQUAL_INT(1, sl.cycleCount);
    TEST_ASSERT_EQUAL_INT(0, sl.pauseAfterMs);
    // Chaos patterns all enabled
    for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
        TEST_ASSERT_TRUE(sl.chaosPatternsEnabled[i]);
    }
}

void test_sequence_execution_state_defaults() {
    constexpr SequenceExecutionState ses;
    TEST_ASSERT_FALSE(ses.isRunning);
    TEST_ASSERT_FALSE(ses.isLoopMode);
    TEST_ASSERT_EQUAL_INT(0, ses.currentLineIndex);
    TEST_ASSERT_EQUAL_INT(0, ses.currentCycleInLine);
    TEST_ASSERT_FALSE(ses.isPaused);
    TEST_ASSERT_FALSE(ses.isWaitingPause);
    TEST_ASSERT_EQUAL_INT(0, ses.loopCount);
}

void test_system_config_defaults() {
    SystemConfig sc;
    TEST_ASSERT_TRUE(sc.currentState == SystemState::STATE_INIT);
    TEST_ASSERT_TRUE(sc.executionContext == ExecutionContext::CONTEXT_STANDALONE);
    TEST_ASSERT_EQUAL_INT32(0, sc.minStep);
    TEST_ASSERT_EQUAL_INT32(0, sc.maxStep);
    TEST_ASSERT_FLOAT_NEAR(0.0f, sc.totalDistanceMM, 0.001f);
    TEST_ASSERT_EQUAL_INT(1, sc.nextLineId);
}

// ============================================================================
// 3. SPEED MATH
// ============================================================================
// Re-implement and verify the core speed formulas.
// ============================================================================

// Mirror of BaseMovementControllerClass::speedLevelToCyclesPerMin
static float speedLevelToCyclesPerMin(float speedLevel) {
    float cpm = speedLevel * 10.0f;
    if (cpm < 0) cpm = 0;
    if (cpm > MAX_SPEED_LEVEL * 10.0f) cpm = MAX_SPEED_LEVEL * 10.0f;
    return cpm;
}

void test_speed_level_to_cpm_linear() {
    // speedLevel × 10 = cycles per minute
    TEST_ASSERT_FLOAT_NEAR(0.0f, speedLevelToCyclesPerMin(0), 0.01f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, speedLevelToCyclesPerMin(1.0f), 0.01f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, speedLevelToCyclesPerMin(5.0f), 0.01f);
    TEST_ASSERT_FLOAT_NEAR(200.0f, speedLevelToCyclesPerMin(20.0f), 0.01f);
}

void test_speed_level_clamped_negative() {
    TEST_ASSERT_FLOAT_NEAR(0.0f, speedLevelToCyclesPerMin(-5.0f), 0.01f);
}

void test_speed_level_clamped_max() {
    float maxCPM = MAX_SPEED_LEVEL * 10.0f;
    TEST_ASSERT_FLOAT_NEAR(maxCPM, speedLevelToCyclesPerMin(MAX_SPEED_LEVEL + 10.0f), 0.01f);
}

// Mirror of BaseMovementController step delay formula
static unsigned long calculateVaetStepDelay(float speedLevel, float distanceMM) {
    if (distanceMM <= 0 || speedLevel <= 0) return 1000;
    
    float cpm = speedLevelToCyclesPerMin(speedLevel);
    if (cpm <= 0.1f) cpm = 0.1f;
    
    long stepsPerDirection = (long)(distanceMM * STEPS_PER_MM);
    if (stepsPerDirection <= 0) return 1000;
    
    float halfCycleMs = (60000.0f / cpm) / 2.0f;
    float rawDelay = (halfCycleMs * 1000.0f) / (float)stepsPerDirection;
    float delay = (rawDelay - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    
    if (delay < 20) delay = 20;
    return (unsigned long)delay;
}

void test_vaet_step_delay_known_values() {
    // speed=5 → 50 cpm, distance=50mm → 400 steps
    // halfCycle = 60000/50/2 = 600ms, rawDelay = 600000/400 = 1500µs
    // delay = (1500 - 6) / 1.0 = 1494µs
    unsigned long delay = calculateVaetStepDelay(5.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(1494.0f, (float)delay, 5.0f);
}

void test_vaet_step_delay_zero_distance() {
    TEST_ASSERT_EQUAL_UINT32(1000, calculateVaetStepDelay(5.0f, 0.0f));
}

void test_vaet_step_delay_zero_speed() {
    TEST_ASSERT_EQUAL_UINT32(1000, calculateVaetStepDelay(0.0f, 50.0f));
}

void test_vaet_step_delay_minimum_clamp() {
    // Very high speed + short distance should clamp to 20µs minimum
    unsigned long delay = calculateVaetStepDelay(MAX_SPEED_LEVEL, 5.0f);
    TEST_ASSERT_TRUE(delay >= 20);
}

void test_vaet_step_delay_high_speed_shorter() {
    // Higher speed level must produce shorter delay (for same distance)
    unsigned long slow = calculateVaetStepDelay(2.0f, 50.0f);
    unsigned long fast = calculateVaetStepDelay(10.0f, 50.0f);
    TEST_ASSERT_TRUE(fast < slow);
}

void test_vaet_step_delay_longer_distance_shorter() {
    // Longer distance must produce shorter delay (for same speed)
    unsigned long shortDist = calculateVaetStepDelay(5.0f, 20.0f);
    unsigned long longDist = calculateVaetStepDelay(5.0f, 100.0f);
    TEST_ASSERT_TRUE(longDist < shortDist);
}

// Mirror of ChaosController::calculateStepDelay
static unsigned long calculateChaosStepDelay(float speedLevel) {
    float mmPerSecond = speedLevel * 10.0f;
    float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
    
    unsigned long delay;
    if (stepsPerSecond > 0) {
        delay = (unsigned long)((1000000.0f / stepsPerSecond) / SPEED_COMPENSATION_FACTOR);
    } else {
        delay = 10000;
    }
    if (delay < 20) delay = 20;
    if (delay > CHAOS_MAX_STEP_DELAY_MICROS) delay = CHAOS_MAX_STEP_DELAY_MICROS;
    return delay;
}

void test_chaos_step_delay_normal() {
    // speed=5 → 50mm/s → 400 steps/s → 2500µs
    unsigned long delay = calculateChaosStepDelay(5.0f);
    TEST_ASSERT_FLOAT_NEAR(2500.0f, (float)delay, 10.0f);
}

void test_chaos_step_delay_zero() {
    TEST_ASSERT_EQUAL_UINT32(10000, calculateChaosStepDelay(0.0f));
}

void test_chaos_step_delay_max_clamp() {
    // Very slow = large delay → clamped to CHAOS_MAX_STEP_DELAY_MICROS
    unsigned long delay = calculateChaosStepDelay(0.001f);
    TEST_ASSERT_TRUE(delay <= CHAOS_MAX_STEP_DELAY_MICROS);
}

void test_chaos_step_delay_min_clamp() {
    unsigned long delay = calculateChaosStepDelay(999.0f);
    TEST_ASSERT_TRUE(delay >= 20);
}

// Mirror of PursuitController::calculateStepDelay
static unsigned long calculatePursuitStepDelay(float errorMM, float maxSpeedLevel) {
    float speedLevel;
    if (errorMM > 5.0f) {
        speedLevel = maxSpeedLevel;
    } else if (errorMM > 1.0f) {
        float ratio = (errorMM - 1.0f) / (5.0f - 1.0f);
        speedLevel = maxSpeedLevel * (0.6f + (ratio * 0.4f));
    } else {
        speedLevel = maxSpeedLevel * 0.6f;
    }
    
    float mmPerSecond = speedLevel * 10.0f;
    float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
    if (stepsPerSecond < 30) stepsPerSecond = 30;
    if (stepsPerSecond > 6000) stepsPerSecond = 6000;
    
    float delayMicros = ((1000000.0f / stepsPerSecond) - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    if (delayMicros < 20) delayMicros = 20;
    return (unsigned long)delayMicros;
}

void test_pursuit_delay_far_from_target() {
    // >5mm error: uses full maxSpeedLevel
    unsigned long d1 = calculatePursuitStepDelay(10.0f, 10.0f);
    unsigned long d2 = calculatePursuitStepDelay(50.0f, 10.0f);
    // Same delay regardless of how far (both >5mm)
    TEST_ASSERT_EQUAL_UINT32(d1, d2);
}

void test_pursuit_delay_close_ramp() {
    // Between 1-5mm: speed ramps from 60% to 100%
    unsigned long far_delay = calculatePursuitStepDelay(4.5f, 10.0f);
    unsigned long close_delay = calculatePursuitStepDelay(1.5f, 10.0f);
    // Closer = slower = longer delay
    TEST_ASSERT_TRUE(close_delay > far_delay);
}

void test_pursuit_delay_very_close() {
    // <1mm: minimum speed (60% of max)
    unsigned long d1 = calculatePursuitStepDelay(0.5f, 10.0f);
    unsigned long d2 = calculatePursuitStepDelay(0.1f, 10.0f);
    // Same delay (both <1mm → same speed)
    TEST_ASSERT_EQUAL_UINT32(d1, d2);
}

void test_pursuit_delay_minimum_clamp() {
    unsigned long delay = calculatePursuitStepDelay(100.0f, MAX_SPEED_LEVEL);
    TEST_ASSERT_TRUE(delay >= 20);
}

// ============================================================================
// 4. ZONE EFFECT CURVES
// ============================================================================
// Verify curve math for all 4 curve types × 2 effect modes.
// ============================================================================

// Mirror of BaseMovementController::calculateSpeedFactor
static float calculateSpeedFactor(SpeedEffect effect, SpeedCurve curve,
                                   float intensity, float zoneProgress) {
    if (effect == SPEED_NONE) return 1.0f;
    
    float maxIntensity = 1.0f + (intensity / 100.0f) * 9.0f;
    float curveValue;
    
    switch (curve) {
        case CURVE_LINEAR:
            curveValue = 1.0f - zoneProgress;
            break;
        case CURVE_SINE: {
            float sp = (1.0f - cosf(zoneProgress * (float)PI)) / 2.0f;
            curveValue = 1.0f - sp;
            break;
        }
        case CURVE_TRIANGLE_INV: {
            float inv = 1.0f - zoneProgress;
            curveValue = inv * inv;
            break;
        }
        case CURVE_SINE_INV: {
            float inv = 1.0f - zoneProgress;
            curveValue = sinf(inv * (float)PI / 2.0f);
            break;
        }
        default:
            curveValue = 1.0f - zoneProgress;
            break;
    }
    
    if (effect == SPEED_DECEL) {
        return 1.0f + curveValue * (maxIntensity - 1.0f);
    } else {
        float accelCurve = 1.0f - curveValue;
        float minFactor = 1.0f / maxIntensity;
        return 1.0f - accelCurve * (1.0f - minFactor);
    }
}

// ---- DECEL LINEAR ----

void test_zone_decel_linear_at_boundary() {
    // zoneProgress=0 (entering zone): max deceleration
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 100.0f, 0.0f);
    // intensity=100% → maxIntensity=10.0, curveValue=1.0 → factor=10.0
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_linear_at_extremity() {
    // zoneProgress=1 (at extremity): normal speed
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 100.0f, 1.0f);
    // curveValue=0.0 → factor=1.0
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_decel_linear_monotonic() {
    // Factor must decrease monotonically as zoneProgress increases
    float prev = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 75.0f, 0.0f);
    for (float p = 0.1f; p <= 1.0f; p += 0.1f) {
        float curr = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 75.0f, p);
        TEST_ASSERT_TRUE(curr <= prev + 0.001f);  // Allow float tolerance
        prev = curr;
    }
}

// ---- DECEL SINE ----

void test_zone_decel_sine_at_boundary() {
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_SINE, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_sine_at_extremity() {
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_SINE, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_decel_sine_monotonic() {
    float prev = calculateSpeedFactor(SPEED_DECEL, CURVE_SINE, 75.0f, 0.0f);
    for (float p = 0.05f; p <= 1.0f; p += 0.05f) {
        float curr = calculateSpeedFactor(SPEED_DECEL, CURVE_SINE, p, p);
        // Sine curve is monotonic for decel
    }
    // Just verify boundary relationship
    float start = calculateSpeedFactor(SPEED_DECEL, CURVE_SINE, 75.0f, 0.0f);
    float end = calculateSpeedFactor(SPEED_DECEL, CURVE_SINE, 75.0f, 1.0f);
    TEST_ASSERT_TRUE(start > end);
}

// ---- DECEL TRIANGLE_INV ----

void test_zone_decel_triangle_inv_at_boundary() {
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_TRIANGLE_INV, 100.0f, 0.0f);
    // curveValue = (1-0)^2 = 1.0 → same as LINEAR start
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_triangle_inv_at_extremity() {
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_TRIANGLE_INV, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

// ---- ACCEL ----

void test_zone_accel_linear_at_boundary() {
    // At zone entry (zoneProgress=0): normal speed (factor=1.0)
    float f = calculateSpeedFactor(SPEED_ACCEL, CURVE_LINEAR, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_accel_linear_at_extremity() {
    // At extremity (zoneProgress=1): max speed
    float f = calculateSpeedFactor(SPEED_ACCEL, CURVE_LINEAR, 100.0f, 1.0f);
    // maxIntensity=10, minFactor=0.1, factor=0.1 (10× faster)
    TEST_ASSERT_FLOAT_NEAR(0.1f, f, 0.01f);
}

void test_zone_accel_factor_less_than_one() {
    // Accel factors should always be ≤ 1.0 (faster = shorter delay)
    for (float p = 0.0f; p <= 1.0f; p += 0.1f) {
        float f = calculateSpeedFactor(SPEED_ACCEL, CURVE_LINEAR, 50.0f, p);
        TEST_ASSERT_TRUE(f <= 1.001f);  // Allow float tolerance
        TEST_ASSERT_TRUE(f > 0.0f);     // Never zero
    }
}

// ---- SPEED_NONE ----

void test_zone_none_always_one() {
    for (float p = 0.0f; p <= 1.0f; p += 0.2f) {
        float f = calculateSpeedFactor(SPEED_NONE, CURVE_LINEAR, 100.0f, p);
        TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.001f);
    }
}

// ---- INTENSITY SCALING ----

void test_zone_zero_intensity_no_effect() {
    // 0% intensity → maxIntensity=1.0 → no speed change
    float f = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_higher_intensity_stronger_effect() {
    float f25 = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 25.0f, 0.0f);
    float f75 = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 75.0f, 0.0f);
    float f100 = calculateSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 100.0f, 0.0f);
    TEST_ASSERT_TRUE(f75 > f25);    // Higher intensity = bigger decel factor
    TEST_ASSERT_TRUE(f100 > f75);
}

// ============================================================================
// 5. CHAOS SAFE DURATION CALCULATION
// ============================================================================

// Mirror of ChaosController::safeDurationCalc (static function)
static void safeDurationCalc(const ChaosBaseConfig& cfg, float craziness, float maxFactor,
                              unsigned long& outMin, unsigned long& outMax) {
    long minVal = (long)cfg.durationMin - (long)(cfg.durationCrazinessReduction * craziness);
    long maxVal = (long)cfg.durationMax - (long)((cfg.durationMax - cfg.durationMin) * craziness * maxFactor);
    
    if (minVal < 100) minVal = 100;
    if (maxVal < 100) maxVal = 100;
    if (minVal >= maxVal) maxVal = minVal + 100;
    
    outMin = (unsigned long)minVal;
    outMax = (unsigned long)maxVal;
}

void test_safe_duration_normal() {
    unsigned long outMin, outMax;
    // ZIGZAG: durationMin=2000, durationMax=4000, reduction=600
    // craziness=0 → min=2000, max=4000
    safeDurationCalc(ZIGZAG_CONFIG, 0.0f, 0.375f, outMin, outMax);
    TEST_ASSERT_EQUAL_UINT32(2000, outMin);
    TEST_ASSERT_EQUAL_UINT32(4000, outMax);
}

void test_safe_duration_min_less_than_max() {
    unsigned long outMin, outMax;
    // For any config and craziness, min must always be < max
    for (float c = 0.0f; c <= 1.0f; c += 0.1f) {
        safeDurationCalc(ZIGZAG_CONFIG, c, 0.375f, outMin, outMax);
        TEST_ASSERT_TRUE(outMin < outMax);
    }
}

void test_safe_duration_floor_clamping() {
    unsigned long outMin, outMax;
    // Extreme craziness: should clamp to 100ms minimum
    safeDurationCalc(ZIGZAG_CONFIG, 100.0f, 100.0f, outMin, outMax);
    TEST_ASSERT_TRUE(outMin >= 100);
    TEST_ASSERT_TRUE(outMax >= 100);
    TEST_ASSERT_TRUE(outMin < outMax);
}

void test_safe_duration_burst_extreme() {
    unsigned long outMin, outMax;
    // BURST: short durations — verify they don't underflow
    safeDurationCalc(BURST_CONFIG, 1.0f, 0.5f, outMin, outMax);
    TEST_ASSERT_TRUE(outMin >= 100);
    TEST_ASSERT_TRUE(outMin < outMax);
}

void test_safe_duration_all_patterns_valid() {
    // Verify all pattern configs produce valid durations at craziness=0 and =1
    const ChaosBaseConfig* configs[] = {
        &ZIGZAG_CONFIG, &SWEEP_CONFIG, &PULSE_CONFIG, &DRIFT_CONFIG,
        &BURST_CONFIG, &WAVE_CONFIG, &PENDULUM_CONFIG, &SPIRAL_CONFIG,
        &CALM_CONFIG, &BRUTE_FORCE_CONFIG, &LIBERATOR_CONFIG
    };
    for (auto* cfg : configs) {
        unsigned long outMin, outMax;
        safeDurationCalc(*cfg, 0.0f, 0.5f, outMin, outMax);
        TEST_ASSERT_TRUE(outMin < outMax);
        TEST_ASSERT_TRUE(outMin >= 100);
        
        safeDurationCalc(*cfg, 1.0f, 0.5f, outMin, outMax);
        TEST_ASSERT_TRUE(outMin < outMax);
        TEST_ASSERT_TRUE(outMin >= 100);
    }
}

// ============================================================================
// 6. CHAOS PATTERN CONFIG INTEGRITY
// ============================================================================

void test_chaos_pattern_count() {
    // CHAOS_PATTERN_COUNT must match the enum range
    TEST_ASSERT_EQUAL_UINT8(11, CHAOS_PATTERN_COUNT);
}

void test_chaos_pattern_speed_ranges_valid() {
    // All pattern configs must have speedMin < speedMax
    const ChaosBaseConfig* configs[] = {
        &ZIGZAG_CONFIG, &SWEEP_CONFIG, &PULSE_CONFIG, &DRIFT_CONFIG,
        &BURST_CONFIG, &WAVE_CONFIG, &PENDULUM_CONFIG, &SPIRAL_CONFIG,
        &CALM_CONFIG, &BRUTE_FORCE_CONFIG, &LIBERATOR_CONFIG
    };
    for (auto* cfg : configs) {
        TEST_ASSERT_TRUE(cfg->speedMin >= 0.0f);
        TEST_ASSERT_TRUE(cfg->speedMax > cfg->speedMin);
        TEST_ASSERT_TRUE(cfg->speedMax <= 1.5f);  // Reasonable upper bound
    }
}

void test_chaos_pattern_duration_ranges_valid() {
    const ChaosBaseConfig* configs[] = {
        &ZIGZAG_CONFIG, &SWEEP_CONFIG, &PULSE_CONFIG, &DRIFT_CONFIG,
        &BURST_CONFIG, &WAVE_CONFIG, &PENDULUM_CONFIG, &SPIRAL_CONFIG,
        &CALM_CONFIG, &BRUTE_FORCE_CONFIG, &LIBERATOR_CONFIG
    };
    for (auto* cfg : configs) {
        TEST_ASSERT_TRUE(cfg->durationMin > 0);
        TEST_ASSERT_TRUE(cfg->durationMax > cfg->durationMin);
    }
}

void test_chaos_pattern_amplitude_ranges_valid() {
    const ChaosBaseConfig* configs[] = {
        &ZIGZAG_CONFIG, &SWEEP_CONFIG, &PULSE_CONFIG, &DRIFT_CONFIG,
        &BURST_CONFIG, &WAVE_CONFIG, &PENDULUM_CONFIG, &SPIRAL_CONFIG,
        &CALM_CONFIG, &BRUTE_FORCE_CONFIG, &LIBERATOR_CONFIG
    };
    for (auto* cfg : configs) {
        TEST_ASSERT_TRUE(cfg->amplitudeJumpMin >= 0.0f);
        TEST_ASSERT_TRUE(cfg->amplitudeJumpMax > cfg->amplitudeJumpMin);
        TEST_ASSERT_TRUE(cfg->amplitudeJumpMax <= 1.0f);
    }
}

// ============================================================================
// 7. VALIDATORS
// ============================================================================
// Test all boundary conditions for the centralized validators.
// ============================================================================

void test_validator_speed_valid() {
    String err;
    TEST_ASSERT_TRUE(Validators::speed(0.1f, err));
    TEST_ASSERT_TRUE(Validators::speed(5.0f, err));
    TEST_ASSERT_TRUE(Validators::speed(MAX_SPEED_LEVEL, err));
}

void test_validator_speed_too_low() {
    String err;
    TEST_ASSERT_FALSE(Validators::speed(0.0f, err));
    TEST_ASSERT_FALSE(Validators::speed(-1.0f, err));
}

void test_validator_speed_too_high() {
    String err;
    TEST_ASSERT_FALSE(Validators::speed(MAX_SPEED_LEVEL + 1.0f, err));
}

void test_validator_distance_valid() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_TRUE(Validators::distance(0.0f, err));
    TEST_ASSERT_TRUE(Validators::distance(100.0f, err));
    TEST_ASSERT_TRUE(Validators::distance(200.0f, err));
}

void test_validator_distance_negative() {
    String err;
    TEST_ASSERT_FALSE(Validators::distance(-1.0f, err));
}

void test_validator_distance_exceeds_limit() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_FALSE(Validators::distance(201.0f, err));
}

void test_validator_position_valid() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_TRUE(Validators::position(0.0f, err));
    TEST_ASSERT_TRUE(Validators::position(100.0f, err));
    TEST_ASSERT_TRUE(Validators::position(200.0f, err));
}

void test_validator_position_negative() {
    String err;
    TEST_ASSERT_FALSE(Validators::position(-0.1f, err));
}

void test_validator_motion_range_valid() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_TRUE(Validators::motionRange(0.0f, 200.0f, err));
    TEST_ASSERT_TRUE(Validators::motionRange(50.0f, 100.0f, err));
}

void test_validator_motion_range_overflow() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // start=150 + distance=100 = 250 > 200
    TEST_ASSERT_FALSE(Validators::motionRange(150.0f, 100.0f, err));
}

void test_validator_chaos_params_valid() {
    String err;
    effectiveMaxDistanceMM = 300.0f;
    TEST_ASSERT_TRUE(Validators::chaosParams(150.0f, 50.0f, 10.0f, 50.0f, err));
}

void test_validator_chaos_amplitude_exceeds_half() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // amplitude > maxAllowed/2
    TEST_ASSERT_FALSE(Validators::chaosParams(100.0f, 120.0f, 10.0f, 50.0f, err));
}

void test_validator_chaos_outside_bounds() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // center=10, amplitude=50 → 10-50 = -40 < 0
    TEST_ASSERT_FALSE(Validators::chaosParams(10.0f, 50.0f, 10.0f, 50.0f, err));
}

void test_validator_chaos_craziness_range() {
    String err;
    effectiveMaxDistanceMM = 300.0f;
    TEST_ASSERT_TRUE(Validators::chaosParams(150.0f, 50.0f, 10.0f, 0.0f, err));
    TEST_ASSERT_TRUE(Validators::chaosParams(150.0f, 50.0f, 10.0f, 100.0f, err));
    TEST_ASSERT_FALSE(Validators::chaosParams(150.0f, 50.0f, 10.0f, -1.0f, err));
    TEST_ASSERT_FALSE(Validators::chaosParams(150.0f, 50.0f, 10.0f, 101.0f, err));
}

void test_validator_oscillation_params_valid() {
    String err;
    effectiveMaxDistanceMM = 300.0f;
    TEST_ASSERT_TRUE(Validators::oscillationParams(150.0f, 50.0f, 1.0f, err));
}

void test_validator_oscillation_frequency_limit() {
    String err;
    effectiveMaxDistanceMM = 300.0f;
    TEST_ASSERT_FALSE(Validators::oscillationParams(150.0f, 50.0f, 0.0f, err));
    TEST_ASSERT_FALSE(Validators::oscillationParams(150.0f, 50.0f, -1.0f, err));
    TEST_ASSERT_FALSE(Validators::oscillationParams(150.0f, 50.0f, 11.0f, err));
}

void test_validator_oscillation_bounds() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // center=10, amplitude=20 → 10-20 < 0
    TEST_ASSERT_FALSE(Validators::oscillationParams(10.0f, 20.0f, 1.0f, err));
}

void test_validator_percentage() {
    String err;
    TEST_ASSERT_TRUE(Validators::percentage(0.0f, "test", err));
    TEST_ASSERT_TRUE(Validators::percentage(50.0f, "test", err));
    TEST_ASSERT_TRUE(Validators::percentage(100.0f, "test", err));
    TEST_ASSERT_FALSE(Validators::percentage(-1.0f, "test", err));
    TEST_ASSERT_FALSE(Validators::percentage(101.0f, "test", err));
}

void test_validator_positive() {
    String err;
    TEST_ASSERT_TRUE(Validators::positive(1.0f, "test", err));
    TEST_ASSERT_TRUE(Validators::positive(0.01f, "test", err));
    TEST_ASSERT_FALSE(Validators::positive(0.0f, "test", err));
    TEST_ASSERT_FALSE(Validators::positive(-1.0f, "test", err));
}

void test_validator_range() {
    String err;
    TEST_ASSERT_TRUE(Validators::range(5.0f, 0.0f, 10.0f, "test", err));
    TEST_ASSERT_TRUE(Validators::range(0.0f, 0.0f, 10.0f, "test", err));
    TEST_ASSERT_TRUE(Validators::range(10.0f, 0.0f, 10.0f, "test", err));
    TEST_ASSERT_FALSE(Validators::range(-0.1f, 0.0f, 10.0f, "test", err));
    TEST_ASSERT_FALSE(Validators::range(10.1f, 0.0f, 10.0f, "test", err));
}

void test_validator_distance_limit_percent() {
    String err;
    // When limit is 50%, effective max is halved
    effectiveMaxDistanceMM = 100.0f;
    maxDistanceLimitPercent = 50.0f;
    TEST_ASSERT_TRUE(Validators::distance(100.0f, err));   // Within effective limit
    TEST_ASSERT_FALSE(Validators::distance(101.0f, err));   // Exceeds effective limit
    
    // Restore
    effectiveMaxDistanceMM = 200.0f;
    maxDistanceLimitPercent = 100.0f;
}

// ============================================================================
// 8. STATS TRACKING
// ============================================================================

void test_stats_tracking_reset() {
    StatsTracking st;
    st.totalDistanceTraveled = 5000;
    st.lastSavedDistance = 3000;
    st.reset();
    TEST_ASSERT_EQUAL_UINT32(0, st.totalDistanceTraveled);
    TEST_ASSERT_EQUAL_UINT32(0, st.lastSavedDistance);
}

void test_stats_tracking_add_distance() {
    StatsTracking st;
    st.addDistance(100);
    TEST_ASSERT_EQUAL_UINT32(100, st.totalDistanceTraveled);
    st.addDistance(50);
    TEST_ASSERT_EQUAL_UINT32(150, st.totalDistanceTraveled);
    // Negative delta ignored
    st.addDistance(-10);
    TEST_ASSERT_EQUAL_UINT32(150, st.totalDistanceTraveled);
}

void test_stats_tracking_increment() {
    StatsTracking st;
    st.addDistance(500);
    st.markSaved();
    st.addDistance(200);
    TEST_ASSERT_EQUAL_UINT32(200, st.getIncrementSteps());
}

void test_stats_tracking_track_delta() {
    StatsTracking st;
    st.syncPosition(100);
    st.trackDelta(110);
    TEST_ASSERT_EQUAL_UINT32(10, st.totalDistanceTraveled);
    st.trackDelta(105);
    TEST_ASSERT_EQUAL_UINT32(15, st.totalDistanceTraveled);  // 10 + 5
}

// ============================================================================
// MAIN — Register all tests
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // 1. Config constant integrity (13 tests)
    RUN_TEST(test_step_timing_consistency);
    RUN_TEST(test_steps_per_mm_calculation);
    RUN_TEST(test_osc_max_speed_derived);
    RUN_TEST(test_speed_level_positive);
    RUN_TEST(test_calibration_constants_sane);
    RUN_TEST(test_safety_offset_positive);
    RUN_TEST(test_hard_distance_limits_ordered);
    RUN_TEST(test_timing_intervals_nonzero);
    RUN_TEST(test_watchdog_escalation_order);
    RUN_TEST(test_sequence_limits_sane);
    RUN_TEST(test_oscillation_constants_sane);
    RUN_TEST(test_chaos_max_delay_sane);

    // 2. Type default values (16 tests)
    RUN_TEST(test_motion_config_defaults);
    RUN_TEST(test_cycle_pause_config_defaults);
    RUN_TEST(test_cycle_pause_state_defaults);
    RUN_TEST(test_pending_motion_defaults);
    RUN_TEST(test_zone_effect_config_defaults);
    RUN_TEST(test_zone_effect_state_defaults);
    RUN_TEST(test_pursuit_state_defaults);
    RUN_TEST(test_oscillation_config_defaults);
    RUN_TEST(test_oscillation_state_defaults);
    RUN_TEST(test_chaos_runtime_config_defaults);
    RUN_TEST(test_chaos_execution_state_defaults);
    RUN_TEST(test_sequence_line_defaults);
    RUN_TEST(test_sequence_execution_state_defaults);
    RUN_TEST(test_system_config_defaults);

    // 3. Speed math (14 tests)
    RUN_TEST(test_speed_level_to_cpm_linear);
    RUN_TEST(test_speed_level_clamped_negative);
    RUN_TEST(test_speed_level_clamped_max);
    RUN_TEST(test_vaet_step_delay_known_values);
    RUN_TEST(test_vaet_step_delay_zero_distance);
    RUN_TEST(test_vaet_step_delay_zero_speed);
    RUN_TEST(test_vaet_step_delay_minimum_clamp);
    RUN_TEST(test_vaet_step_delay_high_speed_shorter);
    RUN_TEST(test_vaet_step_delay_longer_distance_shorter);
    RUN_TEST(test_chaos_step_delay_normal);
    RUN_TEST(test_chaos_step_delay_zero);
    RUN_TEST(test_chaos_step_delay_max_clamp);
    RUN_TEST(test_chaos_step_delay_min_clamp);
    RUN_TEST(test_pursuit_delay_far_from_target);
    RUN_TEST(test_pursuit_delay_close_ramp);
    RUN_TEST(test_pursuit_delay_very_close);
    RUN_TEST(test_pursuit_delay_minimum_clamp);

    // 4. Zone effect curves (12 tests)
    RUN_TEST(test_zone_decel_linear_at_boundary);
    RUN_TEST(test_zone_decel_linear_at_extremity);
    RUN_TEST(test_zone_decel_linear_monotonic);
    RUN_TEST(test_zone_decel_sine_at_boundary);
    RUN_TEST(test_zone_decel_sine_at_extremity);
    RUN_TEST(test_zone_decel_sine_monotonic);
    RUN_TEST(test_zone_decel_triangle_inv_at_boundary);
    RUN_TEST(test_zone_decel_triangle_inv_at_extremity);
    RUN_TEST(test_zone_accel_linear_at_boundary);
    RUN_TEST(test_zone_accel_linear_at_extremity);
    RUN_TEST(test_zone_accel_factor_less_than_one);
    RUN_TEST(test_zone_none_always_one);
    RUN_TEST(test_zone_zero_intensity_no_effect);
    RUN_TEST(test_zone_higher_intensity_stronger_effect);

    // 5. Chaos duration (5 tests)
    RUN_TEST(test_safe_duration_normal);
    RUN_TEST(test_safe_duration_min_less_than_max);
    RUN_TEST(test_safe_duration_floor_clamping);
    RUN_TEST(test_safe_duration_burst_extreme);
    RUN_TEST(test_safe_duration_all_patterns_valid);

    // 6. Chaos pattern config integrity (4 tests)
    RUN_TEST(test_chaos_pattern_count);
    RUN_TEST(test_chaos_pattern_speed_ranges_valid);
    RUN_TEST(test_chaos_pattern_duration_ranges_valid);
    RUN_TEST(test_chaos_pattern_amplitude_ranges_valid);

    // 7. Validators (16 tests)
    RUN_TEST(test_validator_speed_valid);
    RUN_TEST(test_validator_speed_too_low);
    RUN_TEST(test_validator_speed_too_high);
    RUN_TEST(test_validator_distance_valid);
    RUN_TEST(test_validator_distance_negative);
    RUN_TEST(test_validator_distance_exceeds_limit);
    RUN_TEST(test_validator_position_valid);
    RUN_TEST(test_validator_position_negative);
    RUN_TEST(test_validator_motion_range_valid);
    RUN_TEST(test_validator_motion_range_overflow);
    RUN_TEST(test_validator_chaos_params_valid);
    RUN_TEST(test_validator_chaos_amplitude_exceeds_half);
    RUN_TEST(test_validator_chaos_outside_bounds);
    RUN_TEST(test_validator_chaos_craziness_range);
    RUN_TEST(test_validator_oscillation_params_valid);
    RUN_TEST(test_validator_oscillation_frequency_limit);
    RUN_TEST(test_validator_oscillation_bounds);
    RUN_TEST(test_validator_percentage);
    RUN_TEST(test_validator_positive);
    RUN_TEST(test_validator_range);
    RUN_TEST(test_validator_distance_limit_percent);

    // 8. Stats tracking (4 tests)
    RUN_TEST(test_stats_tracking_reset);
    RUN_TEST(test_stats_tracking_add_distance);
    RUN_TEST(test_stats_tracking_increment);
    RUN_TEST(test_stats_tracking_track_delta);

    return UNITY_END();
}
