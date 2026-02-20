// ============================================================================
// CHAOSPATTERNS.H - Chaos Mode Pattern Configurations
// ============================================================================
// All chaos pattern configurations centralized for easy tweaking
// Modify pattern configs to adjust behavior without touching main code
// ============================================================================

#ifndef CHAOS_PATTERNS_H
#define CHAOS_PATTERNS_H

// ============================================================================
// CHAOS PATTERN CONFIGURATION STRUCTURES
// ============================================================================

/**
 * Base configuration for all chaos patterns
 * Contains common parameters: speed, duration, amplitude/jump
 */
struct ChaosBaseConfig {
  float speedMin;                    // Minimum speed (0.0f-1.0f)
  float speedMax;                    // Maximum speed (0.0f-1.0f)
  float speedCrazinessBoost;         // Speed increase per craziness unit
  unsigned long durationMin;         // Minimum pattern duration (ms)
  unsigned long durationMax;         // Maximum pattern duration (ms)
  unsigned long durationCrazinessReduction; // Duration reduction per craziness unit
  float amplitudeJumpMin;            // Minimum amplitude/jump (0.0f-1.0f)
  float amplitudeJumpMax;            // Maximum amplitude/jump (0.0f-1.0f)
};

/**
 * Extension for sinusoidal patterns (WAVE, CALM)
 */
struct ChaosSinusoidalExt {
  float frequencyMin;                // Minimum frequency (Hz)
  float frequencyMax;                // Maximum frequency (Hz)
  int cyclesOverDuration;            // Fixed cycles over duration (0 = use frequencyMin/Max)
};

/**
 * Extension for multi-phase patterns (BRUTE_FORCE, LIBERATOR, PULSE)
 */
struct ChaosMultiPhaseExt {
  float phase2SpeedMin;              // Phase 2 minimum speed (0.0f-1.0f)
  float phase2SpeedMax;              // Phase 2 maximum speed (0.0f-1.0f)
  float phase2SpeedCrazinessBoost;   // Phase 2 speed boost
  unsigned long pauseMin;            // Minimum pause duration (ms)
  unsigned long pauseMax;            // Maximum pause duration (ms)
};

/**
 * Extension for patterns with random pauses (CALM)
 */
struct ChaosPauseExt {
  unsigned long pauseMin;            // Minimum pause duration (ms)
  unsigned long pauseMax;            // Maximum pause duration (ms)
  float pauseChancePercent;          // Pause probability (0-100)
  float pauseTriggerThreshold;       // Sine threshold for pause (0.0f-1.0f)
};

/**
 * Extension for directional patterns (BRUTE_FORCE, LIBERATOR)
 */
struct ChaosDirectionExt {
  int forwardChanceMin;              // Forward probability at 0% craziness (0-100)
  int forwardChanceMax;              // Forward probability at 100% craziness (0-100)
};

// ============================================================================
// PATTERN CONFIGURATION CONSTANTS
// ============================================================================

// ZIGZAG: Rapid random jumps
constexpr ChaosBaseConfig ZIGZAG_CONFIG = {
  .speedMin = 0.40f, .speedMax = 0.70f, .speedCrazinessBoost = 0.30f,
  .durationMin = 2000, .durationMax = 4000, .durationCrazinessReduction = 600,
  .amplitudeJumpMin = 0.60f, .amplitudeJumpMax = 1.00f
};

// SWEEP: Large edge-to-edge sweeps
constexpr ChaosBaseConfig SWEEP_CONFIG = {
  .speedMin = 0.30f, .speedMax = 0.60f, .speedCrazinessBoost = 0.40f,
  .durationMin = 3000, .durationMax = 5000, .durationCrazinessReduction = 1400,
  .amplitudeJumpMin = 0.75f, .amplitudeJumpMax = 1.00f
};

// PULSE: Quick out-and-back pulses
constexpr ChaosBaseConfig PULSE_CONFIG = {
  .speedMin = 0.50f, .speedMax = 0.80f, .speedCrazinessBoost = 0.20f,
  .durationMin = 800, .durationMax = 1500, .durationCrazinessReduction = 400,
  .amplitudeJumpMin = 0.40f, .amplitudeJumpMax = 1.00f
};

// DRIFT: Slow wandering
constexpr ChaosBaseConfig DRIFT_CONFIG = {
  .speedMin = 0.20f, .speedMax = 0.40f, .speedCrazinessBoost = 0.30f,
  .durationMin = 4000, .durationMax = 9000, .durationCrazinessReduction = 1500,
  .amplitudeJumpMin = 0.25f, .amplitudeJumpMax = 0.75f
};

