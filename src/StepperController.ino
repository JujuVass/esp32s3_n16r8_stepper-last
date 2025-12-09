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
// Core (config, types, utilities)
#include "core/Config.h"            // WiFi, OTA, GPIO, Hardware, Timing constants
#include "core/Types.h"             // Data structures and enums
#include "core/GlobalState.h"       // Global state declarations
#include "core/UtilityEngine.h"     // Unified logging, WebSocket, LittleFS manager
#include "core/Validators.h"        // Parameter validation functions

// Hardware abstraction layer
#include "hardware/MotorDriver.h"    // Motor control (Motor.step(), Motor.enable()...)
#include "hardware/ContactSensors.h" // Contact sensors (Contacts.isStartContactActive()...)

// Communication layer
#include "communication/CommandDispatcher.h" // WebSocket command routing
#include "communication/StatusBroadcaster.h" // Status broadcasting (Status.send()...)
#include "communication/NetworkManager.h"    // WiFi, OTA, mDNS, NTP (Network.begin()...)
#include "communication/APIRoutes.h"         // HTTP API routes
#include "communication/FilesystemManager.h" // Filesystem REST API

// Movement controllers
#include "movement/ChaosController.h"        // Chaos mode (Chaos.start(), Chaos.process()...)
#include "movement/OscillationController.h"  // Oscillation mode (Osc.start(), Osc.process()...)
#include "movement/PursuitController.h"      // Pursuit mode (Pursuit.move(), Pursuit.process()...)
#include "movement/BaseMovementController.h" // Base movement + decel zone
#include "movement/CalibrationManager.h"     // Calibration (Calibration.startCalibration()...)
#include "movement/SequenceTableManager.h"   // Sequence table CRUD operations
#include "movement/SequenceExecutor.h"       // Sequence execution (SeqExecutor.start()...)

// ============================================================================
// LOGGING SYSTEM - Managed by UtilityEngine
// ============================================================================
// All logging handled through UtilityEngine.h
// Use: engine->info(), engine->error(), engine->warn(), engine->debug()
// ============================================================================

// Global UtilityEngine instance (initialized in setup)
UtilityEngine* engine = nullptr;

// ============================================================================
// ONBOARD RGB LED (WS2812 on GPIO 48 - Freenove ESP32-S3)
// Using neopixelWrite() - native ESP32 Arduino core function
// ============================================================================
void setRgbLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(PIN_RGB_LED, r, g, b);
}

// ============================================================================
// NOTE: Hardware configuration moved to Config.h
// NOTE: Type definitions (structs, enums) moved to Types.h
// NOTE: Chaos pattern configs moved to ChaosPatterns.h
// ============================================================================

// ============================================================================
// HELPER FUNCTIONS FOR DEBUGGING (Forward declarations)
// ============================================================================
const char* executionContextName(ExecutionContext ctx);  // Used by ChaosController

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

// Statistics - encapsulated in StatsTracking struct
StatsTracking stats;

// Startup
bool needsInitialCalibration = true;

// Stats on-demand tracking
bool statsRequested = false;
unsigned long lastStatsRequestTime = 0;

// ============================================================================
// DEBUG HELPERS
// ============================================================================
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
void sendStatus();

// ============================================================================
// VALIDATION HELPERS
// ============================================================================
// NOTE: All validation functions moved to include/core/Validators.h
// Use: Validators::distance(), Validators::speed(), etc.
// validateOscillationAmplitude() now in OscillationController module

