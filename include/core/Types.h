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

// ============================================================================
// SYSTEM STATE ENUMS
// ============================================================================

enum SystemState {
  STATE_INIT,
  STATE_CALIBRATING,
  STATE_READY,
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_ERROR
};

enum ExecutionContext {
  CONTEXT_STANDALONE,  // Manual execution from UI tab
  CONTEXT_SEQUENCER    // Automatic execution from sequencer
};

enum MovementType {
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
  bool enabled;              // Pause activée/désactivée
  float pauseDurationSec;    // Durée fixe en secondes (si !isRandom)
  bool isRandom;             // Mode aléatoire activé
  float minPauseSec;         // Borne minimum si aléatoire
  float maxPauseSec;         // Borne maximum si aléatoire
  
  CyclePauseConfig() :
    enabled(false),
    pauseDurationSec(1.5),
    isRandom(false),
    minPauseSec(1.5),
    maxPauseSec(5.0) {}
};

struct CyclePauseState {
  bool isPausing;            // En cours de pause actuellement
  unsigned long pauseStartMs; // Timestamp début de pause
  unsigned long currentPauseDuration; // Durée pause actuelle (ms)
  
  CyclePauseState() :
    isPausing(false),
    pauseStartMs(0),
    currentPauseDuration(0) {}
};

// ============================================================================
// VA-ET-VIENT STRUCTURES
// ============================================================================

struct MotionConfig {
  float startPositionMM;
  float targetDistanceMM;
  float speedLevelForward;
  float speedLevelBackward;
  CyclePauseConfig cyclePause;  // Pause entre cycles
  
  MotionConfig() : 
    startPositionMM(0.0),
    targetDistanceMM(50.0),
    speedLevelForward(5.0),
    speedLevelBackward(5.0) {}
};

struct PendingMotionConfig {
  float startPositionMM;
  float distanceMM;
  float speedLevelForward;
  float speedLevelBackward;
  bool hasChanges;
  
  PendingMotionConfig() : 
    startPositionMM(0),
    distanceMM(0),
    speedLevelForward(0),
    speedLevelBackward(0),
    hasChanges(false) {}
};

// ============================================================================
// DECELERATION ZONE
// ============================================================================

enum DecelMode {
  DECEL_LINEAR = 0,           // Linear: constant deceleration rate
  DECEL_SINE = 1,             // Sinusoidal: smooth curve (max slowdown at contact)
  DECEL_TRIANGLE_INV = 2,     // Triangle inverted: weak at start, strong at end
  DECEL_SINE_INV = 3          // Sine inverted: weak at start, strong at end
};

struct DecelZoneConfig {
  bool enabled;
  bool enableStart;
  bool enableEnd;
  float zoneMM;
  float effectPercent;
  DecelMode mode;
  
  DecelZoneConfig() :
    enabled(false),
    enableStart(true),
    enableEnd(true),
    zoneMM(50.0),
    effectPercent(75.0),
    mode(DECEL_SINE) {}
};

// ============================================================================
// PURSUIT MODE
// ============================================================================

struct PursuitState {
  long targetStep;
  long lastTargetStep;
  float maxSpeedLevel;
  float lastMaxSpeedLevel;
  unsigned long stepDelay;
  bool isMoving;
  bool direction;
  
  PursuitState() :
    targetStep(0),
    lastTargetStep(0),
    maxSpeedLevel(10.0),
    lastMaxSpeedLevel(10.0),
    stepDelay(1000),
    isMoving(false),
    direction(true) {}
};

// ============================================================================
// OSCILLATION MODE
// ============================================================================

enum OscillationWaveform {
  OSC_SINE = 0,      // Smooth sinusoidal wave
  OSC_TRIANGLE = 1,  // Linear triangle wave
  OSC_SQUARE = 2     // Square wave (instant direction change)
};

enum RampType {
  RAMP_LINEAR = 0
};

struct OscillationConfig {
  float centerPositionMM;       // Center position for oscillation
  float amplitudeMM;            // Amplitude (±amplitude from center)
  OscillationWaveform waveform; // Waveform type
  float frequencyHz;            // Oscillation frequency (Hz)
  
