// ============================================================================
// CONTACT_SENSORS.CPP - Opto Sensor Implementation (OPTIMIZED)
// ============================================================================
// OPTO LOGIC (verified):
//   HIGH = beam BLOCKED = object detected = ACTIVE
//   LOW  = beam CLEAR   = no object      = INACTIVE
// ============================================================================

#include "hardware/ContactSensors.h"
#include "hardware/MotorDriver.h"
#include "communication/StatusBroadcaster.h"
#include "core/GlobalState.h"
#include "core/UtilityEngine.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

ContactSensors& ContactSensors::getInstance() {
    static ContactSensors instance;
    return instance;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void ContactSensors::init() {
    if (m_initialized) return;
    
    // Opto sensors: HIGH when beam blocked, LOW when clear
    // No pull-up needed - opto drives the signal
    pinMode(PIN_START_CONTACT, INPUT);
    pinMode(PIN_END_CONTACT, INPUT);
    
    m_initialized = true;
    engine->info("✅ Opto sensors initialized (START=GPIO" + String(PIN_START_CONTACT) + 
                 ", END=GPIO" + String(PIN_END_CONTACT) + ") - HIGH=blocked, LOW=clear");
}

// ============================================================================
// OPTO READING - SIMPLE API (no debounce needed)
// HIGH = BLOCKED/ACTIVE, LOW = CLEAR/INACTIVE
// Supports sensorsInverted mode (swaps START <-> END pins)
// ============================================================================

bool ContactSensors::isStartActive() {
    uint8_t pin = sensorsInverted ? PIN_END_CONTACT : PIN_START_CONTACT;
    return digitalRead(pin) == HIGH;
}

bool ContactSensors::isEndActive() {
    uint8_t pin = sensorsInverted ? PIN_START_CONTACT : PIN_END_CONTACT;
    return digitalRead(pin) == HIGH;
}

bool ContactSensors::isStartClear() {
    uint8_t pin = sensorsInverted ? PIN_END_CONTACT : PIN_START_CONTACT;
    return digitalRead(pin) == LOW;
}

bool ContactSensors::isEndClear() {
    uint8_t pin = sensorsInverted ? PIN_START_CONTACT : PIN_END_CONTACT;
    return digitalRead(pin) == LOW;
}

bool ContactSensors::isActive(uint8_t pin) {
    // Generic method - apply inversion if using standard pins
    if (sensorsInverted) {
        if (pin == PIN_START_CONTACT) pin = PIN_END_CONTACT;
        else if (pin == PIN_END_CONTACT) pin = PIN_START_CONTACT;
    }
    return digitalRead(pin) == HIGH;
}

bool ContactSensors::isClear(uint8_t pin) {
    // Generic method - apply inversion if using standard pins
    if (sensorsInverted) {
        if (pin == PIN_START_CONTACT) pin = PIN_END_CONTACT;
        else if (pin == PIN_END_CONTACT) pin = PIN_START_CONTACT;
    }
    return digitalRead(pin) == LOW;
}

// ============================================================================
// DRIFT DETECTION & CORRECTION
// ============================================================================

bool ContactSensors::checkAndCorrectDriftEnd() {
    // LEVEL 3: SOFT DRIFT at END
    // Position beyond config.maxStep but within buffer zone (SAFETY_OFFSET_STEPS)
    // Action: Physically move motor backward to correct position
    
    if (currentStep > config.maxStep && currentStep <= config.maxStep + SAFETY_OFFSET_STEPS) {
        int correctionSteps = currentStep - config.maxStep;
        
        engine->debug(String("🔧 LEVEL 3 - Soft drift END: ") + String(currentStep) + 
              " steps (" + String(currentStep / STEPS_PER_MM, 2) + "mm) → " +
              "Backing " + String(correctionSteps) + " steps to config.maxStep (" + 
              String(config.maxStep / STEPS_PER_MM, 2) + "mm)");
        
        Motor.setDirection(false);  // Backward (includes 50µs delay)
        for (int i = 0; i < correctionSteps; i++) {
            Motor.step();
            currentStep--;  // Update position as we move
        }
        
        // Now physically synchronized at config.maxStep
        currentStep = config.maxStep;
        engine->debug(String("✓ Position physically corrected to config.maxStep (") + 
              String(config.maxStep / STEPS_PER_MM, 2) + "mm)");
        
        return true;  // Drift corrected, caller should reverse direction
    }
    
    return false;  // No drift detected
}

bool ContactSensors::checkAndCorrectDriftStart() {
    // LEVEL 1: SOFT DRIFT at START
    // Negative position within buffer zone (-SAFETY_OFFSET_STEPS to 0)
    // Action: Physically move motor forward to correct position
    
    if (currentStep < 0 && currentStep >= -SAFETY_OFFSET_STEPS) {
        int correctionSteps = abs(currentStep);
        
        engine->debug(String("🔧 LEVEL 1 - Soft drift START: ") + String(currentStep) + 
              " steps (" + String(currentStep / STEPS_PER_MM, 2) + "mm) → " +
              "Advancing " + String(correctionSteps) + " steps to position 0");
        
        Motor.setDirection(true);  // Forward (includes 50µs delay)
        for (int i = 0; i < correctionSteps; i++) {
            Motor.step();
            currentStep++;  // Update position as we move
        }
        
        // Now physically synchronized at position 0
        currentStep = 0;
        config.minStep = 0;
        engine->debug("✓ Position physically corrected to 0");
        
        return true;  // Drift corrected, caller should return
    }
    
    return false;  // No drift detected
}

bool ContactSensors::checkHardDriftEnd() {
    // HARD DRIFT at END: Physical contact reached = critical error
    // OPTIMIZATION: Only test when close to config.maxStep (reduces false positives + CPU overhead)
    
    long stepsToLimit = config.maxStep - currentStep;
    float distanceToLimitMM = stepsToLimit / STEPS_PER_MM;
    
    if (distanceToLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
        // Close to limit → activate opto sensor test (no debounce needed)
        if (isEndActive()) {
            float currentPos = currentStep / STEPS_PER_MM;
            
            engine->error(String("🔴 Hard drift END! Opto triggered at ") + 
                  String(currentPos, 1) + "mm (currentStep: " + String(currentStep) + 
                  " | " + String(distanceToLimitMM, 1) + "mm from limit)");
            
            Status.sendError("❌ CRITICAL ERROR: Opto END triggered - Position drifted beyond safety buffer");
            
            stopMovement();
            config.currentState = STATE_ERROR;
            Motor.disable();  // Safety: disable motor on critical error
            
            return false;  // Hard drift detected - critical error
        }
    }
    
    return true;  // Safe to continue
}

bool ContactSensors::checkHardDriftStart() {
    // HARD DRIFT at START: Physical contact reached = critical error
    // OPTIMIZATION: Only test when close to position 0 (reduces false positives + CPU overhead)
    
    float distanceToStartMM = currentStep / STEPS_PER_MM;
    
    
    if (distanceToStartMM <= HARD_DRIFT_TEST_ZONE_MM) {
        // Close to start → activate opto sensor test (no debounce needed)
        if (isStartActive()) {
            float currentPos = currentStep / STEPS_PER_MM;
            
            engine->error(String("🔴 Hard drift START! Opto triggered at ") +
                  String(currentPos, 1) + "mm (currentStep: " + String(currentStep) + 
                  " | " + String(distanceToStartMM, 1) + "mm from start)");
            
            Status.sendError("❌ CRITICAL ERROR: Opto START triggered - Position drifted beyond safety buffer");
            
            stopMovement();
            config.currentState = STATE_ERROR;
            Motor.disable();  // Safety: disable motor on critical error
            
            return false;  // Hard drift detected - critical error
        }
    }
    
    return true;  // Safe to continue
}
