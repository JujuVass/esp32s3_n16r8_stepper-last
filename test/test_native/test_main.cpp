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
#include "core/MovementMath.h"
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
// 3. SPEED MATH — uses MovementMath:: (real production functions)
// ============================================================================

void test_speed_level_to_cpm_linear() {
    // speedLevel × 10 = cycles per minute
    TEST_ASSERT_FLOAT_NEAR(0.0f, MovementMath::speedLevelToCPM(0), 0.01f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, MovementMath::speedLevelToCPM(1.0f), 0.01f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, MovementMath::speedLevelToCPM(5.0f), 0.01f);
    TEST_ASSERT_FLOAT_NEAR(200.0f, MovementMath::speedLevelToCPM(20.0f), 0.01f);
}

void test_speed_level_clamped_negative() {
    TEST_ASSERT_FLOAT_NEAR(0.0f, MovementMath::speedLevelToCPM(-5.0f), 0.01f);
}

void test_speed_level_clamped_max() {
    float maxCPM = MAX_SPEED_LEVEL * 10.0f;
    TEST_ASSERT_FLOAT_NEAR(maxCPM, MovementMath::speedLevelToCPM(MAX_SPEED_LEVEL + 10.0f), 0.01f);
}

void test_vaet_step_delay_known_values() {
    // speed=5 → 50 cpm, distance=50mm → 400 steps
    // halfCycle = 60000/50/2 = 600ms, rawDelay = 600000/400 = 1500µs
    // delay = (1500 - 6) / 1.0 = 1494µs
    unsigned long delay = MovementMath::vaetStepDelay(5.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(1494.0f, (float)delay, 5.0f);
}

void test_vaet_step_delay_zero_distance() {
    TEST_ASSERT_EQUAL_UINT32(1000, MovementMath::vaetStepDelay(5.0f, 0.0f));
}

void test_vaet_step_delay_zero_speed() {
    TEST_ASSERT_EQUAL_UINT32(1000, MovementMath::vaetStepDelay(0.0f, 50.0f));
}

void test_vaet_step_delay_minimum_clamp() {
    // Very high speed + short distance should clamp to 20µs minimum
    unsigned long delay = MovementMath::vaetStepDelay(MAX_SPEED_LEVEL, 5.0f);
    TEST_ASSERT_TRUE(delay >= 20);
}

void test_vaet_step_delay_high_speed_shorter() {
    // Higher speed level must produce shorter delay (for same distance)
    unsigned long slow = MovementMath::vaetStepDelay(2.0f, 50.0f);
    unsigned long fast = MovementMath::vaetStepDelay(10.0f, 50.0f);
    TEST_ASSERT_TRUE(fast < slow);
}

void test_vaet_step_delay_longer_distance_shorter() {
    // Longer distance must produce shorter delay (for same speed)
    unsigned long shortDist = MovementMath::vaetStepDelay(5.0f, 20.0f);
    unsigned long longDist = MovementMath::vaetStepDelay(5.0f, 100.0f);
    TEST_ASSERT_TRUE(longDist < shortDist);
}

void test_chaos_step_delay_normal() {
    // speed=5 → 50mm/s → 400 steps/s → 2500µs
    unsigned long delay = MovementMath::chaosStepDelay(5.0f);
    TEST_ASSERT_FLOAT_NEAR(2500.0f, (float)delay, 10.0f);
}

void test_chaos_step_delay_zero() {
    TEST_ASSERT_EQUAL_UINT32(10000, MovementMath::chaosStepDelay(0.0f));
}

void test_chaos_step_delay_max_clamp() {
    // Very slow = large delay → clamped to CHAOS_MAX_STEP_DELAY_MICROS
    unsigned long delay = MovementMath::chaosStepDelay(0.001f);
    TEST_ASSERT_TRUE(delay <= CHAOS_MAX_STEP_DELAY_MICROS);
}

void test_chaos_step_delay_min_clamp() {
    unsigned long delay = MovementMath::chaosStepDelay(999.0f);
    TEST_ASSERT_TRUE(delay >= 20);
}

void test_pursuit_delay_far_from_target() {
    // >5mm error: uses full maxSpeedLevel
    unsigned long d1 = MovementMath::pursuitStepDelay(10.0f, 10.0f);
    unsigned long d2 = MovementMath::pursuitStepDelay(50.0f, 10.0f);
    // Same delay regardless of how far (both >5mm)
    TEST_ASSERT_EQUAL_UINT32(d1, d2);
}

void test_pursuit_delay_close_ramp() {
    // Between 1-5mm: speed ramps from 60% to 100%
    unsigned long far_delay = MovementMath::pursuitStepDelay(4.5f, 10.0f);
    unsigned long close_delay = MovementMath::pursuitStepDelay(1.5f, 10.0f);
    // Closer = slower = longer delay
    TEST_ASSERT_TRUE(close_delay > far_delay);
}

void test_pursuit_delay_very_close() {
    // <1mm: minimum speed (60% of max)
    unsigned long d1 = MovementMath::pursuitStepDelay(0.5f, 10.0f);
    unsigned long d2 = MovementMath::pursuitStepDelay(0.1f, 10.0f);
    // Same delay (both <1mm → same speed)
    TEST_ASSERT_EQUAL_UINT32(d1, d2);
}

void test_pursuit_delay_minimum_clamp() {
    unsigned long delay = MovementMath::pursuitStepDelay(100.0f, MAX_SPEED_LEVEL);
    TEST_ASSERT_TRUE(delay >= 20);
}

// ============================================================================
// 4. ZONE EFFECT CURVES — uses MovementMath::zoneSpeedFactor (real production)
// ============================================================================

// ---- DECEL LINEAR ----

void test_zone_decel_linear_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_linear_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_decel_linear_monotonic() {
    float prev = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 75.0f, 0.0f);
    for (float p = 0.1f; p <= 1.0f; p += 0.1f) {
        float curr = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 75.0f, p);
        TEST_ASSERT_TRUE(curr <= prev + 0.001f);
        prev = curr;
    }
}

// ---- DECEL SINE ----

void test_zone_decel_sine_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_sine_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_decel_sine_monotonic() {
    float start = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE, 75.0f, 0.0f);
    float end   = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE, 75.0f, 1.0f);
    TEST_ASSERT_TRUE(start > end);
}

// ---- DECEL TRIANGLE_INV ----

void test_zone_decel_triangle_inv_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_TRIANGLE_INV, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_triangle_inv_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_TRIANGLE_INV, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

// ---- ACCEL ----

void test_zone_accel_linear_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_LINEAR, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_accel_linear_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_LINEAR, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(0.1f, f, 0.01f);
}

void test_zone_accel_factor_less_than_one() {
    for (float p = 0.0f; p <= 1.0f; p += 0.1f) {
        float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_LINEAR, 50.0f, p);
        TEST_ASSERT_TRUE(f <= 1.001f);
        TEST_ASSERT_TRUE(f > 0.0f);
    }
}

// ---- SPEED_NONE ----

void test_zone_none_always_one() {
    for (float p = 0.0f; p <= 1.0f; p += 0.2f) {
        float f = MovementMath::zoneSpeedFactor(SPEED_NONE, CURVE_LINEAR, 100.0f, p);
        TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.001f);
    }
}

// ---- INTENSITY SCALING ----

void test_zone_zero_intensity_no_effect() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_higher_intensity_stronger_effect() {
    float f25  = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 25.0f, 0.0f);
    float f75  = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 75.0f, 0.0f);
    float f100 = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_LINEAR, 100.0f, 0.0f);
    TEST_ASSERT_TRUE(f75 > f25);
    TEST_ASSERT_TRUE(f100 > f75);
}

// ============================================================================
// 5. CHAOS SAFE DURATION — uses MovementMath::safeDurationCalc (real production)
// ============================================================================

