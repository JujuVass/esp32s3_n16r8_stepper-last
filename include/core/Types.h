// ============================================================================
// TYPES.H - Data Structures and Enums
// ============================================================================
// All type definitions (enums, structs) centralized for clarity
// Runtime configuration structures with default values in constructors
// ============================================================================
//
// PAUSE ARCHITECTURE (2 levels):
// ═══════════════════════════════════════════════════════════════════════════
//
// Level 1: USER PAUSE (global)
// ─────────────────────────────
//   Source of truth: config.currentState == STATE_PAUSED
//   Triggered by: User clicking "Pause" button
//   Effect: Stops ALL motor movement immediately
//   Scope: Global - affects all movement modes
//   Code: BaseMovement.togglePause() → config.currentState = STATE_PAUSED
//
// Level 2: CYCLE PAUSE (per-mode automatic pauses)
// ─────────────────────────────────────────────────
//   VAET mode: motionPauseState.isPausing (CyclePauseState struct)
//   OSC mode:  oscPauseState.isPausing (CyclePauseState struct)
//   CHAOS mode: chaosState.isInPatternPause (internal to pattern)
//
//   Triggered by: Automatic timing between cycles
//   Effect: Brief pause at cycle boundaries (start/end positions)
//   Scope: Mode-specific - doesn't affect UI state
//
// IMPORTANT: Never use a simple 'isPaused' boolean - always be explicit:
//   - config.currentState == STATE_PAUSED  (user pause)
//   - *PauseState.isPausing                (cycle pause)
//   - chaosState.isInPatternPause          (pattern-internal pause)
// ═══════════════════════════════════════════════════════════════════════════

#ifndef TYPES_H
#define TYPES_H

#include <array>
#include <cstdint>  // For uint8_t

// ============================================================================
// CHAOS PATTERN COUNT (used by structs below and all chaos-related code)
// ============================================================================
constexpr uint8_t CHAOS_PATTERN_COUNT = 11;

// ============================================================================
// SYSTEM STATE ENUMS
// ============================================================================

enum class SystemState {
  STATE_INIT,
  STATE_CALIBRATING,
  STATE_READY,
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_ERROR
};

enum class ExecutionContext {
  CONTEXT_STANDALONE,  // Manual execution from UI tab
  CONTEXT_SEQUENCER    // Automatic execution from sequencer
};

enum class MovementType {
  MOVEMENT_VAET = 0,        // Va-et-vient (back-and-forth)
  MOVEMENT_OSC = 1,         // Oscillation
  MOVEMENT_CHAOS = 2,       // Chaos mode
  MOVEMENT_PURSUIT = 3,     // Real-time position tracking
  MOVEMENT_CALIBRATION = 4  // Full calibration sequence
};

// ============================================================================
// PAUSE BETWEEN CYCLES (Mode Simple + Oscillation)
// ============================================================================

struct CyclePauseConfig {
  bool enabled = false;              // Pause enabled/disabled
  float pauseDurationSec = 1.5f;    // Fixed duration in seconds (if !isRandom)
  bool isRandom = false;             // Random mode enabled
  float minPauseSec = 0.5f;         // Minimum bound if random
  float maxPauseSec = 5.0f;         // Maximum bound if random

  constexpr CyclePauseConfig() = default;

  /** DRY: Calculate pause duration in ms (random or fixed) */
  unsigned long calculateDurationMs() const {
    if (isRandom) {
      float minVal = min(minPauseSec, maxPauseSec);
      float maxVal = max(minPauseSec, maxPauseSec);
      float randomOffset = (float)random(0, 10000) / 10000.0f;
      return (unsigned long)((minVal + randomOffset * (maxVal - minVal)) * 1000);
    }
    return (unsigned long)(pauseDurationSec * 1000);
  }
};

struct CyclePauseState {
  bool isPausing = false;            // Currently pausing
  unsigned long pauseStartMs = 0; // Pause start timestamp
  unsigned long currentPauseDuration = 0; // Current pause duration (ms)

  constexpr CyclePauseState() = default;
};

