/**
 * ============================================================================
 * OscillationController.cpp - Sinusoidal Oscillation Movement Implementation
 * ============================================================================
 * 
 * Extracted from main stepper_controller_restructured.ino
 * Original functions: calculateOscillationPosition(), validateOscillationAmplitude(),
 *                     doOscillationStep(), startOscillation()
 * 
 * Created: December 2024
 * ============================================================================
 */

#include "movement/OscillationController.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"
#include "movement/SequenceExecutor.h"

// ============================================================================
// OSCILLATION STATE - Owned by this module
// ============================================================================
OscillationConfig oscillation;
OscillationState oscillationState;
CyclePauseState oscPauseState;
float actualOscillationSpeedMMS = 0.0;

// ============================================================================
// LOCAL SINE LOOKUP TABLE (for fast oscillation waveform calculation)
// ============================================================================

#ifdef USE_SINE_LOOKUP_TABLE
static float sineTable[SINE_TABLE_SIZE];
static bool sineTableInitialized = false;

static void initSineTable() {
    if (sineTableInitialized) return;
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        sineTable[i] = -cos((i / (float)SINE_TABLE_SIZE) * 2.0 * PI);
    }
    sineTableInitialized = true;
}

// Fast sine lookup with linear interpolation
static float fastSine(float phase) {
    initSineTable();  // Ensure table is initialized
    float indexFloat = phase * SINE_TABLE_SIZE;
    // Safe modulo: C++ signed % can return negative, so double-mod to guarantee positive
    int index = ((int)indexFloat % SINE_TABLE_SIZE + SINE_TABLE_SIZE) % SINE_TABLE_SIZE;
    int nextIndex = (index + 1) % SINE_TABLE_SIZE;
    
    // Linear interpolation for smooth transitions
    float fraction = indexFloat - floorf(indexFloat);
    return sineTable[index] + (sineTable[nextIndex] - sineTable[index]) * fraction;
}
#endif

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

OscillationControllerClass Osc;

// ============================================================================
// LIFECYCLE
// ============================================================================

void OscillationControllerClass::begin() {
    engine->info("🌊 OscillationController initialized");
}

// ============================================================================
// MAIN CONTROL
// ============================================================================

