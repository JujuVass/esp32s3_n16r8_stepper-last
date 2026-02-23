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
 *   - Core 0 (APP_CPU): StepperNetwork tasks (WiFi, WebSocket, HTTP, OTA)
 *   - Core 1 (PRO_CPU): Motor tasks (stepping, timing-critical operations)
 *   - Mutexes protect shared motion configurations
 *
 * OWNERSHIP TABLE (where each global is defined):
 * ┌───────────────────────────────┬───────────────────────────────────┬──────────────────┐
 * │ Variable                      │ Defined in                        │ Protected by     │
 * ├───────────────────────────────┼───────────────────────────────────┼──────────────────┤
 * │ config                        │ StepperController.cpp             │ stateMutex       │
 * │ motion, pendingMotion         │ StepperController.cpp             │ motionMutex      │
 * │ zoneEffect, zoneEffectState   │ StepperController.cpp             │ motionMutex      │
 * │ motionPauseState, oscPauseState│ StepperController.cpp            │ —                │
 * │ currentStep, startStep, etc.  │ StepperController.cpp             │ volatile 32-bit  │
 * │ stats                         │ StepperController.cpp             │ statsMutex       │
 * │ server, ws                    │ StepperController.cpp             │ — (async)      │
 * │ chaos, chaosState             │ ChaosController.cpp               │ stateMutex       │
 * │ oscillation, oscillationState │ OscillationController.cpp         │ stateMutex       │
 * │ pursuit                       │ PursuitController.cpp             │ —                │
 * │ seqState, sequenceTable[]     │ SequenceExecutor/TableManager.cpp │ —                │
 * │ currentMovement               │ SequenceExecutor.cpp              │ volatile 32-bit  │
 * │ engine                        │ StepperController.cpp             │ — (init in setup)│
 * └───────────────────────────────┴───────────────────────────────────┴──────────────────┘
 * ============================================================================
 */

#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
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

// RAII-style mutex guard for automatic release (C++ scope-based)
class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(10))
        : _mutex(mutex) {
        if (_mutex != NULL) {
            _locked = (xSemaphoreTake(_mutex, timeout) == pdTRUE);
        }
    }
    ~MutexGuard() {
        if (_locked && _mutex != NULL) {
            xSemaphoreGive(_mutex);
        }
    }
    [[nodiscard]] bool isLocked() const { return _locked; }
    [[nodiscard]] explicit operator bool() const { return _locked; }
private:
    SemaphoreHandle_t _mutex;
    bool _locked = false;
};

// NOTE: wsMutex removed — ESPAsyncWebServer runs on LWIP task, no polling needed

// Atomic flags (no mutex needed - set from Core 0, read from Core 1)
// Safe: bool and long are 32-bit on ESP32 Xtensa → single-instruction read/write
extern volatile bool requestCalibration;    // Trigger calibration from motorTask
extern volatile bool calibrationInProgress; // Cooperative flag for calibration mode
extern volatile bool blockingMoveInProgress; // Cooperative flag for blocking moves

// File upload tracking (Core 0 only — set by FilesystemManager HTTP handler)
// Uses timestamp-based expiry: upload considered active if last activity < 5s ago
extern volatile unsigned long lastUploadActivityTime;
extern volatile bool uploadStopDone;         // Prevents repeated stop() calls during batch upload
                                             // (blocking move loops service them from Core 1)

// ============================================================================
// CORE SYSTEM STATE
// ============================================================================

extern SystemConfig config;

// ============================================================================
// MOTION VARIABLES (Core 1 writes, Core 0 reads for display)
// volatile ensures cross-core visibility (32-bit types = atomic on Xtensa LX7)
// NOTE: std::atomic<> considered but unnecessary here — ESP32-S3 Xtensa guarantees
// atomic 32-bit aligned reads/writes. These are single-writer (Core 1) with
// multi-reader (Core 0), so no read-modify-write atomicity is needed.
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

extern AsyncWebServer server;
extern AsyncWebSocket ws;

// ============================================================================
// CALLBACK FUNCTIONS (defined in main, called by modules)
// ============================================================================

extern void sendStatus();
extern void stopMovement();

#endif // GLOBAL_STATE_H
