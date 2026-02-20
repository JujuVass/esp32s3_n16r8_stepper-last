/**
 * ============================================================================
 * OscillationController.h - Sinusoidal Oscillation Movement Module
 * ============================================================================
 * 
 * Handles all oscillation movement logic:
 * - Sinusoidal position calculation with phase accumulation
 * - Multiple waveforms: Sine, Triangle, Square
 * - Smooth frequency/center/amplitude transitions
 * - Ramp in/out for smooth start/stop
 * - Cycle counting with inter-cycle pause support
 * - Speed limiting for hardware protection
 * 
 * Architecture: Singleton pattern with extern references to main globals
 * 
 * Dependencies:
 * - Types.h (OscillationConfig, OscillationState, CyclePauseState)
 * - Config.h (OSC_* constants)
 * - MotorDriver.h (Motor singleton)
 * - ContactSensors.h (Contacts singleton)
 * - UtilityEngine.h (engine singleton)
 * ============================================================================
 */

#ifndef OSCILLATION_CONTROLLER_H
#define OSCILLATION_CONTROLLER_H

#include <Arduino.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/UtilityEngine.h"
#include "core/GlobalState.h"

// ============================================================================
// OSCILLATION STATE - Defined in OscillationController.cpp
// ============================================================================
extern OscillationConfig oscillation;
extern OscillationState oscillationState;
extern CyclePauseState oscPauseState;
extern float actualOscillationSpeedMMS;

// ============================================================================
// CLASS DECLARATION
// ============================================================================

class OscillationControllerClass {
public:
    // ========================================================================
    // LIFECYCLE
    // ========================================================================
    
    /**
     * Initialize oscillation controller
     * Called during setup() after UtilityEngine is available
     */
    void begin();
    
    // ========================================================================
    // MAIN CONTROL
    // ========================================================================
    
    /**
     * Start oscillation movement
     * Validates configuration, initializes state, begins movement
     */
    void start();
    
    /**
     * Process one oscillation step
     * Called from main loop when MovementType::MOVEMENT_OSC is active
     * Handles initial positioning, waveform generation, cycle counting
     */
    void process();
    
    // ========================================================================
    // POSITION CALCULATION
    // ========================================================================
    
    /**
     * Calculate current target position based on oscillation parameters
     * Handles:
     * - Phase accumulation for smooth frequency transitions
     * - Waveform generation (Sine, Triangle, Square)
     * - Amplitude ramping (in/out)
     * - Center position transitions
     * - Speed limiting
     * 
     * @return Target position in mm
     */
    float calculatePosition();
    
    // ========================================================================
    // VALIDATION
    // ========================================================================
    
    /**
     * Validate oscillation amplitude against physical limits
     * Checks that center ± amplitude fits within effective travel
     * 
     * @param centerMM Center position in mm
     * @param amplitudeMM Amplitude in mm (±)
     * @param errorMsg Output: Error message if validation fails
     * @return true if valid, false otherwise
     */
    bool validateAmplitude(float centerMM, float amplitudeMM, String& errorMsg);
    
    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    /**
     * Get actual oscillation speed (considering hardware limits)
     * @return Speed in mm/s
     */
    float getActualSpeed() const { return actualSpeedMMS_; }
    
    /**
     * Get current amplitude (may differ during ramping)
     * @return Current effective amplitude in mm
     */
    float getCurrentAmplitude() const { return oscillationState.currentAmplitude; }
    
    /**
     * Get completed cycles count
     * @return Number of full cycles completed
     */
    int getCompletedCycles() const { return oscillationState.completedCycles; }
    
    /**
     * Check if currently in ramp-in phase
     */
    bool isRampingIn() const { return oscillationState.isRampingIn; }
    
    /**
     * Check if currently in ramp-out phase
     */
    bool isRampingOut() const { return oscillationState.isRampingOut; }
    
    /**
     * Check if doing initial positioning (moving to center before oscillation)
     */
    bool isInitialPositioning() const { return oscillationState.isInitialPositioning; }
    
    /**
     * Check if in inter-cycle pause
     */
    bool isPausing() const { return oscPauseState.isPausing; }

private:
    // ========================================================================
    // INTERNAL STATE
    // ========================================================================
    
    float actualSpeedMMS_ = 0.0f;           // Actual speed considering limits
    bool cyclesCompleteLogged_ = false;      // Prevent duplicate completion logs
    bool firstPositioningCall_ = true;       // Track first positioning call for debug
    bool catchUpWarningLogged_ = false;      // Prevent duplicate catch-up warnings
    unsigned long lastStepMicros_ = 0;       // Own step timing (decoupled from BaseMovement global)
    
    // Log throttle timestamps (member vars — reset in start(), not static)
    unsigned long lastSpeedLimitLogMs_ = 0;
    unsigned long lastTransitionLogMs_ = 0;
    unsigned long lastAmpTransitionLogMs_ = 0;
    unsigned long lastDebugLogMs_ = 0;
    unsigned long lastCenterTransitionLogMs_ = 0;
    
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    /**
     * Handle initial positioning phase (move to center before starting)
     * @return true if still positioning, false if complete
     */
    bool handleInitialPositioning();
    
    /**
     * Handle inter-cycle pause
     * @return true if paused, false if can continue
     */
    bool handleCyclePause();
    
    /**
     * Execute motor steps towards target
     * @param targetStep Target position in steps
     * @param isCatchUp true if in catch-up mode (multiple steps)
     */
    void executeSteps(long targetStep, bool isCatchUp);
    
    /**
     * Check safety contacts near oscillation limits
     * @param targetStep Target position in steps
     * @return true if safe, false if contact hit
     */
    bool checkSafetyContacts(long targetStep);
};

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

extern OscillationControllerClass Osc;

#endif // OSCILLATION_CONTROLLER_H
