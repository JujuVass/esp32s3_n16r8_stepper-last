// ============================================================================
// BASE MOVEMENT CONTROLLER - Core Movement Orchestration
// ============================================================================
// Central movement controller that orchestrates all movement modes:
// - Va-et-vient (back-and-forth) - default mode
// - Oscillation, Chaos, Pursuit - specialized modes
// 
// Provides: parameter updates, step delay calculation, speed conversion,
// pause/stop/return control, step execution
// ============================================================================

#ifndef BASE_MOVEMENT_CONTROLLER_H
#define BASE_MOVEMENT_CONTROLLER_H

#include <Arduino.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/GlobalState.h"
#include "core/UtilityEngine.h"

// ============================================================================
// BASE MOVEMENT CONTROLLER CLASS
// ============================================================================

class BaseMovementControllerClass {
public:
    // ========================================================================
    // CONFIGURATION STATE
    // ========================================================================
    // Note: Phase 1 uses global variables (motion, pendingMotion, motionPauseState)
    // defined in stepper_controller_restructured.ino and declared extern in GlobalState.h
    // Timing variables (lastStartContactMillis, cycleTimeMillis, etc.) are also globals
    // shared with SequenceExecutor - no member duplication needed
    
    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    
    BaseMovementControllerClass();
    
    // ========================================================================
    // PARAMETER UPDATE METHODS
    // ========================================================================
    
    /**
     * Set movement distance
     * If running, queues change for end of cycle
     * @param distMM Distance in millimeters
     */
    void setDistance(float distMM);
    
    /**
     * Set start position
     * If running, queues change for end of cycle
     * @param startMM Start position in millimeters
     */
    void setStartPosition(float startMM);
    
    /**
     * Set forward speed
     * If running, queues change for end of cycle
     * @param speedLevel Speed level (0.1 - MAX_SPEED_LEVEL)
     */
    void setSpeedForward(float speedLevel);
    
    /**
     * Set backward speed
     * If running, queues change for end of cycle
     * @param speedLevel Speed level (0.1 - MAX_SPEED_LEVEL)
     */
    void setSpeedBackward(float speedLevel);
    
    /**
     * Configure cycle pause
     * @param enabled Enable/disable pause between cycles
     * @param durationSec Fixed pause duration (seconds)
     * @param isRandom Use random duration
     * @param minSec Minimum random duration
     * @param maxSec Maximum random duration
     */
    void setCyclePause(bool enabled, float durationSec = 1.5, 
                       bool isRandom = false, float minSec = 1.5, float maxSec = 5.0);
    
    // ========================================================================
    // CALCULATION METHODS
    // ========================================================================
    
    /**
     * Calculate step delays based on current motion config
     * Updates stepDelayMicrosForward and stepDelayMicrosBackward globals
     */
    void calculateStepDelay();
    
    /**
     * Convert speed level (0-20) to cycles per minute (0-200)
     * @param speedLevel Speed level
     * @return Cycles per minute
     */
    float speedLevelToCyclesPerMin(float speedLevel);
    
    /**
     * Convert cycles per minute to speed level
     * @param cpm Cycles per minute
     * @return Speed level
     */
    float cyclesPerMinToSpeedLevel(float cpm);
    
    // ========================================================================
    // ZONE EFFECT METHODS (Speed Effects + Special Effects)
    // ========================================================================
    
    /**
     * Calculate speed factor based on position within effect zone
     * @param zoneProgress Position in zone: 0.0 (at boundary) to 1.0 (at extremity)
     * @return Speed factor (1.0 = normal, >1.0 = slower for DECEL, <1.0 = faster for ACCEL)
     */
    float calculateSpeedFactor(float zoneProgress);
    
    /**
     * Calculate adjusted delay based on position within movement range
     * @param currentPositionMM Current position in mm
     * @param movementStartMM Start position of current movement in mm
     * @param movementEndMM End position of current movement in mm
     * @param baseDelayMicros Base delay in microseconds
     * @param effectiveEnableStart Whether start zone is active (after mirror swap)
     * @param effectiveEnableEnd Whether end zone is active (after mirror swap)
     * @return Adjusted delay in microseconds
     */
    int calculateAdjustedDelay(float currentPositionMM, float movementStartMM, 
                               float movementEndMM, int baseDelayMicros,
                               bool effectiveEnableStart, bool effectiveEnableEnd);
    
    /**
     * Check and trigger random turnback in zone
     * @param distanceIntoZone Distance traveled into the zone (mm)
     * @param isEndZone True if this is the end zone, false for start zone
     */
    void checkAndTriggerRandomTurnback(float distanceIntoZone, bool isEndZone);
    
