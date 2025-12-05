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

// Movement controllers (modular architecture)
#include "movement/ChaosController.h"       // Chaos mode controller (Chaos.start(), Chaos.process()...)
#include "movement/OscillationController.h" // Oscillation mode controller (Osc.start(), Osc.process()...)
#include "movement/PursuitController.h"     // Pursuit mode controller (Pursuit.move(), Pursuit.process()...)
#include "movement/VaEtVientController.h"   // Va-et-vient mode controller (VaEtVient.setDistance()...)

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
// GLOBAL STATE VARIABLES (now managed in SystemConfig struct)
// ============================================================================
// SystemConfig config; declared at setup(), loads from config.json
// Access via: config.currentState, config.executionContext
SystemConfig config;  // Global instance - loaded from config.json in setup()

// ============================================================================
// MOTION VARIABLES (VA-ET-VIENT) - Position managed in SystemConfig
// ============================================================================
volatile long currentStep = 0;
// config.minStep, config.maxStep, config.totalDistanceMM → config.minStep, config.maxStep, config.totalDistanceMM

// Maximum distance limitation (50-100% of config.totalDistanceMM)
float maxDistanceLimitPercent = 100.0;  // Default: no limitation
float effectiveMaxDistanceMM = 0.0;     // Calculated value used for validation

MotionConfig motion;
PendingMotionConfig pendingMotion;

// Pause state for Simple mode (VAET)
CyclePauseState motionPauseState;

bool isPaused = false;
bool movingForward = true;
long startStep = 0;
long targetStep = 0;
bool hasReachedStartStep = false;  // Track if we've reached startStep at least once

// ============================================================================
// TIMING VARIABLES
// ============================================================================
unsigned long lastStepMicros = 0;
unsigned long stepDelayMicrosForward = 1000;
unsigned long stepDelayMicrosBackward = 1000;

unsigned long lastStartContactMillis = 0;
unsigned long cycleTimeMillis = 0;
float measuredCyclesPerMinute = 0;
bool wasAtStart = false;

// Motor direction tracking (for smooth direction changes)
bool currentMotorDirection = HIGH;  // Current physical motor direction

// ============================================================================
// PURSUIT & DECELERATION ZONE
// ============================================================================
PursuitState pursuit;
DecelZoneConfig decelZone;  // Global deceleration zone configuration

// ============================================================================
// OSCILLATION MODE
// ============================================================================
OscillationConfig oscillation;
OscillationState oscillationState;

// Pause state for Oscillation mode
CyclePauseState oscPauseState;

// Global variable for actual oscillation speed (for display)
float actualOscillationSpeedMMS = 0.0;  // Real speed considering hardware limits

// ============================================================================
// CHAOS MODE
// ============================================================================
ChaosRuntimeConfig chaos;
ChaosExecutionState chaosState;

// ============================================================================
// SEQUENCER
// ============================================================================
// Helper functions for debugging
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

// Sequence table (max 20 lines)
#define MAX_SEQUENCE_LINES 20
SequenceLine sequenceTable[MAX_SEQUENCE_LINES];

// Current movement type and sequencer state
MovementType currentMovement = MOVEMENT_VAET;  // Default: Va-et-vient
int sequenceLineCount = 0;
int nextLineId = 1;  // Auto-increment ID counter

SequenceExecutionState seqState;

// ============================================================================
// STATISTICS
// ============================================================================
unsigned long totalDistanceTraveled = 0;  // Total distance (all movements, displayed in UI)
unsigned long lastSavedDistance = 0;      // Last saved value (to calculate increments)
long lastStepForDistance = 0;

// ============================================================================
// STARTUP FLAGS
// ============================================================================
bool needsInitialCalibration = true;

// ============================================================================
// SINE LOOKUP TABLE (Optional Performance Optimization)
// ============================================================================
#ifdef USE_SINE_LOOKUP_TABLE
float sineTable[SINE_TABLE_SIZE];

void initSineTable() {
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    sineTable[i] = -cos((i / (float)SINE_TABLE_SIZE) * 2.0 * PI);
  }
  engine->debug("✅ Sine lookup table initialized (" + String(SINE_TABLE_SIZE) + " points, " + String(SINE_TABLE_SIZE * 4) + " bytes)");
}

// Fast sine lookup with linear interpolation
inline float fastSine(float phase) {
  float indexFloat = phase * SINE_TABLE_SIZE;
  int index = (int)indexFloat % SINE_TABLE_SIZE;
  int nextIndex = (index + 1) % SINE_TABLE_SIZE;
  
  // Linear interpolation for smooth transitions
  float fraction = indexFloat - (int)indexFloat;
  return sineTable[index] + (sineTable[nextIndex] - sineTable[index]) * fraction;
}
#endif

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
void handleCalibrationFailure(int& attempt);
void startMovement(float distMM, float speedLevel);
void calculateStepDelay();
// doStep() removed - now delegated to VaEtVient.doStep() and Chaos.doStep()
void togglePause();
void stopMovement();
void setDistance(float distMM);
void setStartPosition(float startMM);
void setSpeedForward(float speedLevel);
void setSpeedBackward(float speedLevel);
void resetTotalDistance();
void sendStatus();
// webSocketEvent moved to CommandDispatcher module
void saveCurrentSessionStats();

