// ============================================================================
// CONTACT_SENSORS.H - Opto Sensor Abstraction (OPTIMIZED)
// ============================================================================
// OPTO LOGIC:  HIGH = BLOCKED/ACTIVE  |  LOW = CLEAR/INACTIVE
// ============================================================================

#ifndef CONTACT_SENSORS_H
#define CONTACT_SENSORS_H

#include <Arduino.h>
#include "core/Config.h"

class ContactSensors {
public:
    static ContactSensors& getInstance();
    void init();
    
    // ========================================================================
    // SIMPLE API - Direct opto reading (no debounce needed)
    // ========================================================================
    bool isStartActive();       // true if START opto blocked (HIGH)
    bool isEndActive();         // true if END opto blocked (HIGH)
    bool isStartClear();        // true if START opto clear (LOW)
    bool isActive(uint8_t pin); // Generic: true if pin is HIGH (blocked)
    bool isClear(uint8_t pin);  // Generic: true if pin is LOW (clear)
    
    // ========================================================================
    // DRIFT DETECTION & CORRECTION
    // ========================================================================
    bool checkAndCorrectDriftEnd();
    bool checkAndCorrectDriftStart();
    bool checkHardDriftEnd();
    bool checkHardDriftStart();

private:
    ContactSensors() = default;
    ContactSensors(const ContactSensors&) = delete;
    ContactSensors& operator=(const ContactSensors&) = delete;
    bool m_initialized = false;
};

// Global accessor (singleton reference)
extern ContactSensors& Contacts;

#endif // CONTACT_SENSORS_H
