// ============================================================================
// VA-ET-VIENT CONTROLLER - Phase 1
// ============================================================================
// Simple back-and-forth movement mode (default mode)
// Handles: parameter updates, step delay calculation, speed conversion
// 
// Phase 1: Parameter management functions
// Phase 2 (future): Full movement logic extraction from main
// ============================================================================

#ifndef VA_ET_VIENT_CONTROLLER_H
#define VA_ET_VIENT_CONTROLLER_H

#include <Arduino.h>
#include "Types.h"
#include "Config.h"
#include "GlobalState.h"  // Access to global motion, pendingMotion, etc.

// Forward declarations
class UtilityEngine;
extern UtilityEngine* engine;

// ============================================================================
// VA-ET-VIENT CONTROLLER CLASS
// ============================================================================

class VaEtVientControllerClass {
public:
    // ========================================================================
    // CONFIGURATION STATE
    // ========================================================================
    // Note: Phase 1 uses global variables (motion, pendingMotion, motionPauseState)
    // defined in stepper_controller_restructured.ino and declared extern in GlobalState.h
    // These will be moved into this class in Phase 2
    
    // ========================================================================
    // TIMING STATE (for cycle measurement)
    // ========================================================================
    
    unsigned long lastStartContactMillis;
    unsigned long cycleTimeMillis;
    float measuredCyclesPerMinute;
    bool wasAtStart;
    
    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    
    VaEtVientControllerClass();
    
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

private:
    /**
     * Initialize pending changes struct with current values
     */
    void initPendingFromCurrent();
};

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

extern VaEtVientControllerClass VaEtVient;

#endif // VA_ET_VIENT_CONTROLLER_H
