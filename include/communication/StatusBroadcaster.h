/**
 * ============================================================================
 * StatusBroadcaster.h - WebSocket Status Broadcasting
 * ============================================================================
 * 
 * Manages the broadcasting of system status via WebSocket.
 * Handles mode-specific JSON construction and optimization.
 * 
 * Extracted from stepper_controller_restructured.ino (~300 lines)
 * 
 * @author Refactored from main file
 * @version 1.0
 */

#ifndef STATUS_BROADCASTER_H
#define STATUS_BROADCASTER_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "Types.h"
#include "Config.h"
#include "UtilityEngine.h"

// ============================================================================
// EXTERN REFERENCES TO MAIN'S GLOBAL VARIABLES
// ============================================================================

// System config (from main)
extern SystemConfig config;

// Position tracking (from main)
extern volatile long currentStep;
extern bool isPaused;

// Movement type (from main)
extern MovementType currentMovement;

// Total distance traveled (from main)
extern unsigned long totalDistanceTraveled;

// VA-ET-VIENT configs (from main)
extern MotionConfig motion;
extern PendingMotionConfig pendingMotion;
extern DecelZoneConfig decelZone;
extern CyclePauseState motionPauseState;

// Pursuit state (from main)
extern PursuitState pursuit;

// Oscillation configs (from main)
extern OscillationConfig oscillation;
extern OscillationState oscillationState;
extern CyclePauseState oscPauseState;
extern float actualOscillationSpeedMMS;

// Chaos configs (from main)
extern ChaosRuntimeConfig chaos;
extern ChaosExecutionState chaosState;

// Max distance limit (from main)
extern float maxDistanceLimitPercent;
extern float effectiveMaxDistanceMM;

// Stats on-demand tracking (from main)
extern bool statsRequested;

// WebSocket (from main)
extern WebSocketsServer webSocket;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
float speedLevelToCyclesPerMin(float speedLevel);

// ============================================================================
// STATUS BROADCASTER CLASS
// ============================================================================

class StatusBroadcaster {
public:
    // Singleton pattern
    static StatusBroadcaster& getInstance();
    
    // ========================================================================
    // INITIALIZATION
    // ========================================================================
    
    /**
     * Initialize the broadcaster with WebSocket reference
     * @param ws Pointer to WebSocketsServer for broadcasting
     */
    void begin(WebSocketsServer* ws);
    
    // ========================================================================
    // BROADCASTING
    // ========================================================================
    
    /**
     * Send complete system status via WebSocket
     * Optimized for current movement type (only sends relevant data)
     */
    void send();
    
    /**
     * Request system stats to be included in next status broadcast
     * Stats are included on-demand to reduce overhead
     */
    void requestStats() { statsRequested = true; }
    
private:
    StatusBroadcaster() : _webSocket(nullptr) {}
    StatusBroadcaster(const StatusBroadcaster&) = delete;
    StatusBroadcaster& operator=(const StatusBroadcaster&) = delete;
    
    WebSocketsServer* _webSocket;
    
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    /**
     * Add VA-ET-VIENT / Pursuit mode specific fields to JSON
     */
    void addVaEtVientFields(JsonDocument& doc);
    
    /**
     * Add Oscillation mode specific fields to JSON
     */
    void addOscillationFields(JsonDocument& doc);
    
    /**
     * Add Chaos mode specific fields to JSON
     */
    void addChaosFields(JsonDocument& doc);
    
    /**
     * Add system stats fields to JSON (on-demand)
     */
    void addSystemStats(JsonDocument& doc);
};

// Global accessor (singleton)
extern StatusBroadcaster& Status;

#endif // STATUS_BROADCASTER_H
