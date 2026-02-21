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
    bool isStartActive() const;       // true if START opto blocked (HIGH)
    bool isEndActive() const;         // true if END opto blocked (HIGH)
    bool isStartClear() const;        // true if START opto clear (LOW)
    bool isActive(uint8_t pin) const; // Generic: true if pin is HIGH (blocked)
    bool isClear(uint8_t pin) const;  // Generic: true if pin is LOW (clear)

    // ========================================================================
    // DRIFT DETECTION & CORRECTION
    // ========================================================================
    bool checkAndCorrectDriftEnd() const;
    bool checkAndCorrectDriftStart() const;
    bool checkHardDriftEnd() const;
    bool checkHardDriftStart() const;

private:
    ContactSensors() = default;
    ContactSensors(const ContactSensors&) = delete;
    ContactSensors& operator=(const ContactSensors&) = delete;
    bool m_initialized = false;
};

// Global accessor (singleton reference)
extern ContactSensors& Contacts;

#endif // CONTACT_SENSORS_H
