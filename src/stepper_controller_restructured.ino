// ============================================================================
// ESP32-S3 STEPPER MOTOR CONTROLLER WITH WEB INTERFACE
// ============================================================================
// Hardware: ESP32-S3, HSS86 Driver (closed loop), NEMA34 8NM Motor
// Mechanics: HTD 5M belt, 20T pulley → 100mm/rev → 6 steps/mm
// Features: Automatic calibration, va-et-vient motion, web control
// ============================================================================

// ============================================================================
// LIBRARIES
// ============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>  // OTA (Over-The-Air) updates
#include <ESPmDNS.h>     // mDNS for local domain name (esp32-stepper.local)
#include <functional>     // For std::function (recursive directory listing)

// ============================================================================
// PROJECT CONFIGURATION HEADERS
// ============================================================================
#include "Config.h"               // WiFi, OTA, GPIO, Hardware, Timing constants
#include "Types.h"                // Data structures and enums
#include "UtilityEngine.h"        // Unified logging, WebSocket, LittleFS manager

// Web layer (reorganized structure)
#include "web/FilesystemManager.h"  // Filesystem browser API
#include "web/APIRoutes.h"          // API routes module (extracted from main)

// Core utilities
#include "core/Validators.h"        // Parameter validation functions

// Hardware abstraction layer (modular architecture)
#include "hardware/MotorDriver.h"    // Motor control abstraction (Motor.step(), Motor.enable()...)
#include "hardware/ContactSensors.h" // Contact sensors abstraction (Contacts.isStartContactActive()...)

// Controllers (modular architecture)
#include "controllers/CalibrationManager.h" // Calibration controller (Calibration.startCalibration()...)

// Communication layer (modular architecture)
#include "communication/CommandDispatcher.h" // WebSocket command routing
#include "communication/StatusBroadcaster.h" // Status broadcasting (Status.send()...)
#include "communication/NetworkManager.h"    // WiFi, OTA, mDNS, NTP (Network.begin()...)

// Movement controllers (modular architecture)
#include "movement/ChaosController.h"       // Chaos mode controller (Chaos.start(), Chaos.process()...)
#include "movement/OscillationController.h" // Oscillation mode controller (Osc.start(), Osc.process()...)
#include "movement/PursuitController.h"     // Pursuit mode controller (Pursuit.move(), Pursuit.process()...)
#include "movement/BaseMovementController.h"   // Base movement + decel zone (BaseMovement.validateDecelZone()...)

// Sequencer (modular architecture)
#include "sequencer/SequenceTableManager.h" // Sequence table CRUD operations
#include "sequencer/SequenceExecutor.h"     // Sequence execution engine (SeqExecutor.start()...)

// ============================================================================
// LOGGING SYSTEM - Managed by UtilityEngine
// ============================================================================
// All logging handled through UtilityEngine.h
// Use: engine->info(), engine->error(), engine->warn(), engine->debug()
// ============================================================================

// Global UtilityEngine instance (initialized in setup)
UtilityEngine* engine = nullptr;

// ============================================================================
// NOTE: Hardware configuration moved to Config.h
// NOTE: Type definitions (structs, enums) moved to Types.h
// NOTE: Chaos pattern configs moved to ChaosPatterns.h
// ============================================================================

// ============================================================================
// HELPER FUNCTIONS FOR DEBUGGING (Forward declarations)
// ============================================================================
const char* movementTypeName(int type);  // Forward declaration
const char* executionContextName(ExecutionContext ctx);  // Forward declaration

// ============================================================================
// GLOBAL STATE DEFINITIONS (extern declarations in GlobalState.h)
// ============================================================================

// Core state
SystemConfig config;

// Position tracking
volatile long currentStep = 0;
long startStep = 0;
long targetStep = 0;
bool movingForward = true;
bool hasReachedStartStep = false;

// Motion configuration
MotionConfig motion;
PendingMotionConfig pendingMotion;
CyclePauseState motionPauseState;