    /**
     * Reset random turnback state
     */
    void resetRandomTurnback();
    
    /**
     * Check and handle end pause (returns true if currently pausing)
     * @return true if pausing and should not step
     */
    bool checkAndHandleEndPause();
    
    /**
     * Trigger end pause at extremity
     */
    void triggerEndPause();
    
    /**
     * Validate and adjust zone size to ensure it doesn't exceed movement amplitude
     */
    void validateZoneEffect();
    
    /**
     * Legacy alias for validateZoneEffect
     */
    void validateDecelZone();
    
    // Legacy alias for calculateSpeedFactor (backward compatibility)
    float calculateSlowdownFactor(float zoneProgress) { return calculateSpeedFactor(zoneProgress); }
    
    // ========================================================================
    // PENDING CHANGES MANAGEMENT
    // ========================================================================
    
    /**
     * Apply pending motion changes
     * Called at end of backward cycle in doStep()
     */
    void applyPendingChanges();
    
    /**
     * Check if there are pending changes
     * @return true if changes are queued
     */
    bool hasPendingChanges() const { return pendingMotion.hasChanges; }
    
    /**
     * Clear pending changes without applying
     */
    void clearPendingChanges() { pendingMotion.hasChanges = false; }
    
    // ========================================================================
    // STATUS METHODS
    // ========================================================================
    
    /**
     * Get current motion configuration (read-only access)
     */
    const MotionConfig& getConfig() const { return motion; }
    
    /**
     * Reset cycle timing measurements
     */
    void resetCycleTiming();
    
    // ========================================================================
    // MOVEMENT CONTROL (Phase 2)
    // ========================================================================
    
    /**
     * Toggle pause state
     * Saves stats before pausing, resets oscillation timer on resume
     */
    void togglePause();
    
    /**
     * Stop all movement
     * Handles all movement types, saves stats, resets states
     */
    void stop();
    
    /**
     * Start va-et-vient movement
     * @param distMM Distance to travel in mm
     * @param speedLevel Speed level (1-20)
     */
    void start(float distMM, float speedLevel);
    
    /**
     * Return motor to start position
     * Recovery mechanism, works from ERROR state
     */
    void returnToStart();
    
    // ========================================================================
    // MAIN LOOP PROCESSING (Phase 4D - encapsulates timing + decel + step)
    // ========================================================================
    
    /**
     * Process va-et-vient movement (call in main loop)
     * Handles:
     * - Cycle pause timing
     * - Step timing with deceleration zone adjustment
     * - Step execution via doStep()
     * 
     * Encapsulates all logic previously in main loop() for MOVEMENT_VAET
     */
    void process();
    
    // ========================================================================
    // STEP EXECUTION (Phase 3 - extracted from main doStep())
    // ========================================================================
    
    /**
     * Execute one step of va-et-vient movement
     * Called from main loop() when currentMovement == MOVEMENT_VAET
     * 
     * Handles:
     * - Forward/backward stepping
     * - Drift detection & correction (via ContactSensors)
     * - Cycle completion with pending changes
     * - Cycle pause management
     * - Cycle timing measurement
     * - Distance tracking
     */
    void doStep();
    
    /**
     * Process cycle completion (end of backward movement)
     * Applies pending changes, handles pause, measures timing
     * Called internally by doStep() when reaching startStep
     */
    void processCycleCompletion();
    
    /**
     * Handle cycle pause if enabled
     * @return true if pausing (caller should return), false to continue
     */
    bool handleCyclePause();
    
    /**
     * Measure and log cycle timing
     */
    void measureCycleTime();
    
    /**
     * Track distance traveled
     * Updates totalDistanceTraveled global
     */
    void trackDistance();
    
    // ========================================================================
    // CYCLE COUNTING
    // ========================================================================
    
    /**
     * Get current cycle count (incremented each time startStep is reached)
     * @return Number of completed cycles since last reset
     */
    unsigned long getCycleCount() const { return _cycleCounter; }
    
    /**
     * Reset cycle counter to zero
     * Called when movement stops
     */
    void resetCycleCount() { _cycleCounter = 0; }

private:
    // Cycle counting state
    unsigned long _cycleCounter = 0;
    
    /**
     * Initialize pending changes struct with current values
     */
    void initPendingFromCurrent();
};

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

extern BaseMovementControllerClass BaseMovement;

#endif // BASE_MOVEMENT_CONTROLLER_H
