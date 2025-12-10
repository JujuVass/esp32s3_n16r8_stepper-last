// ============================================================================
// ESP32-S3 STEPPER MOTOR CONTROLLER WITH WEB INTERFACE
// ============================================================================
// Hardware: ESP32-S3, HSS86 Driver (closed loop), NEMA34 8NM Motor
// Mechanics: HTD 5M belt, 18T pulley → 90mm/rev → 6.67 steps/mm
// Features: Automatic calibration, va-et-vient motion, web control
// ============================================================================

// ============================================================================
// LIBRARIES
// ============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// ============================================================================
// PROJECT HEADERS
// ============================================================================
#include "core/Config.h"
#include "core/Types.h"
#include "core/GlobalState.h"
#include "core/UtilityEngine.h"

#include "hardware/MotorDriver.h"
#include "hardware/ContactSensors.h"

#include "communication/CommandDispatcher.h"
#include "communication/StatusBroadcaster.h"
#include "communication/NetworkManager.h"
#include "communication/APIRoutes.h"
#include "communication/FilesystemManager.h"

#include "movement/ChaosController.h"
#include "movement/OscillationController.h"
#include "movement/PursuitController.h"
#include "movement/BaseMovementController.h"
#include "movement/CalibrationManager.h"
#include "movement/SequenceTableManager.h"
#include "movement/SequenceExecutor.h"

// ============================================================================
// LOGGING - Use engine->info(), engine->error(), engine->warn(), engine->debug()
// ============================================================================

// Global UtilityEngine instance (initialized in setup)
UtilityEngine* engine = nullptr;

// ============================================================================
// ONBOARD RGB LED (WS2812 on GPIO 48 - Freenove ESP32-S3)
// ============================================================================
void setRgbLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(PIN_RGB_LED, r, g, b);
}

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
// DUAL-CORE FREERTOS - Task handles & Synchronization
// ============================================================================
TaskHandle_t motorTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;
SemaphoreHandle_t motionMutex = NULL;
SemaphoreHandle_t stateMutex = NULL;
volatile bool emergencyStop = false;
volatile bool requestCalibration = false;  // Flag to trigger calibration from Core 1

// ============================================================================
// WEB SERVER INSTANCES
// ============================================================================
WebServer server(80);
WebSocketsServer webSocket(81);
FilesystemManager filesystemManager(server);

// Forward declaration
void sendStatus();

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
  
  // ============================================================================
  // 8. DUAL-CORE FREERTOS INITIALIZATION - STA mode only
  // ============================================================================
  
  // Create mutexes for shared data protection
  motionMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();
  
  if (motionMutex == NULL || stateMutex == NULL) {
    engine->error("❌ Failed to create FreeRTOS mutexes!");
    return;
  }
  
  // Create MOTOR task on Core 1 (PRO_CPU) - HIGH priority for real-time stepping
  xTaskCreatePinnedToCore(
    motorTask,           // Task function
    "MotorTask",         // Name
    4096,                // Stack size (bytes)
    NULL,                // Parameters
    10,                  // Priority (10 = high, ensures motor runs first)
    &motorTaskHandle,    // Task handle
    1                    // Core 1 (PRO_CPU)
  );
  
  // Create NETWORK task on Core 0 (APP_CPU) - Normal priority for network ops
  xTaskCreatePinnedToCore(
    networkTask,         // Task function
    "NetworkTask",       // Name
    8192,                // Stack size (larger for JSON serialization)
    NULL,                // Parameters
    1,                   // Priority (1 = normal)
    &networkTaskHandle,  // Task handle
    0                    // Core 0 (APP_CPU)
  );
  
  engine->info("✅ DUAL-CORE initialized: Motor=Core1(P10), Network=Core0(P1)");
}

