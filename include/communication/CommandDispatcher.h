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
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
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
    void begin(AsyncWebSocket* ws);

    /**
     * Main WebSocket event handler - register with ws.onEvent()
     */
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

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
    AsyncWebSocket* _webSocket = nullptr;

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
     * Commands: setOscillation, startOscillation, stopOscillation
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
    [[nodiscard]] bool parseJsonCommand(const String& jsonStr, JsonDocument& doc);

    /**
     * Validate and report errors via WebSocket
     * @return true if validation passed
     */
    [[nodiscard]] bool validateAndReport(bool isValid, const String& errorMsg);

    /**
     * Apply cycle pause configuration from JSON to a CyclePauseConfig struct
     * Shared logic for both Simple (updateCyclePause) and Oscillation (updateCyclePauseOsc)
     * @param target Reference to the CyclePauseConfig to update
     * @param doc JSON document with pause parameters
     * @param label Log label (e.g., "VAET" or "OSC")
     */
    void applyCyclePauseConfig(CyclePauseConfig& target, JsonDocument& doc, const char* label);

    // ========================================================================
    // EXTRACTED COMMAND BODIES (reduce cognitive complexity of handlers)
    // ========================================================================

    /** Start command logic (validation + movement start) */
    bool cmdStart(JsonDocument& doc);

    /** addSequenceLine command logic (parse + validate + add) */
    bool cmdAddSequenceLine(const JsonDocument& doc);

    /** Zone effect configuration parsing from JSON */
    void applyZoneEffectConfig(JsonDocument& doc);

    /** Oscillation config from setOscillation command */
    bool applyOscillationConfig(JsonDocument& doc);

    /** startChaos command body */
    bool cmdStartChaos(JsonDocument& doc);

    /** setChaosConfig command body */
    bool cmdSetChaosConfig(JsonDocument& doc);

    // ========================================================================
    // SUB-HANDLERS (extracted to reduce cognitive complexity)
    // ========================================================================

    /** handleBasicCommands sub: system settings (maxDistanceLimit, sensorsInverted) */
    bool handleSystemSettingsCommands(const char* cmd, JsonDocument& doc);

    /** handleBasicCommands sub: debug & stats (toggleDebug, requestStats) */
    bool handleDebugAndStatsCommands(const char* cmd, JsonDocument& doc);

    /** applyZoneEffectConfig sub: zone enable/disable + mirror + zoneSize */
    void applyZoneSettings(JsonDocument& doc);

    /** applyZoneEffectConfig sub: speedEffect + speedCurve + intensity */
    void applySpeedEffectConfig(JsonDocument& doc);

    /** applyZoneEffectConfig sub: randomTurnback + turnbackChance */
    void applyTurnbackConfig(JsonDocument& doc);

    /** applyZoneEffectConfig sub: endPause settings */
    void applyEndPauseConfig(JsonDocument& doc);

    /** applyZoneEffectConfig sub: debug logging of zone config */
    void logZoneEffectDebug();

    /** handleSequencerCommands sub: CRUD operations (add/delete/update/move/reorder/duplicate/toggle/clear/get) */
    bool handleSequencerCRUD(const char* cmd, JsonDocument& doc);

    /** handleSequencerCommands sub: execution control (start/loop/stop/pause/skip) */
    bool handleSequencerControl(const char* cmd);

    /** handleSequencerCommands sub: import/export */
    bool handleSequencerIO(const char* cmd, JsonDocument& doc);

    /** applyOscillationConfig sub: live transitions for center/amplitude changes */
    void applyOscillationLiveTransitions(float oldCenter, float oldAmplitude,
                                          float oldFrequency, OscillationWaveform oldWaveform);
};

// ============================================================================
// GLOBAL ACCESSOR
// ============================================================================

inline CommandDispatcher& Dispatcher = CommandDispatcher::getInstance();
