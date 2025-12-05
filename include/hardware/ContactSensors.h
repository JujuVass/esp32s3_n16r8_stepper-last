// ============================================================================
// CONTACT_SENSORS.H - Limit Switch / Contact Sensor Abstraction
// ============================================================================
// Hardware abstraction for physical limit switches (start/end contacts)
// Provides debounced reading with configurable parameters
// ============================================================================

#ifndef CONTACT_SENSORS_H
#define CONTACT_SENSORS_H

#include <Arduino.h>
#include "Config.h"

/**
 * Contact Sensors Abstraction
 * 
 * Singleton class providing hardware abstraction for limit switches.
 * Handles debouncing and provides multiple read modes (fast/safe).
 * 
 * Hardware connections (from Config.h):
 * - PIN_START_CONTACT (47): Start position limit switch (Pull-up)
 * - PIN_END_CONTACT (21): End position limit switch (Pull-up)
 * 
 * Contact behavior:
 * - Contacts are normally HIGH (open) with internal pull-up
 * - Contact engaged = LOW (closed to ground)
 * - Debouncing uses majority voting (e.g., 2/3 or 3/5 readings)
 */
class ContactSensors {
public:
    /**
     * Get singleton instance
     * @return Reference to the global ContactSensors instance
     */
    static ContactSensors& getInstance();
    
    /**
     * Initialize GPIO pins for contact sensors
     * Must be called in setup() before any contact reading
     */
    void init();
    
    // ========================================================================
    // DEBOUNCED READING (Safe - for critical operations)
    // ========================================================================
    
    /**
     * Read START contact with debouncing
     * Uses majority voting to reject noise/bounce
     * 
     * @param checks Number of samples to take (default: 3)
     * @param delayUs Microseconds between samples (default: 100µs)
     * @return true if contact is engaged (switch closed/LOW)
     */
    bool isStartContactActive(uint8_t checks = 3, uint16_t delayUs = 100);
    
    /**
     * Read END contact with debouncing
     * Uses majority voting to reject noise/bounce
     * 
     * @param checks Number of samples to take (default: 3)
     * @param delayUs Microseconds between samples (default: 100µs)
     * @return true if contact is engaged (switch closed/LOW)
     */
    bool isEndContactActive(uint8_t checks = 3, uint16_t delayUs = 100);
    
    // ========================================================================
    // RAW READING (Fast - for high-frequency polling)
    // ========================================================================
    
    /**
     * Read START contact raw (no debouncing)
     * Use only when speed is critical and occasional false reads are acceptable
     * 
     * @return true if contact is engaged (switch closed/LOW)
     */
    bool readStartContactRaw();
    
    /**
     * Read END contact raw (no debouncing)
     * Use only when speed is critical and occasional false reads are acceptable
     * 
     * @return true if contact is engaged (switch closed/LOW)
     */
    bool readEndContactRaw();
    
    // ========================================================================
    // GENERIC DEBOUNCED READING
    // ========================================================================
    
    /**
     * Generic debounced contact reading
     * Can be used for any digital input with debouncing
     * 
     * Algorithm: Takes N samples, requires majority (N/2+1) to confirm state
     * Example: 3 checks requires 2 matching, 5 checks requires 3 matching
     * 
     * @param pin GPIO pin number
     * @param expectedState Expected state when contact is active (typically LOW)
     * @param checks Number of samples (default: 3)
     * @param delayUs Microseconds between samples (default: 100µs)
     * @return true if majority of samples match expectedState
     */
    bool readDebounced(uint8_t pin, uint8_t expectedState, uint8_t checks = 3, uint16_t delayUs = 100);
    
    // ========================================================================
    // DRIFT DETECTION & CORRECTION (Phase 3)
    // ========================================================================
    // Multi-level drift handling for va-et-vient and chaos modes
    // Called during doStep() to detect and correct position drift
    
    /**
     * Check and correct soft drift at END position
     * Soft drift = position beyond maxStep but within buffer zone
     * Action: Physically moves motor backward to correct position
     * 
     * @return true if drift was detected and corrected (caller should reverse direction)
     */
    bool checkAndCorrectDriftEnd();
    
    /**
     * Check and correct soft drift at START position
     * Soft drift = negative position within buffer zone
     * Action: Physically moves motor forward to correct position
     * 
     * @return true if drift was detected and corrected (caller should return)
     */
    bool checkAndCorrectDriftStart();
    
    /**
     * Check for hard drift (physical contact) at END
     * Hard drift = critical error, physical contact reached
     * Action: Emergency stop, ERROR state
     * 
     * @return true if safe to continue, false if hard drift detected (critical error)
     */
    bool checkHardDriftEnd();
    
    /**
     * Check for hard drift (physical contact) at START
     * Hard drift = critical error, physical contact reached
     * Action: Emergency stop, ERROR state
     * 
     * @return true if safe to continue, false if hard drift detected (critical error)
     */
    bool checkHardDriftStart();

private:
    // Singleton pattern
    ContactSensors() = default;
    ContactSensors(const ContactSensors&) = delete;
    ContactSensors& operator=(const ContactSensors&) = delete;
    
    bool m_initialized = false;
};

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
// Global accessor for simplified syntax throughout codebase
// Usage: Contacts.isStartContactActive() instead of ContactSensors::getInstance().isStartContactActive()

#define Contacts ContactSensors::getInstance()

#endif // CONTACT_SENSORS_H
