/**
 * ============================================================================
 * ChaosController.h - Chaos Mode Movement Controller
 * ============================================================================
 * 
 * Manages random pattern generation and execution for chaos mode.
 * Implements 11 different movement patterns with configurable parameters.
 * 
 * Patterns:
 * - ZIGZAG, SWEEP, PULSE, DRIFT, BURST (discrete)
 * - WAVE, PENDULUM, SPIRAL, CALM (continuous)
 * - BRUTE_FORCE, LIBERATOR (multi-phase)
 * 
 * Dependencies:
 * - ChaosPatterns.h for pattern configurations
 * - Types.h for ChaosRuntimeConfig, ChaosExecutionState
 * - MotorDriver for motor control
 * 
 * @author Refactored from stepper_controller_restructured.ino
 * @version 1.0
 */

#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include "Types.h"
#include "Config.h"
#include "ChaosPatterns.h"
#include "UtilityEngine.h"
#include "GlobalState.h"
#include "hardware/ContactSensors.h"

// ============================================================================
// CHAOS CONTROLLER CLASS
// ============================================================================

class ChaosController {
public:
    /**
     * Get singleton instance
     */
    static ChaosController& getInstance();
    
    // ========================================================================
    // MAIN API
    // ========================================================================
    
    /**
     * Start chaos mode execution
     * Validates configuration and begins pattern generation
     */
    void start();
    
    /**
     * Stop chaos mode
     * Logs statistics and resets state
     */
    void stop();
    
    /**
     * Process chaos execution (call from loop())
     * Non-blocking, handles pattern changes and stepping
     */
    void process();
    
    /**
     * Check if chaos mode is currently running
     */
    bool isRunning() const { return chaosState.isRunning; }
    
    // ========================================================================
    // LIMIT CHECKING (called from doStep)
    // ========================================================================
    
    /**
     * Check chaos amplitude limits before stepping
     * @return true if safe to step, false if limit hit
     */
    bool checkLimits();
    
    /**
     * Execute one step in chaos mode
     * Handles drift detection, amplitude limits, and stepping
     * Called internally by process() - replaces global doStep() for chaos
     */
    void doStep();

private:
    ChaosController() = default;
    ~ChaosController() = default;
    ChaosController(const ChaosController&) = delete;
    ChaosController& operator=(const ChaosController&) = delete;
    
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    /**
     * Generate a new random chaos pattern
     * Selects from enabled patterns with weighted probability
     */
    void generatePattern();
    
    /**
     * Calculate step delay based on current speed level
     */
    void calculateStepDelay();
    
    /**
     * Calculate effective limits for chaos mode
     * @param minLimit Output: effective minimum limit (mm)
     * @param maxLimit Output: effective maximum limit (mm)
     */
    inline void calculateLimits(float& minLimit, float& maxLimit);
    
    /**
     * Calculate maximum safe amplitude from center
     * @param minLimit Effective minimum limit (mm)
     * @param maxLimit Effective maximum limit (mm)
     * @return Maximum safe amplitude (mm)
     */
    inline float calculateMaxAmplitude(float minLimit, float maxLimit);
    
    /**
     * Force direction at limits to prevent infinite loop
     * @return true if direction was forced
     */
    inline bool forceDirectionAtLimits(float currentPos, float minLimit, float maxLimit, bool& movingFwd);
    
    // ========================================================================
    // PATTERN HANDLERS
    // ========================================================================
    
    void handleZigzag(float craziness, float effectiveMinLimit, float effectiveMaxLimit, 
                      float& speedMultiplier, unsigned long& patternDuration);
    void handleSweep(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                     float& speedMultiplier, unsigned long& patternDuration);
    void handlePulse(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                     float& speedMultiplier, unsigned long& patternDuration);
    void handleDrift(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                     float& speedMultiplier, unsigned long& patternDuration);
    void handleBurst(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                     float& speedMultiplier, unsigned long& patternDuration);
    void handleWave(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                    float& speedMultiplier, unsigned long& patternDuration);
    void handlePendulum(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                        float& speedMultiplier, unsigned long& patternDuration);
    void handleSpiral(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                      float& speedMultiplier, unsigned long& patternDuration);
    void handleCalm(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                    float& speedMultiplier, unsigned long& patternDuration);
    void handleBruteForce(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                          float& speedMultiplier, unsigned long& patternDuration);
    void handleLiberator(float craziness, float effectiveMinLimit, float effectiveMaxLimit,
                         float& speedMultiplier, unsigned long& patternDuration);
    
    // ========================================================================
    // CONTINUOUS PATTERN PROCESSING
    // ========================================================================
    
    void processWave(float effectiveMinLimit, float effectiveMaxLimit);
    void processCalm(float effectiveMinLimit, float effectiveMaxLimit);
    
    // ========================================================================
    // AT-TARGET HANDLERS
    // ========================================================================
    
    void handlePulseAtTarget(float effectiveMinLimit, float effectiveMaxLimit);
    void handlePendulumAtTarget(float effectiveMinLimit, float effectiveMaxLimit);
    void handleSpiralAtTarget(float effectiveMinLimit, float effectiveMaxLimit, float maxPossibleAmplitude);
    void handleSweepAtTarget(float effectiveMinLimit, float effectiveMaxLimit);
    void handleBruteForceAtTarget(float effectiveMinLimit, float effectiveMaxLimit);
    void handleLiberatorAtTarget(float effectiveMinLimit, float effectiveMaxLimit);
    void handleDiscreteAtTarget();
};

// Global accessor
extern ChaosController& Chaos;

// Helper function name conversion
extern const char* executionContextName(ExecutionContext ctx);
