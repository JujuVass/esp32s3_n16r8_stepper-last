// ============================================================================
// CONTACT_SENSORS.CPP - Limit Switch / Contact Sensor Implementation
// ============================================================================

#include "hardware/ContactSensors.h"
#include "hardware/MotorDriver.h"
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
    if (m_initialized) return;  // Prevent double initialization
    
    // Configure contact pins as inputs with internal pull-up
    // Contacts are normally open (HIGH), closed when engaged (LOW)
    pinMode(PIN_START_CONTACT, INPUT_PULLUP);
    pinMode(PIN_END_CONTACT, INPUT_PULLUP);
    
    m_initialized = true;
}

// ============================================================================
// DEBOUNCED READING - SPECIFIC CONTACTS
// ============================================================================

bool ContactSensors::isStartContactActive(uint8_t checks, uint16_t delayUs) {
    // Contact is active when LOW (closed to ground)
    return readDebounced(PIN_START_CONTACT, LOW, checks, delayUs);
}

bool ContactSensors::isEndContactActive(uint8_t checks, uint16_t delayUs) {
    // Contact is active when LOW (closed to ground)
    return readDebounced(PIN_END_CONTACT, LOW, checks, delayUs);
}

// ============================================================================
// RAW READING (NO DEBOUNCE)
// ============================================================================

bool ContactSensors::readStartContactRaw() {
    return digitalRead(PIN_START_CONTACT) == LOW;
}

bool ContactSensors::readEndContactRaw() {
    return digitalRead(PIN_END_CONTACT) == LOW;
}

// ============================================================================
// GENERIC DEBOUNCED READING
// ============================================================================

bool ContactSensors::readDebounced(uint8_t pin, uint8_t expectedState, uint8_t checks, uint16_t delayUs) {
    // Majority voting algorithm
    // Requires (checks/2 + 1) matching reads to confirm state
    // Example: 3 checks requires 2/3, 5 checks requires 3/5
    
    int validCount = 0;
    int requiredValid = (checks + 1) / 2;  // Ceiling division for majority
    
    for (uint8_t i = 0; i < checks; i++) {
        if (digitalRead(pin) == expectedState) {
            validCount++;
            
            // Early exit: majority already reached
            if (validCount >= requiredValid) {
                return true;
            }
        }
        
        // Delay between samples (except after last sample)
        if (i < checks - 1) {
            delayMicroseconds(delayUs);
        }
    }
    
    // Not enough valid reads to confirm expected state
    return false;
}

// ============================================================================
// DRIFT DETECTION & CORRECTION (Phase 3)
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
        // Close to limit → activate physical contact test
        if (readDebounced(PIN_END_CONTACT, LOW, 5, 75)) {
            float currentPos = currentStep / STEPS_PER_MM;
            
            engine->error(String("🔴 Hard drift END! Physical contact at ") + 
                  String(currentPos, 1) + "mm (currentStep: " + String(currentStep) + 
                  " | " + String(distanceToLimitMM, 1) + "mm from limit)");
            
            sendError("❌ ERREUR CRITIQUE: Contact END atteint - Position dérivée au-delà du buffer de sécurité");
            
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
        // Close to start → activate physical contact test
        if (readDebounced(PIN_START_CONTACT, LOW, 5, 75)) {
            float currentPos = currentStep / STEPS_PER_MM;
            
            engine->error(String("🔴 Hard drift START! Physical contact at ") +
                  String(currentPos, 1) + "mm (currentStep: " + String(currentStep) + 
                  " | " + String(distanceToStartMM, 1) + "mm from start)");
            
            sendError("❌ ERREUR CRITIQUE: Contact START atteint - Position dérivée au-delà du buffer de sécurité");
            
            stopMovement();
            config.currentState = STATE_ERROR;
            Motor.disable();  // Safety: disable motor on critical error
            
            return false;  // Hard drift detected - critical error
        }
    }
    
    return true;  // Safe to continue
}