// Distance limits
float maxDistanceLimitPercent = 100.0;
float effectiveMaxDistanceMM = 0.0;

// Timing
unsigned long lastStepMicros = 0;
unsigned long stepDelayMicrosForward = 1000;
unsigned long stepDelayMicrosBackward = 1000;
unsigned long lastStartContactMillis = 0;
unsigned long cycleTimeMillis = 0;
float measuredCyclesPerMinute = 0;
bool wasAtStart = false;

// Statistics
unsigned long totalDistanceTraveled = 0;
unsigned long lastSavedDistance = 0;
long lastStepForDistance = 0;

// Startup
bool needsInitialCalibration = true;

// Stats on-demand tracking
bool statsRequested = false;
unsigned long lastStatsRequestTime = 0;

// ============================================================================
// DEBUG HELPERS
// ============================================================================
const char* movementTypeName(int type) {
  switch((MovementType)type) {
    case MOVEMENT_VAET: return "VAET";
    case MOVEMENT_OSC: return "OSC";
    case MOVEMENT_CHAOS: return "CHAOS";
    case MOVEMENT_PURSUIT: return "PURSUIT";
    case MOVEMENT_CALIBRATION: return "CALIBRATION";
    default: return "UNKNOWN";
  }
}

const char* executionContextName(ExecutionContext ctx) {
  switch(ctx) {
    case CONTEXT_STANDALONE: return "STANDALONE";
    case CONTEXT_SEQUENCER: return "SEQUENCER";
    default: return "UNKNOWN";
  }
}

// ============================================================================
// WEB SERVER INSTANCES
// ============================================================================
WebServer server(80);
WebSocketsServer webSocket(81);

// Filesystem Manager (handles all file operations via REST API)
FilesystemManager filesystemManager(server);

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
// HTML page now served from LittleFS (/data/index.html)

// ============================================================================
// NOTE: Module Singletons are used directly:
//   - SeqTable.addLine(), SeqTable.deleteLine(), etc.
//   - SeqExecutor.start(), SeqExecutor.stop(), etc.
//   - Osc.start(), Osc.process(), etc.
//   - Pursuit.move(), Pursuit.process()
//   - Status.send()
//   - Chaos.start(), Chaos.stop(), etc.
//   - Calibration.start(), etc.
// No inline wrappers needed - callers use singletons directly.
// ============================================================================

// Core functions (defined below in this file)
void resetTotalDistance();
void sendStatus();
void saveCurrentSessionStats();

// ============================================================================
// UTILITY HELPERS
// ============================================================================
/**
 * Convert boolean to JSON string literal
 * Replaces 100+ occurrences of '? "true" : "false"' pattern
 * 
 * @param value Boolean value to convert
 * @return "true" or "false" as const char* (no String allocation)
 */
inline const char* boolToJson(bool value) {
  return value ? "true" : "false";
}

// NOTE: readContactDebounced() removed - use Contacts.readDebounced() directly

/**
 * Service WebSocket and HTTP server for specified duration (non-blocking delay)
 * Replaces repetitive while(millis() - start < duration) loops with yield/loop pattern
 * 
 * @param durationMs Duration in milliseconds to keep servicing
 */
void serviceWebSocketFor(unsigned long durationMs) {
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    yield();
    webSocket.loop();
    server.handleClient();
  }
}

// Error notification helper
void sendError(String message);

// ============================================================================
// VALIDATION HELPERS
// ============================================================================
// NOTE: All validation functions moved to include/core/Validators.h
// Use: Validators::distance(), Validators::speed(), etc.
// validateOscillationAmplitude() now in OscillationController module

// Validation + error reporting helper
bool validateAndReport(bool isValid, const String& errorMsg);

// Deceleration zone functions - delegated to DecelZoneController module
// Functions: calculateSlowdownFactor(), calculateAdjustedDelay(), validateDecelZone()

// Pursuit mode - delegated to PursuitController module
// Functions: pursuitMove(), doPursuitStep()

// Oscillation mode - delegated to OscillationController module
// Functions: startOscillation(), doOscillationStep(), calculateOscillationPosition(), validateOscillationAmplitude()

