// ============================================================================
// CALIBRATION_MANAGER.H - Stepper Motor Calibration Controller
// ============================================================================
// Handles all calibration logic: initial calibration, return to start,
// validation, and error recovery
// ============================================================================

#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include "core/Config.h"
#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"

// No forward declarations needed — ESPAsyncWebServer handles events asynchronously

/**
 * Calibration Manager
 *
 * Singleton class managing all calibration operations for the stepper motor.
 * Handles contact detection, position synchronization, and error recovery.
 *
 * Calibration process:
 * 1. Find START contact (move backward until contact detected)
 * 2. Release contact slowly + add safety offset → Position 0
 * 3. Find END contact (move forward until contact detected)
 * 4. Release contact + safety offset → config.maxStep
 * 5. Return to START and validate accuracy
 *
 * Dependencies:
 * - MotorDriver: For motor control (step, direction, enable)
 * - ContactSensors: For debounced contact reading
 * - WebSocket/Server: For status updates during long operations
 */
class CalibrationManager {
public:
    // ========================================================================
    // SINGLETON ACCESS
    // ========================================================================

    /**
     * Get singleton instance
     * @return Reference to the global CalibrationManager instance
     */
    static CalibrationManager& getInstance();

    // ========================================================================
    // INITIALIZATION
    // ========================================================================

    /**
     * Initialize calibration manager
     * Must be called in setup() after all modules are initialized
     */
    void init();

    // ========================================================================
    // CALIBRATION OPERATIONS
    // ========================================================================

    /**
     * Start full calibration sequence
     * Finds START and END contacts, measures total distance, returns to position 0
     *
     * This is a BLOCKING operation that may take several seconds.
     * WebSocket is serviced during operation to maintain connection.
     *
     * @return true if calibration successful, false on error
     */
    bool startCalibration();

    /**
     * Return to START position (position 0)
     * Uses contact detection for precise repositioning (same as calibration)
     * Can be used to recover from ERROR state.
     *
     * @return true if successful, false on error
     */
    bool returnToStart();

    // ========================================================================
    // CALLBACKS (set by main code)
    // ========================================================================

    /**
     * Set callback for status updates
     * Called to send WebSocket status messages during calibration
     */
    void setStatusCallback(void (*callback)());

    /**
     * Set callback for error messages
     * Called when calibration encounters an error
     */
    void setErrorCallback(void (*callback)(const String& msg));

    /**
     * Set callback for movement completion
     * Called when calibration completes (for sequencer integration)
     */
    void setCompletionCallback(void (*callback)());

private:
    // Singleton pattern
    CalibrationManager() = default;
    CalibrationManager(const CalibrationManager&) = delete;
    CalibrationManager& operator=(const CalibrationManager&) = delete;

    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================

    /**
     * Find a contact (START or END) by moving in specified direction
     * @param moveForward Direction to move (true=forward, false=backward)
     * @param contactPin GPIO pin for contact sensor
     * @param contactName Name for logging ("START" or "END")
     * @return true if contact found, false on timeout/error
     */
    bool findContact(bool moveForward, uint8_t contactPin, const char* contactName);

    /**
     * Release contact slowly and add safety margin
     * @param contactPin GPIO pin to monitor
     * @param moveForward Direction to move away from contact
     */
    void releaseContact(uint8_t contactPin, bool moveForward);

    /**
     * Handle calibration failure (disable motor, update state)
     * @return false (always, for chaining)
     */
    bool handleFailure();

    /** Validate calibrated distance range. @return 0=OK, 1=retry, -1=fail */
    int validateDistance();

    /** Validate accuracy against tolerance. @return 0=OK, 1=retry, -1=fail */
    int validateCalibrationAccuracy();

    /** Position at 10% and finalize calibrated state. */
    bool finalizeCalibration();

    /**
     * Validate calibration accuracy by checking return position
     * @return Error percentage (0 = perfect)
     */
    float validateAccuracy();

    /**
     * Service delay during long operations (yield + status callback)
     * @param durationMs How long to delay (in ms)
     */
    void serviceDelay(unsigned long durationMs);

    /**
     * Conditionally yield every N steps (reduces nesting in step loops)
     * @param stepCount Current step counter
     */
    void yieldIfDue(unsigned long stepCount);

    /**
     * Position motor at a target step offset from current (with WS service)
     * @param targetSteps Target step position
     */
    void positionAtOffset(long targetSteps);

    /**
     * Emergency decontact from END sensor
     * @return true if successfully decontacted, false on failure
     */
    bool emergencyDecontactEnd();

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    // No external dependencies needed — async WebSocket handles itself

    // Callbacks
    void (*statusCallback_)() = nullptr;
    void (*errorCallback_)(const String&) = nullptr;
    void (*completionCallback_)() = nullptr;

    // State
    bool initialized_ = false;
    bool calibrated_ = false;
    int attemptCount_ = 0;
    float lastErrorPercent_ = 0.0f;

    // Calibration results
    long maxStep_ = 0;
    float totalDistanceMM_ = 0.0f;
};

// ============================================================================
// GLOBAL ACCESSOR (singleton reference)
// ============================================================================
// Usage: Calibration.startCalibration() instead of CalibrationManager::getInstance().startCalibration()

inline CalibrationManager& Calibration = CalibrationManager::getInstance();

#endif // CALIBRATION_MANAGER_H