// NOTE: sendError() moved to StatusBroadcaster (Status.sendError())
// NOTE: validateAndReport() moved to CommandDispatcher
// NOTE: parseJsonCommand() moved to CommandDispatcher
// NOTE: resetTotalDistance() moved to UtilityEngine (engine->resetTotalDistance())
// NOTE: saveCurrentSessionStats() moved to UtilityEngine (engine->saveCurrentSessionStats())

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
  // 0. RGB LED INITIALIZATION (Early for visual feedback) - OFF initially
  // ============================================================================
  setRgbLed(0, 0, 0);
  
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
  // 2. NETWORK (WiFi - determines AP or STA mode)
  // ============================================================================
  Network.begin();
  
  // ============================================================================
  // 3. WEB SERVERS (HTTP + WebSocket)
  // ============================================================================
  server.begin();
  webSocket.begin();
  webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    Dispatcher.onWebSocketEvent(num, type, payload, length);
  });
  
  // ============================================================================
  // 4. API ROUTES (WiFi config routes needed in both modes)
  // ============================================================================
  filesystemManager.registerRoutes();
  setupAPIRoutes();
  engine->info("✅ HTTP (80) + WebSocket (81) servers started");
  
  // ============================================================================
  // AP MODE: Minimal setup complete - WiFi configuration only
  // ============================================================================
  if (Network.isAPMode()) {
    setRgbLed(0, 0, 50);  // Start with BLUE (dimmed) - waiting for config
    engine->info("\n╔════════════════════════════════════════════════════════╗");
    engine->info("║  MODE AP - CONFIGURATION WiFi                          ║");
    engine->info("║  Accès: http://192.168.4.1                             ║");
    engine->info("║  Connectez-vous au réseau: ESP32-Stepper-Setup         ║");
    engine->info("║  LED: Bleu/Rouge clignotant (attente config)           ║");
    engine->info("║       Vert fixe = config OK, Rouge fixe = échec        ║");
    engine->info("╚════════════════════════════════════════════════════════╝\n");
    return;  // Skip stepper initialization in AP mode
  }
  
  // ============================================================================
  // STA MODE: Full stepper controller initialization
  // ============================================================================
  
  // WiFi connected - LED GREEN
  setRgbLed(0, 50, 0);  // GREEN = WiFi OK (dimmed)
  
  // Command dispatcher and status broadcaster
  Dispatcher.begin(&webSocket);
  Status.begin(&webSocket);
  SeqExecutor.begin(&webSocket);  // SequenceExecutor needs WebSocket for status updates
  engine->info("✅ Command dispatcher + Status broadcaster ready");
  
  // ============================================================================
  // 5. HARDWARE (Motor + Contacts) - STA mode only
  // ============================================================================
  Motor.init();
  Contacts.init();
  Motor.setDirection(false);
  engine->info("✅ Hardware initialized (Motor + Contacts)");
  
  // ============================================================================
  // 6. CALIBRATION MANAGER - STA mode only
  // ============================================================================
  Calibration.init(&webSocket, &server);
  Calibration.setStatusCallback(sendStatus);
  Calibration.setErrorCallback([](const String& msg) { Status.sendError(msg); });
  Calibration.setCompletionCallback([]() { SeqExecutor.onMovementComplete(); });
  engine->info("✅ CalibrationManager ready");
  
  // ============================================================================
  // 7. STARTUP COMPLETE - STA mode
  // ============================================================================
  engine->printStatus();
  
  config.currentState = STATE_READY;
  engine->info("\n╔════════════════════════════════════════════════════════╗");
  engine->info("║  WEB INTERFACE READY!                                  ║");
  engine->info("║  Access: http://" + WiFi.localIP().toString() + "                          ║");
  engine->info("║  Auto-calibration starts in 1 second...               ║");
  engine->info("║  LED: Vert (WiFi connecté)                             ║");
  engine->info("╚════════════════════════════════════════════════════════╝\n");
}

// ============================================================================
// MAIN LOOP (REFACTORED ARCHITECTURE)
// ============================================================================
// Clean separation between MovementType (WHAT) and ExecutionContext (WHO)
void loop() {
  // ═══════════════════════════════════════════════════════════════════════════
  // AP MODE: Minimal loop - only web services for WiFi configuration
  // LED alternates BLUE/RED to indicate AP mode (waiting for config)
  // After successful config: LED stays GREEN (blink disabled)
  // After failed config: LED stays RED for 3s then resumes blinking
  // ═══════════════════════════════════════════════════════════════════════════
  if (Network.isAPMode()) {
    // Handle Captive Portal DNS (redirects all DNS to ESP32 for auto-popup)
    Network.handleCaptivePortal();
    
    // Blink LED Blue/Red every 500ms (only if blink enabled)
    static unsigned long lastLedToggle = 0;
    static bool ledIsBlue = true;
    
    if (Network.apLedBlinkEnabled && millis() - lastLedToggle > 500) {
      lastLedToggle = millis();
      if (ledIsBlue) {
        setRgbLed(50, 0, 0);  // RED (dimmed)
      } else {
        setRgbLed(0, 0, 50);  // BLUE (dimmed)
      }
      ledIsBlue = !ledIsBlue;
    }
    
    server.handleClient();
    webSocket.loop();
    return;  // Skip all stepper/OTA handling in AP mode
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // STA MODE: Full stepper control with OTA support
  // ═══════════════════════════════════════════════════════════════════════════
  
  // OTA UPDATE HANDLER (Must be called in every loop iteration)
  Network.handleOTA();
  
  // WiFi CONNECTION HEALTH CHECK (mDNS re-announce after reconnection)
  // Ensures esp32-stepper.local stays responsive
  Network.checkConnectionHealth();
  
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
            String(stats.totalDistanceTraveled / 1000000.0, 2) + " km");
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
// MOVEMENT CONTROL WRAPPERS
// ============================================================================

void stopMovement() {
  BaseMovement.stop();
}