// Speed conversion utilities
float speedLevelToCyclesPerMin(float speedLevel);
float cyclesPerMinToSpeedLevel(float cpm);

// ============================================================================
// STATS ON-DEMAND TRACKING
// ============================================================================
// Track if frontend has requested system stats (for optimization)
bool statsRequested = false;
unsigned long lastStatsRequestTime = 0;

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

/**
 * Debounced contact reading with majority voting
 * Tolerates bounce while rejecting false positives
 * 
 * @param pin Contact pin to read (PIN_START_CONTACT or PIN_END_CONTACT)
 * @param expectedState Expected state (LOW = contact engaged, HIGH = not engaged)
 * @param checks Number of checks (default: 3, requires 2/3 majority)
 * @param delayMicros Microseconds between checks (default: 100µs)
 * @return true if majority of checks confirm expectedState
 * 
 * NOTE: Now delegates to Contacts.readDebounced() from hardware/ContactSensors.h
 */
inline bool readContactDebounced(int pin, int expectedState, int checks = 3, int delayMicros = 100) {
  // Delegate to modular ContactSensors
  return Contacts.readDebounced(pin, expectedState, checks, delayMicros);
}

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
// Core parameter validation functions
bool validateDistance(float distMM, String& errorMsg);
bool validateSpeed(float speedLevel, String& errorMsg);
bool validatePosition(float positionMM, String& errorMsg);

// Mode-specific validation functions
bool validateChaosParams(float centerMM, float amplitudeMM, float maxSpeed, float craziness, String& errorMsg);
bool validateOscillationParams(float centerMM, float amplitudeMM, float frequencyHz, String& errorMsg);
// validateOscillationAmplitude() now in OscillationController module

// Validation + error reporting helper
bool validateAndReport(bool isValid, const String& errorMsg);

// Deceleration zone functions
float calculateSlowdownFactor(float zoneProgress);
int calculateAdjustedDelay(float currentPositionMM, float movementStartMM, float movementEndMM, int baseDelayMicros);
void validateDecelZone();

// Pursuit mode - delegated to PursuitController module
// Functions: pursuitMove(), doPursuitStep()

// Oscillation mode - delegated to OscillationController module
// Functions: startOscillation(), doOscillationStep(), calculateOscillationPosition(), validateOscillationAmplitude()

// Chaos mode
void generateChaosPattern();
void processChaosExecution();
void startChaos();
void stopChaos();