// ============================================================================
// STATS TRACKING - Distance tracking encapsulation
// volatile fields: written by Core 1 (trackDelta), read by Core 0 (StatusBroadcaster)
// Compound operations (reset, save) must be protected by statsMutex
// ============================================================================

struct StatsTracking {
  volatile unsigned long totalDistanceTraveled = 0;  // Total steps traveled (session)
  volatile unsigned long lastSavedDistance = 0;      // Last saved value (for increment calc)
  volatile long lastStepForDistance = 0;             // Last step position (for delta calc)

  StatsTracking() = default;

  // Reset all counters — CALLER MUST HOLD statsMutex
  void reset() {
    totalDistanceTraveled = 0;
    lastSavedDistance = 0;
  }

  // Add distance traveled (in steps)
  void addDistance(long delta) {
    if (delta > 0) totalDistanceTraveled = totalDistanceTraveled + delta;
  }

  // Get increment since last save (in steps) — CALLER MUST HOLD statsMutex
  unsigned long getIncrementSteps() const {
    return totalDistanceTraveled - lastSavedDistance;
  }

  // Mark current distance as saved — CALLER MUST HOLD statsMutex
  void markSaved() {
    lastSavedDistance = totalDistanceTraveled;
  }

  // Sync lastStepForDistance with current position (Core 1 only, no mutex needed)
  void syncPosition(long currentStep) {
    lastStepForDistance = currentStep;
  }

  // Track distance from last position to current (Core 1 hot path, no mutex needed)
  // Individual 32-bit writes are atomic on Xtensa — safe without mutex
  void trackDelta(long currentStep) {
    long delta = abs(currentStep - lastStepForDistance);
    addDistance(delta);
    lastStepForDistance = currentStep;
  }
};

// ============================================================================
// VA-ET-VIENT STRUCTURES
// ============================================================================

struct MotionConfig {
  float startPositionMM = 0.0f;
  float targetDistanceMM = 50.0f;
  float speedLevelForward = 5.0f;
  float speedLevelBackward = 5.0f;
  CyclePauseConfig cyclePause;  // Inter-cycle pause

  constexpr MotionConfig() = default;
};

struct PendingMotionConfig {
  float startPositionMM = 0;
  float distanceMM = 0;
  float speedLevelForward = 0;
  float speedLevelBackward = 0;
  bool hasChanges = false;

  constexpr PendingMotionConfig() = default;
};

// ============================================================================
// ZONE EFFECTS (replaces DECELERATION ZONE)
// ============================================================================

// Speed effect type (mutually exclusive)
enum class SpeedEffect {
  SPEED_NONE = 0,             // No speed change in zone
  SPEED_DECEL = 1,            // Deceleration (slow down)
  SPEED_ACCEL = 2             // Acceleration (punch effect)
};

// Speed curve type (how the effect is applied)
enum class SpeedCurve {
  CURVE_LINEAR = 0,           // Linear: constant rate
  CURVE_SINE = 1,             // Sinusoidal: smooth S-curve
  CURVE_TRIANGLE_INV = 2,     // Triangle inverted: weak at start, strong at end
  CURVE_SINE_INV = 3          // Sine inverted: weak at start, strong at end
};

struct ZoneEffectConfig {
  // === Zone Settings ===
  bool enabled = false;               // Master enable for zone effects
  bool enableStart = true;            // Apply effects at start position
  bool enableEnd = true;              // Apply effects at end position
  bool mirrorOnReturn = false;        // Physical position mode: zones stay at physical position regardless of direction
  float zoneMM = 50.0f;              // Zone size in mm (10-200)

  // === Speed Effect ===
  SpeedEffect speedEffect = SpeedEffect::SPEED_DECEL;    // NONE, DECEL, ACCEL
  SpeedCurve speedCurve = SpeedCurve::CURVE_SINE;        // Curve type
  float speedIntensity = 75.0f;      // 0-100% intensity

  // === Random Turnback ===
  bool randomTurnbackEnabled = false; // Enable random turnback in zone
  uint8_t turnbackChance = 30;       // 0-100% chance per zone entry

