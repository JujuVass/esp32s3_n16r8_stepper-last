/**
 * ============================================================================
 * GlobalState.h - Centralized Global Variables Declaration
 * ============================================================================
 * 
 * This header centralizes all global variable declarations to avoid
 * duplicate extern declarations scattered across module headers.
 * 
 * Include this header in any module that needs access to global state.
 * The actual definitions remain in stepper_controller_restructured.ino
 * 
 * Created: December 2024
 * ============================================================================
 */

#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include "Types.h"      // Must come before any SystemConfig usage
#include "Config.h"     // Constants like STEPS_PER_MM, SAFETY_OFFSET_STEPS
#include "UtilityEngine.h" // For SystemConfig definition

// Forward declarations for module singletons
class VaEtVientControllerClass;
extern VaEtVientControllerClass VaEtVient;

// ============================================================================
// CORE SYSTEM STATE
// ============================================================================

extern SystemConfig config;

// ============================================================================
// MOTION VARIABLES
// ============================================================================

extern volatile long currentStep;
extern long startStep;
extern long targetStep;
extern bool movingForward;
extern bool hasReachedStartStep;
extern bool isPaused;
extern unsigned long lastStepMicros;
extern unsigned long stepDelayMicrosForward;
extern unsigned long stepDelayMicrosBackward;

// ============================================================================
// MOVEMENT TYPE
// ============================================================================

extern MovementType currentMovement;

// ============================================================================
// DISTANCE LIMITS
// ============================================================================

extern float effectiveMaxDistanceMM;
extern float maxDistanceLimitPercent;

// ============================================================================
// MOTION CONFIGURATIONS
// ============================================================================

extern MotionConfig motion;
extern PendingMotionConfig pendingMotion;
extern DecelZoneConfig decelZone;
extern CyclePauseState motionPauseState;

// ============================================================================
// PURSUIT MODE
// ============================================================================

extern PursuitState pursuit;

// ============================================================================
// OSCILLATION MODE
// ============================================================================

extern OscillationConfig oscillation;
extern OscillationState oscillationState;
extern CyclePauseState oscPauseState;
extern float actualOscillationSpeedMMS;

// ============================================================================
// CHAOS MODE
// ============================================================================

extern ChaosRuntimeConfig chaos;
extern ChaosExecutionState chaosState;

// ============================================================================
// SEQUENCER
// ============================================================================

extern SequenceLine sequenceTable[];
extern int sequenceLineCount;
extern SequenceExecutionState seqState;

// ============================================================================
// STATISTICS TRACKING
// ============================================================================

extern unsigned long totalDistanceTraveled;
extern long lastStepForDistance;
extern unsigned long lastStartContactMillis;
extern unsigned long cycleTimeMillis;
extern float measuredCyclesPerMinute;
extern bool wasAtStart;

// Stats on-demand
extern bool statsRequested;
extern unsigned long lastStatsRequestTime;

// ============================================================================
// WEB SERVERS
// ============================================================================

extern WebServer server;
extern WebSocketsServer webSocket;

// ============================================================================
// CALLBACK FUNCTIONS (defined in main, called by modules)
// ============================================================================

// Error/status reporting
extern void sendError(String message);
extern void sendStatus();

// Movement callbacks
extern void doStep();
extern void stopMovement();
extern void returnToStart();

// Utility functions
extern void updateEffectiveMaxDistance();
extern void validateDecelZone();
extern void resetTotalDistance();
extern void saveCurrentSessionStats();

// Movement control (defined in main)
extern void startMovement(float distMM, float speedLevel);
extern void togglePause();
extern void setDistance(float dist);
extern void setStartPosition(float startMM);
extern void setSpeedForward(float speed);
extern void setSpeedBackward(float speed);

// Chaos control (wrappers to Chaos singleton)
extern void startChaos();
extern void stopChaos();

#endif // GLOBAL_STATE_H