// ============================================================================
// SETUP - INITIALIZATION
// ============================================================================
void setup() {
  Serial.begin(115200);
  // Allow serial to initialize (non-critical, keep simple delay)
  delay(1000);
  
  // Initialize random seed for cycle pause random mode
  randomSeed(analogRead(0) + esp_random());
  
  // ============================================================================
  // UTILITY ENGINE INITIALIZATION (MUST BE FIRST!)
  // ============================================================================
  // Creates unified instance for WebSocket, Logging, and LittleFS management
  engine = new UtilityEngine(webSocket);
  if (!engine->initialize()) {
    Serial.println("❌ UtilityEngine initialization failed!");
    // Continue anyway - engine will degrade gracefully
  } else {
    engine->info("✅ UtilityEngine initialized and ready");
  }
  
  engine->info("\n=== ESP32-S3 Stepper Controller ===");
  
  // ============================================================================
  // HARDWARE ABSTRACTION LAYER INITIALIZATION
  // ============================================================================
  // Initialize modular hardware drivers (MotorDriver + ContactSensors)
  Motor.init();      // Initializes PIN_PULSE, PIN_DIR, PIN_ENABLE
  Contacts.init();   // Initializes PIN_START_CONTACT, PIN_END_CONTACT
  engine->info("✅ Hardware abstraction layer initialized (Motor + Contacts)");
  
  // ============================================================================
  // CALIBRATION MANAGER INITIALIZATION
  // ============================================================================
  // Initialize CalibrationManager with WebSocket and Server references
  // Callbacks will be set after WebSocket is fully initialized
  Calibration.init(&webSocket, &server);
  engine->info("✅ CalibrationManager initialized");
  
  // Legacy compatibility: ensure direction tracking variable is in sync
  Motor.setDirection(false);  // Initialize direction to backward
  
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  engine->info("Connecting to WiFi: " + String(ssid));
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);  // WiFi connection polling - acceptable
    Serial.print(".");  // Visual progress indicator (standard Arduino pattern)
    attempts++;
    
    if (attempts % 10 == 0) {
      engine->info(String("\n[") + String(attempts) + "/60] WiFi connecting...");
    }
  }
  Serial.println();  // Newline after dots (keep for visual formatting)
  
  if (WiFi.status() == WL_CONNECTED) {
    engine->info("✅ WiFi connected!");
    engine->info("🌐 IP Address: " + WiFi.localIP().toString());
    engine->info("🔄 OTA Mode: ACTIVE - Updates via WiFi enabled!");
    
    // ============================================================================
    // mDNS (Multicast DNS) - Access via http://esp32-stepper.local
    // ============================================================================
    if (MDNS.begin(otaHostname)) {
      engine->info("✅ mDNS responder started: http://" + String(otaHostname) + ".local");
      MDNS.addService("http", "tcp", 80);  // Announce HTTP service on port 80
    } else {
      engine->error("❌ Error starting mDNS responder");
    }
    
    // Configure time with NTP (GMT+1 = 3600 seconds, daylight saving = 0)
    // Date: October 22, 2025
    configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
    engine->info("⏰ NTP time configured (GMT+1)");
    
    // Wait a bit for time sync
    delay(1000);
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo->tm_year > (2020 - 1900)) {
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
      engine->info("✓ Time synchronized: " + String(timeStr));
    }
    
    // ============================================================================
    // OTA (Over-The-Air) UPDATE CONFIGURATION
    // ============================================================================
    ArduinoOTA.setHostname(otaHostname);
    // No password for now (otaPassword is empty)
    if (strlen(otaPassword) > 0) {
      ArduinoOTA.setPassword(otaPassword);
    }
    
    ArduinoOTA.onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
      engine->info("🔄 OTA Update starting: " + type);
      
      // CRITICAL: Flush and close log file before OTA
      if (engine) {
        engine->flushLogBuffer(true);  // Force flush pending logs
      }
      
      // CRITICAL: Stop all movements and disable motor during OTA
      stopMovement();
      Motor.disable();  // Disable motor for safety during OTA
      
      // Stop sequencer if running
      if (seqState.isRunning) {
        SeqExecutor.stop();
      }
    });
    
    ArduinoOTA.onEnd([]() {
      engine->info("✅ OTA Update complete - Rebooting...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      static unsigned int lastPercent = 0;
      unsigned int percent = (progress * 100) / total;
      
      // Log every 10% to avoid spam
      if (percent >= lastPercent + 10) {
        engine->info("📥 OTA Progress: " + String(percent) + "%");
        lastPercent = percent;
      }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
      engine->error("❌ OTA Error [" + String(error) + "]: ");
      if (error == OTA_AUTH_ERROR) engine->error("   Authentication Failed");
      else if (error == OTA_BEGIN_ERROR) engine->error("   Begin Failed");
      else if (error == OTA_CONNECT_ERROR) engine->error("   Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) engine->error("   Receive Failed");
      else if (error == OTA_END_ERROR) engine->error("   End Failed");
    });
    
    ArduinoOTA.begin();
    engine->info("✅ OTA Ready - Hostname: " + String(otaHostname));
    
  } else {
    engine->error("❌ WiFi connection failed!");
  }
  
  // Print engine status after WiFi setup
  engine->printStatus();
  
  // ============================================================================
  // HTTP SERVER CONFIGURATION
  // ============================================================================
  // All HTTP routes are now managed in APIRoutes.h (setupAPIRoutes())
  // ============================================================================
  
  server.begin();
  engine->info("HTTP server started");

  // ============================================================================
  // FILESYSTEM MANAGER - Register all REST API routes
  // ============================================================================
  filesystemManager.registerRoutes();
  engine->info("✅ Filesystem Manager initialized - Routes ready:");
  engine->info("   GET  /filesystem           - Filesystem browser UI");
  engine->info("   GET  /api/fs/list          - List files (recursive JSON)");
  engine->info("   GET  /api/fs/download      - Download file");
  engine->info("   GET  /api/fs/read          - Read file content (text)");
  engine->info("   POST /api/fs/write         - Save edited file");
  engine->info("   POST /api/fs/upload        - Upload file (multipart)");
  engine->info("   POST /api/fs/delete        - Delete file");
  engine->info("   POST /api/fs/clear         - Clear all files");

  // ============================================================================
  // SETUP API ROUTES (Phase 3.4 - extracted from main)
  // ============================================================================
  setupAPIRoutes();
  engine->info("✅ API Routes initialized :");
  engine->info("   GET  /               - Web interface");
  engine->info("   GET  /style.css      - Stylesheet");
  engine->info("   GET  /api/stats      - Daily statistics");
  engine->info("   GET  /api/playlists  - Preset playlists");
  engine->info("   GET  /logs           - Log files");
  
  // ============================================================================
  // VERIFY PLAYLIST FILE INTEGRITY AT STARTUP
  // ============================================================================
  if (LittleFS.exists(PLAYLIST_FILE_PATH)) {
    File pFile = LittleFS.open(PLAYLIST_FILE_PATH, "r");
    if (pFile) {
      size_t pSize = pFile.size();
      String pContent = pFile.readString();
      pFile.close();
      
      engine->info("📋 Playlist file found: " + String(PLAYLIST_FILE_PATH));
      engine->info("   Size: " + String(pSize) + " bytes");
      
      if (pSize == 0 || pContent.length() == 0) {
        engine->warn("   ⚠️ File is EMPTY");
      } else {
        // Validate JSON structure
        JsonDocument pDoc;
        DeserializationError pError = deserializeJson(pDoc, pContent);
        if (pError) {
          engine->error("   ❌ JSON CORRUPTED: " + String(pError.c_str()));
          engine->warn("   🔧 File will be reset on first access");
        } else {
          int simpleCount = pDoc["simple"].size();
          int oscCount = pDoc["oscillation"].size();
          int chaosCount = pDoc["chaos"].size();
          int total = simpleCount + oscCount + chaosCount;
          engine->info("   ✅ JSON valid - " + String(total) + " presets loaded");
          engine->debug("      Simple: " + String(simpleCount) + 
                        ", Osc: " + String(oscCount) + 
                        ", Chaos: " + String(chaosCount));
        }
      }
    } else {
      engine->error("📋 ❌ Failed to open playlist file!");
    }
  } else {
    engine->info("📋 No playlist file found (will be created on first save)");
  }
  
  // Start WebSocket server
  webSocket.begin();
  
  // Initialize CommandDispatcher and register as WebSocket event handler
  Dispatcher.begin(&webSocket);
  webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    Dispatcher.onWebSocketEvent(num, type, payload, length);
  });
  engine->info("WebSocket server started on port 81 (CommandDispatcher routing)");
  
  // ============================================================================
  // CALIBRATION MANAGER CALLBACKS
  // ============================================================================
  // Set callbacks now that sendStatus() and sendError() are available
  Calibration.setStatusCallback(sendStatus);
  Calibration.setErrorCallback([](const String& msg) { sendError(msg); });
  Calibration.setCompletionCallback([]() { SeqExecutor.onMovementComplete(); });
  engine->info("✅ CalibrationManager callbacks configured");
  
  // ============================================================================
  // INITIALIZE SINE LOOKUP TABLE (Optional)
  // ============================================================================
  #ifdef USE_SINE_LOOKUP_TABLE
  initSineTable();
  #endif
  
  config.currentState = STATE_READY;
  engine->info("\n╔════════════════════════════════════════════════════════╗");
  engine->info("║  WEB INTERFACE READY!                                  ║");
  engine->info("║  Access: http://" + WiFi.localIP().toString() + "                          ║");
  engine->info("║  Auto-calibration starts in 2 seconds...              ║");
  engine->info("║  Connect now to see calibration overlay!              ║");
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
  ArduinoOTA.handle();
  
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
      if (config.currentState == STATE_RUNNING && !isPaused) {
        
        // 🆕 NOUVEAU: Vérifier si en pause entre cycles
        if (motionPauseState.isPausing) {
          unsigned long elapsedMs = millis() - motionPauseState.pauseStartMs;
          if (elapsedMs >= motionPauseState.currentPauseDuration) {
            // Fin de pause, reprendre le mouvement
            motionPauseState.isPausing = false;
            movingForward = true;  // Reprendre direction forward
            engine->debug("▶️ Fin pause cycle VAET");
          }
          // Pendant la pause, ne rien faire (pas de step)
          break;
        }
        
        unsigned long currentMicros = micros();
        unsigned long currentDelay = movingForward ? stepDelayMicrosForward : stepDelayMicrosBackward;
        
        // Apply deceleration zone adjustment if enabled
        if (decelZone.enabled && hasReachedStartStep) {
          float currentPositionMM = (currentStep - startStep) / STEPS_PER_MM;
          
          // CRITICAL: Direction matters!
          float movementStartMM, movementEndMM;
          if (movingForward) {
            movementStartMM = 0.0;
            movementEndMM = motion.targetDistanceMM;
          } else {
            // Inverted for backward movement
            movementStartMM = motion.targetDistanceMM;
            movementEndMM = 0.0;
          }
          
          currentDelay = calculateAdjustedDelay(currentPositionMM, movementStartMM, movementEndMM, currentDelay);
        }
        
        if (currentMicros - lastStepMicros >= currentDelay) {
          lastStepMicros = currentMicros;
          VaEtVient.doStep();  // Phase 3: Delegated to VaEtVient module
        }
      }
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
      if (config.currentState == STATE_RUNNING && !isPaused) {
        Osc.process();
      }
      break;
      
    case MOVEMENT_CHAOS:
      // CHAOS: Random chaotic patterns (delegated to ChaosController module)
      if (config.currentState == STATE_RUNNING && !isPaused) {
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
  
  if (config.currentState == STATE_RUNNING && !isPaused) {
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
// MOTOR CONTROL - Low Level (Delegating to MotorDriver module)
// ============================================================================
// These wrapper functions maintain backward compatibility while delegating
// to the new modular MotorDriver class. This allows incremental migration.
// ============================================================================


// ============================================================================
// MOTOR CONTROL - Calibration Helper Functions
// ============================================================================

bool findContactWithService(int dirPin, int contactPin, const char* contactName) {
  
  bool inDir = (dirPin == HIGH);

  Motor.setDirection(inDir);
  unsigned long stepCount = 0;

  // Search for contact with debouncing (3 checks, 25µs interval)
  // Fast debounce during search phase (non-critical)
  while (Contacts.readDebounced(contactPin, HIGH, 3, 25)) {
    Motor.step();
    currentStep += inDir ? 1 : -1;
    delayMicroseconds(CALIB_DELAY);
    stepCount++;
    
    if (stepCount % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
      yield();
      webSocket.loop();
      server.handleClient();
    }
    
    if (stepCount > CALIBRATION_MAX_STEPS) {
      String errorMsg = "❌ ERROR: Contact ";
      errorMsg += contactName;
      errorMsg += " introuvable";
      sendError(errorMsg);
      Motor.disable();  // Safety: disable motor on calibration failure
      config.currentState = STATE_ERROR;
      return false;
    }
  }

  // Validate END contact is within expected range (filter mechanical bounces)
  // If contact detected BEFORE minimum distance → likely false positive
  if (contactPin == PIN_END_CONTACT) {
        // Contact detected - Zone validation
    long detectedSteps = abs(currentStep);
    long minExpectedSteps = (long)(HARD_MIN_DISTANCE_MM * STEPS_PER_MM);
    if (detectedSteps < minExpectedSteps) {
      engine->error("Contact END détecté AVANT zone valide (" + 
                    String(detectedSteps) + " < " + String(minExpectedSteps) + " steps)");
      engine->error("→ Rebond mécanique probable - Position ignorée");
      
      // Ignore false contact and continue search (recursive retry)
      return findContactWithService(dirPin, contactPin, contactName);
    }
  }

  return true;
}

bool returnToStartContact() {

  engine->debug("Expected steps to return: ~" + String(config.maxStep));
  
  // ============================================================================
  // SAFETY CHECK: Detect if stuck at END contact (recovery from ERROR state)
  // ============================================================================
  if (Contacts.readDebounced(PIN_END_CONTACT, LOW, 3, 50)) {
    engine->warn("⚠️ Détection contact END actif - Déblocage automatique...");
    sendStatus();  // Show "Décollement en cours..."
    
    Motor.setDirection(false);  // Backward (away from END)
    
    // Emergency decontact: Move backward until END contact releases
    int emergencySteps = 0;
    const int MAX_EMERGENCY_STEPS = 300;  // ~50mm safety margin
    
    while (Contacts.readDebounced(PIN_END_CONTACT, LOW, 3, 50) && emergencySteps < MAX_EMERGENCY_STEPS) {
      Motor.step();
      currentStep--;
      emergencySteps++;
      delayMicroseconds(CALIB_DELAY);  // Slow speed for safety
      
      // Service WebSocket every 50 steps
      if (emergencySteps % 50 == 0) {
        yield();
        webSocket.loop();
        server.handleClient();
      }
    }
    
    if (emergencySteps >= MAX_EMERGENCY_STEPS) {
      sendError("❌ ERREUR: Impossible de décoller du contact END - Vérifiez mécaniquement");
      Motor.disable();  // Safety: disable motor on error
      config.currentState = STATE_ERROR;
      return false;
    }
    
    engine->info("✓ Décollement réussi (" + String(emergencySteps) + " steps, " + 
                 String(emergencySteps / STEPS_PER_MM, 1) + " mm)");
    delay(200);  // Let mechanics settle
  }
  
  Motor.setDirection(false);  // Backward to START contact
  
  // Search for START contact with debouncing (3 checks, 100µs interval)
  // Precise value needed for accurate return positioning
  while (Contacts.readDebounced(PIN_START_CONTACT, HIGH, 3, 100)) {
    Motor.step();
    currentStep--;
    delayMicroseconds(CALIB_DELAY);
    if (currentStep % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
      yield();
      webSocket.loop();
      server.handleClient();
    }
    if (currentStep < -CALIBRATION_ERROR_MARGIN_STEPS) {
      sendError("❌ ERROR: Impossible de retourner au contact START!");
      Motor.disable();  // Safety: disable motor on error
      config.currentState = STATE_ERROR;
      return false;
    }
  }
  
  // Contact detected - apply calibration offset directly
  engine->debug("Start contact detected - applying calibration offset...");
  
  // Move forward slowly until contact releases (decontact)
  // Standard debounce for decontact phase (3 checks, 100µs)
  Motor.setDirection(true);
  while (Contacts.readDebounced(PIN_START_CONTACT, LOW, 3, 100)) {
    Motor.step();
    delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);  // Same speed as initial calibration
  }
  
  // Add safety margin (same as initial calibration)
  for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
    Motor.step();
    delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
  }
  
  // NOW we're at the SAME physical position as initial calibration position 0
  currentStep = 0;
  
  return true;
}