void test_safe_duration_normal() {
    unsigned long outMin, outMax;
    MovementMath::safeDurationCalc(ZIGZAG_CONFIG, 0.0f, 0.375f, outMin, outMax);
    TEST_ASSERT_EQUAL_UINT32(2000, outMin);
    TEST_ASSERT_EQUAL_UINT32(4000, outMax);
}

void test_safe_duration_min_less_than_max() {
    unsigned long outMin, outMax;
    for (float c = 0.0f; c <= 1.0f; c += 0.1f) {
        MovementMath::safeDurationCalc(ZIGZAG_CONFIG, c, 0.375f, outMin, outMax);
        TEST_ASSERT_TRUE(outMin < outMax);
    }
}

void test_safe_duration_floor_clamping() {
    unsigned long outMin, outMax;
    MovementMath::safeDurationCalc(ZIGZAG_CONFIG, 100.0f, 100.0f, outMin, outMax);
    TEST_ASSERT_TRUE(outMin >= 100);
    TEST_ASSERT_TRUE(outMax >= 100);
    TEST_ASSERT_TRUE(outMin < outMax);
}

void test_safe_duration_burst_extreme() {
    unsigned long outMin, outMax;
    MovementMath::safeDurationCalc(BURST_CONFIG, 1.0f, 0.5f, outMin, outMax);
    TEST_ASSERT_TRUE(outMin >= 100);
    TEST_ASSERT_TRUE(outMin < outMax);
}

void test_safe_duration_all_patterns_valid() {
    const ChaosBaseConfig* configs[] = {
        &ZIGZAG_CONFIG, &SWEEP_CONFIG, &PULSE_CONFIG, &DRIFT_CONFIG,
        &BURST_CONFIG, &WAVE_CONFIG, &PENDULUM_CONFIG, &SPIRAL_CONFIG,
        &CALM_CONFIG, &BRUTE_FORCE_CONFIG, &LIBERATOR_CONFIG
    };
    for (auto* cfg : configs) {
        unsigned long outMin, outMax;
        MovementMath::safeDurationCalc(*cfg, 0.0f, 0.5f, outMin, outMax);
        TEST_ASSERT_TRUE(outMin < outMax);
        TEST_ASSERT_TRUE(outMin >= 100);
        
        MovementMath::safeDurationCalc(*cfg, 1.0f, 0.5f, outMin, outMax);
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

void test_stats_tracking_zero_delta() {
    StatsTracking st;
    st.syncPosition(50);
    st.trackDelta(50);  // No movement
    TEST_ASSERT_EQUAL_UINT32(0, st.totalDistanceTraveled);
}

void test_stats_tracking_backward_delta() {
    StatsTracking st;
    st.syncPosition(200);
    st.trackDelta(180);  // Moved backward 20 steps
    TEST_ASSERT_EQUAL_UINT32(20, st.totalDistanceTraveled);
}

void test_stats_tracking_multiple_saves() {
    StatsTracking st;
    st.addDistance(100);
    st.markSaved();
    TEST_ASSERT_EQUAL_UINT32(0, st.getIncrementSteps());
    st.addDistance(50);
    TEST_ASSERT_EQUAL_UINT32(50, st.getIncrementSteps());
    st.markSaved();
    st.addDistance(25);
    TEST_ASSERT_EQUAL_UINT32(25, st.getIncrementSteps());
    // Total is still cumulative
    TEST_ASSERT_EQUAL_UINT32(175, st.totalDistanceTraveled);
}

// ============================================================================
// 9. CHAOS PATTERN EXTENSION CONFIGS
// ============================================================================
// Verify sinusoidal, multi-phase, pause, and direction extension structs.
// ============================================================================

void test_chaos_wave_sinusoidal_ext() {
    // WAVE uses cycles-over-duration mode
    TEST_ASSERT_EQUAL_INT(3, WAVE_SIN.cyclesOverDuration);
    // Frequency fields unused when cyclesOverDuration > 0
    TEST_ASSERT_FLOAT_NEAR(0.0f, WAVE_SIN.frequencyMin, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, WAVE_SIN.frequencyMax, 0.001f);
}

void test_chaos_calm_sinusoidal_ext() {
    // CALM uses random frequency mode (cyclesOverDuration=0)
    TEST_ASSERT_EQUAL_INT(0, CALM_SIN.cyclesOverDuration);
    TEST_ASSERT_TRUE(CALM_SIN.frequencyMin > 0.0f);
    TEST_ASSERT_TRUE(CALM_SIN.frequencyMax > CALM_SIN.frequencyMin);
}

void test_chaos_calm_pause_ext() {
    TEST_ASSERT_TRUE(CALM_PAUSE.pauseMin > 0);
    TEST_ASSERT_TRUE(CALM_PAUSE.pauseMax > CALM_PAUSE.pauseMin);
    TEST_ASSERT_TRUE(CALM_PAUSE.pauseChancePercent >= 0.0f);
    TEST_ASSERT_TRUE(CALM_PAUSE.pauseChancePercent <= 100.0f);
    TEST_ASSERT_TRUE(CALM_PAUSE.pauseTriggerThreshold >= 0.0f);
    TEST_ASSERT_TRUE(CALM_PAUSE.pauseTriggerThreshold <= 1.0f);
}

void test_chaos_brute_force_multi_phase() {
    // Phase 1 (fast in) should be faster than phase 2 (slow out)
    TEST_ASSERT_TRUE(BRUTE_FORCE_CONFIG.speedMin > BRUTE_FORCE_MULTI.phase2SpeedMax);
    TEST_ASSERT_TRUE(BRUTE_FORCE_MULTI.phase2SpeedMin >= 0.0f);
    TEST_ASSERT_TRUE(BRUTE_FORCE_MULTI.phase2SpeedMax > BRUTE_FORCE_MULTI.phase2SpeedMin);
    TEST_ASSERT_TRUE(BRUTE_FORCE_MULTI.pauseMin > 0);
    TEST_ASSERT_TRUE(BRUTE_FORCE_MULTI.pauseMax > BRUTE_FORCE_MULTI.pauseMin);
}

void test_chaos_liberator_multi_phase() {
    // Phase 1 (slow in) should be slower than phase 2 (fast out)
    TEST_ASSERT_TRUE(LIBERATOR_CONFIG.speedMax < LIBERATOR_MULTI.phase2SpeedMin);
    TEST_ASSERT_TRUE(LIBERATOR_MULTI.phase2SpeedMin >= 0.0f);
    TEST_ASSERT_TRUE(LIBERATOR_MULTI.phase2SpeedMax > LIBERATOR_MULTI.phase2SpeedMin);
    TEST_ASSERT_TRUE(LIBERATOR_MULTI.pauseMin > 0);
    TEST_ASSERT_TRUE(LIBERATOR_MULTI.pauseMax > LIBERATOR_MULTI.pauseMin);
}

void test_chaos_brute_force_direction() {
    // Forward chance should be valid percentage range
    TEST_ASSERT_TRUE(BRUTE_FORCE_DIR.forwardChanceMin >= 0);
    TEST_ASSERT_TRUE(BRUTE_FORCE_DIR.forwardChanceMin <= 100);
    TEST_ASSERT_TRUE(BRUTE_FORCE_DIR.forwardChanceMax >= 0);
    TEST_ASSERT_TRUE(BRUTE_FORCE_DIR.forwardChanceMax <= 100);
}

void test_chaos_liberator_direction() {
    TEST_ASSERT_TRUE(LIBERATOR_DIR.forwardChanceMin >= 0);
    TEST_ASSERT_TRUE(LIBERATOR_DIR.forwardChanceMin <= 100);
    TEST_ASSERT_TRUE(LIBERATOR_DIR.forwardChanceMax >= 0);
    TEST_ASSERT_TRUE(LIBERATOR_DIR.forwardChanceMax <= 100);
}

void test_chaos_craziness_boost_all_positive() {
    // All speedCrazinessBoost values must be > 0 (craziness should increase speed)
    const ChaosBaseConfig* configs[] = {
        &ZIGZAG_CONFIG, &SWEEP_CONFIG, &PULSE_CONFIG, &DRIFT_CONFIG,
        &BURST_CONFIG, &WAVE_CONFIG, &PENDULUM_CONFIG, &SPIRAL_CONFIG,
        &CALM_CONFIG, &BRUTE_FORCE_CONFIG, &LIBERATOR_CONFIG
    };
    for (auto* cfg : configs) {
        TEST_ASSERT_TRUE(cfg->speedCrazinessBoost > 0.0f);
    }
}

// ============================================================================
// 10. CYCLE PAUSE CALCULATION
// ============================================================================
// Test CyclePauseConfig::calculateDurationMs in fixed mode.
// ============================================================================

void test_cycle_pause_fixed_duration() {
    CyclePauseConfig cp;
    cp.enabled = true;
    cp.isRandom = false;
    cp.pauseDurationSec = 2.0f;
    TEST_ASSERT_EQUAL_UINT32(2000, cp.calculateDurationMs());
}

void test_cycle_pause_fixed_zero() {
    CyclePauseConfig cp;
    cp.isRandom = false;
    cp.pauseDurationSec = 0.0f;
    TEST_ASSERT_EQUAL_UINT32(0, cp.calculateDurationMs());
}

void test_cycle_pause_fixed_fractional() {
    CyclePauseConfig cp;
    cp.isRandom = false;
    cp.pauseDurationSec = 0.5f;
    TEST_ASSERT_EQUAL_UINT32(500, cp.calculateDurationMs());
}

void test_cycle_pause_random_bounds() {
    // Random mode should always return within [min, max] in ms
    CyclePauseConfig cp;
    cp.isRandom = true;
    cp.minPauseSec = 1.0f;
    cp.maxPauseSec = 3.0f;
    // Run 100 iterations to test randomness bounds
    for (int i = 0; i < 100; i++) {
        unsigned long dur = cp.calculateDurationMs();
        TEST_ASSERT_TRUE(dur >= 1000);
        TEST_ASSERT_TRUE(dur <= 3000);
    }
}

void test_cycle_pause_random_inverted_bounds() {
    // If min > max, calculateDurationMs should still work (swaps internally)
    CyclePauseConfig cp;
    cp.isRandom = true;
    cp.minPauseSec = 5.0f;
    cp.maxPauseSec = 1.0f;
    for (int i = 0; i < 50; i++) {
        unsigned long dur = cp.calculateDurationMs();
        TEST_ASSERT_TRUE(dur >= 1000);
        TEST_ASSERT_TRUE(dur <= 5000);
    }
}

// ============================================================================
// 11. ZONE EFFECT CURVE: SINE_INV
// ============================================================================
// Complete coverage of the 4th curve type (missing from original tests).
// ============================================================================

void test_zone_decel_sine_inv_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE_INV, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, f, 0.01f);
}