  // Ramp in/out for smooth transitions
  bool enableRampIn;
  float rampInDurationMs;
  RampType rampInType;
  
  bool enableRampOut;
  float rampOutDurationMs;
  RampType rampOutType;
  
  int cycleCount;              // Number of cycles (0 = infinite)
  bool returnToCenter;         // Return to center after completion
  
  CyclePauseConfig cyclePause; // Pause entre cycles
  
  OscillationConfig() :
    centerPositionMM(0),
    amplitudeMM(20.0),
    waveform(OSC_SINE),
    frequencyHz(0.5),
    enableRampIn(true),
    rampInDurationMs(2000.0),
    rampInType(RAMP_LINEAR),
    enableRampOut(true),
    rampOutDurationMs(2000.0),
    rampOutType(RAMP_LINEAR),
    cycleCount(0),
    returnToCenter(true) {}
};

struct OscillationState {
  unsigned long startTimeMs;    // Start time for phase tracking
  unsigned long rampStartMs;    // Ramp start time
  float currentAmplitude;       // Current amplitude (for ramping)
  int completedCycles;          // Completed full cycles
  bool isRampingIn;             // Currently ramping in
  bool isRampingOut;            // Currently ramping out
  bool isReturning;             // Returning to center
  bool isInitialPositioning;    // Moving to starting position (before oscillation starts)
  
  // Frequency transition (smooth frequency changes with phase continuity)
  bool isTransitioning;         // Currently transitioning between frequencies
  unsigned long transitionStartMs;  // Transition start time
  float oldFrequencyHz;         // Previous frequency
  float targetFrequencyHz;      // Target frequency
  float accumulatedPhase;       // Accumulated phase (0.0 to infinity) for smooth transitions
  unsigned long lastPhaseUpdateMs;  // Last time phase was updated
  float lastPhase;              // Last phase value (for cycle counting)
  
  // Center position transition (smooth center changes)
  bool isCenterTransitioning;   // Currently transitioning to new center position
  unsigned long centerTransitionStartMs;  // Center transition start time
  float oldCenterMM;            // Previous center position
  float targetCenterMM;         // Target center position
  
  // Amplitude transition (smooth amplitude changes)
  bool isAmplitudeTransitioning;  // Currently transitioning to new amplitude
  unsigned long amplitudeTransitionStartMs;  // Amplitude transition start time
  float oldAmplitudeMM;         // Previous amplitude
  float targetAmplitudeMM;      // Target amplitude
  
  OscillationState() :
    startTimeMs(0),
    rampStartMs(0),
    currentAmplitude(0),
    completedCycles(0),
    isRampingIn(false),
    isRampingOut(false),
    isReturning(false),
    isInitialPositioning(false),
    isTransitioning(false),
    transitionStartMs(0),
    oldFrequencyHz(0),
    targetFrequencyHz(0),
    accumulatedPhase(0.0),
    lastPhaseUpdateMs(0),
    lastPhase(0.0),
    isCenterTransitioning(false),
    centerTransitionStartMs(0),
    oldCenterMM(0),
    targetCenterMM(0),
    isAmplitudeTransitioning(false),
    amplitudeTransitionStartMs(0),
    oldAmplitudeMM(0),
    targetAmplitudeMM(0) {}
};

// ============================================================================
// CHAOS MODE
// ============================================================================

enum ChaosPattern {
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
  float centerPositionMM;      // Center position for chaos movements
  float amplitudeMM;           // Maximum deviation from center (±)
  float maxSpeedLevel;         // Maximum speed level (1-MAX_SPEED_LEVEL)
  unsigned long durationSeconds; // Total duration (0 = infinite)
  unsigned long seed;          // Random seed (0 = use micros())
  float crazinessPercent;      // Degree of madness 0-100% (affects speed, duration, jump size)
  bool patternsEnabled[11];    // Enable/disable each pattern (ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR)
  
