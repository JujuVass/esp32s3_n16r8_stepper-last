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

// Forward declarations (avoid circular includes)
class WebSocketsServer;
class WebServer;

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
     * Initialize calibration manager with external dependencies
     * Must be called in setup() after WebSocket and WebServer are initialized
     * 
     * @param ws Pointer to WebSocketsServer instance
     * @param server Pointer to WebServer instance
     */
    void init(WebSocketsServer* ws, WebServer* server);
    
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
    
    /**
     * Quick position check - verify if at expected position
     * Does NOT move motor, just checks current step vs expected
     * 
     * @return true if currentStep == 0
     */
    bool isAtStart() const;
    
    /**
     * Check if system has been calibrated
     * @return true if calibration has completed successfully
     */
    bool isCalibrated() const;
    
    // ========================================================================
    // CALIBRATION RESULTS (read-only access)
    // ========================================================================
    
    /**
     * Get calibrated total distance in mm
     * @return Total travel distance (0 if not calibrated)
     */
    float getTotalDistanceMM() const;
    
    /**
     * Get maximum step position
     * @return Max step count (0 if not calibrated)
     */
    long getMaxStep() const;
    
    /**
     * Get last calibration error percentage
     * @return Error % from last return-to-start validation
     */
    float getLastErrorPercent() const;
    
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
    
    /**
     * Validate calibration accuracy by checking return position
     * @return Error percentage (0 = perfect)
     */
    float validateAccuracy();
    
    /**
     * Service WebSocket during long operations
     * @param durationMs How long to service (in ms)
     */
    void serviceWebSocket(unsigned long durationMs);
    
    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================
    
    // External dependencies (set via init())
    WebSocketsServer* m_webSocket = nullptr;
    WebServer* m_server = nullptr;
    
    // Callbacks
    void (*m_statusCallback)() = nullptr;
    void (*m_errorCallback)(const String&) = nullptr;
    void (*m_completionCallback)() = nullptr;
    
    // State
    bool m_initialized = false;
    bool m_calibrated = false;
    int m_attemptCount = 0;
    float m_lastErrorPercent = 0.0f;
    
    // Calibration results
    long m_maxStep = 0;
    float m_totalDistanceMM = 0.0f;
};

// ============================================================================
// CONVENIENCE MACRO
// ============================================================================
// Global accessor for simplified syntax
// Usage: Calibration.startCalibration() instead of CalibrationManager::getInstance().startCalibration()

#define Calibration CalibrationManager::getInstance()

#endif // CALIBRATION_MANAGER_H