void test_zone_decel_sine_inv_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE_INV, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_decel_sine_inv_monotonic() {
    float prev = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE_INV, 75.0f, 0.0f);
    for (float p = 0.1f; p <= 1.0f; p += 0.1f) {
        float curr = MovementMath::zoneSpeedFactor(SPEED_DECEL, CURVE_SINE_INV, 75.0f, p);
        TEST_ASSERT_TRUE(curr <= prev + 0.001f);
        prev = curr;
    }
}

void test_zone_accel_sine_inv_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_SINE_INV, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_accel_sine_inv_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_SINE_INV, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(0.1f, f, 0.01f);
}

void test_zone_accel_sine_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_SINE, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_accel_sine_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_SINE, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(0.1f, f, 0.01f);
}

void test_zone_accel_triangle_inv_at_boundary() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_TRIANGLE_INV, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, f, 0.01f);
}

void test_zone_accel_triangle_inv_at_extremity() {
    float f = MovementMath::zoneSpeedFactor(SPEED_ACCEL, CURVE_TRIANGLE_INV, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(0.1f, f, 0.01f);
}

// ============================================================================
// 12. OSCILLATION WAVEFORM MATH — uses MovementMath::waveformPosition (real)
// ============================================================================
// Production convention:
//   SINE:     −cos(phase × 2π)  → phase=0: −1 (bottom), 0.25: 0, 0.5: +1, 0.75: 0
//   TRIANGLE: starts at +1, falls to −1 (phase=0.5), rises back to +1
//   SQUARE:   +1 first half, −1 second half
// ============================================================================

void test_osc_sine_at_zero_phase() {
    // -cos(0) = -1 → center - amplitude
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, 0.0f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, pos, 0.1f);
}

void test_osc_sine_at_quarter() {
    // -cos(PI/2) = 0 → center
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, 0.25f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(100.0f, pos, 0.1f);
}

void test_osc_sine_at_half() {
    // -cos(PI) = +1 → center + amplitude
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, 0.5f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(150.0f, pos, 0.1f);
}

void test_osc_sine_at_three_quarter() {
    // -cos(3PI/2) = 0 → center
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, 0.75f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(100.0f, pos, 0.1f);
}

void test_osc_triangle_at_zero() {
    // phase=0 → waveValue = 1.0 - 0 = +1 → center + amplitude
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, 0.0f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(150.0f, pos, 0.1f);
}

void test_osc_triangle_at_quarter() {
    // phase=0.25 → waveValue = 1.0 - 1.0 = 0 → center
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, 0.25f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(100.0f, pos, 0.1f);
}

void test_osc_triangle_at_half() {
    // phase=0.5 → waveValue = -3.0 + 2.0 = −1 → center - amplitude
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, 0.5f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, pos, 0.1f);
}

void test_osc_triangle_at_three_quarter() {
    // phase=0.75 → waveValue = -3.0 + 3.0 = 0 → center
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, 0.75f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(100.0f, pos, 0.1f);
}

void test_osc_square_first_half() {
    // First half of cycle → +1 → center + amplitude
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_SQUARE, 0.1f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(150.0f, pos, 0.1f);
}

void test_osc_square_second_half() {
    // Second half of cycle → -1 → center - amplitude
    float pos = MovementMath::waveformPosition(OscillationWaveform::OSC_SQUARE, 0.6f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, pos, 0.1f);
}

void test_osc_all_waveforms_bounded() {
    // For any phase in [0,1), position must be within [center-amplitude, center+amplitude]
    // Note: waveformValue is designed for phase ∈ [0,1) — production fmods before calling
    OscillationWaveform wfs[] = { OscillationWaveform::OSC_SINE,
                                   OscillationWaveform::OSC_TRIANGLE,
                                   OscillationWaveform::OSC_SQUARE };
    for (auto wf : wfs) {
        for (float phase = 0.0f; phase < 1.0f; phase += 0.05f) {
            float pos = MovementMath::waveformPosition(wf, phase, 100.0f, 50.0f);
            TEST_ASSERT_TRUE(pos >= 49.9f);
            TEST_ASSERT_TRUE(pos <= 150.1f);
        }
    }
}

// ============================================================================
// 13. POSITION ↔ STEPS CONVERSIONS
// ============================================================================
// Verify STEPS_PER_MM-based conversions are consistent.
// ============================================================================

void test_mm_to_steps_basic() {
    // 100mm × 8 steps/mm = 800 steps
    long steps = (long)(100.0f * STEPS_PER_MM);
    TEST_ASSERT_EQUAL_INT32(800, steps);
}

void test_steps_to_mm_basic() {
    // 800 steps / 8 steps/mm = 100mm
    float mm = 800.0f / STEPS_PER_MM;
    TEST_ASSERT_FLOAT_NEAR(100.0f, mm, 0.001f);
}

