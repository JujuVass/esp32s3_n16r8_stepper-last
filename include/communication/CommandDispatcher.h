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
 * Module Singletons Used:
 * - SeqTable: Sequence table CRUD operations
 * - SeqExecutor: Sequence execution control
 * - Osc: Oscillation mode control
 * - Pursuit: Pursuit mode control
 * - Chaos: Chaos mode control
 * - Calibration: Calibration operations
 * 
 * @author Refactored from stepper_controller_restructured.ino
 * @version 2.0 - Cleaned up extern declarations
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/UtilityEngine.h"
#include "core/Validators.h"
#include "core/GlobalState.h"

// Module singletons
#include "movement/SequenceExecutor.h"
#include "movement/SequenceTableManager.h"
#include "movement/OscillationController.h"
#include "movement/PursuitController.h"
#include "movement/ChaosController.h"
#include "movement/BaseMovementController.h"
#include "movement/CalibrationManager.h"

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
