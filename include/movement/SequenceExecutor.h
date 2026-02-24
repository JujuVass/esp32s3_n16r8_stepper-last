/**
 * ============================================================================
 * SequenceExecutor.h - Sequence Execution Engine
 * ============================================================================
 *
 * Manages the execution of sequence tables: starting, stopping, pausing,
 * advancing through lines, and coordinating with movement controllers.
 */

#ifndef SEQUENCE_EXECUTOR_H
#define SEQUENCE_EXECUTOR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/UtilityEngine.h"
#include "core/GlobalState.h"

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Sequencer state - defined in SequenceExecutor.cpp
extern SequenceExecutionState seqState;
extern volatile MovementType currentMovement;

// ============================================================================
// SEQUENCE EXECUTOR CLASS
// ============================================================================

class SequenceExecutor {
public:
    // Singleton pattern
    static SequenceExecutor& getInstance();

    // ========================================================================
    // INITIALIZATION
    // ========================================================================

    /**
     * Initialize the executor with WebSocket reference
     * @param ws Pointer to WebSocketsServer for status broadcasting
     */
    void begin(AsyncWebSocket* ws);

    // ========================================================================
    // SEQUENCE CONTROL
    // ========================================================================

    /**
     * Start sequence execution
     * @param loopMode false=single pass, true=infinite loop
     */
    void start(bool loopMode = false);

    /**
     * Stop sequence execution
     */
    void stop();

    /**
     * Toggle pause/resume
     */
    void togglePause();

    /**
     * Skip to next sequence line
     */
    void skipToNextLine();

    // ========================================================================
    // MAIN LOOP PROCESSING
    // ========================================================================

    /**
     * Process sequence execution (call in main loop)
     * Advances through lines and manages timing
     */
    void process();

    // ========================================================================
    // STATUS
    // ========================================================================

    /**
     * Send sequence status via WebSocket
     */
    void sendStatus();

    /**
     * Check if sequence is currently running
     */
    bool isRunning() const { return seqState.isRunning; }

    /**
     * Check if sequence is paused
     */
    bool isPaused() const { return seqState.isPaused; }

    // ========================================================================
    // COMPLETION HANDLER
    // ========================================================================

    /**
     * Called when a movement cycle completes
     * Handles cycle counting and state transitions
     */
    void onMovementComplete();

    /**
     * Blocking move to a specific step position.
     * Sets/clears blockingMoveInProgress flag. Broadcasts status every 250 ms.
     * @param targetStepPos Target step position to reach
     * @param timeoutMs Maximum time allowed for the move (default 30s)
     * @return true if position reached, false if timeout
     */
    [[nodiscard]] bool blockingMoveToStep(long targetStepPos, unsigned long timeoutMs = 30000);

private:
    SequenceExecutor() = default;
    SequenceExecutor(const SequenceExecutor&) = delete;
    SequenceExecutor& operator=(const SequenceExecutor&) = delete;

    AsyncWebSocket* _webSocket = nullptr;
    unsigned long _lastPauseStatusSend = 0;  // Rate-limit status during line pauses

    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================

    /**
     * Position motor for next sequence line
     */
    void positionForNextLine();

    /**
     * Complete sequence execution (cleanup, optional return to start)
     * @param autoReturnToStart If true, motor returns to position 0 before cleanup
     */
    void completeSequence(bool autoReturnToStart);

    /**
     * Check and handle sequence end logic
     * @return true if sequence continues, false if ended
     */
    bool checkAndHandleSequenceEnd();

    /**
     * Start VA-ET-VIENT movement for current line
     */
    void startVaEtVientLine(const SequenceLine* line);

    /**
     * Start OSCILLATION movement for current line
     */
    void startOscillationLine(const SequenceLine* line);

    /**
     * Start CHAOS movement for current line
     */
    void startChaosLine(const SequenceLine* line);

    /**
     * Start CALIBRATION for current line
     */
    void startCalibrationLine(const SequenceLine* line);

    /** Handle completion of current line (pause or advance) — returns false if sequence ended */
    bool handleLineCompletion(const SequenceLine* line);

    /** Start the next cycle for the current line */
    void startNextCycle();
};

// Global accessor (singleton)
inline SequenceExecutor& SeqExecutor = SequenceExecutor::getInstance();

#endif // SEQUENCE_EXECUTOR_H
