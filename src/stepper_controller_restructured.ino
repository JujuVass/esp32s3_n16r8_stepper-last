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
#include "Config.h"           // WiFi, OTA, GPIO, Hardware, Timing constants
#include "Types.h"            // Data structures and enums
#include "FilesystemManager.h"  // Filesystem browser API
#include "UtilityEngine.h"     // Unified logging, WebSocket, LittleFS manager
#include "APIRoutes.h"        // API routes module (extracted from main)
#include "Validators.h"       // Parameter validation functions

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

// Sequence table functions - now delegated to SequenceTableManager module
// These inline wrappers maintain API compatibility
inline SequenceLine parseSequenceLineFromJson(JsonVariantConst obj) { return SeqTable.parseFromJson(obj); }
inline String validateSequenceLinePhysics(const SequenceLine& line) { return SeqTable.validatePhysics(line); }
inline int addSequenceLine(SequenceLine newLine) { return SeqTable.addLine(newLine); }
inline bool updateSequenceLine(int lineId, SequenceLine updatedLine) { return SeqTable.updateLine(lineId, updatedLine); }
inline bool deleteSequenceLine(int lineId) { return SeqTable.deleteLine(lineId); }
inline bool moveSequenceLine(int lineId, int direction) { return SeqTable.moveLine(lineId, direction); }
inline bool toggleSequenceLine(int lineId, bool enabled) { return SeqTable.toggleLine(lineId, enabled); }
inline void clearSequenceTable() { SeqTable.clear(); }
inline String exportSequenceToJson() { return SeqTable.exportToJson(); }
inline int importSequenceFromJson(String jsonData) { return SeqTable.importFromJson(jsonData); }
inline int duplicateSequenceLine(int lineId) { return SeqTable.duplicateLine(lineId); }
inline void broadcastSequenceTable() { SeqTable.broadcast(); }
inline void sendJsonResponse(const char* type, const String& data) { SeqTable.sendJsonResponse(type, data); }

// Sequence execution functions - delegated to SequenceExecutor module
inline void startSequenceExecution(bool loopMode) { SeqExecutor.start(loopMode); }
inline void stopSequenceExecution() { SeqExecutor.stop(); }
inline void toggleSequencePause() { SeqExecutor.togglePause(); }
inline void skipToNextSequenceLine() { SeqExecutor.skipToNextLine(); }
inline void processSequenceExecution() { SeqExecutor.process(); }
inline void sendSequenceStatus() { SeqExecutor.sendStatus(); }
inline void onMovementComplete() { SeqExecutor.onMovementComplete(); }

// Status broadcasting - delegated to StatusBroadcaster module
inline void sendStatus() { Status.send(); }

// Functions
//void stepMotor();
void handleCalibrationFailure(int& attempt);
//void startCalibration();
void startMovement(float distMM, float speedLevel);
void calculateStepDelay();
void doStep();
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
bool validateOscillationAmplitude(float centerMM, float amplitudeMM, String& errorMsg);

// Validation + error reporting helper
bool validateAndReport(bool isValid, const String& errorMsg);

// Deceleration zone functions
float calculateSlowdownFactor(float zoneProgress);
int calculateAdjustedDelay(float currentPositionMM, float movementStartMM, float movementEndMM, int baseDelayMicros);
void validateDecelZone();

// Pursuit mode
void pursuitMove(float targetPositionMM, float maxSpeedLevel);
void doPursuitStep();