// Chaos mode - delegated to ChaosController module
// Functions: Chaos.start(), Chaos.stop(), Chaos.process()

// ============================================================================
// SETUP - INITIALIZATION
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);  // Brief pause for Serial stability
  
  // ============================================================================
  // 1. FILESYSTEM & LOGGING (First for early logging capability)
  // ============================================================================
  engine = new UtilityEngine(webSocket);
  if (!engine->initialize()) {
    Serial.println("❌ UtilityEngine initialization failed!");
  } else {
    engine->info("✅ UtilityEngine initialized (LittleFS + Logging ready)");
  }
  
  engine->info("\n=== ESP32-S3 Stepper Controller ===");
  randomSeed(analogRead(0) + esp_random());
  
  // ============================================================================
  // 2. NETWORK (WiFi + mDNS + NTP + OTA)
  // ============================================================================
  Network.begin();
  
  // ============================================================================
  // 3. WEB SERVERS (HTTP + WebSocket) - Immediately after WiFi
  // ============================================================================
  server.begin();
  webSocket.begin();
  
  Dispatcher.begin(&webSocket);
  webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    Dispatcher.onWebSocketEvent(num, type, payload, length);
  });
  
  Status.begin(&webSocket);
  SeqExecutor.begin(&webSocket);  // CRITICAL: SequenceExecutor needs WebSocket for status updates
  engine->info("✅ HTTP (80) + WebSocket (81) servers started");
  
  // ============================================================================
  // 4. API ROUTES & FILESYSTEM MANAGER
  // ============================================================================
  filesystemManager.registerRoutes();
  setupAPIRoutes();
  engine->info("✅ API routes registered");
  
  // ============================================================================
  // 5. HARDWARE (Motor + Contacts)
  // ============================================================================
  Motor.init();
  Contacts.init();
  Motor.setDirection(false);
  engine->info("✅ Hardware initialized (Motor + Contacts)");
  
  // ============================================================================
  // 6. CALIBRATION MANAGER
  // ============================================================================
  Calibration.init(&webSocket, &server);
  Calibration.setStatusCallback(sendStatus);
  Calibration.setErrorCallback([](const String& msg) { sendError(msg); });
  Calibration.setCompletionCallback([]() { SeqExecutor.onMovementComplete(); });
  engine->info("✅ CalibrationManager ready");
  
  // ============================================================================
  // 7. STARTUP COMPLETE
  // ============================================================================
  engine->printStatus();
  
  config.currentState = STATE_READY;
  engine->info("\n╔════════════════════════════════════════════════════════╗");
  engine->info("║  WEB INTERFACE READY!                                  ║");
  engine->info("║  Access: http://" + WiFi.localIP().toString() + "                          ║");
  engine->info("║  Auto-calibration starts in 1 second...               ║");
  engine->info("╚════════════════════════════════════════════════════════╝\n");
}

