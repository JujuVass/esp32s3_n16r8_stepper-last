/**
 * ============================================================================
 * GlobalState.h - Centralized Global Variables Declaration
 * ============================================================================
 * 
 * Declares extern for variables that are truly GLOBAL and shared across modules.
 * Actual definitions are in main (.ino) or owning module .cpp files.
 * 
 * Module-specific data is owned by modules - include their header:
 *   - chaos, chaosState          → ChaosController.h
 *   - oscillation, oscState...   → OscillationController.h
 *   - pursuit                    → PursuitController.h
 *   - decelZone                  → BaseMovementController.h (integrated)
 *   - seqState, currentMovement  → SequenceExecutor.h
 *   - sequenceTable[]            → SequenceTableManager.h
 * 
 * Created: December 2024
 * ============================================================================
 */

#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include "Types.h"
#include "Config.h"

// Forward declaration
struct SystemConfig;

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
extern unsigned long lastStepMicros;
extern unsigned long stepDelayMicrosForward;
extern unsigned long stepDelayMicrosBackward;

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
extern CyclePauseState motionPauseState;
extern DecelZoneConfig decelZone;  // Owned by BaseMovementController.cpp

// ============================================================================
// STATISTICS TRACKING
// ============================================================================

extern StatsTracking stats;  // Encapsulated distance tracking
extern unsigned long lastStartContactMillis;
extern unsigned long cycleTimeMillis;
extern float measuredCyclesPerMinute;
extern bool wasAtStart;
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

extern void sendError(String message);
extern void sendStatus();
extern void stopMovement();
extern void returnToStart();
extern void updateEffectiveMaxDistance();
extern void resetTotalDistance();
extern void saveCurrentSessionStats();
extern void togglePause();

#endif // GLOBAL_STATE_H