float validateCalibrationAccuracy() {
  engine->debug("✓ Returned to start contact - Current position: " + String(currentStep) + " steps (" + String(currentStep / STEPS_PER_MM, 2) + " mm)");
  
  long stepDifference = abs(currentStep);
  float differencePercent = ((float)stepDifference / (float)config.maxStep) * 100.0;
  
  if (stepDifference == 0) {
    engine->debug(" ✓ PERFECT!");
  } else {
    engine->warn(" ⚠️ Difference: " + String(stepDifference) + " steps (" + 
          String((float)stepDifference / STEPS_PER_MM, 2) + " mm, " + 
          String(differencePercent, 1) + " %)");
  }
  
  return differencePercent;
}

// ============================================================================
// MOTOR CONTROL - Calibration Error Handler
// ============================================================================

void handleCalibrationFailure(int& attempt) {
  Motor.disable();  // Safety: disable motor on calibration failure
  config.currentState = STATE_ERROR;
  
  attempt++;
  if (attempt >= MAX_CALIBRATION_RETRIES) {
    sendError("❌ ERROR: Calibration échouée après 3 tentatives");
    attempt = 0;
  }
}

// ============================================================================
// DECELERATION ZONE FUNCTIONS
// ============================================================================

/**
 * Calculate slowdown factor based on position within deceleration zone
 * @param zoneProgress Position in zone: 0.0 (at boundary/contact) to 1.0 (exiting zone)
 * @return Slowdown factor (1.0 = normal speed, >1.0 = slower)
 */