void OscillationControllerClass::start() {
    MutexGuard guard(stateMutex);
    if (!guard) {
        engine->warn("OscillationController::start: mutex timeout");
        return;
    }
    
    // Validate configuration
    String errorMsg;
    if (!validateAmplitude(oscillation.centerPositionMM, oscillation.amplitudeMM, errorMsg)) {
        Status.sendError("❌ " + errorMsg);
        config.currentState = STATE_ERROR;
        return;
    }
    
    // Initialize oscillation state
    oscillationState.startTimeMs = millis();
    oscillationState.rampStartMs = millis();
    oscillationState.currentAmplitude = 0;
    oscillationState.completedCycles = 0;  // ✅ CRITICAL: Reset cycle counter for sequencer
    oscillationState.isRampingIn = oscillation.enableRampIn;
    oscillationState.isRampingOut = false;
    oscillationState.isReturning = false;
    oscillationState.isInitialPositioning = true;  // 🚀 Active le positionnement progressif initial
    
    // 🎯 RESET PHASE TRACKING for smooth transitions
    oscillationState.accumulatedPhase = 0.0;
    oscillationState.lastPhaseUpdateMs = 0;  // Will be initialized on first calculatePosition() call
    oscillationState.lastPhase = 0.0;  // Reset cycle counter tracking
    oscillationState.isTransitioning = false;
    
    // 🎯 RESET CENTER TRANSITION for fresh start
    oscillationState.isCenterTransitioning = false;
    oscillationState.centerTransitionStartMs = 0;
    oscillationState.oldCenterMM = 0;
    oscillationState.targetCenterMM = 0;
    
    // 🎯 RESET AMPLITUDE TRANSITION for fresh start
    oscillationState.isAmplitudeTransitioning = false;
    oscillationState.amplitudeTransitionStartMs = 0;
    oscillationState.oldAmplitudeMM = 0;
    oscillationState.targetAmplitudeMM = 0;
    
    // 🎯 CALCULATE INITIAL ACTUAL SPEED for display
    float theoreticalPeakSpeed = 2.0 * PI * oscillation.frequencyHz * oscillation.amplitudeMM;
    actualSpeedMMS_ = min(theoreticalPeakSpeed, OSC_MAX_SPEED_MM_S);
    
    // NOTE: Don't stop sequencer here - start() can be called BY the sequencer (P4 translation)
    // Conflict safety is handled in WebSocket handlers (startMovement, enablePursuitMode, etc.)
    
    // Start movement (config.currentState is single source of truth for pause state)
    config.currentState = STATE_RUNNING;
    
    // Set movement type (config.executionContext remains unchanged - can be STANDALONE or SEQUENCER)
    currentMovement = MOVEMENT_OSC;
    
    // Reset internal tracking flags
    firstPositioningCall_ = true;
    cyclesCompleteLogged_ = false;
    catchUpWarningLogged_ = false;
    lastStepMicros_ = micros();
    
    String waveformName = "SINE";
    if (oscillation.waveform == OSC_TRIANGLE) waveformName = "TRIANGLE";
    if (oscillation.waveform == OSC_SQUARE) waveformName = "SQUARE";
    
    engine->info(String("✅ Oscillation started:\n") +
          "   Centre: " + String(oscillation.centerPositionMM, 1) + " mm\n" +
          "   Amplitude: ±" + String(oscillation.amplitudeMM, 1) + " mm\n" +
          "   Frequency: " + String(oscillation.frequencyHz, 3) + " Hz\n" +
          "   Waveform: " + waveformName + "\n" +
          "   Ramp in: " + String(oscillation.enableRampIn ? "YES" : "NO") + "\n" +
          "   Ramp out: " + String(oscillation.enableRampOut ? "YES" : "NO"));
}

void OscillationControllerClass::process() {
    // 🆕 Handle inter-cycle pause
    if (handleCyclePause()) {
        return;  // Still in pause
    }
    
    // Handle initial positioning phase
    if (oscillationState.isInitialPositioning) {
        if (handleInitialPositioning()) {
            return;  // Still positioning
        }
    }
    
    // Calculate target position FIRST (this updates completedCycles counter)
    float targetPositionMM = calculatePosition();
    
    // THEN check if cycle count reached (after counter update)
    if (oscillation.cycleCount > 0 && oscillationState.completedCycles >= oscillation.cycleCount) {
        // Log only once when cycles complete
        if (!cyclesCompleteLogged_) {
            engine->debug("✅ OSC: Cycles complete! " + String(oscillationState.completedCycles) + "/" + String(oscillation.cycleCount));
            cyclesCompleteLogged_ = true;
        }
        
        if (oscillation.enableRampOut && !oscillationState.isRampingOut) {
            // Start ramp out
            oscillationState.isRampingOut = true;
            oscillationState.rampStartMs = millis();
            cyclesCompleteLogged_ = false;  // Reset for next oscillation
        } else if (!oscillation.enableRampOut) {
            // Stop immediately (set state to PAUSED)
            config.currentState = STATE_PAUSED;
            
            // NEW ARCHITECTURE: Use unified completion handler
            SeqExecutor.onMovementComplete();
            
            if (oscillation.returnToCenter) {
                oscillationState.isReturning = true;
            }
            cyclesCompleteLogged_ = false;  // Reset for next oscillation
            return;
        }
    }
    
    // Convert to steps
    long targetStep = (long)(targetPositionMM * STEPS_PER_MM);
    
    // Safety check contacts near limits
    if (!checkSafetyContacts(targetStep)) {
        return;  // Contact hit - stop
    }
    
    // Move towards target position
    long errorSteps = targetStep - currentStep;
    
    if (errorSteps == 0) {
        return;  // Already at target
    }
    
    // 🚀 SPEED CALCULATION: Calculate effective frequency (capped if exceeds max speed)
    float effectiveFrequency = oscillation.frequencyHz;
    if (oscillation.amplitudeMM > 0.0) {
        float maxAllowedFreq = OSC_MAX_SPEED_MM_S / (2.0 * PI * oscillation.amplitudeMM);
        if (oscillation.frequencyHz > maxAllowedFreq) {
            effectiveFrequency = maxAllowedFreq;
        }
    }
    
    // Calculate actual peak speed using effective frequency
    actualSpeedMMS_ = 2.0 * PI * effectiveFrequency * oscillation.amplitudeMM;
    
    // Use minimum step delay (speed is controlled by effective frequency, not delay)
    unsigned long currentMicros = micros();
    unsigned long elapsedMicros = currentMicros - lastStepMicros_;
    
    if (elapsedMicros < OSC_MIN_STEP_DELAY_MICROS) {
        return;  // Too early for next step
    }
    
    // Catch-up only on critical error (>3mm), otherwise 1 smooth step following the sine formula
    long absErrorSteps = abs(errorSteps);
    float errorMM = absErrorSteps / STEPS_PER_MM;
    bool isCatchUp = (errorMM > OSC_CATCH_UP_THRESHOLD_MM);
    
    if (isCatchUp && !catchUpWarningLogged_) {
        engine->warn("⚠️ OSC Catch-up enabled: error of " + String(errorMM, 1) + "mm (threshold: " + String(OSC_CATCH_UP_THRESHOLD_MM, 1) + "mm)");
        catchUpWarningLogged_ = true;
    }
    
    // Execute steps
    executeSteps(targetStep, isCatchUp);
    
    lastStepMicros_ = currentMicros;
}

