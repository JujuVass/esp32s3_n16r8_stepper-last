// ============================================================================
// ESP32-S3 STEPPER MOTOR CONTROLLER WITH WEB INTERFACE
// ============================================================================
// Hardware: ESP32-S3, HSS86 Driver (closed loop), NEMA34 8NM Motor
// Mechanics: HTD 5M belt, 20T pulley → 100mm/rev → 8.0 steps/mm
// Features: Automatic calibration, va-et-vient motion, web control
// ============================================================================

// ============================================================================
// LIBRARIES
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_core_dump.h>

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
  rgbLedWrite(PIN_RGB_LED, r, g, b);
}

// ============================================================================
// GLOBAL STATE DEFINITIONS (extern declarations in GlobalState.h)
// ============================================================================

// Core state
SystemConfig config;

// Position tracking
volatile long currentStep = 0;
volatile long startStep = 0;
volatile long targetStep = 0;
volatile bool movingForward = true;
bool hasReachedStartStep = false;

// Motion configuration
constinit MotionConfig motion;
constinit PendingMotionConfig pendingMotion;
constinit CyclePauseState motionPauseState;

// Distance limits
volatile float maxDistanceLimitPercent = 100.0;
volatile float effectiveMaxDistanceMM = 0.0;

// Sensor configuration
volatile bool sensorsInverted = false;  // Loaded from NVS

// Timing
unsigned long lastStepMicros = 0;
volatile unsigned long stepDelayMicrosForward = 1000;
volatile unsigned long stepDelayMicrosBackward = 1000;
volatile unsigned long lastStartContactMillis = 0;
volatile unsigned long cycleTimeMillis = 0;
volatile float measuredCyclesPerMinute = 0;
volatile bool wasAtStart = false;

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
SemaphoreHandle_t statsMutex = NULL;
SemaphoreHandle_t wsMutex = NULL;
volatile bool requestCalibration = false;  // Flag to trigger calibration from Core 1
volatile bool calibrationInProgress = false;  // Cooperative flag: networkTask skips webSocket/server
volatile bool blockingMoveInProgress = false;  // Cooperative flag: networkTask skips webSocket/server during blocking moves

// ============================================================================
// WEB SERVER INSTANCES
// ============================================================================
WebServer server(80);
WebSocketsServer webSocket(81);
FilesystemManager filesystemManager(server);

// Forward declarations (required for .cpp — functions used before definition)
void sendStatus();
void stopMovement();
void motorTask(void* param);
void networkTask(void* param);