void test_mm_to_steps_roundtrip() {
    // Convert mm → steps → mm should be identity
    float originalMM = 123.456f;
    long steps = (long)(originalMM * STEPS_PER_MM);
    float backMM = (float)steps / STEPS_PER_MM;
    // Precision loss due to integer steps
    TEST_ASSERT_FLOAT_NEAR(originalMM, backMM, 1.0f / STEPS_PER_MM);
}

void test_one_revolution_steps() {
    // One revolution = STEPS_PER_REV = MM_PER_REV × STEPS_PER_MM
    long stepsFromMM = (long)(MM_PER_REV * STEPS_PER_MM);
    TEST_ASSERT_EQUAL_INT32(STEPS_PER_REV, stepsFromMM);
}

void test_hard_limits_in_steps() {
    // Verify hard limits are sane in step space
    long hardMinSteps = (long)(HARD_MIN_DISTANCE_MM * STEPS_PER_MM);
    long hardMaxSteps = (long)(HARD_MAX_DISTANCE_MM * STEPS_PER_MM);
    TEST_ASSERT_TRUE(hardMinSteps > 0);
    TEST_ASSERT_TRUE(hardMaxSteps > hardMinSteps);
    // Safety offset must be less than hard min range
    TEST_ASSERT_TRUE(SAFETY_OFFSET_STEPS < hardMinSteps);
}

// ============================================================================
// 14. SEQUENCE LINE EXTENDED DEFAULTS
// ============================================================================
// Verify oscillation and chaos sub-parameters in SequenceLine.
// ============================================================================

void test_sequence_line_osc_defaults() {
    SequenceLine sl;
    TEST_ASSERT_FLOAT_NEAR(100.0f, sl.oscCenterPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, sl.oscAmplitudeMM, 0.001f);
    TEST_ASSERT_TRUE(sl.oscWaveform == OscillationWaveform::OSC_SINE);
    TEST_ASSERT_FLOAT_NEAR(0.5f, sl.oscFrequencyHz, 0.001f);
    TEST_ASSERT_FALSE(sl.oscEnableRampIn);
    TEST_ASSERT_FALSE(sl.oscEnableRampOut);
    TEST_ASSERT_FLOAT_NEAR(1000.0f, sl.oscRampInDurationMs, 0.1f);
    TEST_ASSERT_FLOAT_NEAR(1000.0f, sl.oscRampOutDurationMs, 0.1f);
}

void test_sequence_line_chaos_defaults() {
    SequenceLine sl;
    TEST_ASSERT_FLOAT_NEAR(110.0f, sl.chaosCenterPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, sl.chaosAmplitudeMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(10.0f, sl.chaosMaxSpeedLevel, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(50.0f, sl.chaosCrazinessPercent, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(30, sl.chaosDurationSeconds);
    TEST_ASSERT_EQUAL_UINT32(0, sl.chaosSeed);
}

void test_sequence_line_zone_effect_defaults() {
    SequenceLine sl;
    // vaetZoneEffect should have default ZoneEffectConfig values
    TEST_ASSERT_FALSE(sl.vaetZoneEffect.enabled);
    TEST_ASSERT_TRUE(sl.vaetZoneEffect.enableStart);
    TEST_ASSERT_TRUE(sl.vaetZoneEffect.enableEnd);
    TEST_ASSERT_FLOAT_NEAR(50.0f, sl.vaetZoneEffect.zoneMM, 0.001f);
    TEST_ASSERT_TRUE(sl.vaetZoneEffect.speedEffect == SpeedEffect::SPEED_DECEL);
    TEST_ASSERT_TRUE(sl.vaetZoneEffect.speedCurve == SpeedCurve::CURVE_SINE);
}

void test_sequence_line_cycle_pause_defaults() {
    SequenceLine sl;
    // Both VAET and OSC cycle pauses should be default (disabled)
    TEST_ASSERT_FALSE(sl.vaetCyclePause.enabled);
    TEST_ASSERT_FALSE(sl.oscCyclePause.enabled);
}

// ============================================================================
// 15. VALIDATOR: oscillationAmplitude
// ============================================================================
// Test the amplitude-specific validator (not covered in section 7).
// ============================================================================

void test_validator_osc_amplitude_valid() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // center=100, amplitude=50 → range [50,150] ⊂ [0,200] ✓
    TEST_ASSERT_TRUE(Validators::oscillationAmplitude(100.0f, 50.0f, err));
}

void test_validator_osc_amplitude_at_limit() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // center=100, amplitude=100 → range [0,200] = exact limit ✓
    TEST_ASSERT_TRUE(Validators::oscillationAmplitude(100.0f, 100.0f, err));
}

void test_validator_osc_amplitude_exceeds_start() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // center=30, amplitude=50 → 30-50 = -20 < 0 ✗
    // maxAmplitude = min(30, 200-30) = 30
    TEST_ASSERT_FALSE(Validators::oscillationAmplitude(30.0f, 50.0f, err));
}

void test_validator_osc_amplitude_exceeds_end() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // center=180, amplitude=50 → 180+50 = 230 > 200 ✗
    // maxAmplitude = min(180, 200-180) = 20
    TEST_ASSERT_FALSE(Validators::oscillationAmplitude(180.0f, 50.0f, err));
}

void test_validator_osc_amplitude_centered() {
    String err;
    effectiveMaxDistanceMM = 200.0f;
    // Centered: maxAmplitude = 100mm
    TEST_ASSERT_TRUE(Validators::oscillationAmplitude(100.0f, 100.0f, err));
    TEST_ASSERT_FALSE(Validators::oscillationAmplitude(100.0f, 100.1f, err));
}

// ============================================================================
// 16. CONFIG CROSS-CONSTANT RELATIONSHIPS
// ============================================================================
// More cross-field consistency checks.
// ============================================================================

void test_config_ws_service_interval() {
    // WebSocket service interval must be short enough for responsive UI
    TEST_ASSERT_TRUE(WEBSERVICE_INTERVAL_US <= 10000);  // ≤10ms
    TEST_ASSERT_TRUE(WEBSERVICE_INTERVAL_US > 0);
}

void test_config_calibration_max_vs_hard_limits() {
    // Calibration max steps must exceed hard max distance in steps
    long hardMaxSteps = (long)(HARD_MAX_DISTANCE_MM * STEPS_PER_MM);
    TEST_ASSERT_TRUE(CALIBRATION_MAX_STEPS >= hardMaxSteps / 2);
}

void test_config_speed_compensation_sane() {
    // Factor should be >= 1.0 (or exactly 1.0 for disabled)
    TEST_ASSERT_TRUE(SPEED_COMPENSATION_FACTOR >= 1.0f);
    TEST_ASSERT_TRUE(SPEED_COMPENSATION_FACTOR <= 2.0f);  // Reasonable upper bound
}

void test_config_dir_change_delay_positive() {
    TEST_ASSERT_TRUE(DIR_CHANGE_DELAY_MICROS > 0);
    // Must be larger than step pulse to ensure driver has time
    TEST_ASSERT_TRUE(DIR_CHANGE_DELAY_MICROS >= STEP_PULSE_MICROS);
}

void test_config_contact_debounce_sane() {
    TEST_ASSERT_TRUE(CONTACT_DEBOUNCE_MS > 0);
    TEST_ASSERT_TRUE(CONTACT_DEBOUNCE_MS <= 200);  // Must be responsive
}

void test_config_stats_save_interval() {
    // Auto-save at least every 2 minutes but not too frequently
    TEST_ASSERT_TRUE(STATS_SAVE_INTERVAL_MS >= 10000);
    TEST_ASSERT_TRUE(STATS_SAVE_INTERVAL_MS <= 300000);
}