// ============================================================================
// MAIN LOOP (REFACTORED ARCHITECTURE)
// ============================================================================
// Clean separation between MovementType (WHAT) and ExecutionContext (WHO)
void loop() {
  // ═══════════════════════════════════════════════════════════════════════════
  // OTA UPDATE HANDLER (Must be called in every loop iteration)
  // ═══════════════════════════════════════════════════════════════════════════
  Network.handleOTA();
  
  // ═══════════════════════════════════════════════════════════════════════════
  // INITIAL CALIBRATION (with delay for web interface access)
  // ═══════════════════════════════════════════════════════════════════════════
  static bool calibrationStarted = false;
  static unsigned long calibrationDelayStart = 0;
  
  if (needsInitialCalibration && !calibrationStarted) {
    if (calibrationDelayStart == 0) {
      calibrationDelayStart = millis();
      engine->info("=== Web interface ready - Calibration will start in 1 second ===");
      engine->info("Connect now to see calibration overlay!");
    }

    // Wait 1 second to allow users to connect
    if (millis() - calibrationDelayStart >= 1000) {
      calibrationStarted = true;
      engine->info("=== Starting automatic calibration ===");
      delay(100);
      Calibration.startCalibration();
      needsInitialCalibration = false;
    }
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // MOVEMENT EXECUTION (Based on MovementType - WHAT to execute)
  // ═══════════════════════════════════════════════════════════════════════════
  switch (currentMovement) {
    case MOVEMENT_VAET:
      // VA-ET-VIENT: Classic back-and-forth movement
      // All logic (timing, decel, stepping) encapsulated in BaseMovement.process()
      BaseMovement.process();
      break;
      
    case MOVEMENT_PURSUIT:
      // PURSUIT: Real-time position tracking (non-blocking)
      if (pursuit.isMoving) {
        unsigned long currentMicros = micros();
        // SAFE: Unsigned arithmetic handles overflow correctly
        if (currentMicros - lastStepMicros >= pursuit.stepDelay) {
          lastStepMicros = currentMicros;
          Pursuit.process();
        }
      }
      break;
      
    case MOVEMENT_OSC:
      // OSCILLATION: Sinusoidal oscillation with ramping
      if (config.currentState == STATE_RUNNING) {
        Osc.process();
      }
      break;
      
    case MOVEMENT_CHAOS:
      // CHAOS: Random chaotic patterns (delegated to ChaosController module)
      if (config.currentState == STATE_RUNNING) {
        Chaos.process();
      }
      break;
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // SEQUENCER MANAGEMENT (Based on config.executionContext - WHO controls)
  // ═══════════════════════════════════════════════════════════════════════════
  if (config.executionContext == CONTEXT_SEQUENCER) {
    SeqExecutor.process();
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // WEB SERVICES (Non-blocking with wraparound-safe timing)
  // ═══════════════════════════════════════════════════════════════════════════
  static unsigned long lastServiceUpdate = 0;
  unsigned long currentMicros = micros();
  // SAFE: Wraparound-safe comparison using unsigned arithmetic
  if (currentMicros - lastServiceUpdate > WEBSERVICE_INTERVAL_US) {
    lastServiceUpdate = currentMicros;
    server.handleClient();
    webSocket.loop();
  }
  
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > STATUS_UPDATE_INTERVAL_MS) {
    lastUpdate = millis();
    sendStatus();
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // LOG BUFFER FLUSH (Every 5 seconds)
  // ═══════════════════════════════════════════════════════════════════════════
  if (engine) {
    engine->flushLogBuffer();
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // STATUS LOGGING (Periodic summary)
  // ═══════════════════════════════════════════════════════════════════════════
  static unsigned long lastSummary = 0;
  static unsigned long cycleCounter = 0;
  
  if (config.currentState == STATE_RUNNING) {
    // Count cycles (increment when we reach start position)
    static bool lastWasAtStart = false;
    bool nowAtStart = (currentStep == startStep);
    if (nowAtStart && !lastWasAtStart) {
      cycleCounter++;
    }
    lastWasAtStart = nowAtStart;
    
    // Print summary every 60 seconds (avoid log spam)
    if (millis() - lastSummary > SUMMARY_LOG_INTERVAL_MS) {
      engine->debug("Status: " + String(cycleCounter) + " cycles | " + 
            String(totalDistanceTraveled / 1000000.0, 2) + " km");
      lastSummary = millis();
    }
  } else {
    // Reset summary timer when not running
    lastSummary = millis();
    if (config.currentState != STATE_RUNNING) {
      cycleCounter = 0;  // Reset counter when stopped
    }
  }
}

// ============================================================================
// ERROR NOTIFICATION HELPER
// ============================================================================

/**
 * Send error message via WebSocket AND Serial
 * Ensures user sees errors even without Serial monitor
 */
void sendError(String message) {
  // Use structured logging
  engine->error(message);
  
  // Send to all WebSocket clients (only if clients connected)
  if (webSocket.connectedClients() > 0) {
    JsonDocument doc;
    doc["type"] = "error";
    doc["message"] = message;  // ArduinoJson handles escaping automatically
    
    String json;
    serializeJson(doc, json);
    webSocket.broadcastTXT(json);
  }
}

// ============================================================================
// VALIDATION HELPERS
// ============================================================================

/**
 * Helper function to validate and report errors
 * Sends error to WebSocket if validation fails
 */
bool validateAndReport(bool isValid, const String& errorMsg) {
  if (!isValid) {
    sendError("❌ " + errorMsg);
  }
  return isValid;
}

// ============================================================================
// EFFECTIVE MAX DISTANCE - Calculate usable distance based on limit percent
// ============================================================================
void updateEffectiveMaxDistance() {
  effectiveMaxDistanceMM = config.totalDistanceMM * (maxDistanceLimitPercent / 100.0);
  engine->debug(String("📏 Effective max distance: ") + String(effectiveMaxDistanceMM, 1) + 
        " mm (" + String(maxDistanceLimitPercent, 0) + "% of " + 
        String(config.totalDistanceMM, 1) + " mm)");
}

// ============================================================================
// MOTOR CONTROL - Stepping delegated to controllers
// - BaseMovement.process() for MOVEMENT_VAET
// - Chaos.process() for MOVEMENT_CHAOS
// - Osc.process() for MOVEMENT_OSC
// ============================================================================

void resetTotalDistance() {
  // Save any unsaved distance before resetting
  saveCurrentSessionStats();
  
  // Now reset counters
  totalDistanceTraveled = 0;
  lastSavedDistance = 0;  // Also reset last saved to prevent negative increment
  engine->info("🔄 Total distance counter reset to 0");
}

// ============================================================================
// STATUS BROADCASTING - Delegates to StatusBroadcaster module
// ============================================================================

/**
 * Broadcast current system status via WebSocket
 * Delegates to StatusBroadcaster singleton
 */
void sendStatus() {
  Status.send();
}

// ============================================================================
// STATISTICS - Delegates to UtilityEngine
// ============================================================================

/**
 * Save current session's total distance to daily stats
 * Called when:
 * - User clicks STOP button
 * - Mode change occurs
 * - WebSocket disconnects
 * 
 * Uses totalDistanceTraveled (global counter in steps)
 * IMPORTANT: Only saves the INCREMENT since last save to avoid double-counting
 */
void saveCurrentSessionStats() {
  // Calculate distance increment since last save (in steps)
  unsigned long incrementSteps = totalDistanceTraveled - lastSavedDistance;
  
  // Convert to millimeters
  float incrementMM = incrementSteps / STEPS_PER_MM;
  
  if (incrementMM <= 0) {
    engine->debug("📊 No new distance to save (no increment since last save)");
    return;
  }
  
  // Save increment to daily stats via UtilityEngine
  engine->incrementDailyStats(incrementMM);
  
  engine->debug(String("💾 Session stats saved: +") + String(incrementMM, 1) + "mm (total session: " + String(totalDistanceTraveled / STEPS_PER_MM, 1) + "mm)");
  
  // Update last saved distance (but keep totalDistanceTraveled for UI display)
  lastSavedDistance = totalDistanceTraveled;
}

// ============================================================================
// MOVEMENT CONTROL WRAPPERS
// ============================================================================

void stopMovement() {
  BaseMovement.stop();
}

void togglePause() {
  BaseMovement.togglePause();
}

void returnToStart() {
  BaseMovement.returnToStart();
}

// ============================================================================
// JSON PARSING HELPERS (using ArduinoJson for robustness)
// ============================================================================

/**
 * Parse JSON command and extract parameters safely
 * Returns true if parsing successful, false otherwise
 */
bool parseJsonCommand(const String& jsonStr, JsonDocument& doc) {
  DeserializationError error = deserializeJson(doc, jsonStr);
  
  if (error) {
    engine->error("JSON parse error: " + String(error.c_str()));
    sendError("❌ Commande JSON invalide: " + String(error.c_str()));
    return false;
  }
  
  return true;
}