// Oscillation mode
float calculateOscillationPosition();
bool validateOscillationAmplitude(float centerMM, float amplitudeMM, String& errorMsg);
void doOscillationStep();
void startOscillation();

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
        stopSequenceExecution();
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
  Calibration.setCompletionCallback(onMovementComplete);
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
          doStep();
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
          doPursuitStep();
        }
      }
      break;
      
    case MOVEMENT_OSC:
      // OSCILLATION: Sinusoidal oscillation with ramping
      if (config.currentState == STATE_RUNNING && !isPaused) {
        doOscillationStep();
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
    processSequenceExecution();
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
  // ✅ Stop sequence if running (user manually starts simple mode)
  if (seqState.isRunning) {
    engine->debug("startMovement(): stopping sequence because user manually started movement");
    stopSequenceExecution();
  }
  
  // Auto-calibrate if not yet done
  if (config.totalDistanceMM == 0) {
    engine->warn("Not calibrated - auto-calibrating...");
    Calibration.startCalibration();
    if (config.totalDistanceMM == 0) return;
  }
  
  // Block start if in error state
  if (config.currentState == STATE_ERROR) {
    sendError("❌ Impossible de démarrer: Système en état ERREUR - Utilisez 'Retour Départ' ou recalibrez");
    return;
  }
  
  if (config.currentState != STATE_READY && config.currentState != STATE_PAUSED && config.currentState != STATE_RUNNING) {
    return;
  }
  
  // Validate and limit distance if needed
  if (motion.startPositionMM + distMM > config.totalDistanceMM) {
    if (motion.startPositionMM >= config.totalDistanceMM) {
      sendError("❌ ERROR: Position de départ dépasse le maximum");
      return;
    }
    distMM = config.totalDistanceMM - motion.startPositionMM;
  }
  
  // If already running, queue changes for next cycle
  if (config.currentState == STATE_RUNNING) {
    pendingMotion.startPositionMM = motion.startPositionMM;
    pendingMotion.distanceMM = distMM;
    pendingMotion.speedLevelForward = speedLevel;
    pendingMotion.speedLevelBackward = speedLevel;
    pendingMotion.hasChanges = true;
    return;
  }
  
  motion.targetDistanceMM = distMM;
  motion.speedLevelForward = speedLevel;
  motion.speedLevelBackward = speedLevel;
  
  engine->info("▶ Start movement: " + String(distMM, 1) + " mm @ speed " + 
        String(speedLevel, 1) + " (" + String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
  
  calculateStepDelay();
  
  // digitalWrite(PIN_ENABLE, LOW);
  
  // CRITICAL: Initialize step timing BEFORE setting STATE_RUNNING
  lastStepMicros = micros();
  
  // Calculate step positions
  startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
  targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
  
  // NOW set running state - lastStepMicros is properly initialized
  config.currentState = STATE_RUNNING;
  isPaused = false;
  currentMovement = MOVEMENT_VAET;  // Reset to Simple mode (va-et-vient)
  
  // Determine starting direction based on current position
  if (currentStep <= startStep) {
    movingForward = true;  // Need to go forward to start position
  } else if (currentStep >= targetStep) {
    movingForward = false;  // Already past target, go backward
  } else {
    movingForward = true;  // Continue forward to target
  }
  
  // Initialize PIN_DIR based on starting direction
  Motor.setDirection(movingForward);

  
  // Initialize distance tracking
  lastStepForDistance = currentStep;
  
  // Reset speed measurement
  lastStartContactMillis = 0;
  cycleTimeMillis = 0;
  measuredCyclesPerMinute = 0;
  wasAtStart = false;
  
  // Reset startStep reached flag
  // If we're already at or past startStep, mark it as reached
  hasReachedStartStep = (currentStep >= startStep);
    
  engine->debug("🚀 Starting movement: currentStep=" + String(currentStep) + 
        " startStep=" + String(startStep) + " targetStep=" + String(targetStep) + 
        " movingForward=" + String(movingForward ? "YES" : "NO"));
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
  // FIXED CYCLE TIME - speed adapts to distance
  if (motion.targetDistanceMM <= 0 || motion.speedLevelForward <= 0 || motion.speedLevelBackward <= 0) {
    stepDelayMicrosForward = 1000;
    stepDelayMicrosBackward = 1000;
    return;
  }
  
  // Convert speed levels to cycles/min
  float cyclesPerMinuteForward = speedLevelToCyclesPerMin(motion.speedLevelForward);
  float cyclesPerMinuteBackward = speedLevelToCyclesPerMin(motion.speedLevelBackward);
  
  // Safety: prevent division by zero
  if (cyclesPerMinuteForward <= 0.1) cyclesPerMinuteForward = 0.1;
  if (cyclesPerMinuteBackward <= 0.1) cyclesPerMinuteBackward = 0.1;
  
  long stepsPerDirection = (long)(motion.targetDistanceMM * STEPS_PER_MM);
  
  // Calculate forward delay with compensation for system overhead
  float halfCycleForwardMs = (60000.0 / cyclesPerMinuteForward) / 2.0;
  float rawDelayForward = (halfCycleForwardMs * 1000.0) / (float)stepsPerDirection;
  float delayForward = (rawDelayForward - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
  
  // Calculate backward delay (can be different) with compensation
  float halfCycleBackwardMs = (60000.0 / cyclesPerMinuteBackward) / 2.0;
  float rawDelayBackward = (halfCycleBackwardMs * 1000.0) / (float)stepsPerDirection;
  float delayBackward = (rawDelayBackward - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
  
  stepDelayMicrosForward = (unsigned long)delayForward;
  stepDelayMicrosBackward = (unsigned long)delayBackward;
  
  // Minimum delay for HSS86 safety (50kHz max = 20µs period)
  if (stepDelayMicrosForward < 20) {
    stepDelayMicrosForward = 20;
    engine->warn("⚠️ Forward speed limited! Distance " + String(motion.targetDistanceMM, 0) + 
          "mm too long for speed " + String(motion.speedLevelForward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
          String(cyclesPerMinuteForward, 0) + " c/min)");
  }
  if (stepDelayMicrosBackward < 20) {
    stepDelayMicrosBackward = 20;
    engine->warn("⚠️ Backward speed limited! Distance " + String(motion.targetDistanceMM, 0) + 
          "mm too long for speed " + String(motion.speedLevelBackward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
          String(cyclesPerMinuteBackward, 0) + " c/min)");
  }
  
  float avgCyclesPerMin = (cyclesPerMinuteForward + cyclesPerMinuteBackward) / 2.0;
  float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0;
  float totalCycleTime = (60000.0 / cyclesPerMinuteForward / 2.0) + (60000.0 / cyclesPerMinuteBackward / 2.0);
  
  engine->debug("⚙️  " + String(stepsPerDirection) + " steps × 2 directions | Forward: " + 
        String(motion.speedLevelForward, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(cyclesPerMinuteForward, 0) + " c/min, " + 
        String(stepDelayMicrosForward) + " µs) | Backward: " + String(motion.speedLevelBackward, 1) + 
        "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(cyclesPerMinuteBackward, 0) + " c/min, " + String(stepDelayMicrosBackward) + 
        " µs) | Avg: " + String(avgSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(avgCyclesPerMin, 0) + 
        " c/min) | Total: " + String((int)totalCycleTime) + " ms");
}

void doStep() {
  if (movingForward) {
    // MOVING FORWARD (startStep → targetStep)
    
    // ═══════════════════════════════════════════════════════════════════════
    // MULTI-LEVEL DRIFT DETECTION: END (Physical Correction)
    // ═══════════════════════════════════════════════════════════════════════
    
    // LEVEL 3: SOFT DRIFT (Position beyond config.maxStep within buffer zone)
    // Action: Physically move motor backward to correct position
    if (currentStep > config.maxStep && currentStep <= config.maxStep + SAFETY_OFFSET_STEPS) {
      int correctionSteps = currentStep - config.maxStep;  // Ex: config.maxStep+5 → 5 steps to back
      
      engine->debug(String("🔧 LEVEL 3 - Soft drift END: ") + String(currentStep) + 
            " steps (" + String(currentStep / STEPS_PER_MM, 2) + "mm) → " +
            "Backing " + String(correctionSteps) + " steps to config.maxStep (" + 
            String(config.maxStep / STEPS_PER_MM, 2) + "mm)");
      
      Motor.setDirection(false);  // Backward (includes 50µs delay)
      for (int i = 0; i < correctionSteps; i++) {
        Motor.step();
        currentStep--;  // Update position as we move
      }
      
      // Now physically synchronized at config.maxStep
      currentStep = config.maxStep;
      engine->debug(String("✓ Position physically corrected to config.maxStep (") + 
            String(config.maxStep / STEPS_PER_MM, 2) + "mm)");
      
      movingForward = false;  // Reverse direction
      return;
    }
    
    // HARD DRIFT (Physical contact reached = critical error)
    // 🆕 OPTIMISATION: Test UNIQUEMENT si proche de config.maxStep (réduit faux positifs + overhead CPU)
    // Action: Emergency stop, ERROR state
    long stepsToLimit = config.maxStep - currentStep;  // Steps remaining until limit
    float distanceToLimitMM = stepsToLimit / STEPS_PER_MM;
    
    if (distanceToLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
      // Close to limit → activate physical contact test
      if (Contacts.readDebounced(PIN_END_CONTACT, LOW, 5, 75)) {
        float currentPos = currentStep / STEPS_PER_MM;
        
        engine->error(String("🔴 Hard drift END! Physical contact at ") + 
              String(currentPos, 1) + "mm (currentStep: " + String(currentStep) + 
              " | " + String(distanceToLimitMM, 1) + "mm from limit)");
        
        sendError("❌ ERREUR CRITIQUE: Contact END atteint - Position dérivée au-delà du buffer de sécurité");
        
        stopMovement();
        config.currentState = STATE_ERROR;
        Motor.disable();  // Safety: disable motor on critical error
        return;
      }
    }
    
    // Check if reached target position
    if (currentStep + 1 > targetStep) {
      movingForward = false;
      return;
    }
    
    // Check if we've reached startStep for the first time (initial approach phase)
    if (!hasReachedStartStep && currentStep >= startStep) {
      hasReachedStartStep = true;
    }
    
    // CHAOS MODE SAFETY: Check amplitude limits (delegated to ChaosController)
    if (!Chaos.checkLimits()) return;
    
    // Set direction EVERY step (like old working code)
    Motor.setDirection(true);  // Forward
    Motor.step();
    currentStep++;
    
    // Update total distance traveled (unified logic for both directions)
    long delta = abs(currentStep - lastStepForDistance);
    if (delta > 0) {
      totalDistanceTraveled += delta;
      lastStepForDistance = currentStep;
    }
    
  } else {
    // MOVING BACKWARD (targetStep → startStep)
    
    // ═══════════════════════════════════════════════════════════════════════
    // MULTI-LEVEL DRIFT DETECTION: START (Physical Correction)
    // ═══════════════════════════════════════════════════════════════════════
    
    // SOFT DRIFT (Negative position within buffer zone)
    // Action: Physically move motor forward to correct position
    if (currentStep < 0 && currentStep >= -SAFETY_OFFSET_STEPS) {
      int correctionSteps = abs(currentStep);  // Ex: -3 → 3 steps to advance
      
      engine->debug(String("🔧 LEVEL 1 - Soft drift START: ") + String(currentStep) + 
            " steps (" + String(currentStep / STEPS_PER_MM, 2) + "mm) → " +
            "Advancing " + String(correctionSteps) + " steps to position 0");
      
      Motor.setDirection(true);  // Forward (includes 50µs delay)
      for (int i = 0; i < correctionSteps; i++) {
        Motor.step();
        currentStep++;  // Update position as we move
      }
      
      // Now physically synchronized at position 0
      currentStep = 0;
      config.minStep = 0;
      engine->debug("✓ Position physically corrected to 0");
      return;
    }
    
    // HARD DRIFT (Physical contact reached = critical error)
    // 🆕 OPTIMISATION: Test UNIQUEMENT si proche de position 0 (réduit faux positifs + overhead CPU)
    // Action: Emergency stop, ERROR state
    float distanceToStartMM = currentStep / STEPS_PER_MM;
    
    if (distanceToStartMM <= HARD_DRIFT_TEST_ZONE_MM) {
      // Close to start → activate physical contact test
      if (Contacts.readDebounced(PIN_START_CONTACT, LOW, 5, 75)) {
        float currentPos = currentStep / STEPS_PER_MM;

        engine->error(String("🔴 Hard drift START! Physical contact at ") +
              String(currentPos, 1) + "mm (currentStep: " + String(currentStep) + 
              " | " + String(distanceToStartMM, 1) + "mm from start)");
        
        sendError("❌ ERREUR CRITIQUE: Contact START atteint - Position dérivée au-delà du buffer de sécurité");
        
        stopMovement();
        config.currentState = STATE_ERROR;
        Motor.disable();  // Safety: disable motor on critical error
        return;
      }
    }
    
    // First, execute the step
    if (currentStep > config.minStep + WASATSTART_THRESHOLD_STEPS) {
      wasAtStart = false;
    }
    
    // CHAOS MODE SAFETY: Check amplitude limits (delegated to ChaosController)
    if (!Chaos.checkLimits()) return;
    
    // Set direction EVERY step (like old working code)
    Motor.setDirection(false);  // Backward
    Motor.step();
    currentStep--;
    
    // Update total distance traveled (unified logic for both directions)
    long delta = abs(currentStep - lastStepForDistance);
    if (delta > 0) {
      totalDistanceTraveled += delta;
      lastStepForDistance = currentStep;
    }
    
    // Then check if we reached startStep (end of backward movement)
    // ONLY reverse if we've already been to startStep once (va-et-vient mode active)
    // Skip this logic in CHAOS mode (uses targetStep only, no startStep)
    if (currentMovement != MOVEMENT_CHAOS && currentStep <= startStep && hasReachedStartStep) {
      
      // ✅ CRITICAL: Apply pending changes at end of cycle BEFORE reversing direction
      // This applies for ALL start positions (0mm, 25mm, etc.)
      if (pendingMotion.hasChanges) {
        engine->debug(String("🔄 New config: ") + String(pendingMotion.distanceMM, 1) + 
              "mm @ F" + String(pendingMotion.speedLevelForward, 1) + 
              "/B" + String(pendingMotion.speedLevelBackward, 1));
        
        motion.startPositionMM = pendingMotion.startPositionMM;
        motion.targetDistanceMM = pendingMotion.distanceMM;
        motion.speedLevelForward = pendingMotion.speedLevelForward;
        motion.speedLevelBackward = pendingMotion.speedLevelBackward;
        pendingMotion.hasChanges = false;
        
        calculateStepDelay();
        startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
        targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
      }
      
      // 🆕 NOUVEAU: Vérifier si pause entre cycles activée
      if (motion.cyclePause.enabled) {
        // Calculer durée de pause
        if (motion.cyclePause.isRandom) {
          // Mode aléatoire: tirer une valeur entre min et max
          // 🆕 SÉCURITÉ: Garantir min ≤ max (défense en profondeur)
          float minVal = min(motion.cyclePause.minPauseSec, motion.cyclePause.maxPauseSec);
          float maxVal = max(motion.cyclePause.minPauseSec, motion.cyclePause.maxPauseSec);
          float range = maxVal - minVal;
          float randomOffset = (float)random(0, 10000) / 10000.0;  // 0.0 à 1.0
          float pauseSec = minVal + (randomOffset * range);
          motionPauseState.currentPauseDuration = (unsigned long)(pauseSec * 1000);
        } else {
          // Mode fixe
          motionPauseState.currentPauseDuration = (unsigned long)(motion.cyclePause.pauseDurationSec * 1000);
        }
        
        // Démarrer la pause
        motionPauseState.isPausing = true;
        motionPauseState.pauseStartMs = millis();
        
        engine->debug("⏸️ Pause cycle VAET: " + String(motionPauseState.currentPauseDuration) + "ms");
        
        // NE PAS reverser la direction maintenant, attendre la fin de la pause
        return;
      }
      
      movingForward = true;
      // Direction will be set on next doStep() call when entering FORWARD block
      
      // NEW ARCHITECTURE: Unified completion handler
      if (config.executionContext == CONTEXT_SEQUENCER) {
        // SEQUENCER: Call onMovementComplete() to increment cycle counter
        onMovementComplete();
      } else {
        // STANDALONE: VA-ET-VIENT loops infinitely, keep STATE_RUNNING
        // config.currentState stays STATE_RUNNING to continue looping
      }
      
      // Measure cycle time (for all start positions)
      if (!wasAtStart) {
        unsigned long currentMillis = millis();
        if (lastStartContactMillis > 0) {
          cycleTimeMillis = currentMillis - lastStartContactMillis;
          measuredCyclesPerMinute = 60000.0 / cycleTimeMillis;
          
          float avgTargetCPM = (speedLevelToCyclesPerMin(motion.speedLevelForward) + speedLevelToCyclesPerMin(motion.speedLevelBackward)) / 2.0;
          float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0;
          float diffPercent = ((measuredCyclesPerMinute - avgTargetCPM) / avgTargetCPM) * 100.0;
          
          // Only log if difference is significant (> 15% after compensation)
          if (abs(diffPercent) > 15.0) {
            engine->debug(String("⏱️  Cycle timing: ") + String(cycleTimeMillis) + 
                  " ms | Target: " + String(avgSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
                  String(avgTargetCPM, 0) + " c/min) | Actual: " + 
                  String(measuredCyclesPerMinute, 1) + " c/min | ⚠️ Diff: " + 
                  String(diffPercent, 1) + " %");
          }
        }
        lastStartContactMillis = currentMillis;
        wasAtStart = true;
      }
      
      // CRITICAL: Set PIN_DIR for next forward movement
      Motor.setDirection(true);  // Forward
      
      // CRITICAL: Return here to allow next doStep() call to start forward movement
      engine->debug(String("🔄 End of backward cycle - State: ") + String(config.currentState) + 
            " | Movement: " + movementTypeName(currentMovement) + 
            " | movingForward: " + String(movingForward) + 
            " | seqState.isRunning: " + String(seqState.isRunning));
      return;
    }
  }
}

// ============================================================================
// MOTOR CONTROL - doStep() Helper Functions
// ============================================================================

void togglePause() {
  if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
    // 💾 Save stats BEFORE toggling pause (save accumulated distance)
    if (!isPaused) {
      // Going from RUNNING → PAUSED: save current session
      saveCurrentSessionStats();
      engine->debug("💾 Stats saved before pause");
    }
    
    isPaused = !isPaused;
    config.currentState = isPaused ? STATE_PAUSED : STATE_RUNNING;
    
    // 🆕 CORRECTION: Reset timer en mode oscillation pour éviter le saut de phase lors de la reprise
    if (!isPaused && currentMovement == MOVEMENT_OSC) {
      oscillationState.lastPhaseUpdateMs = millis();
      engine->debug("🔄 Phase gelée après pause (évite à-coup)");
    }
    
    engine->info(isPaused ? "Paused" : "Resumed");
  }
}

void stopMovement() {
  if (currentMovement == MOVEMENT_PURSUIT) {
    pursuit.isMoving = false;
    // Keep motor enabled - HSS86 needs to stay synchronized
    // digitalWrite(PIN_ENABLE, HIGH);
    
    // Save session stats before stopping
    saveCurrentSessionStats();
    return;
  }
  
  // Stop oscillation if running (important for sequence stop)
  if (currentMovement == MOVEMENT_OSC) {
    currentMovement = MOVEMENT_VAET;  // Reset to default mode
    engine->debug("🌊 Oscillation stopped by stopMovement()");
  }
  
  // Stop chaos if running (important for sequence stop)
  if (chaosState.isRunning) {
    chaosState.isRunning = false;
    engine->debug("⚡ Chaos stopped by stopMovement()");
  }
  
  // Reset pause states
  motionPauseState.isPausing = false;
  oscPauseState.isPausing = false;
  
  // Stop simple mode
  if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
    // CRITICAL: Keep motor enabled to maintain HSS86 synchronization
    // Disabling and re-enabling causes step loss with startPosition > 0
    // digitalWrite(PIN_ENABLE, HIGH);
    
    config.currentState = STATE_READY;
    isPaused = false;
    
    pendingMotion.hasChanges = false;
    
    // Save session stats before stopping
    saveCurrentSessionStats();
  }
}

// ============================================================================
// MOTOR CONTROL - Parameter Updates
// ============================================================================

void setDistance(float distMM) {
  // Limit distance to valid range
  if (motion.startPositionMM + distMM > config.totalDistanceMM) {
    distMM = config.totalDistanceMM - motion.startPositionMM;
  }
  
  if (config.currentState == STATE_RUNNING && !isPaused) {
    // Queue change for end of cycle
    if (!pendingMotion.hasChanges) {
      pendingMotion.startPositionMM = motion.startPositionMM;
      pendingMotion.distanceMM = distMM;
      pendingMotion.speedLevelForward = motion.speedLevelForward;
      pendingMotion.speedLevelBackward = motion.speedLevelBackward;
    } else {
      pendingMotion.distanceMM = distMM;
    }
    pendingMotion.hasChanges = true;
  } else {
    // Apply immediately
    motion.targetDistanceMM = distMM;
    startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
    targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
    calculateStepDelay();
  }
}

void setStartPosition(float startMM) {
  if (startMM < 0) startMM = 0;
  if (startMM > config.totalDistanceMM) {
    startMM = config.totalDistanceMM;
    engine->warn(String("⚠️ Start position limited to ") + String(startMM, 1) + " mm (maximum)");
  }
  
  // Validate start position + distance don't exceed maximum
  bool distanceWasAdjusted = false;
  if (startMM + motion.targetDistanceMM > config.totalDistanceMM) {
    motion.targetDistanceMM = config.totalDistanceMM - startMM;
    distanceWasAdjusted = true;
    engine->warn(String("⚠️ Distance auto-adjusted to ") + String(motion.targetDistanceMM, 1) + " mm to fit within maximum");
  }
  
  bool wasRunning = (config.currentState == STATE_RUNNING && !isPaused);
  
  if (wasRunning) {
    // Queue change for end of cycle
    if (!pendingMotion.hasChanges) {
      // First pending change - initialize all values
      pendingMotion.startPositionMM = startMM;
      pendingMotion.distanceMM = motion.targetDistanceMM;
      pendingMotion.speedLevelForward = motion.speedLevelForward;
      pendingMotion.speedLevelBackward = motion.speedLevelBackward;
    } else {
      // Already have pending changes - only update start position and distance
      // Keep pending speeds unchanged (they may have been set by speed commands)
      pendingMotion.startPositionMM = startMM;
      pendingMotion.distanceMM = motion.targetDistanceMM;
    }
    pendingMotion.hasChanges = true;
    
    engine->debug(String("⏳ Start position queued: ") + String(startMM) + " mm (will apply at end of cycle)");
  } else {
    // Apply immediately
    motion.startPositionMM = startMM;
    startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
    targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
    calculateStepDelay();
    
    engine->debug(String("✓ Start position updated: ") + String(motion.startPositionMM) + " mm");
    
    // If distance was auto-adjusted, send immediate status update to sync UI
    if (distanceWasAdjusted) {
      sendStatus();
    }
  }
}

void setSpeedForward(float speedLevel) {
  float oldSpeedLevel = motion.speedLevelForward;
  bool wasRunning = (config.currentState == STATE_RUNNING && !isPaused);
  
  if (wasRunning) {
    // Only update forward speed in pending changes
    // Keep other pending values unchanged (they may have been set by other commands)
    if (!pendingMotion.hasChanges) {
      // First pending change - initialize all values
      pendingMotion.startPositionMM = motion.startPositionMM;
      pendingMotion.distanceMM = motion.targetDistanceMM;
      pendingMotion.speedLevelForward = speedLevel;
      pendingMotion.speedLevelBackward = motion.speedLevelBackward;
    } else {
      // Already have pending changes - only update forward speed
      pendingMotion.speedLevelForward = speedLevel;
    }
    pendingMotion.hasChanges = true;
    engine->debug(String("⏳ Forward speed queued: ") + String(oldSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " → " + 
          String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
  } else {
    motion.speedLevelForward = speedLevel;
    engine->debug(String("✓ Forward speed: ") + String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
          String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
    calculateStepDelay();
  }
}

void setSpeedBackward(float speedLevel) {
  float oldSpeedLevel = motion.speedLevelBackward;
  bool wasRunning = (config.currentState == STATE_RUNNING && !isPaused);
  
  if (wasRunning) {
    // Only update backward speed in pending changes
    // Keep other pending values unchanged (they may have been set by other commands)
    if (!pendingMotion.hasChanges) {
      // First pending change - initialize all values
      pendingMotion.startPositionMM = motion.startPositionMM;
      pendingMotion.distanceMM = motion.targetDistanceMM;
      pendingMotion.speedLevelForward = motion.speedLevelForward;
      pendingMotion.speedLevelBackward = speedLevel;
    } else {
      // Already have pending changes - only update backward speed
      pendingMotion.speedLevelBackward = speedLevel;
    }
    pendingMotion.hasChanges = true;
    engine->debug(String("⏳ Backward speed queued: ") + String(oldSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " → " + 
          String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
  } else {
    motion.speedLevelBackward = speedLevel;
    engine->debug(String("✓ Backward speed: ") + String(speedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) + " (" + 
          String(speedLevelToCyclesPerMin(speedLevel), 0) + " c/min)");
    calculateStepDelay();
  }
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

void pursuitMove(float targetPositionMM, float maxSpeedLevel) {
  // Safety check: calibration required
  if (config.totalDistanceMM == 0) {
    sendError("❌ Mode Pursuit nécessite une calibration d'abord!");
    return;
  }
  
  // Update pursuit parameters
  pursuit.maxSpeedLevel = maxSpeedLevel;
  
  // Clamp target to valid range (respect effective max distance limit)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  if (targetPositionMM < 0) targetPositionMM = 0;
  if (targetPositionMM > maxAllowed) targetPositionMM = maxAllowed;
  
  // Convert to steps and CLAMP to calibrated limits (config.minStep/config.maxStep)
  pursuit.targetStep = (long)(targetPositionMM * STEPS_PER_MM);
  if (pursuit.targetStep < config.minStep) pursuit.targetStep = config.minStep;
  if (pursuit.targetStep > config.maxStep) pursuit.targetStep = config.maxStep;
  
  long errorSteps = pursuit.targetStep - currentStep;
  float errorMM = abs(errorSteps) / STEPS_PER_MM;
  
  // If already at target, don't do anything
  if (errorSteps == 0) {
    pursuit.isMoving = false;
    return;
  }
  
  // Only recalculate speed if target or speed setting changed significantly
  bool targetChanged = (abs(pursuit.targetStep - pursuit.lastTargetStep) > 6);  // >1mm change
  bool speedSettingChanged = (abs(pursuit.maxSpeedLevel - pursuit.lastMaxSpeedLevel) > 0.5);
  bool hasWork = (errorSteps != 0);  // There's actual movement needed
  
  if (targetChanged || speedSettingChanged || !pursuit.isMoving || hasWork) {
    // Calculate speed based on error (aggressive proportional control)
    // Ultra-aggressive speed profile for NEMA34 8NM - keep speed high until very close
    float speedLevel;
    
    if (errorMM > 5.0) {
      // Far from target (>5mm): use max speed
      speedLevel = pursuit.maxSpeedLevel;
    } else if (errorMM > 1.0) {
      // Close to target (1-5mm): smooth ramp from max to 60%
      float ratio = (errorMM - 1.0) / (5.0 - 1.0);
      speedLevel = pursuit.maxSpeedLevel * (0.6 + (ratio * 0.4));
    } else {
      // Very close (<1mm): minimum speed for precision
      speedLevel = pursuit.maxSpeedLevel * 0.6;
    }
    
    // Direct conversion: speedLevel → mm/sec → steps/sec WITH compensation
    float mmPerSecond = speedLevel * 10.0;  // speedLevel*10 → mm/s (ex: MAX_SPEED_LEVEL→(MAX_SPEED_LEVEL*10)mm/s, 10→100mm/s)
    float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
    
    // Limit to safe range (NEMA34 can handle more)
    if (stepsPerSecond < 30) stepsPerSecond = 30;  // Higher minimum for responsiveness
    if (stepsPerSecond > 6000) stepsPerSecond = 6000;  // HSS86 safe limit (close to 10kHz)
    
    float delayMicros = ((1000000.0 / stepsPerSecond) - STEP_EXECUTION_TIME_MICROS) / SPEED_COMPENSATION_FACTOR;
    if (delayMicros < 20) delayMicros = 20;  // Absolute minimum (50kHz)
    
    pursuit.stepDelay = (unsigned long)delayMicros;
    
    // Remember last values
    pursuit.lastTargetStep = pursuit.targetStep;
    pursuit.lastMaxSpeedLevel = pursuit.maxSpeedLevel;
  }
  
  // Set direction and track it
  bool moveForward = (errorSteps > 0);
  
  // If direction changed, add a small delay for driver to respond
  if (pursuit.isMoving && (moveForward != pursuit.direction)) {
    delayMicroseconds(50);  // Brief pause for HSS86 direction change
  }
  
  pursuit.direction = moveForward;
  Motor.setDirection(moveForward);
  
  // Ensure motor is enabled (should already be, but ensure on first call)
  Motor.enable();
  pursuit.isMoving = true;
}

void doPursuitStep() {
  long errorSteps = pursuit.targetStep - currentStep;
  
  // If at target, stop moving but keep motor enabled
  if (errorSteps == 0) {
    pursuit.isMoving = false;
    return;
  }
  
  // Safety: respect calibrated limits (don't go beyond config.minStep/config.maxStep)
  bool moveForward = (errorSteps > 0);
  
  if (moveForward && currentStep >= config.maxStep) {
    // Already at max limit - stop here
    pursuit.targetStep = currentStep;
    pursuit.isMoving = false;
    engine->warn("⚠️ Pursuit: reached config.maxStep limit");
    return;
  }
  
  if (!moveForward && currentStep <= config.minStep) {
    // Already at min limit - stop here
    pursuit.targetStep = currentStep;
    pursuit.isMoving = false;
    engine->warn("⚠️ Pursuit: reached config.minStep limit");
    return;
  }
  
  // 🆕 OPTIMISATION: HARD DRIFT detection - Test UNIQUEMENT si proche des limites
  // Test END contact si poursuite vers limite haute
  if (moveForward) {
    long stepsToLimit = config.maxStep - currentStep;
    float distanceToLimitMM = stepsToLimit / STEPS_PER_MM;
    
    if (distanceToLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
      if (Contacts.readDebounced(PIN_END_CONTACT, LOW, 3, 50)) {
        pursuit.isMoving = false;
        pursuit.targetStep = currentStep;
        sendError("❌ PURSUIT: Contact END atteint - arrêt sécurité");
        config.currentState = STATE_ERROR;
        return;
      }
    }
  } else {
    // Test START contact si poursuite vers limite basse
    float distanceToStartMM = currentStep / STEPS_PER_MM;
    
    if (distanceToStartMM <= HARD_DRIFT_TEST_ZONE_MM) {
      if (Contacts.readDebounced(PIN_START_CONTACT, LOW, 3, 50)) {
        pursuit.isMoving = false;
        pursuit.targetStep = currentStep;
        sendError("❌ PURSUIT: Contact START atteint - arrêt sécurité");
        config.currentState = STATE_ERROR;
        return;
      }
    }
  }
  
  // Execute one step
  Motor.step();
  
  if (moveForward) {
    currentStep++;
    if (currentStep > lastStepForDistance) {
      totalDistanceTraveled += (currentStep - lastStepForDistance);
      lastStepForDistance = currentStep;
    }
  } else {
    currentStep--;
    if (lastStepForDistance > currentStep) {
      totalDistanceTraveled += (lastStepForDistance - currentStep);
      lastStepForDistance = currentStep;
    }
  }
}

// ============================================================================
// OSCILLATION MODE FUNCTIONS
// ============================================================================

// Calculate oscillation position based on current time and waveform
float calculateOscillationPosition() {
  unsigned long currentMs = millis();
  
  // 🎯 SMOOTH FREQUENCY TRANSITION: Use accumulated phase for perfect continuity
  float effectiveFrequency = oscillation.frequencyHz;
  
  // 🚀 SPEED LIMIT: Cap frequency if theoretical speed exceeds OSC_MAX_SPEED_MM_S (300 mm/s)
  if (oscillation.amplitudeMM > 0.0) {
    float maxAllowedFreq = OSC_MAX_SPEED_MM_S / (2.0 * PI * oscillation.amplitudeMM);
    if (oscillation.frequencyHz > maxAllowedFreq) {
      effectiveFrequency = maxAllowedFreq;
      
      // Log warning (throttled to avoid spam)
      static unsigned long lastSpeedLimitLog = 0;
      if (currentMs - lastSpeedLimitLog > 5000) {
        engine->warn("⚠️ Fréquence réduite: " + String(oscillation.frequencyHz, 2) + " Hz → " + 
              String(effectiveFrequency, 2) + " Hz (vitesse max: " + 
              String(OSC_MAX_SPEED_MM_S, 0) + " mm/s)");
        lastSpeedLimitLog = currentMs;
      }
    }
  }
  
  // Initialize phase tracking on first call or after reset
  if (oscillationState.lastPhaseUpdateMs == 0) {
    oscillationState.lastPhaseUpdateMs = currentMs;
    oscillationState.accumulatedPhase = 0.0;
  }
  
  // Calculate time delta since last update
  unsigned long deltaMs = currentMs - oscillationState.lastPhaseUpdateMs;
  oscillationState.lastPhaseUpdateMs = currentMs;
  
  if (oscillationState.isTransitioning) {
    unsigned long transitionElapsed = currentMs - oscillationState.transitionStartMs;
    
    if (transitionElapsed < OSC_FREQ_TRANSITION_DURATION_MS) {
      // Linear interpolation of frequency
      float progress = (float)transitionElapsed / (float)OSC_FREQ_TRANSITION_DURATION_MS;
      effectiveFrequency = oscillationState.oldFrequencyHz + 
                          (oscillationState.targetFrequencyHz - oscillationState.oldFrequencyHz) * progress;
      
      // Reduced logging: every 200ms (was 100ms)
      static unsigned long lastTransitionLog = 0;
      if (currentMs - lastTransitionLog > OSC_TRANSITION_LOG_INTERVAL_MS) {
        engine->debug("🔄 Transition: " + String(effectiveFrequency, 3) + " Hz (" + String(progress * 100, 0) + "%)");
        lastTransitionLog = currentMs;
      }
    } else {
      // Transition complete
      oscillationState.isTransitioning = false;
      effectiveFrequency = oscillation.frequencyHz;
      engine->info("✅ Transition terminée: " + String(effectiveFrequency, 3) + " Hz");
    }
  }
  
  // 🔥 ACCUMULATE PHASE: Add phase increment based on time delta and current frequency
  // phase increment = frequency (cycles/sec) × time (sec)
  float phaseIncrement = effectiveFrequency * (deltaMs / 1000.0);
  oscillationState.accumulatedPhase += phaseIncrement;
  
  // Calculate phase (0.0 to 1.0 per cycle) using modulo
  float phase = fmod(oscillationState.accumulatedPhase, 1.0);
  
  // Calculate waveform value (-1.0 to +1.0)
  float waveValue = 0.0;
  
  switch (oscillation.waveform) {
    case OSC_SINE:
      // Use -cos to start at maximum (like a wave crest)
      // cos(0) = 1, cos(PI) = -1, cos(2*PI) = 1
      #ifdef USE_SINE_LOOKUP_TABLE
      waveValue = fastSine(phase);  // Lookup table (2µs)
      #else
      waveValue = -cos(phase * 2.0 * PI);  // Hardware FPU (15µs)
      #endif
      break;
      
    case OSC_TRIANGLE:
      // Symmetric triangle: starts at +1, goes to -1, back to +1
      if (phase < 0.5) {
        waveValue = 1.0 - (phase * 4.0);  // Fall: +1 to -1
      } else {
        waveValue = -3.0 + (phase * 4.0);  // Rise: -1 to +1
      }
      break;
      
    case OSC_SQUARE:
      // Square wave: starts at +1, switches to -1 at halfway
      waveValue = (phase < 0.5) ? 1.0 : -1.0;
      break;
  }
  
  // Track completed cycles
  // ⚠️ Don't increment during ramp out - we've already reached target cycle count
  if (!oscillationState.isRampingOut && phase < oscillationState.lastPhase) {  // Cycle wrap-around detected
    oscillationState.completedCycles++;
    engine->debug("🔄 Cycle " + String(oscillationState.completedCycles) + "/" + String(oscillation.cycleCount));
    
    // 🆕 NOUVEAU: Vérifier si pause entre cycles activée
    if (oscillation.cyclePause.enabled) {
      // Calculer durée de pause
      if (oscillation.cyclePause.isRandom) {
        // 🆕 SÉCURITÉ: Garantir min ≤ max (défense en profondeur)
        float minVal = min(oscillation.cyclePause.minPauseSec, oscillation.cyclePause.maxPauseSec);
        float maxVal = max(oscillation.cyclePause.minPauseSec, oscillation.cyclePause.maxPauseSec);
        float range = maxVal - minVal;
        float randomOffset = (float)random(0, 10000) / 10000.0;
        float pauseSec = minVal + (randomOffset * range);
        oscPauseState.currentPauseDuration = (unsigned long)(pauseSec * 1000);
      } else {
        oscPauseState.currentPauseDuration = (unsigned long)(oscillation.cyclePause.pauseDurationSec * 1000);
      }
      
      oscPauseState.isPausing = true;
      oscPauseState.pauseStartMs = millis();
      
      engine->debug("⏸️ Pause cycle OSC: " + String(oscPauseState.currentPauseDuration) + "ms");
    }
    
    // ⚠️ Send status update to frontend when cycle completes (Bug #1 fix)
    if (config.executionContext == CONTEXT_SEQUENCER) {
      sendSequenceStatus();
    }
  }
  oscillationState.lastPhase = phase;
  
  // Calculate current amplitude with ramping
  // ✅ PRO MODE: Rampes uniquement au début/fin de TOUTE la ligne (pas entre cycles)
  float effectiveAmplitude = oscillation.amplitudeMM;
  
  // 🎯 SMOOTH AMPLITUDE TRANSITION: Interpolate amplitude when changed during oscillation
  if (oscillationState.isAmplitudeTransitioning) {
    unsigned long ampElapsed = currentMs - oscillationState.amplitudeTransitionStartMs;
    
    if (ampElapsed < OSC_AMPLITUDE_TRANSITION_DURATION_MS) {
      // Linear interpolation of amplitude
      float progress = (float)ampElapsed / (float)OSC_AMPLITUDE_TRANSITION_DURATION_MS;
      effectiveAmplitude = oscillationState.oldAmplitudeMM + 
                          (oscillationState.targetAmplitudeMM - oscillationState.oldAmplitudeMM) * progress;
      
      // Log transition progress (every 200ms)
      static unsigned long lastAmpTransitionLog = 0;
      if (currentMs - lastAmpTransitionLog > OSC_TRANSITION_LOG_INTERVAL_MS) {
        engine->debug("🔄 Amplitude transition: " + String(effectiveAmplitude, 1) + " mm (" + String(progress * 100, 0) + "%)");
        lastAmpTransitionLog = currentMs;
      }
    } else {
      // Transition complete
      oscillationState.isAmplitudeTransitioning = false;
      effectiveAmplitude = oscillation.amplitudeMM;
      engine->info("✅ Amplitude transition terminée: " + String(effectiveAmplitude, 1) + " mm");
    }
  }
  
  // Consolidated debug logging: every 5s (reduced from multiple 2s logs)
  static unsigned long lastDebugMs = 0;
  if (currentMs - lastDebugMs > OSC_DEBUG_LOG_INTERVAL_MS) {
    engine->debug("� OSC: amp=" + String(effectiveAmplitude, 1) + "/" + String(oscillation.amplitudeMM, 1) + 
          "mm, center=" + String(oscillation.centerPositionMM, 1) + 
          "mm, rampIn=" + String(oscillationState.isRampingIn) + 
          ", rampOut=" + String(oscillationState.isRampingOut));
    lastDebugMs = currentMs;
  }
  
  if (oscillationState.isRampingIn) {
    unsigned long rampElapsed = currentMs - oscillationState.rampStartMs;
    
    if (rampElapsed < OSC_RAMP_START_DELAY_MS) {
      // Phase de stabilisation : amplitude = 0
      effectiveAmplitude = 0;
    } else if (rampElapsed < (oscillation.rampInDurationMs + OSC_RAMP_START_DELAY_MS)) {
      // Phase de rampe : calculer la progression depuis la fin du délai
      unsigned long adjustedElapsed = rampElapsed - OSC_RAMP_START_DELAY_MS;
      float rampProgress = (float)adjustedElapsed / (float)oscillation.rampInDurationMs;
      effectiveAmplitude = oscillation.amplitudeMM * rampProgress;
    } else {
      // Ramp in complete - switch to full amplitude
      oscillationState.isRampingIn = false;
      effectiveAmplitude = oscillation.amplitudeMM;
    }
  } else if (oscillationState.isRampingOut) {
    unsigned long rampElapsed = currentMs - oscillationState.rampStartMs;
    if (rampElapsed < oscillation.rampOutDurationMs) {
      float rampProgress = 1.0 - ((float)rampElapsed / (float)oscillation.rampOutDurationMs);
      effectiveAmplitude = oscillation.amplitudeMM * rampProgress;
    } else {
      // Ramp out complete, stop oscillation
      effectiveAmplitude = 0;
      oscillationState.isRampingOut = false;
      isPaused = true;  // Stop movement
      
      // ✅ SEQUENCE MODE: Set state to READY and notify sequencer
      if (seqState.isRunning) {
        config.currentState = STATE_READY;
        currentMovement = MOVEMENT_VAET;  // Reset to VA-ET-VIENT for sequencer
        onMovementComplete();  // CRITICAL: Notify sequencer that oscillation is complete
      }
    }
  }
  
  oscillationState.currentAmplitude = effectiveAmplitude;
  
  // 🎯 SMOOTH CENTER TRANSITION: Interpolate center position when changed
  float effectiveCenterMM = oscillation.centerPositionMM;
  
  if (oscillationState.isCenterTransitioning) {
    unsigned long centerElapsed = currentMs - oscillationState.centerTransitionStartMs;
    
    if (centerElapsed < OSC_CENTER_TRANSITION_DURATION_MS) {
      // Linear interpolation of center position
      float progress = (float)centerElapsed / (float)OSC_CENTER_TRANSITION_DURATION_MS;
      effectiveCenterMM = oscillationState.oldCenterMM + 
                          (oscillationState.targetCenterMM - oscillationState.oldCenterMM) * progress;
      
      // Log transition progress (every 200ms)
      static unsigned long lastCenterTransitionLog = 0;
      if (currentMs - lastCenterTransitionLog > OSC_TRANSITION_LOG_INTERVAL_MS) {
        engine->debug("🎯 Centre transition: " + String(effectiveCenterMM, 1) + " mm (" + String(progress * 100, 0) + "%)");
        lastCenterTransitionLog = currentMs;
      }
    } else {
      // Transition complete
      oscillationState.isCenterTransitioning = false;
      effectiveCenterMM = oscillation.centerPositionMM;
      engine->info("✅ Centre transition terminée: " + String(effectiveCenterMM, 1) + " mm");
    }
  }
  
  // Calculate final position
  float targetPositionMM = effectiveCenterMM + (waveValue * effectiveAmplitude);
  
  // Clamp to physical limits with warning
  float minPositionMM = config.minStep / STEPS_PER_MM;
  float maxPositionMM = config.maxStep / STEPS_PER_MM;
  
  if (targetPositionMM < minPositionMM) {
    engine->warn("⚠️ OSC: Limité par START (" + String(targetPositionMM, 1) + "→" + String(minPositionMM, 1) + "mm)");
    targetPositionMM = minPositionMM;
  }
  
  if (targetPositionMM > maxPositionMM) {
    engine->warn("⚠️ OSC: Limité par END (" + String(targetPositionMM, 1) + "→" + String(maxPositionMM, 1) + "mm)");
    targetPositionMM = maxPositionMM;
  }
  
  return targetPositionMM;
}

// Check if oscillation amplitude is valid given center position and effective limits
bool validateOscillationAmplitude(float centerMM, float amplitudeMM, String& errorMsg) {
  // Use effective max distance (respects limitation percentage)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  float minRequired = centerMM - amplitudeMM;
  float maxRequired = centerMM + amplitudeMM;
  
  if (minRequired < 0) {
    errorMsg = "Amplitude trop grande: position minimum < 0 mm (" + String(minRequired, 1) + "mm)";
    return false;
  }
  
  if (maxRequired > maxAllowed) {
    errorMsg = "Amplitude trop grande: position maximum > " + String(maxAllowed, 1) + " mm (" + String(maxRequired, 1) + "mm)";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " [Limitation " + String(maxDistanceLimitPercent, 0) + "%]";
    }
    return false;
  }
  
  return true;
}

// Execute one step of oscillation movement
void doOscillationStep() {
  // 🆕 NOUVEAU: Vérifier si en pause entre cycles
  if (oscPauseState.isPausing) {
    unsigned long elapsedMs = millis() - oscPauseState.pauseStartMs;
    if (elapsedMs >= oscPauseState.currentPauseDuration) {
      // Pause terminée - AJUSTER le timer pour éviter un "saut" de phase
      unsigned long pauseDuration = elapsedMs;
      oscillationState.lastPhaseUpdateMs = millis();  // Reset le timer pour éviter deltaMs énorme
      
      oscPauseState.isPausing = false;
      engine->debug("▶️ Fin pause cycle OSC (" + String(pauseDuration) + "ms) - Phase gelée");
    } else {
      return;  // Pause en cours, ne rien faire
    }
  }
  
  // Constantes de timing pour les différentes phases
  const unsigned long POSITIONING_STEP_DELAY_MICROS = 1000;  // Positionnement initial lent (25mm/s)
  
  // POSITIONNEMENT INITIAL: Aller d'abord au centre avant de commencer l'oscillation
  if (oscillationState.isInitialPositioning) {
    // Cible = centre (pas de sinusoïde pendant le positionnement initial)
    float targetPositionMM = oscillation.centerPositionMM;
    long targetStep = (long)(targetPositionMM * STEPS_PER_MM);
    long errorSteps = targetStep - currentStep;
    
    // DEBUG: Log position actuelle au premier appel
    static bool firstCall = true;
    if (firstCall) {
      float currentMM = currentStep / STEPS_PER_MM;
      engine->debug("🚀 Début positionnement: Position=" + String(currentMM, 1) + 
            "mm → Cible=" + String(targetPositionMM, 1) + 
            "mm (erreur=" + String(errorSteps) + " steps = " + 
            String(errorSteps / STEPS_PER_MM, 1) + "mm)");
      firstCall = false;
    }
    
    if (errorSteps == 0) {
      return;  // Already at target
    }
    
    unsigned long currentMicros = micros();
    unsigned long elapsedMicros = currentMicros - lastStepMicros;
    
    if (elapsedMicros < OSC_POSITIONING_STEP_DELAY_MICROS) {
      return;  // Trop tôt pour le prochain step
    }
    
    // Mouvement ultra-doux: 1 step à la fois
    bool moveForward = (errorSteps > 0);
    Motor.setDirection(moveForward);
    Motor.step();
    
    if (moveForward) {
      currentStep++;
      if (currentStep > lastStepForDistance) {
        totalDistanceTraveled += (currentStep - lastStepForDistance);
        lastStepForDistance = currentStep;
      }
    } else {
      currentStep--;
      if (lastStepForDistance > currentStep) {
        totalDistanceTraveled += (lastStepForDistance - currentStep);
        lastStepForDistance = currentStep;
      }
    }
    
    lastStepMicros = currentMicros;
    
    // Désactiver le positionnement initial quand on est au centre
    long absErrorSteps = abs(errorSteps);
    if (absErrorSteps < (long)(OSC_INITIAL_POSITIONING_TOLERANCE_MM * STEPS_PER_MM)) {
      oscillationState.isInitialPositioning = false;
      oscillationState.startTimeMs = millis();  // Reset timer
      engine->debug("✅ Positionnement terminé");
    }
    
    return;  // Ne pas calculer la position oscillante pendant le positionnement
  }
  
  // Calculate target position FIRST (this updates completedCycles counter)
  float targetPositionMM = calculateOscillationPosition();
  
  // THEN check if cycle count reached (after counter update)
  if (oscillation.cycleCount > 0 && oscillationState.completedCycles >= oscillation.cycleCount) {
    // Log only once when cycles complete
    static bool cyclesCompleteLogged = false;
    if (!cyclesCompleteLogged) {
      engine->debug("✅ OSC: Cycles complete! " + String(oscillationState.completedCycles) + "/" + String(oscillation.cycleCount));
      cyclesCompleteLogged = true;
    }
    
    if (oscillation.enableRampOut && !oscillationState.isRampingOut) {
      // Start ramp out
      oscillationState.isRampingOut = true;
      oscillationState.rampStartMs = millis();
      cyclesCompleteLogged = false;  // Reset for next oscillation
    } else if (!oscillation.enableRampOut) {
      // Stop immediately
      isPaused = true;
      
      // NEW ARCHITECTURE: Use unified completion handler
      onMovementComplete();
      
      if (oscillation.returnToCenter) {
        oscillationState.isReturning = true;
      }
      cyclesCompleteLogged = false;  // Reset for next oscillation
      return;
    }
  }
  
  // Continue with target position calculation
  long targetStep = (long)(targetPositionMM * STEPS_PER_MM);
  
  // 🆕 OPTIMISATION: Safety check contacts - Test UNIQUEMENT si oscillation proche des limites
  // Calcul des positions extrêmes de l'oscillation
  float minOscPositionMM = oscillation.centerPositionMM - oscillation.amplitudeMM;
  float maxOscPositionMM = oscillation.centerPositionMM + oscillation.amplitudeMM;
  
  // Test END contact uniquement si oscillation approche de la limite haute
  float distanceToEndLimitMM = config.totalDistanceMM - maxOscPositionMM;
  if (distanceToEndLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
    if (targetStep >= config.maxStep && Contacts.readDebounced(PIN_END_CONTACT, LOW)) {
      sendError("❌ OSCILLATION: Contact END atteint de manière inattendue (amplitude proche limite)");
      isPaused = true;
      return;
    }
  }

  // Test START contact uniquement si oscillation approche de la limite basse
  if (minOscPositionMM <= HARD_DRIFT_TEST_ZONE_MM) {
    if (targetStep <= config.minStep && readContactDebounced(PIN_START_CONTACT, LOW)) {
      sendError("❌ OSCILLATION: Contact START atteint de manière inattendue (amplitude proche limite)");
      isPaused = true;
      return;
    }
  }
  
  // Move towards target position
  long errorSteps = targetStep - currentStep;
  
  if (errorSteps == 0) {
    return;  // Already at target
  }
  
  // 🚀 SPEED CALCULATION: Calculate effective frequency (capped if exceeds max speed)
  float effectiveFrequency = oscillation.frequencyHz;
  if (oscillation.amplitudeMM > 0.0) {
    float maxAllowedFreq = OSC_MAX_SPEED_MM_S / (2.0 * PI * oscillation.amplitudeMM);
    if (oscillation.frequencyHz > maxAllowedFreq) {
      effectiveFrequency = maxAllowedFreq;
    }
  }
  
  // Calculate actual peak speed using effective frequency
  actualOscillationSpeedMMS = 2.0 * PI * effectiveFrequency * oscillation.amplitudeMM;
  
  // Use minimum step delay (speed is controlled by effective frequency, not delay)
  unsigned long currentMicros = micros();
  unsigned long elapsedMicros = currentMicros - lastStepMicros;
  
  if (elapsedMicros < OSC_MIN_STEP_DELAY_MICROS) {
    return;  // Too early for next step
  }
  
  // � POSITIONNEMENT INITIAL PROGRESSIF: Mouvement doux vers la première position
  if (oscillationState.isInitialPositioning) {
    // Limiter à 1 step par appel pendant le positionnement initial (très doux)
    int stepsToExecute = 1;
    
    // Désactiver le positionnement initial quand on est proche (dans 5mm de la cible)
    long absErrorSteps = abs(errorSteps);
    if (absErrorSteps < (long)(5.0 * STEPS_PER_MM)) {
      oscillationState.isInitialPositioning = false;
      engine->debug("✓ Positionnement initial terminé, début oscillation");
    }
    
    bool moveForward = (errorSteps > 0);
    Motor.setDirection(moveForward);
    Motor.step();
    
    if (moveForward) {
      currentStep++;
      if (currentStep > lastStepForDistance) {
        totalDistanceTraveled += (currentStep - lastStepForDistance);
        lastStepForDistance = currentStep;
      }
    } else {
      currentStep--;
      if (lastStepForDistance > currentStep) {
        totalDistanceTraveled += (lastStepForDistance - currentStep);
        lastStepForDistance = currentStep;
      }
    }
    
    lastStepMicros = currentMicros;
    return;
  }
  
  // 🎯 STRATÉGIE "RALENTIR LA FORMULE": Seulement catch-up si erreur critique (>5mm)
  // Sinon, 1 step fluide = le moteur suit naturellement la formule sinusoïdale
  int stepsToExecute;
  long absErrorSteps = abs(errorSteps);
  float errorMM = absErrorSteps / STEPS_PER_MM;
  
  if (errorMM > OSC_CATCH_UP_THRESHOLD_MM) {
    stepsToExecute = min(absErrorSteps, (long)OSC_MAX_STEPS_PER_CATCH_UP);
    
    // Log warning uniquement la première fois que le catch-up s'active
    static bool catchUpWarningLogged = false;
    if (!catchUpWarningLogged) {
      engine->warn("⚠️ OSC Catch-up activé: erreur de " + String(errorMM, 1) + "mm (seuil: " + String(OSC_CATCH_UP_THRESHOLD_MM, 1) + "mm)");
      catchUpWarningLogged = true;
    }
  } else {
    // OSCILLATION NORMALE: 1 step fluide = suivre naturellement la sinusoïde
    stepsToExecute = 1;
  }
  
  // Execute steps
  bool moveForward = (errorSteps > 0);
  Motor.setDirection(moveForward);
  
  for (int i = 0; i < stepsToExecute; i++) {
    Motor.step();
    if (moveForward) {
      currentStep++;
      // Track distance traveled
      if (currentStep > lastStepForDistance) {
        totalDistanceTraveled += (currentStep - lastStepForDistance);
        lastStepForDistance = currentStep;
      }
    } else {
      currentStep--;
      // Track distance traveled
      if (lastStepForDistance > currentStep) {
        totalDistanceTraveled += (lastStepForDistance - currentStep);
        lastStepForDistance = currentStep;
      }
    }
  }
  
  lastStepMicros = currentMicros;
}

// Start oscillation mode
void startOscillation() {
  // Validate configuration
  String errorMsg;
  if (!validateOscillationAmplitude(oscillation.centerPositionMM, oscillation.amplitudeMM, errorMsg)) {
    sendError("❌ " + errorMsg);
    config.currentState = STATE_ERROR;
    return;
  }
  
  // Initialize oscillation state
  oscillationState.startTimeMs = millis();
  oscillationState.rampStartMs = millis();
  oscillationState.currentAmplitude = 0;
  oscillationState.completedCycles = 0;  // ✅ CRITICAL: Reset cycle counter for sequencer
  oscillationState.isRampingIn = oscillation.enableRampIn;
  oscillationState.isRampingOut = false;
  oscillationState.isReturning = false;
  oscillationState.isInitialPositioning = true;  // 🚀 Active le positionnement progressif initial
  
  // 🎯 RESET PHASE TRACKING for smooth transitions
  oscillationState.accumulatedPhase = 0.0;
  oscillationState.lastPhaseUpdateMs = 0;  // Will be initialized on first calculateOscillationPosition() call
  oscillationState.lastPhase = 0.0;  // Reset cycle counter tracking
  oscillationState.isTransitioning = false;
  
  // 🎯 RESET CENTER TRANSITION for fresh start
  oscillationState.isCenterTransitioning = false;
  oscillationState.centerTransitionStartMs = 0;
  oscillationState.oldCenterMM = 0;
  oscillationState.targetCenterMM = 0;
  
  // 🎯 RESET AMPLITUDE TRANSITION for fresh start
  oscillationState.isAmplitudeTransitioning = false;
  oscillationState.amplitudeTransitionStartMs = 0;
  oscillationState.oldAmplitudeMM = 0;
  oscillationState.targetAmplitudeMM = 0;
  
  // 🎯 CALCULATE INITIAL ACTUAL SPEED for display
  float theoreticalPeakSpeed = 2.0 * PI * oscillation.frequencyHz * oscillation.amplitudeMM;
  actualOscillationSpeedMMS = min(theoreticalPeakSpeed, OSC_MAX_SPEED_MM_S);
  
  // NOTE: Don't stop sequencer here - startOscillation() can be called BY the sequencer (P4 translation)
  // Conflict safety is handled in WebSocket handlers (startMovement, enablePursuitMode, etc.)
  
  // Start movement
  config.currentState = STATE_RUNNING;
  isPaused = false;
  
  // Set movement type (config.executionContext remains unchanged - can be STANDALONE or SEQUENCER)
  currentMovement = MOVEMENT_OSC;
  
  String waveformName = "SINE";
  if (oscillation.waveform == OSC_TRIANGLE) waveformName = "TRIANGLE";
  if (oscillation.waveform == OSC_SQUARE) waveformName = "SQUARE";
  
  engine->info(String("✅ Oscillation started:\n") +
        "   Centre: " + String(oscillation.centerPositionMM, 1) + " mm\n" +
        "   Amplitude: ±" + String(oscillation.amplitudeMM, 1) + " mm\n" +
        "   Fréquence: " + String(oscillation.frequencyHz, 3) + " Hz\n" +
        "   Forme: " + waveformName + "\n" +
        "   Rampe entrée: " + String(oscillation.enableRampIn ? "OUI" : "NON") + "\n" +
        "   Rampe sortie: " + String(oscillation.enableRampOut ? "OUI" : "NON"));
}

void returnToStart() {
  engine->info("🔄 Returning to start...");
  
  if (config.currentState == STATE_RUNNING || config.currentState == STATE_PAUSED) {
    stopMovement();
    delay(100);
  }
  
  // Allow returnToStart even from ERROR state (recovery mechanism)
  if (config.currentState == STATE_ERROR) {
    engine->info("   → Recovering from ERROR state");
  }
  
  Motor.enable();  // Enable motor
  config.currentState = STATE_CALIBRATING;
  sendStatus();  // Show calibration overlay
  delay(50);
  
  // ============================================================================
  // REFACTORED: Use returnToStartContact() for precise positioning
  // This ensures position 0 is IDENTICAL to calibration position 0
  // (contact + decontact + SAFETY_OFFSET_STEPS)
  // ============================================================================
  
  bool success = returnToStartContact();
  
  if (!success) {
    // Error already logged by returnToStartContact()
    return;
  }
  
  // Reset position variables (already done in returnToStartContact, but explicit here)
  currentStep = 0;
  config.minStep = 0;
  
  engine->info("✓ Return to start complete - Position synchronized with calibration");
  
  // Keep motor enabled - HSS86 needs to stay synchronized
  config.currentState = STATE_READY;
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