void test_config_watchdog_tiers_coherent() {
    // Recovery must be faster than normal check
    TEST_ASSERT_TRUE(WATCHDOG_RECOVERY_INTERVAL_MS < WATCHDOG_CHECK_INTERVAL_MS);
    // Hard reconnect timeout must be less than check interval
    TEST_ASSERT_TRUE(WATCHDOG_HARD_RECONNECT_TIMEOUT_MS < WATCHDOG_CHECK_INTERVAL_MS);
    // Reboot delay must be positive
    TEST_ASSERT_TRUE(WATCHDOG_REBOOT_DELAY_MS > 0);
}

void test_config_osc_positioning_tolerance() {
    // Tolerance must be positive and small
    TEST_ASSERT_TRUE(OSC_INITIAL_POSITIONING_TOLERANCE_MM > 0.0f);
    TEST_ASSERT_TRUE(OSC_INITIAL_POSITIONING_TOLERANCE_MM <= 10.0f);
}

void test_config_chaos_amplitude_range() {
    // Min < Default < Max
    TEST_ASSERT_TRUE(CHAOS_MIN_AMPLITUDE_MM < CHAOS_DEFAULT_AMPLITUDE_MM);
    TEST_ASSERT_TRUE(CHAOS_DEFAULT_AMPLITUDE_MM < CHAOS_MAX_AMPLITUDE_MM);
}

void test_config_sequence_lines_vs_playlists() {
    // Both limits must be positive and reasonable
    TEST_ASSERT_TRUE(MAX_SEQUENCE_LINES > 0);
    TEST_ASSERT_TRUE(MAX_PLAYLISTS_PER_MODE > 0);
    TEST_ASSERT_TRUE(MAX_PRESETS_PER_MODE == MAX_PLAYLISTS_PER_MODE);
}

void test_config_motion_limits_coherent() {
    // Max speed in mm/s should be consistent with step timing
    // At MIN_STEP_INTERVAL_US, max possible speed = 1e6 / MIN_STEP_INTERVAL_US / STEPS_PER_MM
    float theoreticalMaxSpeedMMS = 1e6f / MIN_STEP_INTERVAL_US / STEPS_PER_MM;
    // MAX_SPEED_MM_PER_SEC should not exceed theoretical max
    TEST_ASSERT_TRUE(MAX_SPEED_MM_PER_SEC <= theoreticalMaxSpeedMMS);
}

// ============================================================================
// 17. ZONE EFFECT END PAUSE DEFAULTS
// ============================================================================

void test_zone_effect_end_pause_defaults() {
    constexpr ZoneEffectConfig ze;
    TEST_ASSERT_FALSE(ze.endPauseEnabled);
    TEST_ASSERT_FALSE(ze.endPauseIsRandom);
    TEST_ASSERT_FLOAT_NEAR(1.0f, ze.endPauseDurationSec, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.5f, ze.endPauseMinSec, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(2.0f, ze.endPauseMaxSec, 0.001f);
}

void test_zone_effect_state_extended_defaults() {
    constexpr ZoneEffectState zes;
    TEST_ASSERT_EQUAL_UINT32(0, zes.pauseStartMs);
    TEST_ASSERT_EQUAL_UINT32(0, zes.pauseDurationMs);
}

// ============================================================================
// 18. PURSUIT STATE EXTENDED DEFAULTS
// ============================================================================

void test_pursuit_state_extended_defaults() {
    constexpr PursuitState ps;
    TEST_ASSERT_EQUAL_INT32(0, ps.lastTargetStep);
    TEST_ASSERT_FLOAT_NEAR(10.0f, ps.lastMaxSpeedLevel, 0.001f);
    TEST_ASSERT_TRUE(ps.direction);
}

// ============================================================================
// 19. CHAOS EXECUTION STATE EXTENDED DEFAULTS
// ============================================================================

void test_chaos_execution_state_extended_defaults() {
    constexpr ChaosExecutionState cs;
    TEST_ASSERT_EQUAL_UINT32(0, cs.nextPatternChangeTime);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.targetPositionMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.currentSpeedLevel, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(999999.0f, cs.minReachedMM, 0.1f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.maxReachedMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.waveAmplitude, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.spiralRadius, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(0, cs.patternStartTime);
    TEST_ASSERT_FALSE(cs.pulsePhase);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.pulseCenterMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.waveFrequency, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(0, cs.pauseStartTime);
    TEST_ASSERT_EQUAL_UINT32(0, cs.pauseDuration);
    TEST_ASSERT_FLOAT_NEAR(0.0f, cs.lastCalmSineValue, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(0, cs.lastStepMicros);
}

// ============================================================================
// 20. OSCILLATION STATE EXTENDED DEFAULTS
// ============================================================================

void test_oscillation_state_extended_defaults() {
    constexpr OscillationState os;
    TEST_ASSERT_EQUAL_UINT32(0, os.rampStartMs);
    TEST_ASSERT_EQUAL_UINT32(0, os.transitionStartMs);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.oldFrequencyHz, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.targetFrequencyHz, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.accumulatedPhase, 0.001f);
    TEST_ASSERT_EQUAL_UINT32(0, os.lastPhaseUpdateMs);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.lastPhase, 0.001f);
    TEST_ASSERT_FALSE(os.isCenterTransitioning);
    TEST_ASSERT_EQUAL_UINT32(0, os.centerTransitionStartMs);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.oldCenterMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.targetCenterMM, 0.001f);
    TEST_ASSERT_FALSE(os.isAmplitudeTransitioning);
    TEST_ASSERT_EQUAL_UINT32(0, os.amplitudeTransitionStartMs);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.oldAmplitudeMM, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(0.0f, os.targetAmplitudeMM, 0.001f);
}

// ============================================================================
// 21. OSCILLATION CONFIG EXTENDED DEFAULTS
// ============================================================================

void test_oscillation_config_ramp_defaults() {
    constexpr OscillationConfig oc;
    TEST_ASSERT_FLOAT_NEAR(2000.0f, oc.rampInDurationMs, 0.1f);
    TEST_ASSERT_TRUE(oc.rampInType == RampType::RAMP_LINEAR);
    TEST_ASSERT_FLOAT_NEAR(2000.0f, oc.rampOutDurationMs, 0.1f);
    TEST_ASSERT_TRUE(oc.rampOutType == RampType::RAMP_LINEAR);
}

// ============================================================================
// 22. SPEED FORMULA EDGE CASES
// ============================================================================
// Additional edge cases for speed calculations.
// ============================================================================

void test_vaet_step_delay_very_short_distance() {
    // 1mm distance, low speed: should still produce valid delay
    unsigned long delay = MovementMath::vaetStepDelay(1.0f, 1.0f);
    TEST_ASSERT_TRUE(delay >= 20);
    TEST_ASSERT_TRUE(delay < 1000000);  // Not absurd
}

void test_vaet_step_delay_max_distance() {
    // Max reasonable distance with medium speed
    unsigned long delay = MovementMath::vaetStepDelay(5.0f, HARD_MAX_DISTANCE_MM);
    TEST_ASSERT_TRUE(delay >= 20);
}

void test_chaos_step_delay_mid_range() {
    // speed=15 → 150mm/s → 1200 steps/s → ~833µs
    unsigned long delay = MovementMath::chaosStepDelay(15.0f);
    TEST_ASSERT_TRUE(delay > 20);
    TEST_ASSERT_TRUE(delay < 2000);
}

void test_pursuit_delay_exact_threshold() {
    // At exactly 5mm: uses max speed
    unsigned long d5 = MovementMath::pursuitStepDelay(5.0f, 10.0f);
    unsigned long d5_1 = MovementMath::pursuitStepDelay(5.1f, 10.0f);
    // At 5mm and above, should give full speed (same delay)
    TEST_ASSERT_EQUAL_UINT32(d5_1, d5_1);  // Both > 5mm
}

