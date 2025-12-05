/**
 * ============================================================================
 * PursuitController.cpp - Real-time Position Tracking Implementation
 * ============================================================================
 * 
 * Extracted from main stepper_controller_restructured.ino
 * Original functions: pursuitMove(), doPursuitStep()
 * 
 * Created: December 2024
 * ============================================================================
 */

#include "movement/PursuitController.h"
#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"

// ============================================================================
// PURSUIT STATE - Owned by this module (Phase 4D migration)
// ============================================================================
PursuitState pursuit;

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
        sendError("❌ Mode Pursuit nécessite une calibration d'abord!");
        return;
    }
    
    // Update pursuit parameters
    pursuit.maxSpeedLevel = maxSpeedLevel;
    
    // Clamp target to valid range (respect effective max distance limit)
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    if (targetPositionMM < 0) targetPositionMM = 0;
    if (targetPositionMM > maxAllowed) targetPositionMM = maxAllowed;
    
    // Convert to steps and CLAMP to calibrated limits (config.minStep/config.maxStep)
    pursuit.targetStep = (long)(targetPositionMM * STEPS_PER_MM);
    if (pursuit.targetStep < config.minStep) pursuit.targetStep = config.minStep;
    if (pursuit.targetStep > config.maxStep) pursuit.targetStep = config.maxStep;
    
    long errorSteps = pursuit.targetStep - currentStep;
    float errorMM = abs(errorSteps) / STEPS_PER_MM;
    
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
        pursuit.stepDelay = calculateStepDelay(errorMM);
        
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
        currentStep++;
        if (currentStep > lastStepForDistance) {
            totalDistanceTraveled += (currentStep - lastStepForDistance);
            lastStepForDistance = currentStep;
        }
    } else {
        currentStep--;
        if (lastStepForDistance > currentStep) {
            totalDistanceTraveled += (lastStepForDistance - currentStep);
            lastStepForDistance = currentStep;
        }
    }
}

void PursuitControllerClass::stop() {
    pursuit.isMoving = false;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

unsigned long PursuitControllerClass::calculateStepDelay(float errorMM) {
    // Calculate speed based on error (aggressive proportional control)
    // Ultra-aggressive speed profile for NEMA34 8NM - keep speed high until very close
    float speedLevel;
    
    if (errorMM > 5.0) {
        // Far from target (>5mm): use max speed
        speedLevel = pursuit.maxSpeedLevel;
    } else if (errorMM > 1.0) {
        // Close to target (1-5mm): smooth ramp from max to 60%
        float ratio = (errorMM - 1.0) / (5.0 - 1.0);
        speedLevel = pursuit.maxSpeedLevel * (0.6 + (ratio * 0.4));
    } else {
        // Very close (<1mm): minimum speed for precision
        speedLevel = pursuit.maxSpeedLevel * 0.6;
    }
    
    // Direct conversion: speedLevel → mm/sec → steps/sec WITH compensation
    float mmPerSecond = speedLevel * 10.0;  // speedLevel*10 → mm/s (ex: MAX_SPEED_LEVEL→(MAX_SPEED_LEVEL*10)mm/s, 10→100mm/s)
    float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
    
    // Limit to safe range (NEMA34 can handle more)
    if (stepsPerSecond < 30) stepsPerSecond = 30;  // Higher minimum for responsiveness
    if (stepsPerSecond > 6000) stepsPerSecond = 6000;  // HSS86 safe limit (close to 10kHz)
    
    float delayMicros = ((1000000.0 / stepsPerSecond) - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    if (delayMicros < 20) delayMicros = 20;  // Absolute minimum (50kHz)
    
    return (unsigned long)delayMicros;
}

bool PursuitControllerClass::checkSafetyContacts(bool moveForward) {
    // 🆕 OPTIMISATION: HARD DRIFT detection - Test UNIQUEMENT si proche des limites
    // Test END contact si poursuite vers limite haute
    if (moveForward) {
        long stepsToLimit = config.maxStep - currentStep;
        float distanceToLimitMM = stepsToLimit / STEPS_PER_MM;
        
        if (distanceToLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
            if (Contacts.readDebounced(PIN_END_CONTACT, LOW, 3, 50)) {
                pursuit.isMoving = false;
                pursuit.targetStep = currentStep;
                sendError("❌ PURSUIT: Contact END atteint - arrêt sécurité");
                config.currentState = STATE_ERROR;
                return false;
            }
        }
    } else {
        // Test START contact si poursuite vers limite basse
        float distanceToStartMM = currentStep / STEPS_PER_MM;
        
        if (distanceToStartMM <= HARD_DRIFT_TEST_ZONE_MM) {
            if (Contacts.readDebounced(PIN_START_CONTACT, LOW, 3, 50)) {
                pursuit.isMoving = false;
                pursuit.targetStep = currentStep;
                sendError("❌ PURSUIT: Contact START atteint - arrêt sécurité");
                config.currentState = STATE_ERROR;
                return false;
            }
        }
    }
    
    return true;  // Safe to continue
}