// ============================================================================
// SETUP - INITIALIZATION
// ============================================================================
// Reset reason decoder (runs before logging is ready → Serial only)
static const char* getResetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:  return "POWER_ON";
    case ESP_RST_EXT:      return "EXTERNAL_PIN";
    case ESP_RST_SW:       return "SOFTWARE (ESP.restart)";
    case ESP_RST_PANIC:    return "PANIC (crash/exception)";
    case ESP_RST_INT_WDT:  return "INTERRUPT_WDT (task starved)";
    case ESP_RST_TASK_WDT: return "TASK_WDT (task hung)";
    case ESP_RST_WDT:      return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP:return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT (power dip!)";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);  // Brief pause for Serial stability
  
  // ============================================================================
  // RESET REASON (logged ASAP — before anything else, helps diagnose reboots)
  // ============================================================================
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("\n🔄 RESET REASON: %s (code %d)\n", getResetReasonName(resetReason), (int)resetReason);
  
  // ============================================================================
  // 0. RGB LED INITIALIZATION (Early for visual feedback) - OFF initially
  // ============================================================================
  setRgbLed(0, 0, 0);
  
  // ============================================================================
  // 1. FILESYSTEM & LOGGING (First for early logging capability)
  // ============================================================================
  // Static instance: lives forever (ESP32 never frees — reboot = cleanup)
  static UtilityEngine engineInstance(webSocket);
  engine = &engineInstance;
  if (!engine->initialize()) {
    Serial.println("❌ UtilityEngine initialization failed!");
  } else {
    engine->info("✅ UtilityEngine initialized (LittleFS + Logging ready)");
  }
  
  // Log reset reason to file too (persistent across reboots)
  engine->warn(String("🔄 RESET REASON: ") + getResetReasonName(resetReason) + " (code " + String((int)resetReason) + ")");
  if (resetReason == ESP_RST_BROWNOUT) {
    engine->error("⚡ BROWNOUT detected! Check power supply (USB cable, PSU capacity, motor current draw)");
  } else if (resetReason == ESP_RST_PANIC) {
    engine->error("💥 PANIC crash detected! Reading coredump from flash...");
    // Coredump-to-flash: the panic handler writes crash state to the coredump partition
    // automatically. On next boot we read the summary.
    esp_core_dump_summary_t summary;
    esp_err_t cdErr = esp_core_dump_get_summary(&summary);
    if (cdErr == ESP_OK) {
      engine->error(String("💥 Crashed task: ") + summary.exc_task);
      engine->error(String("💥 Exception PC: 0x") + String(summary.exc_pc, HEX));
      // Log backtrace addresses
      String bt = "💥 Backtrace:";
      for (uint32_t i = 0; i < summary.exc_bt_info.depth && i < 16; i++) {
        bt += " 0x" + String(summary.exc_bt_info.bt[i], HEX);
      }
      engine->error(bt);
      if (summary.exc_bt_info.corrupted) {
        engine->warn("⚠️ Backtrace may be corrupted");
      }
      engine->error("💥 Full decode: pio run -t coredump-info");
    } else {
      engine->error(String("💥 Could not read coredump (err ") + String(cdErr) + ") — use Serial monitor for live backtrace");
    }
  } else if (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT) {
    engine->error("⏱️ WATCHDOG timeout! A task is blocked or starving other tasks");
  }
  
  engine->info("\n=== ESP32-S3 Stepper Controller ===");
  randomSeed(analogRead(0) + esp_random());
  
  // ============================================================================
  // 2. NETWORK (WiFi - determines AP or STA mode)
  // ============================================================================
  StepperNetwork.begin();
  
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
  // AP_SETUP MODE: Minimal setup complete - WiFi configuration only
  // ============================================================================
  if (StepperNetwork.isAPSetupMode()) {
    setRgbLed(0, 0, 50);  // Start with BLUE (dimmed) - waiting for config
    engine->info("\n╔════════════════════════════════════════════════════════╗");
    engine->info("║  MODE AP_SETUP - WiFi CONFIGURATION                    ║");
    engine->info("║  Access: http://192.168.4.1                            ║");
    engine->info("║  Connect to network: " + String(otaHostname) + "-Setup           ║");
    engine->info("║  LED: Blue/Red blinking (awaiting config)              ║");
    engine->info("║       Solid Green = config OK, Solid Red = failure     ║");
    engine->info("╚════════════════════════════════════════════════════════╝\n");
    return;  // Skip stepper initialization in AP_SETUP mode
  }
  
  // ============================================================================
  // STA+AP or AP_DIRECT: Full stepper controller initialization
  // ============================================================================
  
  // LED color based on mode
  if (StepperNetwork.isSTAMode()) {
    setRgbLed(0, 50, 0);  // GREEN = WiFi connected + AP
  } else {
    setRgbLed(0, 25, 50);  // CYAN = AP Direct mode
  }
  
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
  
  config.currentState = SystemState::STATE_READY;
  engine->info("\n╔════════════════════════════════════════════════════════╗");
  if (StepperNetwork.isSTAMode()) {
    engine->info("║  WEB INTERFACE READY! (STA+AP)                         ║");
    engine->info("║  STA: http://" + WiFi.localIP().toString() + "                          ║");
    engine->info("║  AP:  http://" + WiFi.softAPIP().toString() + "                       ║");
    engine->info("║  mDNS: http://" + String(otaHostname) + ".local                 ║");
  } else {
    engine->info("║  WEB INTERFACE READY! (AP Direct)                      ║");
    engine->info("║  Access: http://" + WiFi.softAPIP().toString() + "                    ║");
    engine->info("║  StepperNetwork: " + String(otaHostname) + "-AP                         ║");
  }
  engine->info("║  Auto-calibration starts in 1 second...               ║");
  engine->info("╚════════════════════════════════════════════════════════╝\n");
  
  // ============================================================================
  // 8. DUAL-CORE FREERTOS INITIALIZATION - STA mode only
  // ============================================================================
  
  // Create mutexes for shared data protection
  motionMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();
  statsMutex = xSemaphoreCreateMutex();
  wsMutex = xSemaphoreCreateRecursiveMutex();  // Recursive: Logger::log() may re-enter from WS callback
  
  if (motionMutex == NULL || stateMutex == NULL || statsMutex == NULL || wsMutex == NULL) {
    engine->error("❌ Failed to create FreeRTOS mutexes!");
    return;
  }
  
  // Create MOTOR task on Core 1 (PRO_CPU) - HIGH priority for real-time stepping
  xTaskCreatePinnedToCore(
    motorTask,           // Task function
    "MotorTask",         // Name
    6144,                // Stack size (bytes)
    NULL,                // Parameters
    10,                  // Priority (10 = high, ensures motor runs first)
    &motorTaskHandle,    // Task handle
    1                    // Core 1 (PRO_CPU)
  );
  
  // Create NETWORK task on Core 0 (APP_CPU) - Normal priority for network ops
  xTaskCreatePinnedToCore(
    networkTask,         // Task function
    "NetworkTask",       // Name
    12288,                // Stack size (larger for JSON serialization)
    NULL,                // Parameters
    1,                   // Priority (1 = normal)
    &networkTaskHandle,  // Task handle
    0                    // Core 0 (APP_CPU)
  );
  
  engine->info("✅ DUAL-CORE initialized: Motor=Core1(P10), StepperNetwork=Core0(P1)");
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
    // MANUAL CALIBRATION REQUEST (triggered from Core 0 via flag)
    // ═══════════════════════════════════════════════════════════════════════
    if (requestCalibration) {
      requestCalibration = false;
      engine->info("=== Manual calibration requested ===");
      
      // Cooperative flag: networkTask will skip webSocket/server during calibration
      // (CalibrationManager handles them internally via serviceWebSocket())
      calibrationInProgress = true;
      
      Calibration.startCalibration();
      
      // Resume normal networkTask operation
      calibrationInProgress = false;
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
        
        // Cooperative flag: networkTask will skip webSocket/server during calibration
        // (CalibrationManager handles them internally via serviceWebSocket())
        calibrationInProgress = true;
        
        Calibration.startCalibration();
        needsInitialCalibration = false;
        
        // Resume normal networkTask operation
        calibrationInProgress = false;
      }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MOVEMENT EXECUTION (timing-critical, runs on dedicated core)
    // ═══════════════════════════════════════════════════════════════════════
    switch (currentMovement) {
      case MovementType::MOVEMENT_VAET:
        BaseMovement.process();
        break;
        
      case MovementType::MOVEMENT_PURSUIT:
        if (pursuit.isMoving) {
          unsigned long currentMicros = micros();
          if (currentMicros - lastStepMicros >= pursuit.stepDelay) {
            lastStepMicros = currentMicros;
            Pursuit.process();
          }
        }
        break;
        
      case MovementType::MOVEMENT_OSC:
        if (config.currentState == SystemState::STATE_RUNNING) {
          Osc.process();
        }
        break;
        
      case MovementType::MOVEMENT_CHAOS:
        if (config.currentState == SystemState::STATE_RUNNING) {
          Chaos.process();
        }
        break;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // SEQUENCER (logic only, no network blocking)
    // ═══════════════════════════════════════════════════════════════════════
    if (config.executionContext == ExecutionContext::CONTEXT_SEQUENCER) {
      SeqExecutor.process();
    }

    // ALM monitoring always active (safety critical)
    static bool lastAlarmState = false;
    bool alarmActive = Motor.isAlarmActive();
    
    if (alarmActive && !lastAlarmState) {
      engine->warn("🚨 HSS86 ALARM ACTIVE - Check motor/mechanics!");
    } else if (!alarmActive && lastAlarmState) {
      engine->info("✅ HSS86 Alarm cleared");
    }
    lastAlarmState = alarmActive;
    
    // ═══════════════════════════════════════════════════════════════════════
    // PEND + CYCLE COUNTER (periodic stats) - Debug only
    // ═══════════════════════════════════════════════════════════════════════
    if (engine->getLogLevel() == LOG_DEBUG && config.currentState == SystemState::STATE_RUNNING) {
      Motor.updatePendTracking();
      
      static unsigned long lastPendLogMs = 0;
      static unsigned long lastPendCount = 0;
      
      // Periodic HSS86 stats (PEND transitions count is sufficient for health check)
      if (millis() - lastPendLogMs > SUMMARY_LOG_INTERVAL_MS) {
        unsigned long currentPendCount = Motor.getPendInterruptCount();
        unsigned long pendTransitions = currentPendCount - lastPendCount;
        int rawAlm = digitalRead(PIN_ALM);
        
        engine->debug("📊 HSS86: PEND transitions=" + String(pendTransitions) + "/10s" +
              " (total=" + String(currentPendCount) + ") | ALM=" + String(rawAlm));
        
        lastPendCount = currentPendCount;
        lastPendLogMs = millis();
      }

      static unsigned long lastSummary = 0;
      static unsigned long cycleCounter = 0;
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
    }


    
    // ═══════════════════════════════════════════════════════════════════════
    // STACK HIGH-WATER MARK (periodic safety diagnostic)
    // ═══════════════════════════════════════════════════════════════════════
    {
      static unsigned long lastStackCheckMs = 0;
      if (millis() - lastStackCheckMs > STACK_HWM_LOG_INTERVAL_MS) {
        lastStackCheckMs = millis();
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
        engine->info("📐 MotorTask stack HWM: " + String(hwm) + " bytes free (of 6144)");
        if (hwm < 500) {
          engine->warn("⚠️ MotorTask stack critically low! Consider increasing stack size.");
        }
      }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // TASK YIELD - Adaptive based on motor state
    // ═══════════════════════════════════════════════════════════════════════
    if (config.currentState == SystemState::STATE_RUNNING) {
      // Motor running: minimal yield to maintain step timing
      taskYIELD();
    } else {
      // Motor idle: longer delay to reduce CPU usage
      vTaskDelay(pdMS_TO_TICKS(1));  // 1ms when not running
    }
  }
}

