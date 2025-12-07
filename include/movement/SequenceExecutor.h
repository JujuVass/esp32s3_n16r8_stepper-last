/**
 * ============================================================================
 * SequenceExecutor.h - Sequence Execution Engine
 * ============================================================================
 * 
 * Manages the execution of sequence tables: starting, stopping, pausing,
 * advancing through lines, and coordinating with movement controllers.
 * 
 * Extracted from stepper_controller_restructured.ino (~400 lines)
 * 
 * @author Refactored from main file
 * @version 1.0
 */

#ifndef SEQUENCE_EXECUTOR_H
#define SEQUENCE_EXECUTOR_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/UtilityEngine.h"
#include "core/GlobalState.h"

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Sequencer state - defined in SequenceExecutor.cpp (Phase 4D migration)
extern SequenceExecutionState seqState;
extern MovementType currentMovement;

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
    void begin(WebSocketsServer* ws);
    
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
    
private:
    SequenceExecutor() : _webSocket(nullptr) {}
    SequenceExecutor(const SequenceExecutor&) = delete;
    SequenceExecutor& operator=(const SequenceExecutor&) = delete;
    
    WebSocketsServer* _webSocket;
    
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
    /**
     * Position motor for next sequence line
     */
    void positionForNextLine();
    
    /**
     * Check and handle sequence end logic
     * @return true if sequence continues, false if ended
     */
    bool checkAndHandleSequenceEnd();
    
    /**
     * Start VA-ET-VIENT movement for current line
     */
    void startVaEtVientLine(SequenceLine* line);
    
    /**
     * Start OSCILLATION movement for current line
     */
    void startOscillationLine(SequenceLine* line);
    
    /**
     * Start CHAOS movement for current line
     */
    void startChaosLine(SequenceLine* line);
    
    /**
     * Start CALIBRATION for current line
     */
    void startCalibrationLine(SequenceLine* line);
};

// Global accessor (singleton)
extern SequenceExecutor& SeqExecutor;

#endif // SEQUENCE_EXECUTOR_H
