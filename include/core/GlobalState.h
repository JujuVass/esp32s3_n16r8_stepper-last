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
extern SemaphoreHandle_t motionMutex;      // Protects: motion, pendingMotion, zoneEffect
extern SemaphoreHandle_t stateMutex;       // Protects: config.currentState changes
extern SemaphoreHandle_t statsMutex;       // Protects: stats compound operations (reset, save)

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

inline bool takeStatsMutex(TickType_t timeout = pdMS_TO_TICKS(10)) {
    if (statsMutex == NULL) return true;  // Not initialized yet
    return xSemaphoreTake(statsMutex, timeout) == pdTRUE;
}

inline void giveStatsMutex() {
    if (statsMutex != NULL) xSemaphoreGive(statsMutex);
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
// Safe: bool and long are 32-bit on ESP32 Xtensa → single-instruction read/write
extern volatile bool emergencyStop;
extern volatile bool requestCalibration;    // Trigger calibration from motorTask
extern volatile bool calibrationInProgress; // When true, networkTask skips webSocket/server
                                            // (CalibrationManager handles them internally)
extern volatile bool blockingMoveInProgress; // When true, networkTask skips webSocket/server
                                             // (blocking move loops service them from Core 1)

// ============================================================================
// CORE SYSTEM STATE
// ============================================================================

extern SystemConfig config;

// ============================================================================
// MOTION VARIABLES (Core 1 writes, Core 0 reads for display)
// volatile ensures cross-core visibility (32-bit types = atomic on Xtensa)
// ============================================================================

extern volatile long currentStep;           // Safe: 32-bit on ESP32 Xtensa (atomic read/write)
extern volatile long startStep;
extern volatile long targetStep;
extern volatile bool movingForward;
extern bool hasReachedStartStep;            // Core 1 only — no cross-core access
extern unsigned long lastStepMicros;        // Core 1 only — no cross-core access
extern volatile unsigned long stepDelayMicrosForward;
extern volatile unsigned long stepDelayMicrosBackward;

// ============================================================================
// DISTANCE LIMITS (Core 0 writes via CommandDispatcher, Core 1 reads)
// ============================================================================

extern volatile float effectiveMaxDistanceMM;
extern volatile float maxDistanceLimitPercent;

// ============================================================================
// SENSOR CONFIGURATION (Core 0 writes, Core 1 reads)
// ============================================================================

extern volatile bool sensorsInverted;  // false=normal (START=GPIO4, END=GPIO5), true=inverted

// ============================================================================
// MOTION CONFIGURATIONS
// ============================================================================

extern MotionConfig motion;
extern PendingMotionConfig pendingMotion;
extern CyclePauseState motionPauseState;
extern ZoneEffectConfig zoneEffect;  // Zone effects (speed + special effects) - Owned by BaseMovementController.cpp
extern ZoneEffectState zoneEffectState;  // Zone effects runtime state - Owned by BaseMovementController.cpp

// ============================================================================
// STATISTICS TRACKING (Core 1 writes, Core 0 reads for display/save)
// ============================================================================

extern StatsTracking stats;  // Encapsulated distance tracking (statsMutex for compound ops)
extern volatile unsigned long lastStartContactMillis;
extern volatile unsigned long cycleTimeMillis;
extern volatile float measuredCyclesPerMinute;
extern volatile bool wasAtStart;
extern bool statsRequested;            // Core 0 only — no cross-core access
extern unsigned long lastStatsRequestTime;  // Core 0 only — no cross-core access

// ============================================================================
// WEB SERVERS
// ============================================================================

extern WebServer server;
extern WebSocketsServer webSocket;

// ============================================================================
// CALLBACK FUNCTIONS (defined in main, called by modules)
// ============================================================================

extern void sendStatus();
extern void stopMovement();

#endif // GLOBAL_STATE_H