// ============================================================================
// MOTOR TASK - Core 1 (PRO_CPU) - Real-time stepping
// ============================================================================
void motorTask(void* param) {
  engine->info("🔧 MotorTask started on Core " + String(xPortGetCoreID()));
  
  // Initial calibration (with delay for web interface access)
  static bool calibrationStarted = false;
  static unsigned long calibrationDelayStart = 0;
  
  while (true) {
    // ═══════════════════════════════════════════════════════════════════════
    // EMERGENCY STOP CHECK (highest priority, atomic flag)
    // ═══════════════════════════════════════════════════════════════════════
    if (emergencyStop) {
      config.currentState = STATE_READY;
      emergencyStop = false;
      engine->info("🛑 Emergency stop executed");
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MANUAL CALIBRATION REQUEST (triggered from Core 0 via flag)
    // ═══════════════════════════════════════════════════════════════════════
    if (requestCalibration) {
      requestCalibration = false;
      engine->info("=== Manual calibration requested ===");
      
      // Suspend network task during calibration
      if (networkTaskHandle != NULL) {
        vTaskSuspend(networkTaskHandle);
      }
      
      Calibration.startCalibration();
      
      // Resume network task
      if (networkTaskHandle != NULL) {
        vTaskResume(networkTaskHandle);
      }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // INITIAL CALIBRATION (with delay for web interface access)
    // ═══════════════════════════════════════════════════════════════════════
    if (needsInitialCalibration && !calibrationStarted) {
      if (calibrationDelayStart == 0) {
        calibrationDelayStart = millis();
        engine->info("=== Web interface ready - Calibration will start in 1 second ===");
      }
      
      if (millis() - calibrationDelayStart >= 1000) {
        calibrationStarted = true;
        engine->info("=== Starting automatic calibration ===");
        
        // IMPORTANT: Suspend network task during calibration to avoid race condition
        // CalibrationManager calls webSocket.loop() and server.handleClient() internally
        if (networkTaskHandle != NULL) {
          vTaskSuspend(networkTaskHandle);
        }
        
        Calibration.startCalibration();
        needsInitialCalibration = false;
        
        // Resume network task after calibration
        if (networkTaskHandle != NULL) {
          vTaskResume(networkTaskHandle);
        }
      }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MOVEMENT EXECUTION (timing-critical, runs on dedicated core)
    // ═══════════════════════════════════════════════════════════════════════
    switch (currentMovement) {
      case MOVEMENT_VAET:
        BaseMovement.process();
        break;
        
      case MOVEMENT_PURSUIT:
        if (pursuit.isMoving) {
          unsigned long currentMicros = micros();
          if (currentMicros - lastStepMicros >= pursuit.stepDelay) {
            lastStepMicros = currentMicros;
            Pursuit.process();
          }
        }
        break;
        
      case MOVEMENT_OSC:
        if (config.currentState == STATE_RUNNING) {
          Osc.process();
        }
        break;
        
      case MOVEMENT_CHAOS:
        if (config.currentState == STATE_RUNNING) {
          Chaos.process();
        }
        break;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // SEQUENCER (logic only, no network blocking)
    // ═══════════════════════════════════════════════════════════════════════
    if (config.executionContext == CONTEXT_SEQUENCER) {
      SeqExecutor.process();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // HSS86 FEEDBACK MONITORING (ALM & PEND)
    // ═══════════════════════════════════════════════════════════════════════
    Motor.updatePendTracking();
    
    // ALM monitoring (informative only for now)
    static bool lastAlarmState = false;
    bool alarmActive = Motor.isAlarmActive();
    
    if (alarmActive && !lastAlarmState) {
      // Alarm just activated
      engine->warn("🚨 HSS86 ALARM ACTIVE - Check motor/mechanics!");
    } else if (!alarmActive && lastAlarmState) {
      // Alarm cleared
      engine->info("✅ HSS86 Alarm cleared");
    }
    lastAlarmState = alarmActive;
    
    // PEND monitoring during movement (detect motor lag)
    static unsigned long lastPendWarnMs = 0;
    if (config.currentState == STATE_RUNNING) {
      unsigned long lagMs = Motor.getPositionLagMs();
      
      // Warn if motor hasn't reached position for > threshold (significant lag)
      if (lagMs > PEND_LAG_WARN_THRESHOLD_MS && millis() - lastPendWarnMs > PEND_WARN_COOLDOWN_MS) {
        engine->warn("⚠️ Motor position lag: " + String(lagMs) + "ms - possible load/resistance");
        lastPendWarnMs = millis();
      }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CYCLE COUNTER (periodic stats)
    // ═══════════════════════════════════════════════════════════════════════
    static unsigned long lastSummary = 0;
    static unsigned long cycleCounter = 0;
    
    if (config.currentState == STATE_RUNNING) {
      static bool lastWasAtStart = false;
      bool nowAtStart = (currentStep == startStep);
      if (nowAtStart && !lastWasAtStart) {
        cycleCounter++;
      }
      lastWasAtStart = nowAtStart;
      
      if (millis() - lastSummary > SUMMARY_LOG_INTERVAL_MS) {
        engine->debug("Status: " + String(cycleCounter) + " cycles | " + 
              String(stats.totalDistanceTraveled / 1000000.0, 2) + " km");
        lastSummary = millis();
      }
    } else {
      lastSummary = millis();
      cycleCounter = 0;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // TASK YIELD - Adaptive based on motor state
    // ═══════════════════════════════════════════════════════════════════════
    if (config.currentState == STATE_RUNNING) {
      // Motor running: minimal yield to maintain step timing
      taskYIELD();
    } else {
      // Motor idle: longer delay to reduce CPU usage
      vTaskDelay(pdMS_TO_TICKS(1));  // 1ms when not running
    }
  }
}

// ============================================================================
// NETWORK TASK - Core 0 (APP_CPU) - Network operations (can block)
// ============================================================================
void networkTask(void* param) {
  engine->info("🌐 NetworkTask started on Core " + String(xPortGetCoreID()));
  
  while (true) {
    // ═══════════════════════════════════════════════════════════════════════
    // NETWORK SERVICES (can block without affecting motor)
    // ═══════════════════════════════════════════════════════════════════════
    Network.handleOTA();
    Network.checkConnectionHealth();
    
    // HTTP server and WebSocket - these can block!
    server.handleClient();
    webSocket.loop();
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATUS BROADCAST (every 100ms)
    // ═══════════════════════════════════════════════════════════════════════
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > STATUS_UPDATE_INTERVAL_MS) {
      lastUpdate = millis();
      sendStatus();  // Can take 100ms+ without affecting motor!
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LOG BUFFER FLUSH (I/O to filesystem)
    // ═══════════════════════════════════════════════════════════════════════
    if (engine) {
      engine->flushLogBuffer();
    }
    
    // Small delay to prevent watchdog and allow other tasks
    vTaskDelay(pdMS_TO_TICKS(1));  // 1ms between iterations
  }
}

// ============================================================================
// MAIN LOOP - Minimal in dual-core mode (AP mode only)
// ============================================================================
void loop() {
  // ═══════════════════════════════════════════════════════════════════════════
  // AP MODE: WiFi configuration only (no dual-core tasks running)
  // ═══════════════════════════════════════════════════════════════════════════
  if (Network.isAPMode()) {
    Network.handleCaptivePortal();
    
    // Blink LED Blue/Red every 500ms
    static unsigned long lastLedToggle = 0;
    static bool ledIsBlue = true;
    
    if (Network.apLedBlinkEnabled && millis() - lastLedToggle > 500) {
      lastLedToggle = millis();
      setRgbLed(ledIsBlue ? 50 : 0, 0, ledIsBlue ? 0 : 50);
      ledIsBlue = !ledIsBlue;
    }
    
    server.handleClient();
    webSocket.loop();
    return;
  }
  
  // ═══════════════════════════════════════════════════════════════════════════
  // STA MODE: loop() is empty - FreeRTOS tasks handle everything
  // ═══════════════════════════════════════════════════════════════════════════
  // Motor control runs on Core 1 (motorTask)
  // Network operations run on Core 0 (networkTask)
  // This loop just needs to not block the scheduler
  vTaskDelay(portMAX_DELAY);  // Suspend loop() indefinitely
}

// ============================================================================
// GLOBAL CALLBACKS (called by modules)
// ============================================================================

void sendStatus() {
  Status.send();
}

void stopMovement() {
  BaseMovement.stop();
}