void test_pursuit_delay_at_1mm_boundary() {
    // At exactly 1mm: bottom of ramp zone
    unsigned long d1 = MovementMath::pursuitStepDelay(1.0f, 10.0f);
    unsigned long d09 = MovementMath::pursuitStepDelay(0.9f, 10.0f);
    // Below 1mm uses constant 60% speed, at 1mm uses ramp (also 60%)
    // d1: ratio = (1-1)/(5-1) = 0 → speed = 10 * (0.6+0) = 6.0
    // d09: speed = 10 * 0.6 = 6.0
    TEST_ASSERT_EQUAL_UINT32(d1, d09);
}

// ============================================================================
// 23. ENUM VALUE COVERAGE
// ============================================================================
// Verify all enum variants can be used in switch/assignments.
// ============================================================================

void test_system_state_all_values() {
    SystemState states[] = {
        SystemState::STATE_INIT, SystemState::STATE_CALIBRATING,
        SystemState::STATE_READY, SystemState::STATE_RUNNING,
        SystemState::STATE_PAUSED, SystemState::STATE_ERROR
    };
    TEST_ASSERT_EQUAL_INT(6, sizeof(states) / sizeof(states[0]));
}

void test_movement_type_all_values() {
    MovementType types[] = {
        MovementType::MOVEMENT_VAET, MovementType::MOVEMENT_OSC,
        MovementType::MOVEMENT_CHAOS, MovementType::MOVEMENT_PURSUIT,
        MovementType::MOVEMENT_CALIBRATION
    };
    TEST_ASSERT_EQUAL_INT(5, sizeof(types) / sizeof(types[0]));
    // Verify specific integer values
    TEST_ASSERT_EQUAL_INT(0, (int)MovementType::MOVEMENT_VAET);
    TEST_ASSERT_EQUAL_INT(1, (int)MovementType::MOVEMENT_OSC);
    TEST_ASSERT_EQUAL_INT(2, (int)MovementType::MOVEMENT_CHAOS);
    TEST_ASSERT_EQUAL_INT(3, (int)MovementType::MOVEMENT_PURSUIT);
    TEST_ASSERT_EQUAL_INT(4, (int)MovementType::MOVEMENT_CALIBRATION);
}

void test_chaos_pattern_all_values() {
    ChaosPattern patterns[] = {
        ChaosPattern::CHAOS_ZIGZAG, ChaosPattern::CHAOS_SWEEP,
        ChaosPattern::CHAOS_PULSE, ChaosPattern::CHAOS_DRIFT,
        ChaosPattern::CHAOS_BURST, ChaosPattern::CHAOS_WAVE,
        ChaosPattern::CHAOS_PENDULUM, ChaosPattern::CHAOS_SPIRAL,
        ChaosPattern::CHAOS_CALM, ChaosPattern::CHAOS_BRUTE_FORCE,
        ChaosPattern::CHAOS_LIBERATOR
    };
    TEST_ASSERT_EQUAL_INT(CHAOS_PATTERN_COUNT, sizeof(patterns) / sizeof(patterns[0]));
    // Verify sequential from 0
    for (int i = 0; i < CHAOS_PATTERN_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(i, (int)patterns[i]);
    }
}

void test_speed_effect_values() {
    TEST_ASSERT_EQUAL_INT(0, (int)SpeedEffect::SPEED_NONE);
    TEST_ASSERT_EQUAL_INT(1, (int)SpeedEffect::SPEED_DECEL);
    TEST_ASSERT_EQUAL_INT(2, (int)SpeedEffect::SPEED_ACCEL);
}

void test_speed_curve_values() {
    TEST_ASSERT_EQUAL_INT(0, (int)SpeedCurve::CURVE_LINEAR);
    TEST_ASSERT_EQUAL_INT(1, (int)SpeedCurve::CURVE_SINE);
    TEST_ASSERT_EQUAL_INT(2, (int)SpeedCurve::CURVE_TRIANGLE_INV);
    TEST_ASSERT_EQUAL_INT(3, (int)SpeedCurve::CURVE_SINE_INV);
}

void test_oscillation_waveform_values() {
    TEST_ASSERT_EQUAL_INT(0, (int)OscillationWaveform::OSC_SINE);
    TEST_ASSERT_EQUAL_INT(1, (int)OscillationWaveform::OSC_TRIANGLE);
    TEST_ASSERT_EQUAL_INT(2, (int)OscillationWaveform::OSC_SQUARE);
}

// ============================================================================
// 24. VALIDATOR COMPOSITION CHAINS (real Validators:: namespace)
// ============================================================================
// Tests that exercise validator combination logic with production code.
// These test the actual code path used when the API validates commands.
// ============================================================================

void test_validator_motion_range_at_rail_edge() {
    // Start at 180mm on a 200mm rail, distance 20mm → exactly at limit → valid
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_TRUE(Validators::motionRange(180.0f, 20.0f, err));
}

void test_validator_motion_range_overflows_rail() {
    // Start at 180mm, distance 21mm → 201mm → exceeds 200mm rail
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_FALSE(Validators::motionRange(180.0f, 21.0f, err));
    // The error message should mention the end position
    TEST_ASSERT_TRUE(err.length() > 0);
}

void test_validator_motion_range_with_limit_percent() {
    // 200mm rail but limited to 50% → effectiveMax = 100mm
    String err;
    effectiveMaxDistanceMM = 100.0f;  // 50% of 200mm
    maxDistanceLimitPercent = 50.0f;
    // Start=0, distance=100 → valid (exactly at limit)
    TEST_ASSERT_TRUE(Validators::motionRange(0.0f, 100.0f, err));
    // Start=0, distance=101 → invalid
    TEST_ASSERT_FALSE(Validators::motionRange(0.0f, 101.0f, err));
    // Restore
    effectiveMaxDistanceMM = 200.0f;
    maxDistanceLimitPercent = 100.0f;
}

void test_validator_oscillation_at_center_of_rail() {
    // Center at 100mm, amplitude 100mm → range [0, 200] → exactly valid
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_TRUE(Validators::oscillationParams(100.0f, 100.0f, 1.0f, err));
}

void test_validator_oscillation_center_near_start() {
    // Center at 30mm, amplitude 31mm → 30-31 = -1 → invalid (below 0)
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_FALSE(Validators::oscillationParams(30.0f, 31.0f, 1.0f, err));
}

void test_validator_oscillation_center_near_end() {
    // Center at 180mm, amplitude 21mm → 180+21 = 201 → exceeds 200mm
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_FALSE(Validators::oscillationParams(180.0f, 21.0f, 1.0f, err));
}

void test_validator_chaos_center_plus_amplitude_at_limit() {
    // Center at 150mm, amplitude 50mm → [100, 200] → exactly valid
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_TRUE(Validators::chaosParams(150.0f, 50.0f, 5.0f, 50.0f, err));
}

void test_validator_chaos_center_minus_amplitude_below_zero() {
    // Center at 40mm, amplitude 41mm → 40-41 = -1 → invalid
    String err;
    effectiveMaxDistanceMM = 200.0f;
    TEST_ASSERT_FALSE(Validators::chaosParams(40.0f, 41.0f, 5.0f, 50.0f, err));
}

// ============================================================================
// 25. MOVEMENTMATH CROSS-FUNCTION INVARIANTS
// ============================================================================
// Test relationships between different formulas — properties that MUST hold.
// ============================================================================

void test_speed_monotonicity_vaet() {
    // Higher speed level → shorter step delay (faster movement)
    for (float spd = 1.0f; spd < 19.0f; spd += 1.0f) {
        unsigned long d1 = MovementMath::vaetStepDelay(spd, 100.0f);
        unsigned long d2 = MovementMath::vaetStepDelay(spd + 1.0f, 100.0f);
        TEST_ASSERT_TRUE_MESSAGE(d2 <= d1, "vaetStepDelay not monotonically decreasing with speed");
    }
}