// BURST: Fast explosive jumps
constexpr ChaosBaseConfig BURST_CONFIG = {
  .speedMin = 0.60f, .speedMax = 0.90f, .speedCrazinessBoost = 0.10f,
  .durationMin = 600, .durationMax = 1200, .durationCrazinessReduction = 300,
  .amplitudeJumpMin = 0.70f, .amplitudeJumpMax = 1.00f
};

// WAVE: Sinusoidal continuous motion
constexpr ChaosBaseConfig WAVE_CONFIG = {
  .speedMin = 0.25f, .speedMax = 0.50f, .speedCrazinessBoost = 0.25f,
  .durationMin = 6000, .durationMax = 12000, .durationCrazinessReduction = 2000,
  .amplitudeJumpMin = 0.50f, .amplitudeJumpMax = 1.00f
};
constexpr ChaosSinusoidalExt WAVE_SIN = {
  .frequencyMin = 0.0f, .frequencyMax = 0.0f,  // Calculated from duration
  .cyclesOverDuration = 3
};

// PENDULUM: Regular back-and-forth
constexpr ChaosBaseConfig PENDULUM_CONFIG = {
  .speedMin = 0.30f, .speedMax = 0.60f, .speedCrazinessBoost = 0.30f,
  .durationMin = 5000, .durationMax = 8000, .durationCrazinessReduction = 1200,
  .amplitudeJumpMin = 0.60f, .amplitudeJumpMax = 1.00f
};

// SPIRAL: Progressive in/out
constexpr ChaosBaseConfig SPIRAL_CONFIG = {
  .speedMin = 0.20f, .speedMax = 0.45f, .speedCrazinessBoost = 0.30f,
  .durationMin = 5000, .durationMax = 10000, .durationCrazinessReduction = 2500,
  .amplitudeJumpMin = 0.10f, .amplitudeJumpMax = 1.00f
};

// CALM (BREATHING): Slow sinusoidal with pauses
constexpr ChaosBaseConfig CALM_CONFIG = {
  .speedMin = 0.05f, .speedMax = 0.10f, .speedCrazinessBoost = 0.10f,
  .durationMin = 5000, .durationMax = 8000, .durationCrazinessReduction = 800,
  .amplitudeJumpMin = 0.10f, .amplitudeJumpMax = 0.30f
};
constexpr ChaosSinusoidalExt CALM_SIN = {
  .frequencyMin = 0.2f, .frequencyMax = 1.0f,
  .cyclesOverDuration = 0  // Random frequency (not cycles-based)
};
constexpr ChaosPauseExt CALM_PAUSE = {
  .pauseMin = 500, .pauseMax = 2000,
  .pauseChancePercent = 20.0f,
  .pauseTriggerThreshold = 0.95f
};

// BRUTE_FORCE: Fast in, slow out, pause
constexpr ChaosBaseConfig BRUTE_FORCE_CONFIG = {
  .speedMin = 0.70f, .speedMax = 1.00f, .speedCrazinessBoost = 0.30f,
  .durationMin = 3000, .durationMax = 5000, .durationCrazinessReduction = 750,
  .amplitudeJumpMin = 0.60f, .amplitudeJumpMax = 0.90f
};
constexpr ChaosMultiPhaseExt BRUTE_FORCE_MULTI = {
  .phase2SpeedMin = 0.01f, .phase2SpeedMax = 0.10f, .phase2SpeedCrazinessBoost = 0.09f,
  .pauseMin = 500, .pauseMax = 2000
};
constexpr ChaosDirectionExt BRUTE_FORCE_DIR = {
  .forwardChanceMin = 90, .forwardChanceMax = 60
};

// LIBERATOR: Slow in, fast out, pause
constexpr ChaosBaseConfig LIBERATOR_CONFIG = {
  .speedMin = 0.05f, .speedMax = 0.15f, .speedCrazinessBoost = 0.10f,
  .durationMin = 3000, .durationMax = 5000, .durationCrazinessReduction = 750,
  .amplitudeJumpMin = 0.60f, .amplitudeJumpMax = 0.90f
};
constexpr ChaosMultiPhaseExt LIBERATOR_MULTI = {
  .phase2SpeedMin = 0.70f, .phase2SpeedMax = 1.00f, .phase2SpeedCrazinessBoost = 0.30f,
  .pauseMin = 500, .pauseMax = 2000
};
constexpr ChaosDirectionExt LIBERATOR_DIR = {
  .forwardChanceMin = 90, .forwardChanceMax = 60
};

#endif // CHAOS_PATTERNS_H