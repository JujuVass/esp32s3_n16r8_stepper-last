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
#include "Types.h"
#include "Config.h"
#include "UtilityEngine.h"

// ============================================================================
// EXTERN REFERENCES TO MAIN'S GLOBAL VARIABLES
// ============================================================================
// These are defined in the main .ino file and accessed via extern

// Sequence table data (from main)
extern SequenceLine sequenceTable[];
extern int sequenceLineCount;

// Sequence execution state (from main)
extern SequenceExecutionState seqState;

// System config (from main)
extern SystemConfig config;

// Movement configs (from main)
extern MotionConfig motion;
extern OscillationConfig oscillation;
extern ChaosRuntimeConfig chaos;
extern DecelZoneConfig decelZone;
extern CyclePauseState motionPauseState;

// Position tracking (from main)
extern volatile long currentStep;
extern long startStep;
extern long targetStep;
extern bool movingForward;
extern bool hasReachedStartStep;
extern bool isPaused;
extern unsigned long lastStepMicros;

// Movement type (from main)
extern MovementType currentMovement;

// Oscillation state (from main)
extern OscillationState oscillationState;

// Chaos state (from main)
extern ChaosExecutionState chaosState;

// Effective max distance (from main)
extern float effectiveMaxDistanceMM;

// Distance tracking (from main)
extern unsigned long totalDistanceTraveled;
extern long lastStepForDistance;
extern unsigned long lastStartContactMillis;
extern unsigned long cycleTimeMillis;
extern float measuredCyclesPerMinute;
extern bool wasAtStart;

// WebSocket (from main)
extern WebSocketsServer webSocket;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
// Functions defined in main that SequenceExecutor needs to call
void sendStatus();
void sendError(String message);
void stopMovement();
void calculateStepDelay();
void validateDecelZone();
void startOscillation();
void saveCurrentSessionStats();

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
