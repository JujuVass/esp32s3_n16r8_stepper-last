/**
 * ============================================================================
 * CommandDispatcher.h - WebSocket Command Routing Module
 * ============================================================================
 * 
 * Handles all WebSocket command parsing and routing to appropriate handlers.
 * Extracts command dispatch logic from main file for better modularity.
 * 
 * Architecture:
 * - Singleton pattern for global access
 * - Chain of responsibility pattern for command handlers
 * - Each handler returns true if command was processed
 * 
 * Dependencies:
 * - WebSocketsServer for client communication
 * - ArduinoJson for JSON parsing
 * - Various extern functions for actual command execution
 * 
 * @author Refactored from stepper_controller_restructured.ino
 * @version 1.0
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include "Types.h"
#include "Config.h"
#include "UtilityEngine.h"
#include "Validators.h"
#include "sequencer/SequenceExecutor.h"

// ============================================================================
// FORWARD DECLARATIONS - External functions called by handlers
// ============================================================================

// Basic commands
extern void sendStatus();
extern void sendError(String message);
extern void returnToStart();
extern void resetTotalDistance();
extern void saveCurrentSessionStats();
extern void updateEffectiveMaxDistance();

// Motion control
extern void startMovement(float distMM, float speedLevel);
extern void stopMovement();
extern void togglePause();
extern void setDistance(float dist);
extern void setStartPosition(float startMM);
extern void setSpeedForward(float speed);
extern void setSpeedBackward(float speed);

// Pursuit mode
extern void pursuitMove(float targetMM, float maxSpeed);

// Chaos mode
extern void startChaos();
extern void stopChaos();

// Oscillation mode
extern void startOscillation();

// Sequencer (via SequenceExecutor singleton - included via header)
// Use SeqExecutor.start(), SeqExecutor.stop(), etc.
extern void broadcastSequenceTable();
extern int addSequenceLine(SequenceLine newLine);
extern bool deleteSequenceLine(int lineId);
extern bool updateSequenceLine(int lineId, SequenceLine updatedLine);
extern bool moveSequenceLine(int lineId, int direction);
extern int duplicateSequenceLine(int lineId);
extern bool toggleSequenceLine(int lineId, bool enabled);  // Toggle line enabled/disabled
extern void clearSequenceTable();
extern String exportSequenceToJson();
extern int importSequenceFromJson(String jsonData);
extern void sendJsonResponse(const char* type, const String& data);

// Validation helpers
extern String validateSequenceLinePhysics(const SequenceLine& line);
extern SequenceLine parseSequenceLineFromJson(JsonVariantConst obj);
extern void validateDecelZone();

// ============================================================================
// EXTERNAL VARIABLES - Shared state accessed by handlers
// ============================================================================

// Note: engine pointer is declared in UtilityEngine.h

extern SystemConfig config;
extern MotionConfig motion;
extern ChaosRuntimeConfig chaos;
extern ChaosExecutionState chaosState;
extern OscillationConfig oscillation;
extern OscillationState oscillationState;
extern DecelZoneConfig decelZone;
extern SequenceExecutionState seqState;
extern PursuitState pursuit;

extern float effectiveMaxDistanceMM;
extern float maxDistanceLimitPercent;
extern bool isPaused;
extern bool statsRequested;
extern unsigned long lastStatsRequestTime;
extern MovementType currentMovement;
extern int sequenceLineCount;

extern WebSocketsServer webSocket;

// ============================================================================
// COMMAND DISPATCHER CLASS
// ============================================================================

class CommandDispatcher {
public:
    /**
     * Get singleton instance
     */
    static CommandDispatcher& getInstance();
    
    /**
     * Initialize dispatcher with WebSocket server reference
     * @param ws Pointer to WebSocketsServer instance
     */
    void begin(WebSocketsServer* ws);
    
    /**
     * Main WebSocket event handler - register with webSocket.onEvent()
     */
    void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    
    /**
     * Process a command message (called internally or for testing)
     */
    void handleCommand(uint8_t clientNum, const String& message);

private:
    // Singleton - private constructor
    CommandDispatcher() = default;
    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;
    
    // WebSocket server reference
    WebSocketsServer* _webSocket = nullptr;
    
    // ========================================================================
    // COMMAND HANDLERS - Each returns true if command was handled
    // ========================================================================
    
    /**
     * Handler 1/8: Basic system commands
     * Commands: calibrate, start, pause, stop, getStatus, returnToStart, 
     *           resetTotalDistance, saveStats, setMaxDistanceLimit
     */
    bool handleBasicCommands(const char* cmd, JsonDocument& doc);
    
    /**
     * Handler 2/8: Configuration commands
     * Commands: setDistance, setStartPosition, setSpeedForward, setSpeedBackward
     */
    bool handleConfigCommands(const char* cmd, JsonDocument& doc);
    
    /**
     * Handler 3/8: Deceleration zone commands
     * Commands: setDecelZone
     */
    bool handleDecelZoneCommands(const char* cmd, JsonDocument& doc, const String& message);
    
    /**
     * Handler 4/8: Cycle pause commands (VA-ET-VIENT + Oscillation)
     * Commands: updateCyclePause, updateCyclePauseOsc
     */
    bool handleCyclePauseCommands(const char* cmd, JsonDocument& doc);
    
    /**
     * Handler 5/8: Pursuit mode commands
     * Commands: enablePursuitMode, pursuitMove
     */
    bool handlePursuitCommands(const char* cmd, JsonDocument& doc);
    
    /**
     * Handler 6/8: Chaos mode commands
     * Commands: startChaos, stopChaos, setChaosConfig
     */
    bool handleChaosCommands(const char* cmd, JsonDocument& doc, const String& message);
    
    /**
     * Handler 7/8: Oscillation mode commands
     * Commands: setOscillation, setCyclePause, startOscillation, stopOscillation
     */
    bool handleOscillationCommands(const char* cmd, JsonDocument& doc, const String& message);
    
    /**
     * Handler 8/8: Sequencer commands
     * Commands: addSequenceLine, deleteSequenceLine, updateSequenceLine, 
     *           moveSequenceLine, duplicateSequenceLine, toggleSequenceLine,
     *           clearSequence, getSequenceTable, startSequence, loopSequence,
     *           stopSequence, toggleSequencePause, skipSequenceLine,
     *           exportSequence, importSequence, toggleDebug, requestStats
     */
    bool handleSequencerCommands(const char* cmd, JsonDocument& doc, const String& message);
    
    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    
    /**
     * Parse JSON command string into document
     * @return true if parsing successful
     */
    bool parseJsonCommand(const String& jsonStr, JsonDocument& doc);
    
    /**
     * Validate and report errors via WebSocket
     * @return true if validation passed
     */
    bool validateAndReport(bool isValid, const String& errorMsg);
};

// ============================================================================
// GLOBAL ACCESSOR
// ============================================================================

extern CommandDispatcher& Dispatcher;