  ChaosRuntimeConfig() : 
    centerPositionMM(110.0), 
    amplitudeMM(50.0), 
    maxSpeedLevel(5.0),
    durationSeconds(0),
    seed(0),
    crazinessPercent(50.0) {
      // Enable all patterns by default
      for (int i = 0; i < 11; i++) {
        patternsEnabled[i] = true;
      }
    }
};

struct ChaosExecutionState {
  bool isRunning;
  ChaosPattern currentPattern;
  unsigned long startTime;           // Chaos mode start time
  unsigned long nextPatternChangeTime; // When to generate next pattern
  float targetPositionMM;            // Current target position
  float currentSpeedLevel;           // Current speed being used
  float minReachedMM;                // Minimum position reached
  float maxReachedMM;                // Maximum position reached
  unsigned int patternsExecuted;     // Count of patterns executed
  
  // Continuous motion state (for WAVE, PENDULUM, SPIRAL)
  bool movingForward;                // Direction for continuous patterns
  float waveAmplitude;               // Current amplitude for wave
  float spiralRadius;                // Current radius for spiral
  unsigned long patternStartTime;    // When current pattern started
  
  // PULSE specific state (2-phase: OUT + RETURN)
  bool pulsePhase;                   // false = OUT phase, true = RETURN phase
  float pulseCenterMM;               // Center position to return to
  
  // WAVE specific state (sinusoidal continuous)
  float waveFrequency;               // Frequency in Hz for sinusoidal wave
  
  // CALM specific state (breathing/heartbeat)
  // NOTE: isInPatternPause is INTERNAL to chaos patterns, NOT user pause
  // User pause is controlled by config.currentState == STATE_PAUSED
  bool isInPatternPause;             // Currently in pattern's internal pause phase
  unsigned long pauseStartTime;      // When pattern pause started
  unsigned long pauseDuration;       // Duration of current pattern pause
  
  // BRUTE FORCE specific state (3-phase: fast in, slow out, pause)
  uint8_t brutePhase;                // 0=aller rapide, 1=retour lent, 2=pause
  
  // LIBERATOR specific state (3-phase: slow in, fast out, pause)
  uint8_t liberatorPhase;            // 0=aller lent, 1=retour rapide, 2=pause
  
  // Timing control for non-blocking stepping
  unsigned long stepDelay;           // Microseconds between steps
  unsigned long lastStepMicros;      // Last step timestamp
  
  ChaosExecutionState() : 
    isRunning(false),
    currentPattern(CHAOS_ZIGZAG),
    startTime(0),
    nextPatternChangeTime(0),
    targetPositionMM(0),
    currentSpeedLevel(0),
    minReachedMM(999999),
    maxReachedMM(0),
    patternsExecuted(0),
    movingForward(true),
    waveAmplitude(0),
    spiralRadius(0),
    patternStartTime(0),
    pulsePhase(false),
    pulseCenterMM(0),
    waveFrequency(0),
    isInPatternPause(false),
    pauseStartTime(0),
    pauseDuration(0),
    brutePhase(0),
    liberatorPhase(0),
    stepDelay(1000),
    lastStepMicros(0) {}
};

// ============================================================================
// SEQUENCER
// ============================================================================

struct SequenceLine {
  bool enabled;
  MovementType movementType;  // Type of movement for this line
  
  // VA-ET-VIENT parameters
  float startPositionMM;
  float distanceMM;
  float speedForward;
  float speedBackward;
  bool decelStartEnabled;
  bool decelEndEnabled;
  float decelZoneMM;
  float decelEffectPercent;
  DecelMode decelMode;
  
  // VA-ET-VIENT cycle pause
  bool vaetCyclePauseEnabled;
  bool vaetCyclePauseIsRandom;
  float vaetCyclePauseDurationSec;
  float vaetCyclePauseMinSec;
  float vaetCyclePauseMaxSec;
  