  // === End Pause (like cycle pause) ===
  bool endPauseEnabled = false;       // Enable pause at extremity
  bool endPauseIsRandom = false;      // Fixed or random duration
  float endPauseDurationSec = 1.0f;  // Fixed mode: duration in seconds
  float endPauseMinSec = 0.5f;       // Random mode: minimum duration
  float endPauseMaxSec = 2.0f;       // Random mode: maximum duration

  constexpr ZoneEffectConfig() = default;
};

// Runtime state for zone effects (separated from config for clean copy semantics)
// When SequenceExecutor copies zone config from a line, state is simply reset
struct ZoneEffectState {
  bool hasPendingTurnback = false;       // Turnback decision made for this pass
  bool hasRolledForTurnback = false;     // Already rolled dice for this zone entry
  float turnbackPointMM = 0.0f;         // Where to turn back (distance into zone)
  bool isPausing = false;                // Currently in end pause
  unsigned long pauseStartMs = 0;        // When pause started
  unsigned long pauseDurationMs = 0;     // Current pause duration

  constexpr ZoneEffectState() = default;
};

// ============================================================================
// PURSUIT MODE
// ============================================================================

struct PursuitState {
  long targetStep = 0;
  long lastTargetStep = 0;
  float maxSpeedLevel = 10.0f;
  float lastMaxSpeedLevel = 10.0f;
  unsigned long stepDelay = 1000;
  bool isMoving = false;
  bool direction = true;

  constexpr PursuitState() = default;
};

// ============================================================================
// OSCILLATION MODE
// ============================================================================

enum class OscillationWaveform {
  OSC_SINE = 0,      // Smooth sinusoidal wave
  OSC_TRIANGLE = 1,  // Linear triangle wave
  OSC_SQUARE = 2     // Square wave (instant direction change)
};

enum class RampType {
  RAMP_LINEAR = 0
};

struct OscillationConfig {
  float centerPositionMM = 0;       // Center position for oscillation
  float amplitudeMM = 20.0f;        // Amplitude (±amplitude from center)
  OscillationWaveform waveform = OscillationWaveform::OSC_SINE; // Waveform type
  float frequencyHz = 0.5f;         // Oscillation frequency (Hz)

  // Ramp in/out for smooth transitions
  bool enableRampIn = true;
  float rampInDurationMs = 2000.0f;
  RampType rampInType = RampType::RAMP_LINEAR;

  bool enableRampOut = true;
  float rampOutDurationMs = 2000.0f;
  RampType rampOutType = RampType::RAMP_LINEAR;

  int cycleCount = 0;              // Number of cycles (0 = infinite)
  bool returnToCenter = true;      // Return to center after completion

  CyclePauseConfig cyclePause;     // Inter-cycle pause

  constexpr OscillationConfig() = default;
};

struct OscillationState {
  unsigned long startTimeMs = 0;    // Start time for phase tracking
  unsigned long rampStartMs = 0;    // Ramp start time
  float currentAmplitude = 0;       // Current amplitude (for ramping)
  int completedCycles = 0;          // Completed full cycles
  bool isRampingIn = false;         // Currently ramping in
  bool isRampingOut = false;        // Currently ramping out
  bool isReturning = false;         // Returning to center
  bool isInitialPositioning = false; // Moving to starting position (before oscillation starts)

  // Frequency transition (smooth frequency changes with phase continuity)
  bool isTransitioning = false;         // Currently transitioning between frequencies
  unsigned long transitionStartMs = 0;  // Transition start time
  float oldFrequencyHz = 0;         // Previous frequency
  float targetFrequencyHz = 0;      // Target frequency
  float accumulatedPhase = 0.0f;    // Accumulated phase (0.0 to infinity) for smooth transitions
  unsigned long lastPhaseUpdateMs = 0;  // Last time phase was updated
  float lastPhase = 0.0f;           // Last phase value (for cycle counting)

