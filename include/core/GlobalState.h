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
 * DUAL-CORE ARCHITECTURE (ESP32-S3):
 *   - Core 0 (APP_CPU): Network tasks (WiFi, WebSocket, HTTP, OTA)
 *   - Core 1 (PRO_CPU): Motor tasks (stepping, timing-critical operations)
 *   - Mutexes protect shared motion configurations
 * 
 * Created: December 2024
 * ============================================================================
 */

#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "Types.h"
#include "Config.h"

// Forward declaration
struct SystemConfig;

// ============================================================================
// DUAL-CORE TASK HANDLES & SYNCHRONIZATION
// ============================================================================

// Task handles
extern TaskHandle_t motorTaskHandle;
extern TaskHandle_t networkTaskHandle;

// Mutexes for shared data protection
extern SemaphoreHandle_t motionMutex;      // Protects: motion, pendingMotion, decelZone
extern SemaphoreHandle_t stateMutex;       // Protects: config.currentState changes

// ============================================================================
// MUTEX HELPER FUNCTIONS (inline for performance)
// ============================================================================
// Usage: if (takeMotionMutex()) { ... giveMotionMutex(); }
// Default timeout: 10ms (non-blocking for real-time motor task)

inline bool takeMotionMutex(TickType_t timeout = pdMS_TO_TICKS(10)) {
    if (motionMutex == NULL) return true;  // Not initialized yet
    return xSemaphoreTake(motionMutex, timeout) == pdTRUE;
}

inline void giveMotionMutex() {
    if (motionMutex != NULL) xSemaphoreGive(motionMutex);
}

inline bool takeStateMutex(TickType_t timeout = pdMS_TO_TICKS(10)) {
    if (stateMutex == NULL) return true;  // Not initialized yet
    return xSemaphoreTake(stateMutex, timeout) == pdTRUE;
}

inline void giveStateMutex() {
    if (stateMutex != NULL) xSemaphoreGive(stateMutex);
}

// RAII-style mutex guard for automatic release (C++ scope-based)
class MutexGuard {
public:
    MutexGuard(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(10)) 
        : _mutex(mutex), _locked(false) {
        if (_mutex != NULL) {
            _locked = (xSemaphoreTake(_mutex, timeout) == pdTRUE);
        }
    }
    ~MutexGuard() {
        if (_locked && _mutex != NULL) {
            xSemaphoreGive(_mutex);
        }
    }
    bool isLocked() const { return _locked; }
    operator bool() const { return _locked; }
private:
    SemaphoreHandle_t _mutex;
    bool _locked;
};

// Atomic flags (no mutex needed - set from Core 0, read from Core 1)
extern volatile bool emergencyStop;
extern volatile bool requestCalibration;   // Trigger calibration from motorTask

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
// NOTE: sendError() moved to StatusBroadcaster - use Status.sendError()
// NOTE: saveCurrentSessionStats() moved to UtilityEngine - use engine->saveCurrentSessionStats()
// NOTE: resetTotalDistance() moved to UtilityEngine - use engine->resetTotalDistance()
// NOTE: togglePause() removed - use BaseMovement.togglePause() directly
// NOTE: returnToStart() removed - use BaseMovement.returnToStart() directly
// NOTE: updateEffectiveMaxDistance() moved to UtilityEngine - use engine->updateEffectiveMaxDistance()

extern void sendStatus();
extern void stopMovement();

#endif // GLOBAL_STATE_H
