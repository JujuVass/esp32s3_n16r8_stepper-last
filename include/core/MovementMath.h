// ============================================================================
// MOVEMENT MATH — Pure, testable math functions for movement controllers
// ============================================================================
// Extracted from BaseMovementController, ChaosController, PursuitController,
// and OscillationController so that unit tests exercise the REAL production
// formulas instead of local mirrors.
//
// All functions are free (namespace-scoped), header-only, and depend only on
// Config.h + Types.h + <cmath>.  No hardware, no globals, no side effects.
// ============================================================================

#ifndef MOVEMENT_MATH_H
#define MOVEMENT_MATH_H

#include <cmath>
#include "Config.h"
#include "Types.h"
#include "movement/ChaosPatterns.h"

#ifndef PI
#define PI 3.14159265358979323846
#endif

namespace MovementMath {

// ============================================================================
// 1. SPEED CONVERSION
// ============================================================================

/**
 * Convert speed level (0–MAX_SPEED_LEVEL) to cycles per minute.
 * Clamped to [0, MAX_SPEED_LEVEL × 10].
 */
inline float speedLevelToCPM(float speedLevel) {
    float cpm = speedLevel * 10.0f;
    if (cpm < 0) cpm = 0;
    if (cpm > MAX_SPEED_LEVEL * 10.0f) cpm = MAX_SPEED_LEVEL * 10.0f;
    return cpm;
}

// ============================================================================
// 2. VA-ET-VIENT STEP DELAY
// ============================================================================

/**
 * Pure-math step delay for one direction of va-et-vient movement.
 * Returns delay in µs between steps.
 *
 * @param speedLevel  Speed level (0.1–MAX_SPEED_LEVEL)
 * @param distanceMM  Travel distance in mm
 * @return Step delay in µs (minimum 20, fallback 1000 on bad input)
 */
inline unsigned long vaetStepDelay(float speedLevel, float distanceMM) {
    if (distanceMM <= 0 || speedLevel <= 0) return 1000;

    float cpm = speedLevelToCPM(speedLevel);
    if (cpm <= 0.1f) cpm = 0.1f;

    long stepsPerDirection = (long)(distanceMM * STEPS_PER_MM);
    if (stepsPerDirection <= 0) return 1000;

    float halfCycleMs   = (60000.0f / cpm) / 2.0f;
    float rawDelay      = (halfCycleMs * 1000.0f) / (float)stepsPerDirection;
    float delay         = (rawDelay - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;

    if (delay < 20) delay = 20;
    return (unsigned long)delay;
}

// ============================================================================
// 3. CHAOS STEP DELAY
// ============================================================================

/**
 * Step delay for chaos mode (speed-level → mm/s → steps/s → µs).
 * Clamped to [20, CHAOS_MAX_STEP_DELAY_MICROS].
 */
inline unsigned long chaosStepDelay(float speedLevel) {
    float mmPerSecond   = speedLevel * 10.0f;
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

// ============================================================================
// 4. PURSUIT STEP DELAY
// ============================================================================

/**
 * Proportional step delay for pursuit mode.
 * Speed ramps:  >5 mm → 100%, 1–5 mm → 60–100%, <1 mm → 60% of max.
 * Steps/second clamped to [30, 6000].  Delay minimum 20 µs.
 */
inline unsigned long pursuitStepDelay(float errorMM, float maxSpeedLevel) {
    float speedLevel;
    if (errorMM > 5.0f) {
        speedLevel = maxSpeedLevel;
    } else if (errorMM > 1.0f) {
        float ratio = (errorMM - 1.0f) / (5.0f - 1.0f);
        speedLevel  = maxSpeedLevel * (0.6f + ratio * 0.4f);
    } else {
        speedLevel  = maxSpeedLevel * 0.6f;
    }

    float mmPerSecond    = speedLevel * 10.0f;
    float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
    if (stepsPerSecond < 30)   stepsPerSecond = 30;
    if (stepsPerSecond > 6000) stepsPerSecond = 6000;

    float delayMicros = ((1000000.0f / stepsPerSecond) - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    if (delayMicros < 20) delayMicros = 20;
    return (unsigned long)delayMicros;
}

// ============================================================================
// 5. ZONE EFFECT SPEED FACTOR
// ============================================================================

/**
 * Compute the speed-adjustment factor for a zone-effect position.
 *
 * @param effect       SPEED_NONE / SPEED_DECEL / SPEED_ACCEL
 * @param curve        CURVE_LINEAR / CURVE_SINE / CURVE_TRIANGLE_INV / CURVE_SINE_INV
 * @param intensity    0–100 %
 * @param zoneProgress 0.0 = at zone boundary, 1.0 = at extremity
 * @return factor (1.0 = normal, >1 = slower for DECEL, <1 = faster for ACCEL)
 */
inline float zoneSpeedFactor(SpeedEffect effect, SpeedCurve curve,
                             float intensity, float zoneProgress) {
    if (effect == SpeedEffect::SPEED_NONE) return 1.0f;

    float maxIntensity = 1.0f + (intensity / 100.0f) * 9.0f;
    float curveValue;

    switch (curve) {
        case SpeedCurve::CURVE_LINEAR:
            curveValue = 1.0f - zoneProgress;
            break;
        case SpeedCurve::CURVE_SINE: {
            float sp   = (1.0f - cosf(zoneProgress * (float)PI)) / 2.0f;
            curveValue = 1.0f - sp;
            break;
        }
        case SpeedCurve::CURVE_TRIANGLE_INV: {
            float inv  = 1.0f - zoneProgress;
            curveValue = inv * inv;
            break;
        }
        case SpeedCurve::CURVE_SINE_INV: {
            float inv  = 1.0f - zoneProgress;
            curveValue = sinf(inv * (float)PI / 2.0f);
            break;
        }
        default:
            curveValue = 1.0f - zoneProgress;
            break;
    }

    if (effect == SpeedEffect::SPEED_DECEL) {
        return 1.0f + curveValue * (maxIntensity - 1.0f);
    } else {
        float accelCurve = 1.0f - curveValue;
        float minFactor  = 1.0f / maxIntensity;
        return 1.0f - accelCurve * (1.0f - minFactor);
    }
}

// ============================================================================
// 6. CHAOS SAFE DURATION
// ============================================================================

/**
 * Compute safe [min, max) duration for a chaos pattern, preventing
 * unsigned underflow and guaranteeing min < max for random().
 */
inline void safeDurationCalc(const ChaosBaseConfig& cfg, float craziness, float maxFactor,
                             unsigned long& outMin, unsigned long& outMax) {
    long minVal = (long)cfg.durationMin - (long)(cfg.durationCrazinessReduction * craziness);
    long maxVal = (long)cfg.durationMax - (long)((cfg.durationMax - cfg.durationMin) * craziness * maxFactor);

    if (minVal < 100) minVal = 100;
    if (maxVal < 100) maxVal = 100;
    if (minVal >= maxVal) maxVal = minVal + 100;

    outMin = (unsigned long)minVal;
    outMax = (unsigned long)maxVal;
}

// ============================================================================
// 7. OSCILLATION WAVEFORM
// ============================================================================

/**
 * Compute waveform value for oscillation (−1.0 to +1.0).
 *
 * Convention (matches OscillationController production code):
 *   SINE:     −cos(phase × 2π)   → starts at −1 (bottom)
 *   TRIANGLE: starts at +1, falls to −1 over first half, rises back
 *   SQUARE:   +1 for first half, −1 for second half
 *
 * @param waveform  OSC_SINE / OSC_TRIANGLE / OSC_SQUARE
 * @param phase     0.0–1.0 (one cycle)
 * @return waveform sample in [−1, +1]
 */
inline float waveformValue(OscillationWaveform waveform, float phase) {
    switch (waveform) {
        case OscillationWaveform::OSC_SINE:
            return -cosf(phase * 2.0f * (float)PI);

        case OscillationWaveform::OSC_TRIANGLE:
            if (phase < 0.5f) {
                return 1.0f - (phase * 4.0f);   // +1 → −1
            } else {
                return -3.0f + (phase * 4.0f);   // −1 → +1
            }

        case OscillationWaveform::OSC_SQUARE:
            return (phase < 0.5f) ? 1.0f : -1.0f;

        default:
            return 0.0f;
    }
}

/**
 * Convenience: compute waveform position in mm.
 * @return centerMM + waveformValue(...) × amplitudeMM
 */
inline float waveformPosition(OscillationWaveform waveform, float phase,
                              float centerMM, float amplitudeMM) {
    return centerMM + waveformValue(waveform, phase) * amplitudeMM;
}

} // namespace MovementMath

#endif // MOVEMENT_MATH_H