float calculateSlowdownFactor(float zoneProgress) {
  // Maximum slowdown based on effect percentage
  // 0% effect = 1.0 (no slowdown)
  // 100% effect = 10.0 (10× slower at contact)
  float maxSlowdown = 1.0 + (decelZone.effectPercent / 100.0) * 9.0;
  
  float factor;
  
  switch (decelZone.mode) {
    case DECEL_LINEAR:
      // Linear: constant deceleration rate
      // zoneProgress=0.0 (contact) → max slowdown
      // zoneProgress=1.0 (exit) → normal speed
      factor = 1.0 + (1.0 - zoneProgress) * (maxSlowdown - 1.0);
      break;
      
    case DECEL_SINE:
      // Sinusoidal curve (smooth, max slowdown at contact)
      // zoneProgress=0.0: cos(0)=1.0 → smoothProgress=0.0 → max slowdown
      // zoneProgress=1.0: cos(PI)=-1.0 → smoothProgress=1.0 → normal speed
      {
        float smoothProgress = (1.0 - cos(zoneProgress * PI)) / 2.0;
        factor = 1.0 + (1.0 - smoothProgress) * (maxSlowdown - 1.0);
      }
      break;
      
    case DECEL_TRIANGLE_INV:
      // Triangle inverted: weak deceleration at start, strong at end
      // Uses quadratic curve: deceleration increases as we approach contact
      // zoneProgress=0.0 (contact) → max slowdown
      // zoneProgress=1.0 (exit) → normal speed
      // But curve is steeper near contact (inverted triangle shape)
      {
        float invProgress = 1.0 - zoneProgress;  // Invert so 0=exit, 1=contact
        float curved = invProgress * invProgress;  // Square for steeper curve at end
        factor = 1.0 + curved * (maxSlowdown - 1.0);
      }
      break;
      
    case DECEL_SINE_INV:
      // Sine inverted: weak deceleration at start, strong at end
      // Uses inverted sine curve for smooth but asymmetric deceleration
      // zoneProgress=0.0 (contact) → max slowdown
      // zoneProgress=1.0 (exit) → minimal slowdown
      {
        // Inverse sine: starts slow, accelerates deceleration toward contact
        // sin(0) = 0, sin(PI/2) = 1
        float invProgress = 1.0 - zoneProgress;
        float curved = sin(invProgress * PI / 2.0);  // 0 to PI/2 range
        factor = 1.0 + curved * (maxSlowdown - 1.0);
      }
      break;
      
    default:
      factor = 1.0;
      break;
  }
  
  return factor;
}