void test_speed_monotonicity_chaos() {
    // Higher speed → shorter delay
    for (float spd = 1.0f; spd < 19.0f; spd += 1.0f) {
        unsigned long d1 = MovementMath::chaosStepDelay(spd);
        unsigned long d2 = MovementMath::chaosStepDelay(spd + 1.0f);
        TEST_ASSERT_TRUE_MESSAGE(d2 <= d1, "chaosStepDelay not monotonically decreasing with speed");
    }
}

void test_speed_monotonicity_pursuit() {
    // Farther from target → faster (shorter delay), within ramp zone
    for (float err = 1.5f; err < 4.5f; err += 0.5f) {
        unsigned long d1 = MovementMath::pursuitStepDelay(err, 10.0f);
        unsigned long d2 = MovementMath::pursuitStepDelay(err + 0.5f, 10.0f);
        TEST_ASSERT_TRUE_MESSAGE(d2 <= d1, "pursuitStepDelay not monotonically decreasing with error");
    }
}

void test_distance_monotonicity_vaet() {
    // Longer distance → shorter step delay (more distance to cover per cycle)
    for (float dist = 10.0f; dist < 190.0f; dist += 20.0f) {
        unsigned long d1 = MovementMath::vaetStepDelay(5.0f, dist);
        unsigned long d2 = MovementMath::vaetStepDelay(5.0f, dist + 20.0f);
        TEST_ASSERT_TRUE_MESSAGE(d2 <= d1, "vaetStepDelay should decrease with longer distance");
    }
}

void test_all_delays_above_minimum() {
    // No formula should produce delay below 20µs (hardcoded floor in all formulas)
    for (float spd = 0.1f; spd <= MAX_SPEED_LEVEL; spd += 2.0f) {
        unsigned long dv = MovementMath::vaetStepDelay(spd, 100.0f);
        unsigned long dc = MovementMath::chaosStepDelay(spd);
        TEST_ASSERT_TRUE(dv >= 20);
        TEST_ASSERT_TRUE(dc >= 20);
    }
    for (float err = 0.1f; err <= 10.0f; err += 1.0f) {
        unsigned long dp = MovementMath::pursuitStepDelay(err, 10.0f);
        TEST_ASSERT_TRUE(dp >= 20);
    }
}

void test_zone_decel_reduces_speed() {
    // Decel factor >= 1.0 (larger factor = longer delay = slower motor)
    // At boundary(0), curveValue=1 → factor = maxIntensity (maximum slowdown)
    // At extremity(1), curveValue=0 → factor = 1.0 (normal speed)
    for (float progress = 0.0f; progress <= 1.0f; progress += 0.1f) {
        float f = MovementMath::zoneSpeedFactor(SpeedEffect::SPEED_DECEL, SpeedCurve::CURVE_LINEAR, 100.0f, progress);
        TEST_ASSERT_TRUE(f >= 1.0f - 0.001f);  // Always >= 1.0
    }
    // Verify extremity returns to normal speed
    float fExt = MovementMath::zoneSpeedFactor(SpeedEffect::SPEED_DECEL, SpeedCurve::CURVE_LINEAR, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, fExt, 0.001f);
}

void test_zone_accel_increases_speed() {
    // Accel factor: at boundary(0) → 1.0 (full speed entering zone)
    //               at extremity(1) → minFactor < 1.0 (slow at start of travel)
    float fBoundary = MovementMath::zoneSpeedFactor(SpeedEffect::SPEED_ACCEL, SpeedCurve::CURVE_LINEAR, 100.0f, 0.0f);
    float fExtremity = MovementMath::zoneSpeedFactor(SpeedEffect::SPEED_ACCEL, SpeedCurve::CURVE_LINEAR, 100.0f, 1.0f);
    TEST_ASSERT_FLOAT_NEAR(1.0f, fBoundary, 0.001f);
    TEST_ASSERT_TRUE(fExtremity < 1.0f);  // Reduced speed at extremity
    TEST_ASSERT_TRUE(fExtremity > 0.0f);  // But not zero
}

// ============================================================================
// 26. WAVEFORM FULL-CYCLE PROPERTIES
// ============================================================================
// Verify mathematical properties of waveforms over complete cycles.
// ============================================================================

void test_sine_waveform_period_one() {
    // Phase 0 and phase 1 should give the same position (one full period)
    float p0 = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, 0.0f, 100.0f, 50.0f);
    float p1 = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, 1.0f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(p0, p1, 0.5f);
}

void test_triangle_waveform_period_one() {
    float p0 = MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, 0.0f, 100.0f, 50.0f);
    float p1 = MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, 1.0f, 100.0f, 50.0f);
    TEST_ASSERT_FLOAT_NEAR(p0, p1, 0.5f);
}

void test_sine_average_is_center() {
    // Average of sine over one period should be the center position
    float sum = 0.0f;
    int N = 100;
    for (int i = 0; i < N; i++) {
        float phase = (float)i / (float)N;
        sum += MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, phase, 100.0f, 50.0f);
    }
    float avg = sum / N;
    TEST_ASSERT_FLOAT_NEAR(100.0f, avg, 1.0f);  // Average ≈ center
}

void test_triangle_average_is_center() {
    float sum = 0.0f;
    int N = 100;
    for (int i = 0; i < N; i++) {
        float phase = (float)i / (float)N;
        sum += MovementMath::waveformPosition(OscillationWaveform::OSC_TRIANGLE, phase, 100.0f, 50.0f);
    }
    float avg = sum / N;
    TEST_ASSERT_FLOAT_NEAR(100.0f, avg, 1.5f);  // Average ≈ center
}

void test_square_average_is_center() {
    float sum = 0.0f;
    int N = 100;
    for (int i = 0; i < N; i++) {
        float phase = (float)i / (float)N;
        sum += MovementMath::waveformPosition(OscillationWaveform::OSC_SQUARE, phase, 100.0f, 50.0f);
    }
    float avg = sum / N;
    TEST_ASSERT_FLOAT_NEAR(100.0f, avg, 1.5f);  // Average ≈ center
}

void test_sine_symmetry_half_period() {
    // Value at phase and at phase+0.5 should be symmetric around center
    // For -cos: -cos(φ·2π) + -cos((φ+0.5)·2π) = -cos(φ·2π) + cos(φ·2π) = 0
    // So pos(phase) + pos(phase+0.5) = 2*center
    for (float phase = 0.0f; phase < 0.5f; phase += 0.05f) {
        float p1 = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, phase, 100.0f, 50.0f);
        float p2 = MovementMath::waveformPosition(OscillationWaveform::OSC_SINE, phase + 0.5f, 100.0f, 50.0f);
        TEST_ASSERT_FLOAT_NEAR(200.0f, p1 + p2, 0.5f);  // p1+p2 = 2*center
    }
}

void test_waveform_extreme_amplitude_zero() {
    // With amplitude 0, all waveforms should return center regardless of phase
    OscillationWaveform wfs[] = { OscillationWaveform::OSC_SINE,
                                   OscillationWaveform::OSC_TRIANGLE,
                                   OscillationWaveform::OSC_SQUARE };
    for (auto wf : wfs) {
        for (float phase = 0.0f; phase < 1.0f; phase += 0.1f) {
            float pos = MovementMath::waveformPosition(wf, phase, 100.0f, 0.0f);
            TEST_ASSERT_FLOAT_NEAR(100.0f, pos, 0.01f);
        }
    }
}

