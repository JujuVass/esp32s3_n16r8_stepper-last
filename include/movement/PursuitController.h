/**
 * ============================================================================
 * PursuitController.h - Real-time Position Tracking Module
 * ============================================================================
 * 
 * Handles pursuit/tracking mode where motor follows a target position
 * in real-time with proportional speed control.
 * 
 * Features:
 * - Real-time target tracking with proportional speed
 * - Speed ramping based on distance to target
 * - Safety contact detection near limits
 * - Direction change handling for HSS86 driver
 * 
 * Architecture: Singleton pattern with extern references to main globals
 * 
 * Dependencies:
 * - Types.h (PursuitState struct)
 * - Config.h (STEPS_PER_MM, HARD_DRIFT_TEST_ZONE_MM, etc.)
 * - MotorDriver.h (Motor singleton)
 * - ContactSensors.h (Contacts singleton)
 * - UtilityEngine.h (engine singleton)
 * 
 * Created: December 2024
 * ============================================================================
 */

#ifndef PURSUIT_CONTROLLER_H
#define PURSUIT_CONTROLLER_H

#include <Arduino.h>
#include "Types.h"
#include "Config.h"
#include "UtilityEngine.h"
#include "GlobalState.h"

// ============================================================================
// CLASS DECLARATION
// ============================================================================

class PursuitControllerClass {
public:
    // ========================================================================
    // LIFECYCLE
    // ========================================================================
    
    /**
     * Initialize pursuit controller
     * Called during setup() after UtilityEngine is available
     */
    void begin();
    
    // ========================================================================
    // MAIN CONTROL
    // ========================================================================
    
    /**
     * Move to target position with real-time tracking
     * Updates pursuit parameters and starts movement
     * 
     * @param targetPositionMM Target position in millimeters
     * @param maxSpeedLevel Maximum speed level (1-MAX_SPEED_LEVEL)
     */
    void move(float targetPositionMM, float maxSpeedLevel);
    
    /**
     * Process one pursuit step
     * Called from main loop when MOVEMENT_PURSUIT is active and pursuit.isMoving
     * Handles step execution, safety checks, distance tracking
     */
    void process();
    
    /**
     * Stop pursuit movement
     * Called when stopping all movement or switching modes
     */
    void stop();
    
    // ========================================================================
    // STATE ACCESS
    // ========================================================================
    
    /**
     * Check if pursuit is currently moving
     */
    bool isMoving() const { return pursuit.isMoving; }
    
    /**
     * Get current target position in steps
     */
    long getTargetStep() const { return pursuit.targetStep; }
    
    /**
     * Get current step delay
     */
    unsigned long getStepDelay() const { return pursuit.stepDelay; }

private:
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    /**
     * Calculate step delay based on distance to target
     * Uses proportional speed control with distance-based ramping
     * 
     * @param errorMM Distance to target in mm
     * @return Step delay in microseconds
     */
    unsigned long calculateStepDelay(float errorMM);
    
    /**
     * Check safety contacts when near limits
     * @param moveForward Direction of movement
     * @return true if safe to continue, false if contact hit
     */
    bool checkSafetyContacts(bool moveForward);
};

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

extern PursuitControllerClass Pursuit;

#endif // PURSUIT_CONTROLLER_H