/**
 * Calculate adjusted delay based on position within movement range and deceleration zones
 * Zones are RELATIVE to the configured movement range, not absolute calibrated positions
 * 
 * @param currentPositionMM Current absolute position in mm (from START contact)
 * @param movementStartMM Start position of current movement in mm
 * @param movementEndMM End position of current movement in mm
 * @param baseDelayMicros Base delay in microseconds (normal speed)
 * @return Adjusted delay in microseconds (higher = slower)
 */
int calculateAdjustedDelay(float currentPositionMM, float movementStartMM, float movementEndMM, int baseDelayMicros) {
  // If deceleration disabled, return base speed
  if (!decelZone.enabled) {
    return baseDelayMicros;
  }
  
  // Calculate distances relative to movement boundaries
  float distanceFromStart = abs(currentPositionMM - movementStartMM);
  float distanceFromEnd = abs(movementEndMM - currentPositionMM);
  
  float slowdownFactor = 1.0;  // Default: normal speed
  
  // Check if in START deceleration zone
  if (decelZone.enableStart && distanceFromStart <= decelZone.zoneMM) {
    // Progress: 0.0 (at start boundary) → 1.0 (exiting zone toward center)
    float zoneProgress = distanceFromStart / decelZone.zoneMM;
    slowdownFactor = calculateSlowdownFactor(zoneProgress);
  }
  
  // Check if in END deceleration zone (takes priority if overlapping)
  if (decelZone.enableEnd && distanceFromEnd <= decelZone.zoneMM) {
    // Progress: 0.0 (at end boundary) → 1.0 (exiting zone toward center)
    float zoneProgress = distanceFromEnd / decelZone.zoneMM;
    slowdownFactor = calculateSlowdownFactor(zoneProgress);
  }
  
  // Apply slowdown factor to base delay
  return (int)(baseDelayMicros * slowdownFactor);
}