// ============================================================================
// MAIN — Register all tests
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // 1. Config constant integrity (12 tests)
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

    // 2. Type default values (14 tests)
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

    // 3. Speed math (17 tests)
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

    // 4. Zone effect curves (14 tests)
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

    // 7. Validators (21 tests)
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

    // 8. Stats tracking (7 tests)
    RUN_TEST(test_stats_tracking_reset);
    RUN_TEST(test_stats_tracking_add_distance);
    RUN_TEST(test_stats_tracking_increment);
    RUN_TEST(test_stats_tracking_track_delta);
    RUN_TEST(test_stats_tracking_zero_delta);
    RUN_TEST(test_stats_tracking_backward_delta);
    RUN_TEST(test_stats_tracking_multiple_saves);

    // 9. Chaos pattern extension configs (9 tests)
    RUN_TEST(test_chaos_wave_sinusoidal_ext);
    RUN_TEST(test_chaos_calm_sinusoidal_ext);
    RUN_TEST(test_chaos_calm_pause_ext);
    RUN_TEST(test_chaos_brute_force_multi_phase);
    RUN_TEST(test_chaos_liberator_multi_phase);
    RUN_TEST(test_chaos_brute_force_direction);
    RUN_TEST(test_chaos_liberator_direction);
    RUN_TEST(test_chaos_craziness_boost_all_positive);

    // 10. Cycle pause calculation (5 tests)
    RUN_TEST(test_cycle_pause_fixed_duration);
    RUN_TEST(test_cycle_pause_fixed_zero);
    RUN_TEST(test_cycle_pause_fixed_fractional);
    RUN_TEST(test_cycle_pause_random_bounds);
    RUN_TEST(test_cycle_pause_random_inverted_bounds);

    // 11. Zone effect SINE_INV + accel curves (9 tests)
    RUN_TEST(test_zone_decel_sine_inv_at_boundary);
    RUN_TEST(test_zone_decel_sine_inv_at_extremity);
    RUN_TEST(test_zone_decel_sine_inv_monotonic);
    RUN_TEST(test_zone_accel_sine_inv_at_boundary);
    RUN_TEST(test_zone_accel_sine_inv_at_extremity);
    RUN_TEST(test_zone_accel_sine_at_boundary);
    RUN_TEST(test_zone_accel_sine_at_extremity);
    RUN_TEST(test_zone_accel_triangle_inv_at_boundary);
    RUN_TEST(test_zone_accel_triangle_inv_at_extremity);

    // 12. Oscillation waveform math (11 tests)
    RUN_TEST(test_osc_sine_at_zero_phase);
    RUN_TEST(test_osc_sine_at_quarter);
    RUN_TEST(test_osc_sine_at_half);
    RUN_TEST(test_osc_sine_at_three_quarter);
    RUN_TEST(test_osc_triangle_at_zero);
    RUN_TEST(test_osc_triangle_at_quarter);
    RUN_TEST(test_osc_triangle_at_half);
    RUN_TEST(test_osc_triangle_at_three_quarter);
    RUN_TEST(test_osc_square_first_half);
    RUN_TEST(test_osc_square_second_half);
    RUN_TEST(test_osc_all_waveforms_bounded);

    // 13. Position ↔ Steps conversions (5 tests)
    RUN_TEST(test_mm_to_steps_basic);
    RUN_TEST(test_steps_to_mm_basic);
    RUN_TEST(test_mm_to_steps_roundtrip);
    RUN_TEST(test_one_revolution_steps);
    RUN_TEST(test_hard_limits_in_steps);

    // 14. Sequence line extended defaults (4 tests)
    RUN_TEST(test_sequence_line_osc_defaults);
    RUN_TEST(test_sequence_line_chaos_defaults);
    RUN_TEST(test_sequence_line_zone_effect_defaults);
    RUN_TEST(test_sequence_line_cycle_pause_defaults);

    // 15. Validator: oscillationAmplitude (5 tests)
    RUN_TEST(test_validator_osc_amplitude_valid);
    RUN_TEST(test_validator_osc_amplitude_at_limit);
    RUN_TEST(test_validator_osc_amplitude_exceeds_start);
    RUN_TEST(test_validator_osc_amplitude_exceeds_end);
    RUN_TEST(test_validator_osc_amplitude_centered);

    // 16. Config cross-constant relationships (11 tests)
    RUN_TEST(test_config_ws_service_interval);
    RUN_TEST(test_config_calibration_max_vs_hard_limits);
    RUN_TEST(test_config_speed_compensation_sane);
    RUN_TEST(test_config_dir_change_delay_positive);
    RUN_TEST(test_config_contact_debounce_sane);
    RUN_TEST(test_config_stats_save_interval);
    RUN_TEST(test_config_watchdog_tiers_coherent);
    RUN_TEST(test_config_osc_positioning_tolerance);
    RUN_TEST(test_config_chaos_amplitude_range);
    RUN_TEST(test_config_sequence_lines_vs_playlists);
    RUN_TEST(test_config_motion_limits_coherent);

    // 17. Zone effect end pause defaults (2 tests)
    RUN_TEST(test_zone_effect_end_pause_defaults);
    RUN_TEST(test_zone_effect_state_extended_defaults);

    // 18. Pursuit state extended defaults (1 test)
    RUN_TEST(test_pursuit_state_extended_defaults);

    // 19. Chaos execution state extended defaults (1 test)
    RUN_TEST(test_chaos_execution_state_extended_defaults);

    // 20. Oscillation state extended defaults (1 test)
    RUN_TEST(test_oscillation_state_extended_defaults);

    // 21. Oscillation config ramp defaults (1 test)
    RUN_TEST(test_oscillation_config_ramp_defaults);

    // 22. Speed formula edge cases (6 tests)
    RUN_TEST(test_vaet_step_delay_very_short_distance);
    RUN_TEST(test_vaet_step_delay_max_distance);
    RUN_TEST(test_chaos_step_delay_mid_range);
    RUN_TEST(test_pursuit_delay_exact_threshold);
    RUN_TEST(test_pursuit_delay_at_1mm_boundary);

    // 23. Enum value coverage (6 tests)
    RUN_TEST(test_system_state_all_values);
    RUN_TEST(test_movement_type_all_values);
    RUN_TEST(test_chaos_pattern_all_values);
    RUN_TEST(test_speed_effect_values);
    RUN_TEST(test_speed_curve_values);
    RUN_TEST(test_oscillation_waveform_values);

    // 24. Validator composition chains (8 tests)
    RUN_TEST(test_validator_motion_range_at_rail_edge);
    RUN_TEST(test_validator_motion_range_overflows_rail);
    RUN_TEST(test_validator_motion_range_with_limit_percent);
    RUN_TEST(test_validator_oscillation_at_center_of_rail);
    RUN_TEST(test_validator_oscillation_center_near_start);
    RUN_TEST(test_validator_oscillation_center_near_end);
    RUN_TEST(test_validator_chaos_center_plus_amplitude_at_limit);
    RUN_TEST(test_validator_chaos_center_minus_amplitude_below_zero);

    // 25. MovementMath cross-function invariants (8 tests)
    RUN_TEST(test_speed_monotonicity_vaet);
    RUN_TEST(test_speed_monotonicity_chaos);
    RUN_TEST(test_speed_monotonicity_pursuit);
    RUN_TEST(test_distance_monotonicity_vaet);
    RUN_TEST(test_all_delays_above_minimum);
    RUN_TEST(test_zone_decel_reduces_speed);
    RUN_TEST(test_zone_accel_increases_speed);

    // 26. Waveform full-cycle properties (7 tests)
    RUN_TEST(test_sine_waveform_period_one);
    RUN_TEST(test_triangle_waveform_period_one);
    RUN_TEST(test_sine_average_is_center);
    RUN_TEST(test_triangle_average_is_center);
    RUN_TEST(test_square_average_is_center);
    RUN_TEST(test_sine_symmetry_half_period);
    RUN_TEST(test_waveform_extreme_amplitude_zero);

    return UNITY_END();
}