// ============================================================================
// NETWORK TASK - Core 0 (APP_CPU) - StepperNetwork operations (can block)
// ============================================================================
void networkTask(void* param) {
  engine->info("🌐 NetworkTask started on Core " + String(xPortGetCoreID()));
  
  while (true) {
    // ═══════════════════════════════════════════════════════════════════════
    // NETWORK SERVICES (can block without affecting motor)
    // ═══════════════════════════════════════════════════════════════════════
    StepperNetwork.handleOTA();
    StepperNetwork.handleCaptivePortal();  // DNS server for AP clients (all AP modes)
    StepperNetwork.checkConnectionHealth();
    
    // HTTP server and WebSocket - skip during calibration or blocking moves
    // (CalibrationManager/blocking loops handle them internally via serviceWebSocket())
    if (!calibrationInProgress && !blockingMoveInProgress) {
      if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        server.handleClient();
        webSocket.loop();
        xSemaphoreGiveRecursive(wsMutex);
      }
    
      // ═══════════════════════════════════════════════════════════════════════
      // STATUS BROADCAST (adaptive rate: 10Hz active, 5Hz calibrating, 1Hz idle)
      // ═══════════════════════════════════════════════════════════════════════
      static unsigned long lastUpdate = 0;
      if (millis() - lastUpdate > Status.getAdaptiveBroadcastInterval()) {
        lastUpdate = millis();
        if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
          sendStatus();  // Uses webSocket.broadcastTXT - must not run concurrently with Core 1
          xSemaphoreGiveRecursive(wsMutex);
        }
      }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LOG BUFFER FLUSH (I/O to filesystem)
    // ═══════════════════════════════════════════════════════════════════════
    if (engine) {
      engine->flushLogBuffer();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STACK HIGH-WATER MARK (periodic safety diagnostic)
    // ═══════════════════════════════════════════════════════════════════════
    {
      static unsigned long lastStackCheckMs = 0;
      if (millis() - lastStackCheckMs > STACK_HWM_LOG_INTERVAL_MS) {
        lastStackCheckMs = millis();
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
        engine->info("📐 NetworkTask stack HWM: " + String(hwm) + " bytes free (of 12288)");
        if (hwm < 500) {
          engine->warn("⚠️ NetworkTask stack critically low! Consider increasing stack size.");
        }
      }
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
  // AP_SETUP MODE: WiFi configuration only (no dual-core tasks running)
  // ═══════════════════════════════════════════════════════════════════════════
  if (StepperNetwork.isAPSetupMode()) {
    StepperNetwork.handleCaptivePortal();
    
    // Blink LED Blue/Red every 500ms
    static unsigned long lastLedToggle = 0;
    static bool ledIsBlue = true;
    
    if (StepperNetwork.apLedBlinkEnabled && millis() - lastLedToggle > AP_LED_BLINK_INTERVAL_MS) {
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
  // StepperNetwork operations run on Core 0 (networkTask)
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