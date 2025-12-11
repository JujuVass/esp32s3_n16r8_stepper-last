// ============================================================================
// MOTOR_DRIVER.H - HSS86 Stepper Motor Driver Abstraction
// ============================================================================
// Hardware abstraction layer for HSS86 closed-loop stepper driver
// Encapsulates all low-level motor control operations
// ============================================================================

#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <Arduino.h>
#include "core/Config.h"

/**
 * HSS86 Motor Driver Abstraction
 * 
 * Singleton class providing hardware abstraction for the HSS86 stepper driver.
 * All direct GPIO manipulation for motor control should go through this class.
 * 
 * Hardware connections (from Config.h):
 * - PIN_PULSE (12): Step pulse signal (Yellow cable)
 * - PIN_DIR (13): Direction signal (Orange cable)
 * - PIN_ENABLE (14): Enable signal - Active LOW (Red cable)
 * 
 * Timing requirements (HSS86):
 * - Minimum pulse width: 2.5µs (we use STEP_PULSE_MICROS = 3µs)
 * - Direction setup time: 5µs before step (we use DIR_CHANGE_DELAY_MICROS = 30µs)
 * - Enable active LOW
 */
class MotorDriver {
public:
    /**
     * Get singleton instance
     * @return Reference to the global MotorDriver instance
     */
    static MotorDriver& getInstance();
    
    /**
     * Initialize GPIO pins for motor control
     * Must be called in setup() before any motor operations
     */
    void init();
    
    /**
     * Execute a single step pulse
     * Timing: HIGH for STEP_PULSE_MICROS, LOW for STEP_PULSE_MICROS
     * Total execution time: ~6µs (2 × 3µs)
     */
    void step();
    
    /**
     * Set motor direction
     * Only updates GPIO if direction changed (optimization)
     * Includes DIR_CHANGE_DELAY_MICROS delay after direction change
     * 
     * @param forward true = forward (HIGH on DIR pin), false = backward (LOW)
     */
    void setDirection(bool forward);
    
    /**
     * Get current direction setting
     * @return true if forward, false if backward
     */
    bool getDirection() const;
    
    /**
     * Enable motor driver (start holding torque)
     * HSS86 ENABLE is active LOW
     */
    void enable();
    
    /**
     * Disable motor driver (release torque)
     * HSS86 ENABLE is active LOW, so we set HIGH to disable
     */
    void disable();
    
    /**
     * Check if motor is currently enabled
     * @return true if motor is enabled (holding torque)
     */
    bool isEnabled() const;
    
    // ========================================================================
    // HSS86 FEEDBACK SIGNALS (ALM & PEND)
    // ========================================================================
    
    /**
     * Check if HSS86 alarm is active
     * ALM signal is LOW when alarm (position error, over-current, overheat)
     * @return true if alarm is active (problem detected)
     */
    bool isAlarmActive() const;
    
    /**
     * Check if motor has reached commanded position
     * PEND signal is HIGH when motor is at position
     * @return true if motor is at commanded position
     */
    bool isPositionReached() const;
    
    /**
     * Get time since last position reached (PEND went HIGH)
     * Useful for detecting motor lag/load
     * @return milliseconds since PEND was last HIGH, or 0 if currently at position
     */
    unsigned long getPositionLagMs() const;
    
    /**
     * Update PEND tracking (call from motorTask loop)
     * Tracks timing for position lag calculation
     */
    void updatePendTracking();
    
    /**
     * Get PEND interrupt counter (for debugging)
     * Counts both RISING and FALLING edges on PEND pin
     * @return total number of PEND transitions detected by ISR
     */
    unsigned long getPendInterruptCount() const;
    
    /**
     * Reset PEND interrupt counter
     */
    void resetPendInterruptCount();

private:
    // Singleton pattern - prevent external construction
    MotorDriver() = default;
    MotorDriver(const MotorDriver&) = delete;
    MotorDriver& operator=(const MotorDriver&) = delete;
    
    bool m_enabled = false;
    bool m_direction = true;  // true = forward (HIGH)
    bool m_initialized = false;
    
    // PEND tracking for lag detection
    unsigned long m_lastPendHighMs = 0;
    bool m_lastPendState = true;
};

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================
// Global accessor for simplified syntax throughout codebase
// Usage: Motor.step() instead of MotorDriver::getInstance().step()

#define Motor MotorDriver::getInstance()

#endif // MOTOR_DRIVER_H