/**
 * Validate and adjust deceleration zone size to ensure it doesn't exceed movement amplitude
 * Should be called when zone config changes or movement distance changes
 */
void validateDecelZone() {
  if (!decelZone.enabled) {
    return;  // No validation needed if disabled
  }
  
  // Get current movement amplitude (not calibrated max)
  float movementAmplitudeMM = motion.targetDistanceMM;
  
  if (movementAmplitudeMM <= 0) {
    engine->warn("⚠️ Cannot validate decel zone: no movement configured");
    return;
  }
  
  float maxAllowedZone;
  
  // If both zones enabled, each can use max 50% of movement amplitude
  if (decelZone.enableStart && decelZone.enableEnd) {
    maxAllowedZone = movementAmplitudeMM / 2.0;
  } else {
    // If only one zone enabled, it can use entire amplitude
    maxAllowedZone = movementAmplitudeMM;
  }
  
  // Enforce minimum zone size (10mm) - P4 enhancement: catch negative values
  if (decelZone.zoneMM < 0) {
    decelZone.zoneMM = 10.0;
    engine->warn("⚠️ Zone négative détectée, corrigée à 10 mm");
  } else if (decelZone.zoneMM < 10.0) {
    decelZone.zoneMM = 10.0;
    engine->warn("⚠️ Zone augmentée à 10 mm (minimum)");
  }
  
  // Enforce maximum zone size
  if (decelZone.zoneMM > maxAllowedZone) {
    engine->warn("⚠️ Zone réduite de " + String(decelZone.zoneMM, 1) + " mm à " + 
          String(maxAllowedZone, 1) + " mm (max pour amplitude de " + 
          String(movementAmplitudeMM, 1) + " mm)");
    
    decelZone.zoneMM = maxAllowedZone;
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
// NOTE: Main validation logic moved to include/Validators.h
// These wrappers provide backward compatibility with existing code
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

// Legacy wrappers - delegate to Validators namespace
inline bool validateDistance(float distMM, String& errorMsg) {
  return Validators::distance(distMM, errorMsg);
}

inline bool validateSpeed(float speedLevel, String& errorMsg) {
  return Validators::speed(speedLevel, errorMsg);
}

inline bool validatePosition(float positionMM, String& errorMsg) {
  return Validators::position(positionMM, errorMsg);
}

inline bool validateMotionRange(float startMM, float distMM, String& errorMsg) {
  return Validators::motionRange(startMM, distMM, errorMsg);
}

inline bool validateChaosParams(float centerMM, float amplitudeMM, float maxSpeed, float craziness, String& errorMsg) {
  return Validators::chaosParams(centerMM, amplitudeMM, maxSpeed, craziness, errorMsg);
}

inline bool validateOscillationParams(float centerMM, float amplitudeMM, float frequency, String& errorMsg) {
  return Validators::oscillationParams(centerMM, amplitudeMM, frequency, errorMsg);
}

// ============================================================================
// SPEED CONVERSION UTILITIES
// ============================================================================

float speedLevelToCyclesPerMin(float speedLevel) {
  // Convert 0-20 scale to cycles/min (0-200)
  float cpm = speedLevel * 10.0;
  
  // Safety limits
  if (cpm < 0) cpm = 0;
  if (cpm > MAX_SPEED_LEVEL * 10.0) cpm = MAX_SPEED_LEVEL * 10.0;
  
  return cpm;
}

float cyclesPerMinToSpeedLevel(float cpm) {
  return cpm / 10.0;
}

// ============================================================================
// MOTOR CONTROL - Movement
// ============================================================================

void startMovement(float distMM, float speedLevel) {
  VaEtVient.start(distMM, speedLevel);
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

void calculateStepDelay() {
  VaEtVient.calculateStepDelay();
}

// ============================================================================
// MOTOR CONTROL - doStep() removed (Phase 3)
// Stepping now delegated to:
//   - VaEtVient.doStep() for MOVEMENT_VAET
//   - Chaos.doStep() for MOVEMENT_CHAOS (called via Chaos.process())
// ============================================================================

// ============================================================================
// MOTOR CONTROL - Helper Functions (delegates to VaEtVient)
// ============================================================================

void togglePause() {
  VaEtVient.togglePause();
}

void stopMovement() {
  VaEtVient.stop();
}

// ============================================================================
// MOTOR CONTROL - Parameter Updates (delegates to VaEtVient module)
// ============================================================================

void setDistance(float distMM) {
  VaEtVient.setDistance(distMM);
}

void setStartPosition(float startMM) {
  VaEtVient.setStartPosition(startMM);
}

void setSpeedForward(float speedLevel) {
  VaEtVient.setSpeedForward(speedLevel);
}

void setSpeedBackward(float speedLevel) {
  VaEtVient.setSpeedBackward(speedLevel);
}

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
// PURSUIT MODE - Delegated to PursuitController module
// ============================================================================
// Functions moved: pursuitMove(), doPursuitStep()
// Use: Pursuit.move(), Pursuit.process()

// ============================================================================
// OSCILLATION MODE - Delegated to OscillationController module
// ============================================================================
// Functions moved: calculateOscillationPosition(), validateOscillationAmplitude(),
//                  doOscillationStep(), startOscillation()
// Use: Osc.start(), Osc.process(), Osc.calculatePosition(), Osc.validateAmplitude()

void returnToStart() {
  VaEtVient.returnToStart();
}

// ============================================================================
// CHAOS MODE
// ============================================================================

/**
 * Start chaos mode (wrapper to ChaosController)
 */
void startChaos() {
  Chaos.start();
}

/**
 * Stop chaos mode (wrapper to ChaosController)
 */
void stopChaos() {
  Chaos.stop();
}

// ============================================================================
// NOTE: webSocketEvent moved to CommandDispatcher module
// See: src/communication/CommandDispatcher.cpp
// ============================================================================

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

// ============================================================================
// SEQUENCE TABLE MANAGEMENT FUNCTIONS
// ============================================================================
// NOTE: Functions moved to SequenceTableManager module (sequencer/SequenceTableManager.h)
// Inline wrappers defined above delegate to SeqTable singleton:
// - addSequenceLine() -> SeqTable.addLine()
// - deleteSequenceLine() -> SeqTable.deleteLine()
// - updateSequenceLine() -> SeqTable.updateLine()
// - moveSequenceLine() -> SeqTable.moveLine()
// - toggleSequenceLine() -> SeqTable.toggleLine()
// - duplicateSequenceLine() -> SeqTable.duplicateLine()
// - clearSequenceTable() -> SeqTable.clear()
// - exportSequenceToJson() -> SeqTable.exportToJson()
// - importSequenceFromJson() -> SeqTable.importFromJson()
// - validateSequenceLinePhysics() -> SeqTable.validatePhysics()
// - parseSequenceLineFromJson() -> SeqTable.parseFromJson()
// - broadcastSequenceTable() -> SeqTable.broadcast()
// - sendJsonResponse() -> SeqTable.sendJsonResponse()

// ============================================================================
// SEQUENCE EXECUTION FUNCTIONS (Delegated to SequenceExecutor module)
// ============================================================================
// Functions extracted to: src/sequencer/SequenceExecutor.cpp
// 
// Inline wrappers above provide backward-compatible API:
// - startSequenceExecution() -> SeqExecutor.start()
// - stopSequenceExecution() -> SeqExecutor.stop()
// - toggleSequencePause() -> SeqExecutor.togglePause()
// - skipToNextSequenceLine() -> SeqExecutor.skipToNextLine()
// - processSequenceExecution() -> SeqExecutor.process()
// - sendSequenceStatus() -> SeqExecutor.sendStatus()
// - onMovementComplete() -> SeqExecutor.onMovementComplete()

// ============================================================================
// STATUS BROADCASTING (Delegated to StatusBroadcaster module)
// ============================================================================
// Functions extracted to: src/communication/StatusBroadcaster.cpp
// 
// Inline wrapper above provides backward-compatible API:
// - sendStatus() -> Status.send()