  // Center position transition (smooth center changes)
  bool isCenterTransitioning = false;   // Currently transitioning to new center position
  unsigned long centerTransitionStartMs = 0;  // Center transition start time
  float oldCenterMM = 0;            // Previous center position
  float targetCenterMM = 0;         // Target center position

  // Amplitude transition (smooth amplitude changes)
  bool isAmplitudeTransitioning = false;  // Currently transitioning to new amplitude
  unsigned long amplitudeTransitionStartMs = 0;  // Amplitude transition start time
  float oldAmplitudeMM = 0;         // Previous amplitude
  float targetAmplitudeMM = 0;      // Target amplitude

  constexpr OscillationState() = default;
};

// ============================================================================
// CHAOS MODE
// ============================================================================

enum class ChaosPattern {
  CHAOS_ZIGZAG = 0,       // Rapid back-and-forth with random targets (12%)
  CHAOS_SWEEP = 1,        // Smooth sweeps across range (12%)
  CHAOS_PULSE = 2,        // Quick pulses from center (8%)
  CHAOS_DRIFT = 3,        // Slow wandering movements (8%)
  CHAOS_BURST = 4,        // High-speed random jumps (5%)
  CHAOS_WAVE = 5,         // Continuous wave-like motion (15%)
  CHAOS_PENDULUM = 6,     // Regular back-and-forth pendulum (12%)
  CHAOS_SPIRAL = 7,       // Progressive spiral in/out (8%)
  CHAOS_CALM = 8,         // Breathing/heartbeat rhythm (10%)
  CHAOS_BRUTE_FORCE = 9,  // Battering ram: fast in, slow out (10%)
  CHAOS_LIBERATOR = 10    // Extraction: slow in, fast out (10%)
};

struct ChaosRuntimeConfig {
  float centerPositionMM = 110.0f;      // Center position for chaos movements
  float amplitudeMM = 50.0f;           // Maximum deviation from center (±)
  float maxSpeedLevel = 5.0f;          // Maximum speed level (1-MAX_SPEED_LEVEL)
  unsigned long durationSeconds = 0;   // Total duration (0 = infinite)
  unsigned long seed = 0;              // Random seed (0 = use micros())
  float crazinessPercent = 50.0f;      // Degree of madness 0-100% (affects speed, duration, jump size)
  std::array<bool, CHAOS_PATTERN_COUNT> patternsEnabled = {true, true, true, true, true, true, true, true, true, true, true};    // Enable/disable each pattern

  constexpr ChaosRuntimeConfig() = default;
};

struct ChaosExecutionState {
  bool isRunning = false;
  ChaosPattern currentPattern = ChaosPattern::CHAOS_ZIGZAG;
  unsigned long startTime = 0;           // Chaos mode start time
  unsigned long nextPatternChangeTime = 0; // When to generate next pattern
  float targetPositionMM = 0;            // Current target position
  float currentSpeedLevel = 0;           // Current speed being used
  float minReachedMM = 999999;           // Minimum position reached
  float maxReachedMM = 0;                // Maximum position reached
  unsigned int patternsExecuted = 0;     // Count of patterns executed

  // Continuous motion state (for WAVE, PENDULUM, SPIRAL)
  bool movingForward = true;             // Direction for continuous patterns
  float waveAmplitude = 0;               // Current amplitude for wave
  float spiralRadius = 0;                // Current radius for spiral
  unsigned long patternStartTime = 0;    // When current pattern started

  // PULSE specific state (2-phase: OUT + RETURN)
  bool pulsePhase = false;               // false = OUT phase, true = RETURN phase
  float pulseCenterMM = 0;               // Center position to return to

  // WAVE specific state (sinusoidal continuous)
  float waveFrequency = 0;               // Frequency in Hz for sinusoidal wave

  // CALM specific state (breathing/heartbeat)
  // NOTE: isInPatternPause is INTERNAL to chaos patterns, NOT user pause
  // User pause is controlled by config.currentState == STATE_PAUSED
  bool isInPatternPause = false;         // Currently in pattern's internal pause phase
  unsigned long pauseStartTime = 0;      // When pattern pause started
  unsigned long pauseDuration = 0;       // Duration of current pattern pause
  float lastCalmSineValue = 0.0f;        // Last sine value for CALM peak detection

