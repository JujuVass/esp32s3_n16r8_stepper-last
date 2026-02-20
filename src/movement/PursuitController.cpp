/**
 * ============================================================================
 * PursuitController.cpp - Real-time Position Tracking Implementation
 * ============================================================================
 * 
 * Implements pursuit mode: pursuitMove(), doPursuitStep()
 * ============================================================================
 */

#include "movement/PursuitController.h"
#include "communication/StatusBroadcaster.h"  // For Status.sendError()
#include "core/Validators.h"
#include "core/MovementMath.h"
#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"

// ============================================================================
// PURSUIT STATE - Owned by this module
// ============================================================================
constinit PursuitState pursuit;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

PursuitControllerClass Pursuit;

// ============================================================================
// LIFECYCLE
// ============================================================================

void PursuitControllerClass::begin() {
    engine->info("🎯 PursuitController initialized");
}

// ============================================================================
// MAIN CONTROL
// ============================================================================

void PursuitControllerClass::move(float targetPositionMM, float maxSpeedLevel) {
    // Safety check: calibration required
    if (config.totalDistanceMM == 0) {
        Status.sendError("❌ Pursuit mode requires calibration first!");
        return;
    }
    
    // Update pursuit parameters
    pursuit.maxSpeedLevel = maxSpeedLevel;
    
    // Clamp target to valid range (respect effective max distance limit)
    float maxAllowed = Validators::getMaxAllowedMM();
    if (targetPositionMM < 0) targetPositionMM = 0;
    if (targetPositionMM > maxAllowed) targetPositionMM = maxAllowed;
    
    // Convert to steps and CLAMP to calibrated limits (config.minStep/config.maxStep)
    pursuit.targetStep = MovementMath::mmToSteps(targetPositionMM);
    if (pursuit.targetStep < config.minStep) pursuit.targetStep = config.minStep;
    if (pursuit.targetStep > config.maxStep) pursuit.targetStep = config.maxStep;
    
    long errorSteps = pursuit.targetStep - currentStep;
    float errorMM = MovementMath::stepsToMM(abs(errorSteps));
    
    // If already at target, don't do anything
    if (errorSteps == 0) {
        pursuit.isMoving = false;
        return;
    }
    
    // Only recalculate speed if target or speed setting changed significantly
    bool targetChanged = (abs(pursuit.targetStep - pursuit.lastTargetStep) > 6);  // >1mm change
    bool speedSettingChanged = (abs(pursuit.maxSpeedLevel - pursuit.lastMaxSpeedLevel) > 0.5);
    bool hasWork = (errorSteps != 0);  // There's actual movement needed
    
    if (targetChanged || speedSettingChanged || !pursuit.isMoving || hasWork) {
        // Calculate step delay based on error distance
        pursuit.stepDelay = MovementMath::pursuitStepDelay(errorMM, pursuit.maxSpeedLevel);
        
        // Remember last values
        pursuit.lastTargetStep = pursuit.targetStep;
        pursuit.lastMaxSpeedLevel = pursuit.maxSpeedLevel;
    }
    
    // Set direction and track it
    bool moveForward = (errorSteps > 0);
    
    // If direction changed, add a small delay for driver to respond
    if (pursuit.isMoving && (moveForward != pursuit.direction)) {
        delayMicroseconds(50);  // Brief pause for HSS86 direction change
    }
    
    pursuit.direction = moveForward;
    Motor.setDirection(moveForward);
    
    // Ensure motor is enabled (should already be, but ensure on first call)
    Motor.enable();
    pursuit.isMoving = true;
}

void PursuitControllerClass::process() {
    long errorSteps = pursuit.targetStep - currentStep;
    
    // If at target, stop moving but keep motor enabled
    if (errorSteps == 0) {
        pursuit.isMoving = false;
        return;
    }
    
    // Safety: respect calibrated limits (don't go beyond config.minStep/config.maxStep)
    bool moveForward = (errorSteps > 0);
    
    if (moveForward && currentStep >= config.maxStep) {
        // Already at max limit - stop here
        pursuit.targetStep = currentStep;
        pursuit.isMoving = false;
        engine->warn("⚠️ Pursuit: reached config.maxStep limit");
        return;
    }
    
    if (!moveForward && currentStep <= config.minStep) {
        // Already at min limit - stop here
        pursuit.targetStep = currentStep;
        pursuit.isMoving = false;
        engine->warn("⚠️ Pursuit: reached config.minStep limit");
        return;
    }
    
    // Check safety contacts near limits
    if (!checkSafetyContacts(moveForward)) {
        return;  // Contact hit - stopped
    }
    
    // Execute one step
    Motor.step();
    
    if (moveForward) {
        currentStep = currentStep + 1;
    } else {
        currentStep = currentStep - 1;
    }
    // Track distance using StatsTracking
    stats.trackDelta(currentStep);
}

void PursuitControllerClass::stop() {
    pursuit.isMoving = false;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================



bool PursuitControllerClass::checkSafetyContacts(bool moveForward) {
    // Hard drift detection: only test contacts when near limits
    if (moveForward) {
        long stepsToLimit = config.maxStep - currentStep;
        float distanceToLimitMM = MovementMath::stepsToMM(stepsToLimit);
        
        if (distanceToLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
            if (Contacts.isEndActive()) {
                pursuit.isMoving = false;
                pursuit.targetStep = currentStep;
                Status.sendError("❌ PURSUIT: END contact reached - safety stop");
                config.currentState = SystemState::STATE_ERROR;
                return false;
            }
        }
    } else {
        // Test START contact when pursuing toward lower limit
        float distanceToStartMM = MovementMath::stepsToMM(currentStep);
        
        if (distanceToStartMM <= HARD_DRIFT_TEST_ZONE_MM) {
            if (Contacts.isStartActive()) {
                pursuit.isMoving = false;
                pursuit.targetStep = currentStep;
                Status.sendError("❌ PURSUIT: START contact reached - safety stop");
                config.currentState = SystemState::STATE_ERROR;
                return false;
            }
        }
    }
    
    return true;  // Safe to continue
}