  // OSCILLATION parameters
  float oscCenterPositionMM;
  float oscAmplitudeMM;
  OscillationWaveform oscWaveform;
  float oscFrequencyHz;
  bool oscEnableRampIn;
  bool oscEnableRampOut;
  float oscRampInDurationMs;
  float oscRampOutDurationMs;
  
  // OSCILLATION cycle pause
  bool oscCyclePauseEnabled;
  bool oscCyclePauseIsRandom;
  float oscCyclePauseDurationSec;
  float oscCyclePauseMinSec;
  float oscCyclePauseMaxSec;
  
  // CHAOS parameters
  float chaosCenterPositionMM;
  float chaosAmplitudeMM;
  float chaosMaxSpeedLevel;
  float chaosCrazinessPercent;
  unsigned long chaosDurationSeconds;
  unsigned long chaosSeed;
  bool chaosPatternsEnabled[11];  // ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR
  
  // COMMON parameters
  int cycleCount;
  int pauseAfterMs;
  int lineId;
  
  SequenceLine() :
    enabled(true),
    movementType(MOVEMENT_VAET),
    startPositionMM(0),
    distanceMM(100),
    speedForward(5.0),
    speedBackward(5.0),
    decelStartEnabled(false),
    decelEndEnabled(true),
    decelZoneMM(20.0),
    decelEffectPercent(50.0),
    decelMode(DECEL_SINE),
    vaetCyclePauseEnabled(false),
    vaetCyclePauseIsRandom(false),
    vaetCyclePauseDurationSec(0.0),
    vaetCyclePauseMinSec(0.5),
    vaetCyclePauseMaxSec(3.0),
    oscCenterPositionMM(100.0),
    oscAmplitudeMM(50.0),
    oscWaveform(OSC_SINE),
    oscFrequencyHz(0.5),
    oscEnableRampIn(false),
    oscEnableRampOut(false),
    oscRampInDurationMs(1000.0),
    oscRampOutDurationMs(1000.0),
    oscCyclePauseEnabled(false),
    oscCyclePauseIsRandom(false),
    oscCyclePauseDurationSec(0.0),
    oscCyclePauseMinSec(0.5),
    oscCyclePauseMaxSec(3.0),
    chaosCenterPositionMM(110.0),
    chaosAmplitudeMM(50.0),
    chaosMaxSpeedLevel(10.0),
    chaosCrazinessPercent(50.0),
    chaosDurationSeconds(30),
    chaosSeed(0),
    cycleCount(1),
    pauseAfterMs(0),
    lineId(0) {
      // Initialize all chaos patterns to enabled (11 patterns)
      for (int i = 0; i < 11; i++) {
        chaosPatternsEnabled[i] = true;
      }
    }
};

struct SequenceExecutionState {
  bool isRunning;
  bool isLoopMode;
  int currentLineIndex;
  int currentCycleInLine;
  bool isPaused;
  bool isWaitingPause;
  unsigned long pauseEndTime;
  int loopCount;
  unsigned long sequenceStartTime;
  unsigned long lineStartTime;
  
  SequenceExecutionState() :
    isRunning(false),
    isLoopMode(false),
    currentLineIndex(0),
    currentCycleInLine(0),
    isPaused(false),
    isWaitingPause(false),
    pauseEndTime(0),
    loopCount(0),
    sequenceStartTime(0),
    lineStartTime(0) {}
};

// ============================================================================
// PLAYLIST STRUCTURES
// ============================================================================

#define MAX_PRESETS_PER_MODE 20
#define PLAYLIST_FILE_PATH "/playlists.json"

enum PlaylistMode {
  PLAYLIST_SIMPLE = 0,
  PLAYLIST_OSCILLATION = 1,
  PLAYLIST_CHAOS = 2
};

struct PlaylistPreset {
  int id;
  String name;
  unsigned long timestamp;  // Creation time (epoch seconds)
  PlaylistMode mode;
  String configJson;  // JSON string of the config (flexible storage)
  
  PlaylistPreset() :
    id(0),
    name(""),
    timestamp(0),
    mode(PLAYLIST_SIMPLE),
    configJson("{}") {}
};

#endif // TYPES_H