  // BRUTE FORCE specific state (3-phase: fast in, slow out, pause)
  uint8_t brutePhase = 0;                // 0=fast forward, 1=slow return, 2=pause

  // LIBERATOR specific state (3-phase: slow in, fast out, pause)
  uint8_t liberatorPhase = 0;            // 0=slow forward, 1=fast return, 2=pause

  // Timing control for non-blocking stepping
  unsigned long stepDelay = 1000;        // Microseconds between steps
  unsigned long lastStepMicros = 0;      // Last step timestamp

  constexpr ChaosExecutionState() = default;
};

// ============================================================================
// SEQUENCER
// ============================================================================

struct SequenceLine {
  bool enabled = true;
  MovementType movementType = MovementType::MOVEMENT_VAET;  // Type of movement for this line

  // VA-ET-VIENT parameters
  float startPositionMM = 0;
  float distanceMM = 100;
  float speedForward = 5.0f;
  float speedBackward = 5.0f;

  // VA-ET-VIENT Zone Effects (DRY: uses same struct as standalone mode)
  // Runtime state is managed separately by zoneEffectState global
  ZoneEffectConfig vaetZoneEffect;

  // VA-ET-VIENT cycle pause (DRY: uses CyclePauseConfig struct)
  CyclePauseConfig vaetCyclePause;

  // OSCILLATION parameters
  float oscCenterPositionMM = 100.0f;
  float oscAmplitudeMM = 50.0f;
  OscillationWaveform oscWaveform = OscillationWaveform::OSC_SINE;
  float oscFrequencyHz = 0.5f;
  bool oscEnableRampIn = false;
  bool oscEnableRampOut = false;
  float oscRampInDurationMs = 1000.0f;
  float oscRampOutDurationMs = 1000.0f;

  // OSCILLATION cycle pause (DRY: uses CyclePauseConfig struct)
  CyclePauseConfig oscCyclePause;

  // CHAOS parameters
  float chaosCenterPositionMM = 110.0f;
  float chaosAmplitudeMM = 50.0f;
  float chaosMaxSpeedLevel = 10.0f;
  float chaosCrazinessPercent = 50.0f;
  unsigned long chaosDurationSeconds = 30;
  unsigned long chaosSeed = 0;
  std::array<bool, CHAOS_PATTERN_COUNT> chaosPatternsEnabled = {true, true, true, true, true, true, true, true, true, true, true};  // Enable/disable each pattern

  // COMMON parameters
  int cycleCount = 1;
  int pauseAfterMs = 0;
  int lineId = 0;

  constexpr SequenceLine() = default;
};

struct SequenceExecutionState {
  bool isRunning = false;
  bool isLoopMode = false;
  int currentLineIndex = 0;
  int currentCycleInLine = 0;
  bool isPaused = false;
  bool isWaitingPause = false;
  unsigned long pauseEndTime = 0;
  int loopCount = 0;
  unsigned long sequenceStartTime = 0;
  unsigned long lineStartTime = 0;

  constexpr SequenceExecutionState() = default;
};

// ============================================================================
// PLAYLIST STRUCTURES
// ============================================================================

constexpr int MAX_PRESETS_PER_MODE = 20;
constexpr const char* PLAYLIST_FILE_PATH = "/playlists.json";

enum class PlaylistMode {
  PLAYLIST_SIMPLE = 0,
  PLAYLIST_OSCILLATION = 1,
  PLAYLIST_CHAOS = 2
};

struct PlaylistPreset {
  int id = 0;
  String name;                // Defaults to empty string
  unsigned long timestamp = 0;  // Creation time (epoch seconds)
  PlaylistMode mode = PlaylistMode::PLAYLIST_SIMPLE;
  String configJson = "{}";  // JSON string of the config (flexible storage)

  PlaylistPreset() = default;
};

#endif // TYPES_H