// ============================================================================
// POSITION CALCULATION
// ============================================================================

float OscillationControllerClass::calculatePosition() {
    unsigned long currentMs = millis();
    
    // 🎯 SMOOTH FREQUENCY TRANSITION: Use accumulated phase for perfect continuity
    float effectiveFrequency = oscillation.frequencyHz;
    
    // 🚀 SPEED LIMIT: Cap frequency if theoretical speed exceeds OSC_MAX_SPEED_MM_S (300 mm/s)
    if (oscillation.amplitudeMM > 0.0) {
        float maxAllowedFreq = OSC_MAX_SPEED_MM_S / (2.0 * PI * oscillation.amplitudeMM);
        if (oscillation.frequencyHz > maxAllowedFreq) {
            effectiveFrequency = maxAllowedFreq;
            
            // Log warning (throttled to avoid spam)
            static unsigned long lastSpeedLimitLog = 0;
            if (currentMs - lastSpeedLimitLog > 5000) {
                engine->warn("⚠️ Frequency reduced: " + String(oscillation.frequencyHz, 2) + " Hz → " + 
                      String(effectiveFrequency, 2) + " Hz (max speed: " + 
                      String(OSC_MAX_SPEED_MM_S, 0) + " mm/s)");
                lastSpeedLimitLog = currentMs;
            }
        }
    }
    
    // Initialize phase tracking on first call or after reset
    if (oscillationState.lastPhaseUpdateMs == 0) {
        oscillationState.lastPhaseUpdateMs = currentMs;
        oscillationState.accumulatedPhase = 0.0;
    }
    
    // Calculate time delta since last update
    unsigned long deltaMs = currentMs - oscillationState.lastPhaseUpdateMs;
    
    // CAP deltaMs to prevent phase jumps during CPU load spikes (WebSocket reconnect, etc.)
    // Max 50ms = ~20 Hz update rate minimum, prevents jerky motion
    if (deltaMs > 50) {
        deltaMs = 50;
    }
    
    oscillationState.lastPhaseUpdateMs = currentMs;
    
    if (oscillationState.isTransitioning) {
        unsigned long transitionElapsed = currentMs - oscillationState.transitionStartMs;
        
        if (transitionElapsed < OSC_FREQ_TRANSITION_DURATION_MS) {
            // Linear interpolation of frequency
            float progress = (float)transitionElapsed / (float)OSC_FREQ_TRANSITION_DURATION_MS;
            effectiveFrequency = oscillationState.oldFrequencyHz + 
                                (oscillationState.targetFrequencyHz - oscillationState.oldFrequencyHz) * progress;
            
            // Reduced logging: every 200ms (was 100ms)
            static unsigned long lastTransitionLog = 0;
            if (currentMs - lastTransitionLog > OSC_TRANSITION_LOG_INTERVAL_MS) {
                engine->debug("🔄 Transition: " + String(effectiveFrequency, 3) + " Hz (" + String(progress * 100, 0) + "%)");
                lastTransitionLog = currentMs;
            }
        } else {
            // Transition complete
            oscillationState.isTransitioning = false;
            effectiveFrequency = oscillation.frequencyHz;
            engine->info("✅ Transition complete: " + String(effectiveFrequency, 3) + " Hz");
        }
    }
    
    // 🔥 ACCUMULATE PHASE: Add phase increment based on time delta and current frequency
    // phase increment = frequency (cycles/sec) × time (sec)
    float phaseIncrement = effectiveFrequency * (deltaMs / 1000.0);
    oscillationState.accumulatedPhase += phaseIncrement;
    
    // Calculate phase (0.0 to 1.0 per cycle) using modulo
    float phase = fmod(oscillationState.accumulatedPhase, 1.0);
    
    // Calculate waveform value (-1.0 to +1.0)
    float waveValue = 0.0;
    
    switch (oscillation.waveform) {
        case OSC_SINE:
            // Use -cos to start at maximum (like a wave crest)
            // cos(0) = 1, cos(PI) = -1, cos(2*PI) = 1
            #ifdef USE_SINE_LOOKUP_TABLE
            waveValue = fastSine(phase);  // Lookup table (2µs)
            #else
            waveValue = -cos(phase * 2.0 * PI);  // Hardware FPU (15µs)
            #endif
            break;
            
        case OSC_TRIANGLE:
            // Symmetric triangle: starts at +1, goes to -1, back to +1
            if (phase < 0.5) {
                waveValue = 1.0 - (phase * 4.0);  // Fall: +1 to -1
            } else {
                waveValue = -3.0 + (phase * 4.0);  // Rise: -1 to +1
            }
            break;
            
        case OSC_SQUARE:
            // Square wave: starts at +1, switches to -1 at halfway
            waveValue = (phase < 0.5) ? 1.0 : -1.0;
            break;
    }
    
    // Track completed cycles
    // ⚠️ Don't increment during ramp out - we've already reached target cycle count
    if (!oscillationState.isRampingOut && phase < oscillationState.lastPhase) {  // Cycle wrap-around detected
        oscillationState.completedCycles++;
        engine->debug("🔄 Cycle " + String(oscillationState.completedCycles) + "/" + String(oscillation.cycleCount));
        
        // Check if inter-cycle pause is enabled
        if (oscillation.cyclePause.enabled) {
            oscPauseState.currentPauseDuration = oscillation.cyclePause.calculateDurationMs();
            
            oscPauseState.isPausing = true;
            oscPauseState.pauseStartMs = millis();
            
            engine->debug("⏸️ Pause cycle OSC: " + String(oscPauseState.currentPauseDuration) + "ms");
        }
        
        // Send status update to frontend when cycle completes
        if (config.executionContext == CONTEXT_SEQUENCER) {
            SeqExecutor.sendStatus();
        }
    }
    oscillationState.lastPhase = phase;
    
    // Calculate current amplitude with ramping
    // Ramps apply only at start/end of the entire line (not between cycles)
    float effectiveAmplitude = oscillation.amplitudeMM;
    
    // 🎯 SMOOTH AMPLITUDE TRANSITION: Interpolate amplitude when changed during oscillation
    if (oscillationState.isAmplitudeTransitioning) {
        unsigned long ampElapsed = currentMs - oscillationState.amplitudeTransitionStartMs;
        
        if (ampElapsed < OSC_AMPLITUDE_TRANSITION_DURATION_MS) {
            // Linear interpolation of amplitude
            float progress = (float)ampElapsed / (float)OSC_AMPLITUDE_TRANSITION_DURATION_MS;
            effectiveAmplitude = oscillationState.oldAmplitudeMM + 
                                (oscillationState.targetAmplitudeMM - oscillationState.oldAmplitudeMM) * progress;
            
            // Log transition progress (every 200ms)
            static unsigned long lastAmpTransitionLog = 0;
            if (currentMs - lastAmpTransitionLog > OSC_TRANSITION_LOG_INTERVAL_MS) {
                engine->debug("🔄 Amplitude transition: " + String(effectiveAmplitude, 1) + " mm (" + String(progress * 100, 0) + "%)");
                lastAmpTransitionLog = currentMs;
            }
        } else {
            // Transition complete
            oscillationState.isAmplitudeTransitioning = false;
            effectiveAmplitude = oscillation.amplitudeMM;
            engine->info("✅ Amplitude transition complete: " + String(effectiveAmplitude, 1) + " mm");
        }
    }
    
    // Consolidated debug logging: every 5s (reduced from multiple 2s logs)
    static unsigned long lastDebugMs = 0;
    if (currentMs - lastDebugMs > OSC_DEBUG_LOG_INTERVAL_MS) {
        engine->debug("🌊 OSC: amp=" + String(effectiveAmplitude, 1) + "/" + String(oscillation.amplitudeMM, 1) + 
              "mm, center=" + String(oscillation.centerPositionMM, 1) + 
              "mm, rampIn=" + String(oscillationState.isRampingIn) + 
              ", rampOut=" + String(oscillationState.isRampingOut));
        lastDebugMs = currentMs;
    }
    
    if (oscillationState.isRampingIn) {
        unsigned long rampElapsed = currentMs - oscillationState.rampStartMs;
        
        if (rampElapsed < OSC_RAMP_START_DELAY_MS) {
            // Stabilization phase: amplitude = 0
            effectiveAmplitude = 0;
        } else if (rampElapsed < (oscillation.rampInDurationMs + OSC_RAMP_START_DELAY_MS)) {
            // Ramp phase: calculate progress from end of delay
            unsigned long adjustedElapsed = rampElapsed - OSC_RAMP_START_DELAY_MS;
            float rampProgress = (float)adjustedElapsed / (float)oscillation.rampInDurationMs;
            effectiveAmplitude = oscillation.amplitudeMM * rampProgress;
        } else {
            // Ramp in complete - switch to full amplitude
            oscillationState.isRampingIn = false;
            effectiveAmplitude = oscillation.amplitudeMM;
        }
    } else if (oscillationState.isRampingOut) {
        unsigned long rampElapsed = currentMs - oscillationState.rampStartMs;
        if (rampElapsed < oscillation.rampOutDurationMs) {
            float rampProgress = 1.0 - ((float)rampElapsed / (float)oscillation.rampOutDurationMs);
            effectiveAmplitude = oscillation.amplitudeMM * rampProgress;
        } else {
            // Ramp out complete, stop oscillation
            effectiveAmplitude = 0;
            oscillationState.isRampingOut = false;
            config.currentState = STATE_PAUSED;  // Stop movement (single source of truth)
            
            // ✅ SEQUENCE MODE: Set state to READY and notify sequencer
            if (seqState.isRunning) {
                config.currentState = STATE_READY;
                currentMovement = MOVEMENT_VAET;  // Reset to VA-ET-VIENT for sequencer
                SeqExecutor.onMovementComplete();  // CRITICAL: Notify sequencer that oscillation is complete
            }
        }
    }
    
    oscillationState.currentAmplitude = effectiveAmplitude;
    
    // 🎯 SMOOTH CENTER TRANSITION: Interpolate center position when changed
    float effectiveCenterMM = oscillation.centerPositionMM;
    
    if (oscillationState.isCenterTransitioning) {
        unsigned long centerElapsed = currentMs - oscillationState.centerTransitionStartMs;
        
        if (centerElapsed < OSC_CENTER_TRANSITION_DURATION_MS) {
            // Linear interpolation of center position
            float progress = (float)centerElapsed / (float)OSC_CENTER_TRANSITION_DURATION_MS;
            effectiveCenterMM = oscillationState.oldCenterMM + 
                                (oscillationState.targetCenterMM - oscillationState.oldCenterMM) * progress;
            
            // Log transition progress (every 200ms)
            static unsigned long lastCenterTransitionLog = 0;
            if (currentMs - lastCenterTransitionLog > OSC_TRANSITION_LOG_INTERVAL_MS) {
                engine->debug("🎯 Centre transition: " + String(effectiveCenterMM, 1) + " mm (" + String(progress * 100, 0) + "%)");
                lastCenterTransitionLog = currentMs;
            }
        } else {
            // Transition complete
            oscillationState.isCenterTransitioning = false;
            effectiveCenterMM = oscillation.centerPositionMM;
            engine->info("✅ Center transition complete: " + String(effectiveCenterMM, 1) + " mm");
        }
    }
    
    // Calculate final position
    float targetPositionMM = effectiveCenterMM + (waveValue * effectiveAmplitude);
    
    // Clamp to physical limits with warning
    float minPositionMM = config.minStep / STEPS_PER_MM;
    float maxPositionMM = config.maxStep / STEPS_PER_MM;
    
    if (targetPositionMM < minPositionMM) {
        engine->warn("⚠️ OSC: Limited by START (" + String(targetPositionMM, 1) + "→" + String(minPositionMM, 1) + "mm)");
        targetPositionMM = minPositionMM;
    }
    
    if (targetPositionMM > maxPositionMM) {
        engine->warn("⚠️ OSC: Limited by END (" + String(targetPositionMM, 1) + "→" + String(maxPositionMM, 1) + "mm)");
        targetPositionMM = maxPositionMM;
    }
    
    return targetPositionMM;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool OscillationControllerClass::validateAmplitude(float centerMM, float amplitudeMM, String& errorMsg) {
    // Use effective max distance (respects limitation percentage)
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    
    float minRequired = centerMM - amplitudeMM;
    float maxRequired = centerMM + amplitudeMM;
    
    if (minRequired < 0) {
        errorMsg = "Amplitude too large: minimum position < 0 mm (" + String(minRequired, 1) + "mm)";
        return false;
    }
    
    if (maxRequired > maxAllowed) {
        errorMsg = "Amplitude too large: maximum position > " + String(maxAllowed, 1) + " mm (" + String(maxRequired, 1) + "mm)";
        if (maxDistanceLimitPercent < 100.0) {
            errorMsg += " [Limitation " + String(maxDistanceLimitPercent, 0) + "%]";
        }
        return false;
    }
    
    return true;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

bool OscillationControllerClass::handleCyclePause() {
    if (!oscPauseState.isPausing) {
        return false;
    }
    
    unsigned long elapsedMs = millis() - oscPauseState.pauseStartMs;
    if (elapsedMs >= oscPauseState.currentPauseDuration) {
        // Pause complete - reset phase timer to avoid phase jump
        unsigned long pauseDuration = elapsedMs;
        oscillationState.lastPhaseUpdateMs = millis();  // Reset timer to avoid huge deltaMs
        
        oscPauseState.isPausing = false;
        engine->debug("▶️ End cycle pause OSC (" + String(pauseDuration) + "ms) - Phase frozen");
        return false;  // Pause complete, can continue
    }
    
    return true;  // Still pausing
}

bool OscillationControllerClass::handleInitialPositioning() {
    // Target = center (no sine wave during initial positioning)
    float targetPositionMM = oscillation.centerPositionMM;
    long targetStep = (long)(targetPositionMM * STEPS_PER_MM);
    long errorSteps = targetStep - currentStep;
    
    // Log current position on first call
    if (firstPositioningCall_) {
        float currentMM = currentStep / STEPS_PER_MM;
        engine->debug("🚀 Start positioning: Position=" + String(currentMM, 1) + 
              "mm → Target=" + String(targetPositionMM, 1) + 
              "mm (error=" + String(errorSteps) + " steps = " + 
              String(errorSteps / STEPS_PER_MM, 1) + "mm)");
        firstPositioningCall_ = false;
    }
    
    if (errorSteps == 0) {
        return true;  // Already at target (but still in positioning mode until tolerance check)
    }
    
    unsigned long currentMicros = micros();
    unsigned long elapsedMicros = currentMicros - lastStepMicros_;
    
    if (elapsedMicros < OSC_POSITIONING_STEP_DELAY_MICROS) {
        return true;  // Too early for next step
    }
    
    // Ultra-smooth movement: 1 step at a time
    bool moveForward = (errorSteps > 0);
    Motor.setDirection(moveForward);
    Motor.step();
    
    if (moveForward) {
        currentStep++;
    } else {
        currentStep--;
    }
    
    // Track distance using StatsTracking (AFTER currentStep update)
    stats.trackDelta(currentStep);
    
    lastStepMicros_ = currentMicros;
    
    // Disable initial positioning when at center
    long absErrorSteps = abs(errorSteps);
    if (absErrorSteps < (long)(OSC_INITIAL_POSITIONING_TOLERANCE_MM * STEPS_PER_MM)) {
        oscillationState.isInitialPositioning = false;
        oscillationState.startTimeMs = millis();  // Reset timer for oscillation
        oscillationState.rampStartMs = millis();  // 🔧 FIX: Reset ramp timer AFTER positioning
        oscillationState.lastPhaseUpdateMs = 0;   // Reset phase tracking
        engine->debug("✅ Positioning complete - Starting ramp");
        return false;  // Positioning complete
    }
    
    return true;  // Still positioning
}

bool OscillationControllerClass::checkSafetyContacts(long targetStep) {
    // Safety check: only test contacts when oscillation is near limits
    float minOscPositionMM = oscillation.centerPositionMM - oscillation.amplitudeMM;
    float maxOscPositionMM = oscillation.centerPositionMM + oscillation.amplitudeMM;
    
    // Test END contact only if oscillation approaches upper limit
    float distanceToEndLimitMM = config.totalDistanceMM - maxOscPositionMM;
    if (distanceToEndLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
        if (targetStep >= config.maxStep && Contacts.isEndActive()) {
            Status.sendError("❌ OSCILLATION: END contact reached unexpectedly (amplitude near limit)");
            config.currentState = STATE_PAUSED;  // Stop movement (single source of truth)
            return false;
        }
    }

    // Test START contact only if oscillation approaches lower limit
    if (minOscPositionMM <= HARD_DRIFT_TEST_ZONE_MM) {
        if (targetStep <= config.minStep && Contacts.isStartActive()) {
            Status.sendError("❌ OSCILLATION: START contact reached unexpectedly (amplitude near limit)");
            config.currentState = STATE_PAUSED;  // Stop movement (single source of truth)
            return false;
        }
    }
    
    return true;  // Safe
}

void OscillationControllerClass::executeSteps(long targetStep, bool isCatchUp) {
    long errorSteps = targetStep - currentStep;
    long absErrorSteps = abs(errorSteps);
    
    int stepsToExecute;
    if (isCatchUp) {
        stepsToExecute = min(absErrorSteps, (long)OSC_MAX_STEPS_PER_CATCH_UP);
    } else {
        // Normal oscillation: 1 smooth step following the sine wave
        stepsToExecute = 1;
    }
    
    // Execute steps
    bool moveForward = (errorSteps > 0);
    Motor.setDirection(moveForward);
    
    for (int i = 0; i < stepsToExecute; i++) {
        Motor.step();
        if (moveForward) {
            currentStep++;
        } else {
            currentStep--;
        }
        // Track distance traveled using StatsTracking
        stats.trackDelta(currentStep);
    }
}
