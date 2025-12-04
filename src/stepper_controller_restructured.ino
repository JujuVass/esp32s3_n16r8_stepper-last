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
#include "ChaosPatterns.h"    // Chaos pattern configurations
#include "FilesystemManager.h"  // Filesystem browser API
#include "UtilityEngine.h"     // Unified logging, WebSocket, LittleFS manager
#include "APIRoutes.h"        // API routes module (extracted from main)

// Hardware abstraction layer (modular architecture)
#include "hardware/MotorDriver.h"    // Motor control abstraction (Motor.step(), Motor.enable()...)
#include "hardware/ContactSensors.h" // Contact sensors abstraction (Contacts.isStartContactActive()...)

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

// Sequence table functions (forward declarations after SequenceLine definition)
SequenceLine parseSequenceLineFromJson(JsonVariantConst obj);  // Helper to parse JSON into SequenceLine
String validateSequenceLinePhysics(const SequenceLine& line);  // Validate line against physical constraints
int addSequenceLine(SequenceLine newLine);
bool updateSequenceLine(int lineId, SequenceLine updatedLine);
bool deleteSequenceLine(int lineId);
bool moveSequenceLine(int lineId, int direction);
bool toggleSequenceLine(int lineId);
void clearSequenceTable();
String exportSequenceToJson();
int importSequenceFromJson(String jsonData);
int duplicateSequenceLine(int lineId);

// Functions
//void stepMotor();
void handleCalibrationFailure(int& attempt);
void startCalibration();
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
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void incrementDailyStats(float distanceMM);
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
  webSocket.onEvent(webSocketEvent);
  engine->info("WebSocket server started on port 81");
  
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
      startCalibration();
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
      // CHAOS: Random chaotic patterns
      if (config.currentState == STATE_RUNNING && !isPaused) {
        processChaosExecution();
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
// MOTOR CONTROL - Main Calibration Function
// ============================================================================

void startCalibration() {
  static int calibrationAttempt = 0;
  
  engine->info(calibrationAttempt == 0 ? "Starting calibration..." : "Retry calibration...");
  config.currentState = STATE_CALIBRATING;
  
  // Send status updates to ensure WebSocket clients receive calibration state
  for (int i = 0; i < 5; i++) {
    sendStatus();
    serviceWebSocketFor(20);
  }
  
  Motor.enable();  // Enable motor for calibration
  // Motor enable settling time (service WebSocket)
  serviceWebSocketFor(200);

  // ========================================
  // Step 1: Find START contact
  // ========================================
  if (!findContactWithService(LOW, PIN_START_CONTACT, "START")) {
    handleCalibrationFailure(calibrationAttempt);
    return;
  }
  
  engine->debug("✓ Start contact found - releasing contact very slowly...");
  
  // Move forward VERY SLOWLY until contact releases (HIGH)
  // Slow debounce for critical initial calibration phase (3 checks, 200µs)
  Motor.setDirection(true);  // Forward to release contact
  while (Contacts.readDebounced(PIN_START_CONTACT, LOW, 3, 200)) {  
    Motor.step();
    delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);  // 10ms per step = ultra gentle
  }
  
  engine->debug("✓ Contact released - adding safety margin...");
  
  // Move forward another 10 steps (safety margin)
  for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
    Motor.step();
    delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
    if (i % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
      webSocket.loop();
      server.handleClient();
    }
  }
  
  // THIS position = new logical zero
  // Physical motor is at: START contact + decontact + SAFETY_OFFSET_STEPS
  config.minStep = 0;
  currentStep = 0;
  engine->debug(String("✓ Position 0 set (") + String(SAFETY_OFFSET_STEPS) + 
        " steps after START contact decontact)");
  
  // ========================================
  // Step 2: Find END contact
  // ========================================
  if (!findContactWithService(HIGH, PIN_END_CONTACT, "END")) {
    handleCalibrationFailure(calibrationAttempt);
    return;
  }
  
  engine->debug("✓ End contact found - releasing contact very slowly...");
  
  // Move backward VERY SLOWLY until contact releases (HIGH)
  // CRITICAL: Enhanced debounce for END contact (5 checks, 150µs)
  // END contact requires stronger filtering due to mechanical characteristics
  // (pulley vibrations, belt tension variations at max travel)
  Motor.setDirection(false);  // Backward to release contact
  while (Contacts.readDebounced(PIN_END_CONTACT, LOW, 5, 150)) {
    Motor.step();
    currentStep--;
    delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);
  }
  
  engine->debug("✓ Contact released - adding safety margin...");
  
  // Reculer encore 10 steps (safety margin)
  for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
    Motor.step();
    currentStep--;
    delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
    if (i % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
      webSocket.loop();
      server.handleClient();
    }
  }
  
  // Cette position = nouveau config.maxStep logique
  config.maxStep = currentStep;
  config.totalDistanceMM = config.maxStep / STEPS_PER_MM;
  
  // SAFETY: Check minimum distance (detect mechanical issues early)
  if (config.totalDistanceMM < HARD_MIN_DISTANCE_MM) {
    calibrationAttempt++;
    
    if (calibrationAttempt >= MAX_CALIBRATION_RETRIES) {
      sendError("❌ ERROR: Distance calibrée trop courte (" + String(config.totalDistanceMM, 1) + 
                " mm < " + String(HARD_MIN_DISTANCE_MM, 1) + " mm) après 3 tentatives - " +
                "Vérifiez les contacts, la mécanique (courroie, poulies) et le câblage");
      Motor.disable();  // Safety: disable motor on error
      config.currentState = STATE_ERROR;
      calibrationAttempt = 0;
      return;
    }
    
    engine->warn("⚠️ Distance calibrée trop courte: " + String(config.totalDistanceMM, 1) + " mm < " + 
          String(HARD_MIN_DISTANCE_MM, 1) + " mm - Retry " + String(calibrationAttempt) + "/" + 
          String(MAX_CALIBRATION_RETRIES));
    // Brief pause before retry
    serviceWebSocketFor(500);
    startCalibration();  // Recursive retry
    return;
  }
  
  // SAFETY: Check maximum distance (mechanical constraint)
  if (config.totalDistanceMM > HARD_MAX_DISTANCE_MM) {
    engine->warn("⚠️ WARNING: Distance calibrée (" + String(config.totalDistanceMM, 1) + 
          " mm) dépasse limite sécurité (" + String(HARD_MAX_DISTANCE_MM, 1) + " mm)");
    engine->warn("   → Limitation appliquée à " + String(HARD_MAX_DISTANCE_MM, 1) + " mm");
    config.totalDistanceMM = HARD_MAX_DISTANCE_MM;
    config.maxStep = (long)(config.totalDistanceMM * STEPS_PER_MM);
  }
  
  engine->debug("✓ End contact found - Distance: " + String(config.totalDistanceMM, 1) + " mm");
  
  // ========================================
  // Step 3: Return to START
  // ========================================
  if (!returnToStartContact()) {
    handleCalibrationFailure(calibrationAttempt);
    return;
  }
  
  // ========================================
  // Step 4: Validate accuracy
  // ========================================
  float errorPercent = validateCalibrationAccuracy();
  
  if (errorPercent > MAX_CALIBRATION_ERROR_PERCENT) {
    calibrationAttempt++;
    
    if (calibrationAttempt >= MAX_CALIBRATION_RETRIES) {
      sendError("❌ ERROR: Calibration échouée après 3 tentatives - Vérifiez la mécanique (tension courroie, blocages, config driver)");
      Motor.disable();  // Safety: disable motor on error
      config.currentState = STATE_ERROR;
      calibrationAttempt = 0;
      return;
    }
    
    engine->warn("⚠️ Error too large (" + String(errorPercent, 1) + "%) - Retrying...");
    // Brief pause before retry (service WebSocket)
    serviceWebSocketFor(500);
    startCalibration();  // Recursive retry
    return;
  }
  
  // ========================================
  // SUCCESS
  // ========================================
  currentStep = 0;
  config.minStep = 0;
  startStep = 0;
  targetStep = 0;
  
  // CRITICAL: Keep motor enabled after calibration
  // HSS86 closed-loop driver loses synchronization if disabled/re-enabled
  // This prevents step loss when starting with startPosition > 0
  // digitalWrite(PIN_ENABLE, HIGH);  // Disabled - motor stays enabled
  
  config.currentState = STATE_READY;
  calibrationAttempt = 0;
  
  // Reset max distance limit to 100% after calibration
  maxDistanceLimitPercent = 100.0;
  updateEffectiveMaxDistance();
  
  // Initialize oscillation center to middle of effective travel distance
  if (oscillation.centerPositionMM == 0) {
    float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    oscillation.centerPositionMM = effectiveMax / 2.0;
  }

  engine->info("✓ Calibration complete - Motor remains enabled");

  // CRITICAL: Notify sequencer that calibration is complete
  if (config.executionContext == CONTEXT_SEQUENCER) {
    onMovementComplete();
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
 * 
 * @param isValid Result of validation function
 * @param errorMsg Error message to send if validation failed
 * @return true if validation passed, false if failed (error already sent)
 */
bool validateAndReport(bool isValid, const String& errorMsg) {
  if (!isValid) {
    sendError("❌ " + errorMsg);
  }
  return isValid;
}

/**
 * Validate distance parameter
 * Returns true if valid, false otherwise (with error message)
 */
bool validateDistance(float distMM, String& errorMsg) {
  if (distMM < 0) {
    errorMsg = "Distance négative invalide: " + String(distMM, 1) + " mm";
    return false;
  }
  
  // Use effective max distance (with limitation factor applied)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (maxAllowed > 0 && distMM > maxAllowed) {
    errorMsg = "Distance dépasse limite: " + String(distMM, 1) + " > " + String(maxAllowed, 1) + " mm";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (limite " + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  return true;
}

/**
 * Validate speed level parameter (0.1 - MAX_SPEED_LEVEL)
 * Returns true if valid, false otherwise (with error message)
 */
bool validateSpeed(float speedLevel, String& errorMsg) {
  if (speedLevel < 0.1) {
    errorMsg = "Vitesse trop faible: " + String(speedLevel, 1) + " (min: 0.1)";
    return false;
  }
  
  if (speedLevel > MAX_SPEED_LEVEL) {
    errorMsg = "Vitesse trop élevée: " + String(speedLevel, 1) + " (max: " + String(MAX_SPEED_LEVEL, 1) + ")";
    return false;
  }
  
  return true;
}

/**
 * Validate position parameter
 * Returns true if valid, false otherwise (with error message)
 */
bool validatePosition(float positionMM, String& errorMsg) {
  if (positionMM < 0) {
    errorMsg = "Position négative invalide: " + String(positionMM, 1) + " mm";
    return false;
  }
  
  // Use effective max distance (with limitation factor applied)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (maxAllowed > 0 && positionMM > maxAllowed) {
    errorMsg = "Position dépasse limite: " + String(positionMM, 1) + " > " + String(maxAllowed, 1) + " mm";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (limite " + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  return true;
}

/**
 * Validate start position + distance (combined range check)
 * Returns true if valid, false otherwise (with error message)
 */
bool validateMotionRange(float startMM, float distMM, String& errorMsg) {
  // Validate start position alone
  if (!validatePosition(startMM, errorMsg)) {
    return false;
  }
  
  // Validate distance alone
  if (!validateDistance(distMM, errorMsg)) {
    return false;
  }
  
  // Validate combined range
  float endPositionMM = startMM + distMM;
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (maxAllowed > 0 && endPositionMM > maxAllowed) {
    errorMsg = "Position finale dépasse limite: " + String(endPositionMM, 1) + " mm (start " + 
               String(startMM, 1) + " + distance " + String(distMM, 1) + ") > " + 
               String(maxAllowed, 1) + " mm";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (limite " + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  return true;
}

/**
 * Validate Chaos mode parameters
 */
bool validateChaosParams(float centerMM, float amplitudeMM, float maxSpeed, float craziness, String& errorMsg) {
  // Validate center position
  if (!validatePosition(centerMM, errorMsg)) {
    return false;
  }
  
  // Validate amplitude
  if (amplitudeMM <= 0) {
    errorMsg = "Amplitude doit être > 0 mm";
    return false;
  }
  
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (amplitudeMM > maxAllowed / 2.0) {
    errorMsg = "Amplitude trop grande: " + String(amplitudeMM, 1) + " > " + String(maxAllowed / 2.0, 1) + " mm (max)";
    return false;
  }
  
  // Validate that center ± amplitude stays within bounds
  if (centerMM - amplitudeMM < 0) {
    errorMsg = "Centre - amplitude < 0 mm (centre=" + String(centerMM, 1) + ", amplitude=" + String(amplitudeMM, 1) + ")";
    return false;
  }
  
  if (centerMM + amplitudeMM > maxAllowed) {
    errorMsg = "Centre + amplitude > " + String(maxAllowed, 1) + " mm limite";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (" + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  // Validate speed
  if (!validateSpeed(maxSpeed, errorMsg)) {
    return false;
  }
  
  // Validate craziness (0-100%)
  if (craziness < 0 || craziness > 100) {
    errorMsg = "Craziness doit être 0-100% (reçu: " + String(craziness, 1) + ")";
    return false;
  }
  
  return true;
}

/**
 * Validate Oscillation mode parameters
 */
bool validateOscillationParams(float centerMM, float amplitudeMM, float frequency, String& errorMsg) {
  // Validate center position
  if (!validatePosition(centerMM, errorMsg)) {
    return false;
  }
  
  // Validate amplitude
  if (amplitudeMM <= 0) {
    errorMsg = "Amplitude doit être > 0 mm";
    return false;
  }
  
  // Use effective max distance (respects limitation percentage)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  // Validate bounds (center ± amplitude must stay within limits)
  if (centerMM - amplitudeMM < 0) {
    errorMsg = "Centre - amplitude < 0 mm (centre=" + String(centerMM, 1) + ", amplitude=" + String(amplitudeMM, 1) + ")";
    return false;
  }
  
  if (centerMM + amplitudeMM > maxAllowed) {
    errorMsg = "Centre + amplitude > " + String(maxAllowed, 1) + " mm limite (centre=" + String(centerMM, 1) + ", amplitude=" + String(amplitudeMM, 1) + ")";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " [Limitation " + String(maxDistanceLimitPercent, 0) + "%]";
    }
    return false;
  }
  
  // Validate frequency (0.1 to 10 Hz)
  if (frequency < 0.1 || frequency > 10.0) {
    errorMsg = "Fréquence doit être 0.1-10 Hz (reçu: " + String(frequency, 1) + ")";
    return false;
  }
  
  return true;
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
    startCalibration();
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
    
    // CHAOS MODE SAFETY: Check amplitude limits (helper function)
    if (!checkChaosLimits()) return;
    
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
    
    // CHAOS MODE SAFETY: Check amplitude limits (helper function)
    if (!checkChaosLimits()) return;
    
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

/**
 * Check CHAOS mode amplitude limits (prevents stepping outside configured range)
 * CHAOS mode uses center ± amplitude, must respect both configured limits AND physical calibration
 * 
 * @return true if safe to continue, false if limit hit (movingForward already reversed)
 */
bool checkChaosLimits() {
  if (currentMovement != MOVEMENT_CHAOS) return true;  // Not in CHAOS mode
  
  // Pre-calculate positions (optimization: avoid repeated division)
  const float STEPS_PER_MM_INV = 1.0f / STEPS_PER_MM;
  float currentPosMM = currentStep * STEPS_PER_MM_INV;
  float nextPosMM = movingForward ? (currentStep + 1) * STEPS_PER_MM_INV 
                                   : (currentStep - 1) * STEPS_PER_MM_INV;
  
  // 🆕 OPTIMISATION: Test contacts physiques UNIQUEMENT si amplitude proche des limites
  float minChaosPositionMM = chaos.centerPositionMM - chaos.amplitudeMM;
  float maxChaosPositionMM = chaos.centerPositionMM + chaos.amplitudeMM;
  
  if (movingForward) {
    // Check upper limit (use effective max distance to respect limitation)
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    float effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
    
    if (nextPosMM > effectiveMaxLimit) {
      engine->warn(String("🛡️ CHAOS: Hit upper limit! Current: ") + 
            String(currentPosMM, 1) + "mm | Limit: " + String(effectiveMaxLimit, 1) + "mm");
      targetStep = currentStep;
      movingForward = false;  // CRITICAL: Must reverse to go DOWN
      return false;  // Limit hit
    }
    
    // Test END contact si chaos approche de la limite haute
    float distanceToEndLimitMM = config.totalDistanceMM - maxChaosPositionMM;
    if (distanceToEndLimitMM <= HARD_DRIFT_TEST_ZONE_MM) {
      if (Contacts.readDebounced(PIN_END_CONTACT, LOW, 3, 50)) {
        sendError("❌ CHAOS: Contact END atteint - amplitude proche limite");
        config.currentState = STATE_ERROR;
        chaosState.isRunning = false;
        return false;
      }
    }
    
  } else {
    // Check lower limit
    float effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
    
    if (nextPosMM < effectiveMinLimit) {
      engine->debug(String("🛡️ CHAOS: Hit lower limit! Current: ") + 
            String(currentPosMM, 1) + "mm | Limit: " + String(effectiveMinLimit, 1) + "mm");
      targetStep = currentStep;
      movingForward = true;  // CRITICAL: Must reverse to go UP
      return false;  // Limit hit
    }
    
    // Test START contact si chaos approche de la limite basse
    if (minChaosPositionMM <= HARD_DRIFT_TEST_ZONE_MM) {
      if (Contacts.readDebounced(PIN_START_CONTACT, LOW, 3, 50)) {
        sendError("❌ CHAOS: Contact START atteint - amplitude proche limite");
        config.currentState = STATE_ERROR;
        chaosState.isRunning = false;
        return false;
      }
    }
  }
  
  return true;  // No limit hit, safe to step
}

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

/**
 * Increment daily statistics with distance traveled
 * Called automatically when a standalone movement is completed
 */
// ============================================================================
// STATISTICS
// ============================================================================

void incrementDailyStats(float distanceMM) {
  if (distanceMM <= 0) return;
  
  // Get current date (YYYY-MM-DD format)
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
  
  // Load existing stats
  JsonDocument statsDoc;
  if (LittleFS.exists("/stats.json")) {
    File file = LittleFS.open("/stats.json", "r");
    if (file) {
      deserializeJson(statsDoc, file);
      file.close();
    }
  }
  
  // Find or create today's entry
  JsonArray statsArray = statsDoc.as<JsonArray>();
  if (statsArray.isNull()) {
    statsArray = statsDoc.to<JsonArray>();
  }
  
  bool found = false;
  for (JsonObject entry : statsArray) {
    if (strcmp(entry["date"], dateStr) == 0) {
      float current = entry["distanceMM"] | 0.0;
      entry["distanceMM"] = current + distanceMM;
      found = true;
      break;
    }
  }
  
  if (!found) {
    JsonObject newEntry = statsArray.add<JsonObject>();
    newEntry["date"] = dateStr;
    newEntry["distanceMM"] = distanceMM;
  }
  
  // Save back to file
  File file = LittleFS.open("/stats.json", "w");
  if (!file) {
    engine->error("Failed to write stats.json");
    return;
  }
  
  serializeJson(statsDoc, file);
  file.close();
  
  engine->debug(String("📊 Stats: +") + String(distanceMM, 1) + "mm on " + String(dateStr));
}

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
  
  // Save increment to daily stats
  incrementDailyStats(incrementMM);
  
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
// CHAOS MODE - HELPER FUNCTIONS
// ============================================================================

/**
 * Calculate effective limits for chaos mode
 * Respects both chaos amplitude configuration AND effective distance limit
 * @param minLimit Output: effective minimum limit (mm)
 * @param maxLimit Output: effective maximum limit (mm)
 */
inline void calculateChaosLimits(float& minLimit, float& maxLimit) {
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  minLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
  maxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
}

/**
 * Calculate maximum safe amplitude from center (symmetric)
 * Ensures patterns can move equally in both directions without exceeding limits
 * @param minLimit Effective minimum limit (mm)
 * @param maxLimit Effective maximum limit (mm)
 * @return Maximum safe amplitude (mm) - distance from center to nearest limit
 */
inline float calculateMaxPossibleAmplitude(float minLimit, float maxLimit) {
  return min(
    chaos.centerPositionMM - minLimit,  // Space below center
    maxLimit - chaos.centerPositionMM   // Space above center
  );
}

/**
 * Force direction at limits to prevent infinite loop
 * If at lower limit: forces UP, if at upper limit: forces DOWN
 * @param currentPos Current position (mm)
 * @param minLimit Effective minimum limit (mm)
 * @param maxLimit Effective maximum limit (mm)
 * @param movingForward Reference to direction flag (modified if at limit)
 * @return true if direction was forced, false if normal alternation should apply
 */
inline bool forceDirectionAtLimits(float currentPos, float minLimit, float maxLimit, bool& movingForward) {
  if (currentPos <= minLimit + 1.0) {
    // At lower limit! MUST go UP
    movingForward = true;
    return true;  // Direction was forced
  } else if (currentPos >= maxLimit - 1.0) {
    // At upper limit! MUST go DOWN
    movingForward = false;
    return true;  // Direction was forced
  }
  return false;  // Normal alternation
}

// ============================================================================
// CHAOS MODE - RANDOM PATTERN GENERATION
// ============================================================================

/**
 * Generate a new random chaos pattern
 * Weighted probabilities (11 patterns total):
 * WAVE 10%, ZIGZAG 12%, SWEEP 12%, PENDULUM 12%, PULSE 8%, DRIFT 8%, SPIRAL 8%, 
 * BREATHING 15%, BRUTE_FORCE 10%, LIBERATOR 10%, BURST 5%
 * Uses crazinessPercent (0-100%) to modulate speed, duration, and jump size
 * Only selects from enabled patterns
 */
void generateChaosPattern() {
  // Build list of enabled patterns with their weights
  int enabledPatterns[11];
  int weights[11] = {12, 12, 8, 8, 5, 10, 12, 8, 15, 10, 10};  // ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR
  int totalWeight = 0;
  int enabledCount = 0;
  
  for (int i = 0; i < 11; i++) {
    if (chaos.patternsEnabled[i]) {
      enabledPatterns[enabledCount] = i;
      totalWeight += weights[i];
      enabledCount++;
    }
  }
  
  // Safety: if no patterns enabled, enable all
  if (enabledCount == 0) {
    engine->warn("⚠️ No patterns enabled, enabling all");
    for (int i = 0; i < 11; i++) {
      chaos.patternsEnabled[i] = true;
      enabledPatterns[i] = i;
      totalWeight += weights[i];
    }
    enabledCount = 11;
  }
  
  // Calculate EFFECTIVE limits (chaos config constrained by physical limits)
  // This ensures patterns ALWAYS respect both chaos amplitude AND physical limits
  float effectiveMinLimit, effectiveMaxLimit;
  calculateChaosLimits(effectiveMinLimit, effectiveMaxLimit);
  
  // Weighted random selection from enabled patterns
  int roll = random(totalWeight);
  int cumulative = 0;
  
  for (int i = 0; i < enabledCount; i++) {
    int patternIndex = enabledPatterns[i];
    cumulative += weights[patternIndex];
    if (roll < cumulative) {
      chaosState.currentPattern = (ChaosPattern)patternIndex;
      break;
    }
  }
  
  // Craziness factor (0.0 = calm, 1.0 = crazy)
  float craziness = chaos.crazinessPercent / 100.0;
  
  // Pattern-specific parameters (modulated by craziness)
  float speedMultiplier;
  unsigned long patternDuration;
  float jumpMultiplier;  // How far to jump (modulated by craziness)
  
  switch (chaosState.currentPattern) {
    case CHAOS_ZIGZAG: {
      const ChaosBaseConfig& cfg = ZIGZAG_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.375); // 1500/4000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate jump with craziness boost
      jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
      
      float currentPos = currentStep / (float)STEPS_PER_MM;
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      float maxJump = maxPossibleAmplitude * jumpMultiplier;
      float targetOffset = (random(-100, 101) / 100.0) * maxJump;
      chaosState.targetPositionMM = constrain(
        currentPos + targetOffset,
        effectiveMinLimit,
        effectiveMaxLimit
      );
      break;
    }
      
    case CHAOS_SWEEP: {
      const ChaosBaseConfig& cfg = SWEEP_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.7);  // 2800/4000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate amplitude (75-100% sweep range)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      float sweepPercent = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * random(0, 101) / 100.0);
      chaosState.waveAmplitude = maxPossibleAmplitude * sweepPercent;
      
      // Random starting direction
      chaosState.movingForward = random(2) == 0;
      chaosState.patternStartTime = millis();
      
      // Initial target: one extreme edge
      if (chaosState.movingForward) {
        chaosState.targetPositionMM = chaos.centerPositionMM + chaosState.waveAmplitude;
      } else {
        chaosState.targetPositionMM = chaos.centerPositionMM - chaosState.waveAmplitude;
      }
      
      // Ensure within limits
      chaosState.targetPositionMM = constrain(
        chaosState.targetPositionMM,
        effectiveMinLimit,
        effectiveMaxLimit
      );
      break;
    }
      
    case CHAOS_PULSE: {
      const ChaosBaseConfig& cfg = PULSE_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.53);  // 750/1400
      patternDuration = random(durationMin, durationMax);
      
      // Calculate jump amplitude
      jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
      
      // Initialize PULSE state
      chaosState.pulsePhase = false;  // Start with OUT phase
      chaosState.pulseCenterMM = currentStep / (float)STEPS_PER_MM;  // Save current position to return to
      chaosState.patternStartTime = millis();
      
      // Calculate max possible amplitude (respects physical limits)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      
      // Phase 1 (OUT): Go to random position
      float pulseOffset = (random(-100, 101) / 100.0) * maxPossibleAmplitude * jumpMultiplier;
      chaosState.targetPositionMM = constrain(
        chaos.centerPositionMM + pulseOffset,
        effectiveMinLimit,
        effectiveMaxLimit
      );
      
      engine->debug("💓 PULSE Phase 1 (OUT): from=" + String(chaosState.pulseCenterMM, 1) + 
            "mm → target=" + String(chaosState.targetPositionMM, 1) + 
            "mm (will return to " + String(chaosState.pulseCenterMM, 1) + "mm)");
      break;
    }
      
    case CHAOS_DRIFT: {
      const ChaosBaseConfig& cfg = DRIFT_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.6);  // 3000/5000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate jump amplitude
      jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
      
      // Calculate max possible amplitude (respects physical limits)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      
      float currentPos = currentStep / (float)STEPS_PER_MM;
      float drift = (random(-100, 101) / 100.0) * maxPossibleAmplitude * jumpMultiplier;
      chaosState.targetPositionMM = constrain(
        currentPos + drift,
        effectiveMinLimit,
        effectiveMaxLimit
      );
      break;
    }
      
    case CHAOS_BURST: {
      const ChaosBaseConfig& cfg = BURST_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.5);  // 600/1200
      patternDuration = random(durationMin, durationMax);
      
      // Calculate jump amplitude
      jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
      
      // Calculate max possible amplitude (respects physical limits)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      
      float currentPos = currentStep / (float)STEPS_PER_MM;
      float maxJump = maxPossibleAmplitude * jumpMultiplier;
      float burstOffset = (random(-100, 101) / 100.0) * maxJump;
      chaosState.targetPositionMM = constrain(
        currentPos + burstOffset,
        effectiveMinLimit,
        effectiveMaxLimit
      );
      break;
    }
      
    case CHAOS_WAVE: {
      const ChaosBaseConfig& cfg = WAVE_CONFIG;
      const ChaosSinusoidalExt& sin_cfg = WAVE_SIN;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.33);  // 4000/12000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate effective amplitude (cannot exceed physical limits)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      
      // Wave amplitude varies
      chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * random(0, 101) / 100.0);
      
      // Calculate frequency: cyclesOverDuration complete cycles over pattern duration
      chaosState.waveFrequency = sin_cfg.cyclesOverDuration / (patternDuration / 1000.0);
      
      chaosState.patternStartTime = millis();
      
      // WAVE uses continuous calculation, no discrete target
      // Target will be calculated continuously in processChaosExecution()
      chaosState.targetPositionMM = chaos.centerPositionMM;  // Start at center
      
      engine->debug("🌊 WAVE: amplitude=" + String(chaosState.waveAmplitude, 1) + 
            "mm, freq=" + String(chaosState.waveFrequency, 3) + 
            "Hz, duration=" + String(patternDuration) + "ms");
      break;
    }
      
    case CHAOS_PENDULUM: {
      const ChaosBaseConfig& cfg = PENDULUM_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.4);  // 2400/6000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate effective amplitude (cannot exceed physical limits)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      
      // Pendulum swings with random amplitude
      jumpMultiplier = cfg.amplitudeJumpMin + ((cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * random(0, 101) / 100.0);
      chaosState.waveAmplitude = maxPossibleAmplitude * jumpMultiplier;
      chaosState.movingForward = true;
      chaosState.patternStartTime = millis();
      
      // Initial target respects effective limits
      chaosState.targetPositionMM = constrain(
        chaos.centerPositionMM + chaosState.waveAmplitude,
        effectiveMinLimit,
        effectiveMaxLimit
      );
      break;
    }
      
    case CHAOS_SPIRAL: {
      const ChaosBaseConfig& cfg = SPIRAL_CONFIG;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.5);  // 5000/10000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate effective amplitude (cannot exceed physical limits)
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      
      // Start spiral from center, expanding out (amplitudeJumpMin of max possible amplitude)
      chaosState.spiralRadius = maxPossibleAmplitude * cfg.amplitudeJumpMin;
      chaosState.movingForward = random(2) == 0;  // Random direction
      chaosState.patternStartTime = millis();
      
      // Initial target respects effective limits
      if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(
          chaos.centerPositionMM + chaosState.spiralRadius,
          effectiveMinLimit,
          effectiveMaxLimit
        );
      } else {
        chaosState.targetPositionMM = constrain(
          chaos.centerPositionMM - chaosState.spiralRadius,
          effectiveMinLimit,
          effectiveMaxLimit
        );
      }
      break;
    }
    
    case CHAOS_CALM: {
      const ChaosBaseConfig& cfg = CALM_CONFIG;
      const ChaosSinusoidalExt& sin_cfg = CALM_SIN;
      const ChaosPauseExt& pause_cfg = CALM_PAUSE;
      
      // Calculate speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.667); // 2000/3000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate amplitude with craziness boost
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      float amplitudeRange = cfg.amplitudeJumpMax - cfg.amplitudeJumpMin;
      chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + amplitudeRange * craziness);
      
      // Random frequency
      chaosState.waveFrequency = sin_cfg.frequencyMin + ((sin_cfg.frequencyMax - sin_cfg.frequencyMin) * random(0, 101) / 100.0);

      // Pause duration (unused in init, calculated at runtime)
      chaosState.pauseDuration = pause_cfg.pauseMin + (unsigned long)((pause_cfg.pauseMax - pause_cfg.pauseMin) * (1.0 - craziness));
      chaosState.isPaused = false;
      chaosState.patternStartTime = millis();
      
      // Initial target (will be calculated each step)
      chaosState.targetPositionMM = chaos.centerPositionMM;
      break;
    }
    
    case CHAOS_BRUTE_FORCE: {
      const ChaosBaseConfig& cfg = BRUTE_FORCE_CONFIG;
      const ChaosMultiPhaseExt& multi_cfg = BRUTE_FORCE_MULTI;
      const ChaosDirectionExt& dir_cfg = BRUTE_FORCE_DIR;
      
      // Fast phase speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.75);  // 1500/2000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate amplitude with craziness
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
      
      // Direction preference
      int forwardChance = dir_cfg.forwardChanceMin - (int)((dir_cfg.forwardChanceMin - dir_cfg.forwardChanceMax) * craziness);
      chaosState.movingForward = random(100) < forwardChance;
      if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(
          chaos.centerPositionMM + chaosState.waveAmplitude,
          effectiveMinLimit,
          effectiveMaxLimit
        );
      } else {
        chaosState.targetPositionMM = constrain(
          chaos.centerPositionMM - chaosState.waveAmplitude,
          effectiveMinLimit,
          effectiveMaxLimit
        );
      }

      // Pause duration between phases
      chaosState.pauseDuration = multi_cfg.pauseMin + (unsigned long)((multi_cfg.pauseMax - multi_cfg.pauseMin) * (1.0 - craziness));
      chaosState.brutePhase = 0;  // Start with fast phase
      chaosState.patternStartTime = millis();
      break;
    }
    
    case CHAOS_LIBERATOR: {
      const ChaosBaseConfig& cfg = LIBERATOR_CONFIG;
      const ChaosMultiPhaseExt& multi_cfg = LIBERATOR_MULTI;
      const ChaosDirectionExt& dir_cfg = LIBERATOR_DIR;
      
      // Slow phase speed with craziness boost
      float speedMin = cfg.speedMin + (cfg.speedCrazinessBoost * craziness);
      float speedMax = cfg.speedMax + (cfg.speedCrazinessBoost * craziness);
      speedMultiplier = (random(100) / 100.0) * (speedMax - speedMin) + speedMin;
      
      // Calculate duration with craziness reduction
      unsigned long durationMin = cfg.durationMin - (unsigned long)(cfg.durationCrazinessReduction * craziness);
      unsigned long durationMax = cfg.durationMax - (unsigned long)((cfg.durationMax - cfg.durationMin) * craziness * 0.75);  // 1500/2000
      patternDuration = random(durationMin, durationMax);
      
      // Calculate amplitude with craziness
      float maxPossibleAmplitude = calculateMaxPossibleAmplitude(effectiveMinLimit, effectiveMaxLimit);
      chaosState.waveAmplitude = maxPossibleAmplitude * (cfg.amplitudeJumpMin + (cfg.amplitudeJumpMax - cfg.amplitudeJumpMin) * craziness);
      
      // Direction preference
      int forwardChance = dir_cfg.forwardChanceMin - (int)((dir_cfg.forwardChanceMin - dir_cfg.forwardChanceMax) * craziness);
      chaosState.movingForward = random(100) < forwardChance;
      if (chaosState.movingForward) {
        chaosState.targetPositionMM = constrain(
          chaos.centerPositionMM + chaosState.waveAmplitude,
          effectiveMinLimit,
          effectiveMaxLimit
        );
      } else {
        chaosState.targetPositionMM = constrain(
          chaos.centerPositionMM - chaosState.waveAmplitude,
          effectiveMinLimit,
          effectiveMaxLimit
        );
      }

      // Pause duration between phases
      chaosState.pauseDuration = multi_cfg.pauseMin + (unsigned long)((multi_cfg.pauseMax - multi_cfg.pauseMin) * (1.0 - craziness));
      chaosState.liberatorPhase = 0;  // Start with slow phase
      chaosState.patternStartTime = millis();
      break;
    }
  }
  
  // Calculate final speed and clamp within limits
  chaosState.currentSpeedLevel = speedMultiplier * chaos.maxSpeedLevel;
  chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0, MAX_SPEED_LEVEL);
  calculateChaosStepDelay();  // ✅ FIX: Apply new speed to stepDelay for new pattern
  
  // Set next pattern change time
  chaosState.nextPatternChangeTime = millis() + patternDuration;
  
  // Update stats
  chaosState.patternsExecuted++;
  
  // Clamp target within chaos amplitude limits AND physical limits
  // Use the effectiveMinLimit/MaxLimit already calculated at function start
  chaosState.targetPositionMM = constrain(
    chaosState.targetPositionMM,
    effectiveMinLimit,
    effectiveMaxLimit
  );
  
  // Debug output with full chaos config visibility
  String patternName[] = {"ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", "WAVE", "PENDULUM", "SPIRAL", "CALM", "BRUTE_FORCE", "LIBERATOR"};
  float currentPos = currentStep / (float)STEPS_PER_MM;
  engine->debug(String("🎲 Chaos #") + String(chaosState.patternsExecuted) + ": " + 
        patternName[chaosState.currentPattern] + 
        " | Config: center=" + String(chaos.centerPositionMM, 1) + 
        "mm amplitude=" + String(chaos.amplitudeMM, 1) + "mm" +
        " | Current: " + String(currentPos, 1) + "mm" +
        " | Target: " + String(chaosState.targetPositionMM, 1) + "mm" +
        " | Limits: [" + String(effectiveMinLimit, 1) + " - " + String(effectiveMaxLimit, 1) + "]" +
        " | Speed: " + String(chaosState.currentSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0) +
        " | Duration: " + String(patternDuration) + "ms");
}

/**
 * Calculate step delay for chaos mode based on speed level
 * Uses direct speed mapping: speedLevel 1.0 = slow, MAX_SPEED_LEVEL = fast
 */
void calculateChaosStepDelay() {
  // Map speed level (0-MAX_SPEED_LEVEL) to a reasonable mm/s range
  // speedLevel 1.0 → ~10 mm/s (slow but usable)
  // speedLevel 10.0 → ~100 mm/s (medium)
  // speedLevel MAX_SPEED_LEVEL → ~(MAX_SPEED_LEVEL*10) mm/s (fast)
  float mmPerSecond = chaosState.currentSpeedLevel * 10.0;
  
  // Convert to steps/second
  float stepsPerSecond = mmPerSecond * STEPS_PER_MM;
  
  // Convert to microseconds per step WITH system overhead compensation
  if (stepsPerSecond > 0) {
    chaosState.stepDelay = (unsigned long)((1000000.0 / stepsPerSecond) / SPEED_COMPENSATION_FACTOR);
  } else {
    chaosState.stepDelay = 10000;  // Fallback: very slow
  }
  
  // Apply minimum safe delay (HSS86: 50kHz max = 20µs)
  if (chaosState.stepDelay < 20) {
    chaosState.stepDelay = 20;
  }
  
  // Apply maximum delay to prevent ridiculously slow speeds
  if (chaosState.stepDelay > CHAOS_MAX_STEP_DELAY_MICROS) {  // Max 50ms = 20 steps/sec
    chaosState.stepDelay = CHAOS_MAX_STEP_DELAY_MICROS;
  }
}

/**
 * Process chaos execution in main loop (non-blocking)
 */
void processChaosExecution() {
  if (!chaosState.isRunning) return;
  
  // Handle pause for BRUTE_FORCE and LIBERATOR patterns
  if (chaosState.isPaused && 
      (chaosState.currentPattern == CHAOS_BRUTE_FORCE || 
       chaosState.currentPattern == CHAOS_LIBERATOR)) {
    unsigned long pauseElapsed = millis() - chaosState.pauseStartTime;
    if (pauseElapsed >= chaosState.pauseDuration) {
      // Pause complete, resume movement
      chaosState.isPaused = false;
      
      // Phase transition handled in isAtTarget switch below
      String patternName = (chaosState.currentPattern == CHAOS_BRUTE_FORCE) ? "BRUTE_FORCE" : "LIBERATOR";
      engine->debug(String(chaosState.currentPattern == CHAOS_BRUTE_FORCE ? "🔨" : "🔓") + " " + 
            patternName + " pause complete, resuming");
    } else {
      // Still paused, don't move
      return;
    }
  }
  
  // Check if duration limit reached
  if (chaos.durationSeconds > 0) {
    unsigned long elapsed = (millis() - chaosState.startTime) / 1000;
    if (elapsed >= chaos.durationSeconds) {
  engine->info("⏱️ Chaos duration complete: " + String(elapsed) + "s");
  engine->debug(String("processChaosExecution(): config.executionContext=") + executionContextName(config.executionContext) + " seqState.isRunning=" + String(seqState.isRunning));
      
      // Use unified completion handler
      if (config.executionContext == CONTEXT_SEQUENCER) {
        // Sequencer mode: just mark as done and return control
        // ⚠️ CRITICAL: Do NOT disable motor in sequencer mode - HSS would lose position!
        chaosState.isRunning = false;
        
        onMovementComplete();
      } else {
        // Standalone chaos mode - full stop
        stopChaos();
      }
      return;
    }
  }
  
  // Check if time for new pattern
  if (millis() >= chaosState.nextPatternChangeTime) {
    generateChaosPattern();
    
    // Calculate step delay based on current speed level
    calculateChaosStepDelay();
    
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    String patternNames[] = {"ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", "WAVE", "PENDULUM", "SPIRAL", "CALM", "BRUTE_FORCE", "LIBERATOR"};
    engine->debug(String("🎲 Pattern: ") + patternNames[chaosState.currentPattern] + 
          " | Speed: " + String(chaosState.currentSpeedLevel, 1) + 
          "/" + String(MAX_SPEED_LEVEL, 0) + " | Delay: " + String(chaosState.stepDelay) + " µs/step");
  }
  
  // Handle continuous patterns (WAVE, PENDULUM, SPIRAL)
  float currentPos = currentStep / (float)STEPS_PER_MM;
  // Use tolerance: within 2 steps (0.33mm) is considered "at target"
  bool isAtTarget = (abs(currentStep - targetStep) <= 2);
  
  // ✅ FIX WAVE: Calculate continuous sinusoidal position
  // x(t) = center + A·sin(2πft)
  if (chaosState.currentPattern == CHAOS_WAVE) {
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    float t = elapsed / 1000.0;  // Time in seconds
    
    // Calculate sinusoidal position
    float sineValue = sin(2.0 * PI * chaosState.waveFrequency * t);
    chaosState.targetPositionMM = chaos.centerPositionMM + (chaosState.waveAmplitude * sineValue);
    
    // Clamp to limits
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    float effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
    float effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
    
    chaosState.targetPositionMM = constrain(
      chaosState.targetPositionMM,
      effectiveMinLimit,
      effectiveMaxLimit
    );
    
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    // No need to wait for isAtTarget - WAVE is continuously calculated
  }
  
  // ✅ CALM: Breathing pattern with pauses
  // Similar to WAVE but with smaller amplitude and random pauses
  if (chaosState.currentPattern == CHAOS_CALM) {
    // Check if in pause
    if (chaosState.isPaused) {
      unsigned long pauseElapsed = millis() - chaosState.pauseStartTime;
      if (pauseElapsed >= chaosState.pauseDuration) {
        // Pause complete, resume breathing
        chaosState.isPaused = false;
        chaosState.patternStartTime = millis();  // Reset pattern time
        engine->debug("😮 CALM: pause complete, resuming breathing");
      }
      // During pause, don't move - keep current position
      return;
    }
    
    unsigned long elapsed = millis() - chaosState.patternStartTime;
    float t = elapsed / 1000.0;  // Time in seconds
    
    // Calculate sinusoidal position (breathing)
    float sineValue = sin(2.0 * PI * chaosState.waveFrequency * t);
    chaosState.targetPositionMM = chaos.centerPositionMM + (chaosState.waveAmplitude * sineValue);
    
    // Clamp to limits
    float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    float effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
    float effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
    
    chaosState.targetPositionMM = constrain(
      chaosState.targetPositionMM,
      effectiveMinLimit,
      effectiveMaxLimit
    );
    
    targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
    
    // Random chance to pause at peaks/troughs (when sine is near threshold)
    // CRITICAL: Only check ONCE per extremum (not every loop iteration)
    static float lastSineValue = 0.0;
    bool crossedThreshold = (abs(lastSineValue) <= CALM_PAUSE.pauseTriggerThreshold && 
                            abs(sineValue) > CALM_PAUSE.pauseTriggerThreshold);
    lastSineValue = sineValue;
    
    if (crossedThreshold && random(10000) < (int)(CALM_PAUSE.pauseChancePercent * 100)) {
      chaosState.isPaused = true;
      chaosState.pauseStartTime = millis();
      // Random pause duration from config range
      chaosState.pauseDuration = random(CALM_PAUSE.pauseMin, CALM_PAUSE.pauseMax);
      engine->debug("😌 CALM: entering pause for " + String(chaosState.pauseDuration) + "ms");
    }
    
    // No need to wait for isAtTarget - CALM is continuously calculated
  }
  
  // Calculate effective limits for continuous patterns (respect effective distance limit)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  float effectiveMinLimit = max(chaos.centerPositionMM - chaos.amplitudeMM, 0.0f);
  float effectiveMaxLimit = min(chaos.centerPositionMM + chaos.amplitudeMM, maxAllowed);
  
  // Calculate max possible amplitude (for continuous patterns that use waveAmplitude)
  float maxPossibleAmplitude = min(
    chaos.centerPositionMM - effectiveMinLimit,
    effectiveMaxLimit - chaos.centerPositionMM
  );
  
  if (isAtTarget) {
    switch (chaosState.currentPattern) {
      case CHAOS_PULSE: {
        // ✅ FIX: PULSE has 2 phases (OUT + RETURN)
        if (!chaosState.pulsePhase) {
          // Phase 1 just completed (OUT) → Start Phase 2 (RETURN)
          chaosState.pulsePhase = true;
          chaosState.targetPositionMM = chaosState.pulseCenterMM;
          targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
          
          engine->debug("💓 PULSE Phase 2 (RETURN): from=" + String(currentPos, 1) + 
                "mm → return to " + String(chaosState.pulseCenterMM, 1) + "mm");
        } else {
          // Phase 2 completed (RETURN) → Force new pattern immediately
          unsigned long elapsed = millis() - chaosState.patternStartTime;
          const unsigned long MIN_PATTERN_DURATION = 150;
          
          if (elapsed >= MIN_PATTERN_DURATION) {
            chaosState.nextPatternChangeTime = millis();
            engine->debug("💓 PULSE complete after " + String(elapsed) + "ms → force new pattern");
          }
        }
        break;
      }
      
      // NOTE: CHAOS_WAVE removed from this switch - uses continuous calculation above
        
      case CHAOS_PENDULUM: {
        // ✅ FIX: Simple alternation from CENTER (not current position)
        // Pendulum = regular back-and-forth like a metronome
        chaosState.movingForward = !chaosState.movingForward;
        
        // Calculate target from CENTER with fixed amplitude
        if (chaosState.movingForward) {
          chaosState.targetPositionMM = chaos.centerPositionMM + chaosState.waveAmplitude;
        } else {
          chaosState.targetPositionMM = chaos.centerPositionMM - chaosState.waveAmplitude;
        }
        
        // Clamp to limits
        chaosState.targetPositionMM = constrain(
          chaosState.targetPositionMM,
          effectiveMinLimit,
          effectiveMaxLimit
        );
        
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("⚖️ PENDULUM alternate: dir=" + String(chaosState.movingForward ? "UP" : "DOWN") + 
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        break;
      }
        
      case CHAOS_SPIRAL: {
        // ✅ FIX: Calculate progressive spiral radius (10% → 100% → 10%)
        unsigned long elapsed = millis() - chaosState.patternStartTime;
        unsigned long duration = chaosState.nextPatternChangeTime - chaosState.patternStartTime;
        float progress = constrain((float)elapsed / (float)duration, 0.0, 1.0);
        
        // Amplitude progressive: 10% → 100% → 10%
        float currentRadius;
        if (progress < 0.5) {
          // Phase 1: Expansion (10% → 100%)
          currentRadius = maxPossibleAmplitude * (0.1 + 0.9 * (progress * 2.0));
        } else {
          // Phase 2: Contraction (100% → 10%)
          currentRadius = maxPossibleAmplitude * (1.0 - 0.9 * ((progress - 0.5) * 2.0));
        }
        
        // Alternance simple (depuis CENTRE, pas position actuelle)
        chaosState.movingForward = !chaosState.movingForward;
        
        // Calculate target from CENTER with progressive amplitude
        if (chaosState.movingForward) {
          chaosState.targetPositionMM = chaos.centerPositionMM + currentRadius;
        } else {
          chaosState.targetPositionMM = chaos.centerPositionMM - currentRadius;
        }
        
        // Clamp to limits
        chaosState.targetPositionMM = constrain(
          chaosState.targetPositionMM,
          effectiveMinLimit,
          effectiveMaxLimit
        );
        
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("🌀 SPIRAL: progress=" + String(progress * 100, 0) + "%" +
              " radius=" + String(currentRadius, 1) + "mm" +
              " dir=" + String(chaosState.movingForward ? "UP" : "DOWN") +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        break;
      }
      
      case CHAOS_SWEEP: {
        // ✅ FIX: Sweep alternates between edges (full sweep)
        chaosState.movingForward = !chaosState.movingForward;
        
        // Nouvelle cible: l'autre extrême (depuis CENTRE, pas position actuelle)
        if (chaosState.movingForward) {
          chaosState.targetPositionMM = chaos.centerPositionMM + chaosState.waveAmplitude;
        } else {
          chaosState.targetPositionMM = chaos.centerPositionMM - chaosState.waveAmplitude;
        }
        
        // Clamp to limits
        chaosState.targetPositionMM = constrain(
          chaosState.targetPositionMM,
          effectiveMinLimit,
          effectiveMaxLimit
        );
        
        targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
        
        engine->debug("🌊 SWEEP alternate: " + String(chaosState.movingForward ? "UP" : "DOWN") +
              " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        break;
      }
      
      case CHAOS_CALM: {
        // Breathing pattern with pauses - handled via continuous sinusoidal calculation
        // Similar to WAVE but with smaller amplitude and pauses
        // Handled in the continuous section above, just break here
        break;
      }
      
      case CHAOS_BRUTE_FORCE: {
        // 3-phase battering ram: 0=fast in, 1=slow out, 2=pause
        if (chaosState.brutePhase == 0) {
          // Phase 0 completed (fast in) → Start Phase 1 (slow out)
          chaosState.brutePhase = 1;
          chaosState.movingForward = !chaosState.movingForward;
          
          // Slow return (1-10% speed)
          float craziness = chaos.crazinessPercent / 100.0;
          chaosState.currentSpeedLevel = (0.01 + 0.09 * craziness) * chaos.maxSpeedLevel;
          chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0, MAX_SPEED_LEVEL);
          calculateChaosStepDelay();  // ✅ FIX: Apply new speed to stepDelay
          
          // Target opposite extreme
          float amplitude = chaosState.waveAmplitude;
          if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM + amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          } else {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM - amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          }
          targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
          
          engine->debug("🔨 BRUTE_FORCE Phase 1 (slow out): speed=" + String(chaosState.currentSpeedLevel, 1) +
                " target=" + String(chaosState.targetPositionMM, 1) + "mm");
                
        } else if (chaosState.brutePhase == 1) {
          // Phase 1 completed (slow out) → Start Phase 2 (pause)
          chaosState.brutePhase = 2;
          chaosState.isPaused = true;
          chaosState.pauseStartTime = millis();
          
          engine->debug("🔨 BRUTE_FORCE Phase 2 (pause): " + String(chaosState.pauseDuration) + "ms");
          
        } else {
          // Phase 2 completed (pause) → Start Phase 0 (fast in)
          chaosState.brutePhase = 0;
          chaosState.isPaused = false;
          chaosState.movingForward = !chaosState.movingForward;
          
          // Fast attack (70-100% speed)
          float craziness = chaos.crazinessPercent / 100.0;
          chaosState.currentSpeedLevel = (0.7 + 0.3 * craziness) * chaos.maxSpeedLevel;
          chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0, MAX_SPEED_LEVEL);
          calculateChaosStepDelay();  // ✅ FIX: Apply new speed to stepDelay
          
          // Target opposite extreme
          float amplitude = chaosState.waveAmplitude;
          if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM + amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          } else {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM - amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          }
          targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
          
          engine->debug("🔨 BRUTE_FORCE Phase 0 (fast in): speed=" + String(chaosState.currentSpeedLevel, 1) +
                " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        }
        break;
      }
      
      case CHAOS_LIBERATOR: {
        // 3-phase extraction: 0=slow in, 1=fast out, 2=pause
        if (chaosState.liberatorPhase == 0) {
          // Phase 0 completed (slow in) → Start Phase 1 (fast out)
          chaosState.liberatorPhase = 1;
          chaosState.movingForward = !chaosState.movingForward;
          
          // Fast extraction (70-100% speed)
          float craziness = chaos.crazinessPercent / 100.0;
          chaosState.currentSpeedLevel = (0.7 + 0.3 * craziness) * chaos.maxSpeedLevel;
          chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0, MAX_SPEED_LEVEL);
          calculateChaosStepDelay();  // ✅ FIX: Apply new speed to stepDelay
          
          // Target opposite extreme
          float amplitude = chaosState.waveAmplitude;
          if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM + amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          } else {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM - amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          }
          targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
          
          engine->debug("🔓 LIBERATOR Phase 1 (fast out): speed=" + String(chaosState.currentSpeedLevel, 1) +
                " target=" + String(chaosState.targetPositionMM, 1) + "mm");
                
        } else if (chaosState.liberatorPhase == 1) {
          // Phase 1 completed (fast out) → Start Phase 2 (pause)
          chaosState.liberatorPhase = 2;
          chaosState.isPaused = true;
          chaosState.pauseStartTime = millis();
          
          engine->debug("🔓 LIBERATOR Phase 2 (pause): " + String(chaosState.pauseDuration) + "ms");
          
        } else {
          // Phase 2 completed (pause) → Start Phase 0 (slow in)
          chaosState.liberatorPhase = 0;
          chaosState.isPaused = false;
          chaosState.movingForward = !chaosState.movingForward;
          
          // Slow approach (1-10% speed)
          float craziness = chaos.crazinessPercent / 100.0;
          chaosState.currentSpeedLevel = (0.01 + 0.09 * craziness) * chaos.maxSpeedLevel;
          chaosState.currentSpeedLevel = constrain(chaosState.currentSpeedLevel, 1.0, MAX_SPEED_LEVEL);
          calculateChaosStepDelay();  // ✅ FIX: Apply new speed to stepDelay
          
          // Target opposite extreme
          float amplitude = chaosState.waveAmplitude;
          if (chaosState.movingForward) {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM + amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          } else {
            chaosState.targetPositionMM = constrain(
              chaos.centerPositionMM - amplitude,
              effectiveMinLimit,
              effectiveMaxLimit
            );
          }
          targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
          
          engine->debug("🔓 LIBERATOR Phase 0 (slow in): speed=" + String(chaosState.currentSpeedLevel, 1) +
                " target=" + String(chaosState.targetPositionMM, 1) + "mm");
        }
        break;
      }
        
      default: {
        // ✅ FIX: Discrete patterns (ZIGZAG, SWEEP, PULSE, DRIFT, BURST)
        // Problem: These patterns reach target then BLOCK until nextPatternChangeTime
        // Solution: Force immediate pattern change after minimum duration
        
        // ⚠️ CRITICAL: Skip WAVE - it uses continuous calculation (not discrete targets)
        if (chaosState.currentPattern == CHAOS_WAVE) {
          break;  // WAVE never stops at target
        }
        
        unsigned long elapsed = millis() - chaosState.patternStartTime;
        const unsigned long MIN_PATTERN_DURATION = 150;  // 150ms minimum per pattern
        
        if (elapsed >= MIN_PATTERN_DURATION) {
          // Force immediate pattern change
          chaosState.nextPatternChangeTime = millis();
          
          String patternNames[] = {"ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", "WAVE", "PENDULUM", "SPIRAL", "CALM", "BRUTE_FORCE", "LIBERATOR"};
          engine->debug("🎯 Discrete pattern " + patternNames[chaosState.currentPattern] + 
                " reached target after " + String(elapsed) + "ms → force new pattern");
        }
        // Else: keep waiting at target (short wait acceptable)
        break;
      }
    }
  }
  
  // Execute movement (non-blocking with timing control)
  if (currentStep != targetStep) {
    unsigned long currentMicros = micros();
    if (currentMicros - chaosState.lastStepMicros >= chaosState.stepDelay) {
      chaosState.lastStepMicros = currentMicros;
      doStep();
      
      // Update min/max tracking
      float currentPos = currentStep / (float)STEPS_PER_MM;
      if (currentPos < chaosState.minReachedMM) {
        chaosState.minReachedMM = currentPos;
      }
      if (currentPos > chaosState.maxReachedMM) {
        chaosState.maxReachedMM = currentPos;
      }
    }
  }
}

/**
 * Start chaos mode
 */
void startChaos() {
  if (config.currentState != STATE_READY && config.currentState != STATE_PAUSED) {
    engine->error("❌ Cannot start chaos: system not ready");
    return;
  }
  
  // Stop any other modes ONLY if not being called from sequencer
  // NEW ARCHITECTURE: Check execution context instead of flag
  if (seqState.isRunning && config.executionContext != CONTEXT_SEQUENCER) {
    engine->debug("startChaos(): stopping sequence because chaos started outside of sequencer");
    stopSequenceExecution();
  }
  
  // Validate configuration (use effective max distance to respect limitation)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (chaos.amplitudeMM <= 0 || chaos.amplitudeMM > maxAllowed) {
    engine->error("❌ Invalid amplitude: " + String(chaos.amplitudeMM) + " mm (max: " + String(maxAllowed, 1) + " mm)");
    return;
  }
  
  if (chaos.centerPositionMM < chaos.amplitudeMM || 
      chaos.centerPositionMM > maxAllowed - chaos.amplitudeMM) {
    engine->error("❌ Invalid center: " + String(chaos.centerPositionMM) + 
      " mm (amplitude: " + String(chaos.amplitudeMM) + " mm, max: " + String(maxAllowed, 1) + " mm)");
    return;
  }
  
  // Initialize random seed
  if (chaos.seed == 0) {
    randomSeed(micros());
  } else {
    randomSeed(chaos.seed);
  }
  
  // Initialize state
  chaosState.isRunning = true;
  chaosState.startTime = millis();
  chaosState.minReachedMM = currentStep / (float)STEPS_PER_MM;
  chaosState.maxReachedMM = chaosState.minReachedMM;
  chaosState.patternsExecuted = 0;
  chaosState.lastStepMicros = micros();
  
  // Move to center position first if not already there
  float currentPosMM = currentStep / (float)STEPS_PER_MM;
  if (abs(currentPosMM - chaos.centerPositionMM) > 1.0) {  // More than 1mm away
    engine->info("🎯 Déplacement vers le centre: " + String(chaos.centerPositionMM, 1) + " mm");
    targetStep = (long)(chaos.centerPositionMM * STEPS_PER_MM);
    
    // Wait until reached center (blocking for initial positioning)
    Motor.enable();
    unsigned long positioningStart = millis();
    unsigned long lastWsService = millis();
    while (currentStep != targetStep && (millis() - positioningStart < 30000)) {  // 30s timeout
      doStep();
      delayMicroseconds(500);  // Slow positioning speed
      
      // Service WebSocket every 10ms to prevent disconnection
      if (millis() - lastWsService >= 10) {
        webSocket.loop();
        server.handleClient();
        lastWsService = millis();
      }
      yield();
    }
    
    if (currentStep != targetStep) {
      engine->warn("⚠️ Timeout lors du positionnement au centre");
      engine->error("❌ Chaos mode aborted - failed to reach center position");
      
      // CRITICAL: Reset state properly to avoid "system not ready" errors
      chaosState.isRunning = false;
      config.currentState = STATE_READY;
      Motor.disable();  // Disable motor
      
      // Send error to frontend
      sendError("Impossible d'atteindre le centre - timeout après 30s. Vérifiez que le moteur peut bouger librement.");
      
      return;  // Abort chaos start
    }
  }
  
  // Generate first pattern
  generateChaosPattern();
  
  // Calculate step delay for chaos mode
  calculateChaosStepDelay();
  
  targetStep = (long)(chaosState.targetPositionMM * STEPS_PER_MM);
  
  // ✅ FIX P2a: Log first pattern details (like subsequent patterns)
  String patternNames[] = {"ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", "WAVE", "PENDULUM", "SPIRAL", "CALM", "BRUTE_FORCE", "LIBERATOR"};
  engine->debug(String("🎲 Pattern: ") + patternNames[chaosState.currentPattern] + 
        " | Speed: " + String(chaosState.currentSpeedLevel, 1) + 
        "/" + String(MAX_SPEED_LEVEL, 0) + " | Delay: " + String(chaosState.stepDelay) + " µs/step");
  
  config.currentState = STATE_RUNNING;
  isPaused = false;
  
  // Set movement type (config.executionContext remains unchanged - can be STANDALONE or SEQUENCER)
  currentMovement = MOVEMENT_CHAOS;
  
  Motor.enable();  // Enable motor
  
  // Log chaos start (avoid String concatenations to prevent memory issues)
  engine->info("🎲 Chaos mode started");
  engine->info("   Centre: " + String(chaos.centerPositionMM, 1) + " mm");
  engine->info("   Amplitude: ±" + String(chaos.amplitudeMM, 1) + " mm");
  engine->info("   Vitesse max: " + String(chaos.maxSpeedLevel, 1) + "/" + String(MAX_SPEED_LEVEL, 0));
  engine->info("   Degré de folie: " + String(chaos.crazinessPercent, 0) + " %");
  if (chaos.durationSeconds > 0) {
    engine->info("   Durée: " + String(chaos.durationSeconds) + " s");
  } else {
    engine->info("   Durée: INFINIE");
  }
  if (chaos.seed > 0) {
    engine->info("   Seed: " + String(chaos.seed));
  }
}

/**
 * Stop chaos mode
 */
void stopChaos() {
  if (!chaosState.isRunning) return;
  
  chaosState.isRunning = false;
  
  engine->info(String("🛑 Chaos mode stopped:\n") +
        "   Patterns exécutés: " + String(chaosState.patternsExecuted) + "\n" +
        "   Min position: " + String(chaosState.minReachedMM, 1) + " mm\n" +
        "   Max position: " + String(chaosState.maxReachedMM, 1) + " mm\n" +
        "   Plage couverte: " + String(chaosState.maxReachedMM - chaosState.minReachedMM, 1) + " mm");
  
  // Stop movement
  config.currentState = STATE_READY;
  currentMovement = MOVEMENT_VAET;
  isPaused = true;
}

// ============================================================================
// WEB INTERFACE - WebSocket Handlers (Modular Architecture)
// ============================================================================

/**
 * HANDLER 1/7: Basic Commands (calibrate, start, pause, stop, getStatus)
 * Handles fundamental system control commands
 */
bool handleBasicCommands(const char* cmd, JsonDocument& doc) {
  if (strcmp(cmd, "calibrate") == 0) {
    engine->info("Command: Calibration");
    startCalibration();
    return true;
  }
  
  if (strcmp(cmd, "start") == 0) {
    float dist = doc["distance"] | motion.targetDistanceMM;
    float spd = doc["speed"] | motion.speedLevelForward;
    
    String errorMsg;
    // Validate motion range (start + distance must be within effective limit)
    if (!validateAndReport(validateMotionRange(motion.startPositionMM, dist, errorMsg), errorMsg)) return true;
    if (!validateAndReport(validateSpeed(spd, errorMsg), errorMsg)) return true;
    
    engine->info("Command: Start movement (" + String(dist, 1) + "mm @ speed " + String(spd, 1) + ")");
    startMovement(dist, spd);
    return true;
  }
  
  if (strcmp(cmd, "pause") == 0) {
    engine->debug("Command: Pause/Resume");
    togglePause();
    return true;
  }
  
  if (strcmp(cmd, "stop") == 0) {
    engine->info("Command: Stop");
    stopMovement();
    return true;
  }
  
  if (strcmp(cmd, "getStatus") == 0) {
    sendStatus();
    return true;
  }
  
  if (strcmp(cmd, "returnToStart") == 0) {
    engine->debug("Command: Return to start");
    returnToStart();
    return true;
  }
  
  if (strcmp(cmd, "resetTotalDistance") == 0) {
    engine->debug("Command: Reset total distance");
    resetTotalDistance();
    return true;
  }
  
  if (strcmp(cmd, "saveStats") == 0) {
    engine->debug("Command: Save stats");
    saveCurrentSessionStats();
    return true;
  }
  
  if (strcmp(cmd, "setMaxDistanceLimit") == 0) {
    float percent = doc["percent"] | 100.0;
    
    // Validate percent range
    if (percent < 50.0 || percent > 100.0) {
      sendError("⚠️ Limite doit être entre 50% et 100% (reçu: " + String(percent, 0) + "%)");
      return true;
    }
    
    // Only allow changes in READY state
    if (config.currentState != STATE_READY) {
      sendError("⚠️ Modification limite impossible - Système doit être en état PRÊT");
      return true;
    }
    
    maxDistanceLimitPercent = percent;
    updateEffectiveMaxDistance();
    
    engine->info(String("✅ Limite course: ") + String(percent, 0) + "% (" + 
          String(effectiveMaxDistanceMM, 1) + " mm / " + 
          String(config.totalDistanceMM, 1) + " mm)");
    
    sendStatus();
    return true;
  }
  
  return false;  // Command not handled
}

/**
 * HANDLER 2/7: Configuration Commands (setDistance, setSpeed, etc.)
 * Handles parameter updates for motion configuration
 */
bool handleConfigCommands(const char* cmd, JsonDocument& doc) {
  if (strcmp(cmd, "setDistance") == 0) {
    float dist = doc["distance"] | 0.0;
    
    String errorMsg;
    if (!validateAndReport(validateDistance(dist, errorMsg), errorMsg)) return true;
    
    engine->debug("Command: Set distance (" + String(dist, 1) + "mm)");
    setDistance(dist);
    return true;
  }
  
  if (strcmp(cmd, "setStartPosition") == 0) {
    float startPos = doc["startPosition"] | 0.0;
    
    String errorMsg;
    if (!validateAndReport(validatePosition(startPos, errorMsg), errorMsg)) return true;
    
    engine->debug("Command: Set start position (" + String(startPos, 1) + "mm)");
    setStartPosition(startPos);
    return true;
  }
  
  if (strcmp(cmd, "setSpeedForward") == 0) {
    float spd = doc["speed"] | 5.0;
    
    String errorMsg;
    if (!validateAndReport(validateSpeed(spd, errorMsg), errorMsg)) return true;
    
    engine->debug("Command: Set forward speed (" + String(spd, 1) + ")");
    setSpeedForward(spd);
    return true;
  }
  
  if (strcmp(cmd, "setSpeedBackward") == 0) {
    float spd = doc["speed"] | 5.0;
    
    String errorMsg;
    if (!validateAndReport(validateSpeed(spd, errorMsg), errorMsg)) return true;
    
    engine->debug("Command: Set backward speed (" + String(spd, 1) + ")");
    setSpeedBackward(spd);
    return true;
  }
  
  return false;
}

/**
 * HANDLER 3/7: Deceleration Zone Commands
 * Handles deceleration zone configuration
 */
bool handleDecelZoneCommands(const char* cmd, JsonDocument& doc, const String& message) {
  if (message.indexOf("\"cmd\":\"setDecelZone\"") > 0) {
    // Extract boolean values
    decelZone.enabled = message.indexOf("\"enabled\":true") > 0;
    decelZone.enableStart = doc["enableStart"] | decelZone.enableStart;
    decelZone.enableEnd = doc["enableEnd"] | decelZone.enableEnd;
    
    // Extract numeric values
    float zoneMM = doc["zoneMM"] | decelZone.zoneMM;
    float effectPercent = doc["effectPercent"] | decelZone.effectPercent;
    int modeValue = doc["mode"] | (int)decelZone.mode;
    
    if (zoneMM > 0) decelZone.zoneMM = zoneMM;
    if (effectPercent >= 0 && effectPercent <= 100) decelZone.effectPercent = effectPercent;
    if (modeValue >= 0 && modeValue <= 3) decelZone.mode = (DecelMode)modeValue;
    
    validateDecelZone();
    
    // Compact single-line log
    String zones = "";
    if (decelZone.enableStart) zones += "START ";
    if (decelZone.enableEnd) zones += "END";
    engine->debug("✅ Decel config: " + String(decelZone.enabled ? "ON" : "OFF") + 
          (decelZone.enabled ? " | zones=" + zones + "| size=" + String(decelZone.zoneMM, 1) + 
           "mm | effect=" + String(decelZone.effectPercent, 0) + "%" : ""));
    
    sendStatus();
    return true;
  }
  
  return false;
}

/**
 * HANDLER 3.5/7: Cycle Pause Commands (Mode Simple + Oscillation)
 * Handles pause between cycles configuration
 */
bool handleCyclePauseCommands(const char* cmd, JsonDocument& doc) {
  if (strcmp(cmd, "updateCyclePause") == 0) {
    bool enabled = doc["enabled"] | false;
    bool isRandom = doc["isRandom"] | false;
    float pauseDurationSec = doc["pauseDurationSec"] | 1.5;
    float minPauseSec = doc["minPauseSec"] | 1.5;
    float maxPauseSec = doc["maxPauseSec"] | 5.0;
    
    // Validate values
    if (minPauseSec < 0.1) minPauseSec = 0.1;
    if (maxPauseSec < minPauseSec) maxPauseSec = minPauseSec + 0.5;
    if (pauseDurationSec < 0.1) pauseDurationSec = 0.1;
    
    // Apply to Motion (Mode Simple VAET)
    motion.cyclePause.enabled = enabled;
    motion.cyclePause.isRandom = isRandom;
    motion.cyclePause.pauseDurationSec = pauseDurationSec;
    motion.cyclePause.minPauseSec = minPauseSec;
    motion.cyclePause.maxPauseSec = maxPauseSec;
    
    engine->info(String("⏸️ Pause cycle VAET: ") + (enabled ? "activée" : "désactivée") +
          " | Mode: " + (isRandom ? "aléatoire" : "fixe") +
          " | " + (isRandom ? String(minPauseSec, 1) + "-" + String(maxPauseSec, 1) + "s" : String(pauseDurationSec, 1) + "s"));
    
    sendStatus();
    return true;
  }
  
  if (strcmp(cmd, "updateCyclePauseOsc") == 0) {
    bool enabled = doc["enabled"] | false;
    bool isRandom = doc["isRandom"] | false;
    float pauseDurationSec = doc["pauseDurationSec"] | 1.5;
    float minPauseSec = doc["minPauseSec"] | 1.5;
    float maxPauseSec = doc["maxPauseSec"] | 5.0;
    
    // Validate values
    if (minPauseSec < 0.1) minPauseSec = 0.1;
    if (maxPauseSec < minPauseSec) maxPauseSec = minPauseSec + 0.5;
    if (pauseDurationSec < 0.1) pauseDurationSec = 0.1;
    
    // Apply to Oscillation
    oscillation.cyclePause.enabled = enabled;
    oscillation.cyclePause.isRandom = isRandom;
    oscillation.cyclePause.pauseDurationSec = pauseDurationSec;
    oscillation.cyclePause.minPauseSec = minPauseSec;
    oscillation.cyclePause.maxPauseSec = maxPauseSec;
    
    engine->info(String("⏸️ Pause cycle OSC: ") + (enabled ? "activée" : "désactivée") +
          " | Mode: " + (isRandom ? "aléatoire" : "fixe") +
          " | " + (isRandom ? String(minPauseSec, 1) + "-" + String(maxPauseSec, 1) + "s" : String(pauseDurationSec, 1) + "s"));
    
    sendStatus();
    return true;
  }
  
  return false;
}

/**
 * HANDLER 4/7: Pursuit Mode Commands
 * Handles real-time position tracking mode
 */
bool handlePursuitCommands(const char* cmd, JsonDocument& doc) {
  if (strcmp(cmd, "enablePursuitMode") == 0) {
    if (config.currentState == STATE_CALIBRATING) {
      sendError("⚠️ Impossible d'activer le mode Pursuit: calibration en cours");
      return true;
    }
    if (config.currentState == STATE_ERROR) {
      sendError("⚠️ Impossible d'activer le mode Pursuit: système en état erreur");
      return true;
    }
    
    if (seqState.isRunning) {
      stopSequenceExecution();
    }
    
    // Set movement type and context
    currentMovement = MOVEMENT_PURSUIT;
    config.executionContext = CONTEXT_STANDALONE;
    
    if (config.currentState == STATE_RUNNING) {
      config.currentState = STATE_READY;
    }
    
    engine->debug("✅ Mode Pursuit activé");
    sendStatus();
    return true;
  }
  
  if (strcmp(cmd, "pursuitMove") == 0) {
    if (currentMovement != MOVEMENT_PURSUIT) {
      engine->warn("pursuitMove ignored: not in PURSUIT mode");
      return true;
    }
    if (config.currentState == STATE_CALIBRATING) {
      engine->warn("pursuitMove ignored: calibration in progress");
      return true;
    }
    
    float targetPos = doc["targetPosition"] | 0.0;
    float maxSpd = doc["maxSpeed"] | 10.0;
    
    String errorMsg;
    // Note: Position validation removed - pursuitMove() will clamp to limits silently
    if (!validateAndReport(validateSpeed(maxSpd, errorMsg), errorMsg)) return true;
    
    pursuitMove(targetPos, maxSpd);
    return true;
  }
  
  return false;
}

/**
 * HANDLER 5/7: Chaos Mode Commands
 * Handles random pattern generation mode
 */
bool handleChaosCommands(const char* cmd, JsonDocument& doc, const String& message) {
  if (message.indexOf("\"cmd\":\"startChaos\"") > 0) {
    if (config.currentState == STATE_CALIBRATING) {
      sendError("⚠️ Impossible de démarrer le mode Chaos: calibration en cours");
      return true;
    }
    if (config.currentState == STATE_ERROR) {
      sendError("⚠️ Impossible de démarrer le mode Chaos: système en état erreur");
      return true;
    }
    
    // Parse configuration (use effective max distance for default center)
    float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
    chaos.centerPositionMM = doc["centerPositionMM"] | (effectiveMax / 2.0);
    chaos.amplitudeMM = doc["amplitudeMM"] | 50.0;
    chaos.maxSpeedLevel = doc["maxSpeedLevel"] | 10.0;
    chaos.crazinessPercent = doc["crazinessPercent"] | 50.0;
    chaos.durationSeconds = doc["durationSeconds"] | 60;
    chaos.seed = doc["seed"] | (int)millis();
    
    String errorMsg;
    if (!validateAndReport(validateChaosParams(chaos.centerPositionMM, chaos.amplitudeMM, chaos.maxSpeedLevel, chaos.crazinessPercent, errorMsg), errorMsg)) {
      return true;
    }
    
    // Parse patterns array (11 patterns: ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR)
    JsonArray patternsArray = doc["patternsEnabled"];
    if (patternsArray) {
      int idx = 0;
      for (JsonVariant v : patternsArray) {
        if (idx < 11) {
          chaos.patternsEnabled[idx] = v.as<bool>();
          idx++;
        }
      }
    }
    
    engine->info("🌪️ Starting Chaos mode: center=" + String(chaos.centerPositionMM, 1) + 
          "mm, amplitude=±" + String(chaos.amplitudeMM, 1) + "mm, speed=" + 
          String(chaos.maxSpeedLevel, 1) + ", craziness=" + String(chaos.crazinessPercent, 0) + "%");
    
  startChaos();
  engine->debug(String("startChaos() called from WS cmd - config.executionContext=") + executionContextName(config.executionContext) + " seqState.isRunning=" + String(seqState.isRunning));
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"stopChaos\"") > 0) {
    stopChaos();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"setChaosConfig\"") > 0) {
    // Update configuration (live adjustments)
    chaos.centerPositionMM = doc["centerPositionMM"] | chaos.centerPositionMM;
    chaos.amplitudeMM = doc["amplitudeMM"] | chaos.amplitudeMM;
    chaos.maxSpeedLevel = doc["maxSpeedLevel"] | chaos.maxSpeedLevel;
    chaos.crazinessPercent = doc["crazinessPercent"] | chaos.crazinessPercent;
    chaos.durationSeconds = doc["durationSeconds"] | chaos.durationSeconds;
    
    String errorMsg;
    if (!validateAndReport(validateChaosParams(chaos.centerPositionMM, chaos.amplitudeMM, chaos.maxSpeedLevel, chaos.crazinessPercent, errorMsg), errorMsg)) {
      return true;
    }
    
    if (!chaosState.isRunning) {
      chaos.seed = doc["seed"] | chaos.seed;
    }
    
    // Parse patterns array (11 patterns: ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR)
    JsonArray patternsArray = doc["patternsEnabled"];
    if (patternsArray) {
      int idx = 0;
      for (JsonVariant v : patternsArray) {
        if (idx < 11) {
          chaos.patternsEnabled[idx] = v.as<bool>();
          idx++;
        }
      }
    }
    
    // Compact single-line log
    engine->debug("✅ Chaos config: center=" + String(chaos.centerPositionMM, 1) + "mm | amp=±" + 
          String(chaos.amplitudeMM, 1) + "mm | speed=" + String(chaos.maxSpeedLevel, 1) + 
          "/" + String(MAX_SPEED_LEVEL, 0) + " | craziness=" + String(chaos.crazinessPercent, 0) + "% | duration=" + 
          String(chaos.durationSeconds) + "s" + (chaosState.isRunning ? " | ✓ Applied live" : 
          " | seed=" + String(chaos.seed)));
    
    if (chaosState.isRunning) {
      chaosState.nextPatternChangeTime = millis();
    }
    
    sendStatus();
    return true;
  }
  
  return false;
}

/**
 * HANDLER 6/7: Oscillation Mode Commands
 * Handles sinusoidal/triangle/square wave motion
 */
bool handleOscillationCommands(const char* cmd, JsonDocument& doc, const String& message) {
  if (message.indexOf("\"cmd\":\"setOscillation\"") > 0) {
    // Save old values for logging and real-time update detection
    float oldCenter = oscillation.centerPositionMM;
    float oldAmplitude = oscillation.amplitudeMM;
    float oldFrequency = oscillation.frequencyHz;
    OscillationWaveform oldWaveform = oscillation.waveform;
    
    oscillation.centerPositionMM = doc["centerPositionMM"] | oscillation.centerPositionMM;
    oscillation.amplitudeMM = doc["amplitudeMM"] | oscillation.amplitudeMM;
    oscillation.waveform = (OscillationWaveform)(doc["waveform"] | (int)oscillation.waveform);
    oscillation.frequencyHz = doc["frequencyHz"] | oscillation.frequencyHz;
    
    oscillation.enableRampIn = doc["enableRampIn"] | oscillation.enableRampIn;
    oscillation.rampInDurationMs = doc["rampInDurationMs"] | oscillation.rampInDurationMs;
    oscillation.enableRampOut = doc["enableRampOut"] | oscillation.enableRampOut;
    oscillation.rampOutDurationMs = doc["rampOutDurationMs"] | oscillation.rampOutDurationMs;
    
    oscillation.cycleCount = doc["cycleCount"] | oscillation.cycleCount;
    oscillation.returnToCenter = doc["returnToCenter"] | oscillation.returnToCenter;
    
    // Cycle pause parameters (if provided)
    if (doc["cyclePauseEnabled"].is<bool>()) {
      oscillation.cyclePause.enabled = doc["cyclePauseEnabled"];
      oscillation.cyclePause.isRandom = doc["cyclePauseIsRandom"] | false;
      oscillation.cyclePause.pauseDurationSec = doc["cyclePauseDurationSec"] | 0.0f;
      oscillation.cyclePause.minPauseSec = doc["cyclePauseMinSec"] | 0.5f;
      oscillation.cyclePause.maxPauseSec = doc["cyclePauseMaxSec"] | 3.0f;
      engine->debug("⏸️ OSC cycle pause: enabled=" + String(oscillation.cyclePause.enabled) + 
                    ", random=" + String(oscillation.cyclePause.isRandom) + 
                    ", duration=" + String(oscillation.cyclePause.pauseDurationSec, 1) + "s");
    }
    
    // Check if any oscillation parameter changed
    bool paramsChanged = (oldCenter != oscillation.centerPositionMM || 
                          oldAmplitude != oscillation.amplitudeMM ||
                          oldFrequency != oscillation.frequencyHz ||
                          oldWaveform != oscillation.waveform);
    
    // Log changes for debugging
    if (paramsChanged) {
      engine->debug("📝 OSC config: center=" + String(oscillation.centerPositionMM, 1) + 
            "mm | amp=" + String(oscillation.amplitudeMM, 1) + 
            "mm | freq=" + String(oscillation.frequencyHz, 3) + "Hz" +
            (currentMovement == MOVEMENT_OSC && !isPaused ? " | ⚡ Live update (smooth transition)" : ""));
      
      // 🎯 SMOOTH CENTER TRANSITION: Start center interpolation during active oscillation
      if (currentMovement == MOVEMENT_OSC && !isPaused && oldCenter != oscillation.centerPositionMM) {
        oscillationState.isCenterTransitioning = true;
        oscillationState.centerTransitionStartMs = millis();
        oscillationState.oldCenterMM = oldCenter;
        oscillationState.targetCenterMM = oscillation.centerPositionMM;
        engine->info("🎯 Début transition centre: " + String(oldCenter, 1) + "mm → " + String(oscillation.centerPositionMM, 1) + "mm");
      }
      
      // 🎯 SMOOTH AMPLITUDE TRANSITION: Start amplitude interpolation during active oscillation
      if (currentMovement == MOVEMENT_OSC && !isPaused && oldAmplitude != oscillation.amplitudeMM) {
        oscillationState.isAmplitudeTransitioning = true;
        oscillationState.amplitudeTransitionStartMs = millis();
        oscillationState.oldAmplitudeMM = oldAmplitude;
        oscillationState.targetAmplitudeMM = oscillation.amplitudeMM;
        engine->info("🎯 Début transition amplitude: " + String(oldAmplitude, 1) + "mm → " + String(oscillation.amplitudeMM, 1) + "mm");
      }
      
      // 🔥 REAL-TIME UPDATE: Disable ramps for immediate effect during active oscillation
      if (currentMovement == MOVEMENT_OSC && !isPaused) {
        oscillationState.isRampingIn = false;
        oscillationState.isRampingOut = false;
      }
    }
    
    String errorMsg;
    if (!validateAndReport(validateOscillationParams(oscillation.centerPositionMM, oscillation.amplitudeMM, oscillation.frequencyHz, errorMsg), errorMsg)) {
      return true;
    }
    
    engine->debug("✅ Configuration oscillation mise à jour (prise en compte immédiate)");
    sendStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"setCyclePause\"") > 0) {
    const char* mode = doc["mode"];  // "simple" or "oscillation"
    
    if (mode && strcmp(mode, "simple") == 0) {
      // Configure pause for Simple (VA-ET-VIENT) mode
      motion.cyclePause.enabled = doc["enabled"] | false;
      motion.cyclePause.isRandom = doc["isRandom"] | false;
      motion.cyclePause.pauseDurationSec = doc["durationSec"] | 0.0f;
      
      // 🆕 SÉCURITÉ: Clamp min/max + garantir min ≤ max + limit 60s
      float minSec = doc["minSec"] | 0.5f;
      float maxSec = doc["maxSec"] | 3.0f;
      
      // Forcer min ≤ max
      if (minSec > maxSec) {
        float temp = minSec;
        minSec = maxSec;
        maxSec = temp;
      }
      
      // Limiter à 60s maximum (anti-freeze)
      motion.cyclePause.minPauseSec = min(minSec, 60.0f);
      motion.cyclePause.maxPauseSec = min(maxSec, 60.0f);
      
      engine->debug("⏸️ Simple cycle pause: enabled=" + String(motion.cyclePause.enabled) + 
                    ", random=" + String(motion.cyclePause.isRandom) + 
                    ", duration=" + String(motion.cyclePause.pauseDurationSec, 1) + "s" +
                    ", range=[" + String(motion.cyclePause.minPauseSec, 1) + "s-" + 
                    String(motion.cyclePause.maxPauseSec, 1) + "s]");
    } else if (mode && strcmp(mode, "oscillation") == 0) {
      // Configure pause for Oscillation mode
      oscillation.cyclePause.enabled = doc["enabled"] | false;
      oscillation.cyclePause.isRandom = doc["isRandom"] | false;
      oscillation.cyclePause.pauseDurationSec = doc["durationSec"] | 0.0f;
      
      // 🆕 SÉCURITÉ: Clamp min/max + garantir min ≤ max + limit 60s
      float minSec = doc["minSec"] | 0.5f;
      float maxSec = doc["maxSec"] | 3.0f;
      
      // Forcer min ≤ max
      if (minSec > maxSec) {
        float temp = minSec;
        minSec = maxSec;
        maxSec = temp;
      }
      
      // Limiter à 60s maximum (anti-freeze)
      oscillation.cyclePause.minPauseSec = min(minSec, 60.0f);
      oscillation.cyclePause.maxPauseSec = min(maxSec, 60.0f);
      
      engine->debug("⏸️ OSC cycle pause: enabled=" + String(oscillation.cyclePause.enabled) + 
                    ", random=" + String(oscillation.cyclePause.isRandom) + 
                    ", duration=" + String(oscillation.cyclePause.pauseDurationSec, 1) + "s" +
                    ", range=[" + String(oscillation.cyclePause.minPauseSec, 1) + "s-" + 
                    String(oscillation.cyclePause.maxPauseSec, 1) + "s]");
    }
    
    sendStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"startOscillation\"") > 0) {
    if (config.currentState == STATE_INIT || config.currentState == STATE_CALIBRATING) {
      sendError("⚠️ Calibration requise avant de démarrer l'oscillation");
      return true;
    }
    
    if (seqState.isRunning) {
      stopSequenceExecution();
    }
    
    if (config.currentState == STATE_RUNNING) {
      stopMovement();
      // No delay needed - state already updated
    }
    
    startOscillation();
    sendStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"stopOscillation\"") > 0) {
    if (currentMovement == MOVEMENT_OSC) {
      stopMovement();
      currentMovement = MOVEMENT_VAET;
    }
    
    if (seqState.isRunning) {
      stopSequenceExecution();
    }
    
    sendStatus();
    return true;
  }
  
  return false;
}

/**
 * HANDLER 7/7: Sequencer Commands
 * Handles sequence table management and execution
 * Commands: addSequenceLine, deleteSequenceLine, updateSequenceLine, moveSequenceLine,
 *           duplicateSequenceLine, toggleSequenceLine, clearSequence, getSequenceTable,
 *           startSequence, loopSequence, stopSequence, toggleSequencePause,
 *           skipSequenceLine, exportSequence, importSequence (15 commands total)
 */
bool handleSequencerCommands(const char* cmd, JsonDocument& doc, const String& message) {
  
  // ========================================================================
  // SEQUENCE TABLE MANAGEMENT (CRUD operations)
  // ========================================================================
  
  if (message.indexOf("\"cmd\":\"addSequenceLine\"") > 0) {
    SequenceLine newLine = parseSequenceLineFromJson(doc);
    
    // Validate physical constraints first
    String validationError = validateSequenceLinePhysics(newLine);
    if (validationError.length() > 0) {
      sendError("❌ Ligne invalide : " + validationError);
      return true;
    }
    
    // Validate based on movement type
    String errorMsg;
    if (newLine.movementType == MOVEMENT_VAET) {
      // Validate all VA-ET-VIENT parameters
      if (!validateAndReport(validateSpeed(newLine.speedForward, errorMsg), errorMsg)) return true;
      if (!validateAndReport(validateSpeed(newLine.speedBackward, errorMsg), errorMsg)) return true;
    } else if (newLine.movementType == MOVEMENT_OSC) {
      // Validate oscillation parameters (frequency only - speed is dictated by frequency)
      if (newLine.oscFrequencyHz <= 0 || newLine.oscFrequencyHz > 10.0) {
        sendError("❌ Frequency doit être 0.01-10 Hz");
        return true;
      }
    } else if (newLine.movementType == MOVEMENT_CHAOS) {
      // Validate chaos parameters
      if (!validateAndReport(validateSpeed(newLine.chaosMaxSpeedLevel, errorMsg), errorMsg)) return true;
      if (newLine.chaosDurationSeconds < 1 || newLine.chaosDurationSeconds > 3600) {
        sendError("❌ Duration doit être 1-3600 secondes");
        return true;
      }
    }
    
    // Validate cycle count
    if (newLine.cycleCount < 1 || newLine.cycleCount > 9999) {
      sendError("❌ Cycle count doit être 1-9999 (reçu: " + String(newLine.cycleCount) + ")");
      return true;
    }
    
    addSequenceLine(newLine);
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"deleteSequenceLine\"") > 0) {
    int lineId = doc["lineId"] | -1;
    if (lineId < 0) {
      sendError("❌ Line ID invalide");
      return true;
    }
    deleteSequenceLine(lineId);
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"updateSequenceLine\"") > 0) {
    int lineId = doc["lineId"] | -1;
    engine->info("📝 UPDATE: Received lineId=" + String(lineId));
    
    SequenceLine updatedLine = parseSequenceLineFromJson(doc);
    
    // Validate physical constraints first
    String validationError = validateSequenceLinePhysics(updatedLine);
    if (validationError.length() > 0) {
      sendError("❌ Ligne invalide : " + validationError);
      return true;
    }
    
    engine->info("📝 UPDATE: Looking for lineId=" + String(lineId) + " in table");
    for (int i = 0; i < sequenceLineCount; i++) {
      engine->info("   Line " + String(i) + ": ID=" + String(sequenceTable[i].lineId));
    }
    
    updateSequenceLine(lineId, updatedLine);
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"moveSequenceLine\"") > 0) {
    int lineId = doc["lineId"] | -1;
    int direction = doc["direction"] | 0;
    moveSequenceLine(lineId, direction);
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"duplicateSequenceLine\"") > 0) {
    int lineId = doc["lineId"] | -1;
    duplicateSequenceLine(lineId);
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"toggleSequenceLine\"") > 0) {
    int lineId = doc["lineId"] | -1;
    bool enabled = doc["enabled"] | false;
    toggleSequenceLine(lineId, enabled);
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"clearSequence\"") > 0) {
    clearSequenceTable();
    broadcastSequenceTable();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"getSequenceTable\"") > 0) {
    broadcastSequenceTable();
    return true;
  }
  
  // ========================================================================
  // SEQUENCE EXECUTION CONTROL
  // ========================================================================
  
  if (message.indexOf("\"cmd\":\"startSequence\"") > 0) {
    startSequenceExecution(false);  // Single-read mode
    sendSequenceStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"loopSequence\"") > 0) {
    startSequenceExecution(true);  // Loop mode
    sendSequenceStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"stopSequence\"") > 0) {
    stopSequenceExecution();
    sendSequenceStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"toggleSequencePause\"") > 0) {
    toggleSequencePause();
    sendSequenceStatus();
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"skipSequenceLine\"") > 0) {
    skipToNextSequenceLine();
    sendSequenceStatus();
    return true;
  }
  
  // ========================================================================
  // IMPORT/EXPORT
  // ========================================================================
  
  if (message.indexOf("\"cmd\":\"exportSequence\"") > 0) {
    sendJsonResponse("exportData", exportSequenceToJson());
    return true;
  }

  if (message.indexOf("\"cmd\":\"toggleDebug\"") > 0) {
    // Toggle between INFO and DEBUG via UtilityEngine
    if (engine) {
      LogLevel current = engine->getLogLevel();
      LogLevel next = (current == LOG_DEBUG) ? LOG_INFO : LOG_DEBUG;
      engine->setLogLevel(next);
      engine->info(String("Log level set to: ") + (next == LOG_DEBUG ? "DEBUG" : "INFO"));
    }
    return true;
  }
  
  if (message.indexOf("\"cmd\":\"importSequence\"") > 0) {
    int dataStart = message.indexOf("\"jsonData\":\"");
    if (dataStart > 0) {
      dataStart += 12;  // Skip past "jsonData":"
      int dataEnd = message.lastIndexOf("\"}");
      
      if (dataEnd > dataStart) {
        String jsonData = message.substring(dataStart, dataEnd);
        
        // Unescape JSON
        jsonData.replace("\\\"", "\"");
        jsonData.replace("\\n", "");
        jsonData.replace("\\r", "");
        jsonData.replace("\\t", "");
        
        importSequenceFromJson(jsonData);
        broadcastSequenceTable();
      } else {
        sendError("❌ Erreur parsing JSON: dataEnd invalide");
      }
    } else {
      sendError("❌ Erreur parsing JSON: champ jsonData introuvable");
    }
    return true;
  }
  
  // ========================================================================
  // STATS ON-DEMAND COMMAND (Phase 2 Optimization)
  // ========================================================================
  
  if (message.indexOf("\"cmd\":\"requestStats\"") > 0) {
    bool enable = doc["enable"] | false;
    statsRequested = enable;
    lastStatsRequestTime = millis();
    
    engine->debug(String("📊 Stats tracking: ") + (enable ? "ENABLED" : "DISABLED"));
    
    // 💾 Save current session stats when opening Stats panel
    if (enable) {
      saveCurrentSessionStats();
      engine->debug("💾 Stats saved on panel open");
      sendStatus();  // Immediately send stats in next status update
    }
    
    return true;
  }
  
  return false;  // Command not handled by this handler
}

// ============================================================================
// WEB INTERFACE - WebSocket Handler (Main Router)
// ============================================================================

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // When a client connects
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    engine->info(String("WebSocket client #") + String(num) + " connected from " + 
          String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]));
    
    // Don't send status automatically - let client request it via getStatus command
    // This ensures JS is fully loaded before receiving status data
  }
  
  // When a client disconnects
  if (type == WStype_DISCONNECTED) {
    engine->info(String("WebSocket client #") + String(num) + " disconnected");
    
    // Save session stats on disconnect (user closed browser, etc.)
    saveCurrentSessionStats();
  }
  
  if (type == WStype_TEXT) {
    String message = String((char*)payload);
    
    // Parse JSON with ArduinoJson for robust validation
    JsonDocument doc;  // ArduinoJson v7+ auto-allocates memory
    if (!parseJsonCommand(message, doc)) {
      return;  // Error already logged by parseJsonCommand
    }
    
    const char* cmd = doc["cmd"];
    if (!cmd) {
      engine->warn("Commande WebSocket sans champ 'cmd'");
      return;
    }
    
    // ========================================================================
    // COMMAND ROUTER: Try handlers in order
    // ========================================================================
    // If a handler returns true, the command was processed - stop routing
    
    if (handleBasicCommands(cmd, doc)) return;
    if (handleConfigCommands(cmd, doc)) return;
    if (handleDecelZoneCommands(cmd, doc, message)) return;
    if (handleCyclePauseCommands(cmd, doc)) return;  // NEW: Cycle pause handler
    if (handlePursuitCommands(cmd, doc)) return;
    if (handleChaosCommands(cmd, doc, message)) return;
    if (handleOscillationCommands(cmd, doc, message)) return;
    if (handleSequencerCommands(cmd, doc, message)) return;
    
  }
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

// ============================================================================
// SEQUENCE TABLE MANAGEMENT FUNCTIONS
// ============================================================================

/**
 * Add a new line to the sequence table
 * Returns: lineId if successful, -1 if table is full
 */
int addSequenceLine(SequenceLine newLine) {
  if (sequenceLineCount >= MAX_SEQUENCE_LINES) {
    sendError("❌ Séquenceur plein! Max 20 lignes");
    return -1;
  }
  
  newLine.lineId = nextLineId++;
  sequenceTable[sequenceLineCount] = newLine;
  sequenceLineCount++;
  
  engine->info("✅ Ligne ajoutée: ID=" + String(newLine.lineId) + " | Pos:" + 
        String(newLine.startPositionMM, 1) + "mm, Dist:" + String(newLine.distanceMM, 1) + "mm");
  
  return newLine.lineId;
}

/**
 * Delete a line by ID
 * Returns: true if deleted, false if not found
 */
bool deleteSequenceLine(int lineId) {
  int idx = -1;
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].lineId == lineId) {
      idx = i;
      break;
    }
  }
  
  if (idx == -1) {
    sendError("❌ Ligne non trouvée");
    return false;
  }
  
  // Shift lines down
  for (int i = idx; i < sequenceLineCount - 1; i++) {
    sequenceTable[i] = sequenceTable[i + 1];
  }
  
  sequenceLineCount--;
  engine->info("🗑️ Ligne supprimée: ID=" + String(lineId));
  
  return true;
}

/**
 * Update an existing line
 * Returns: true if updated, false if not found
 */
bool updateSequenceLine(int lineId, SequenceLine updatedLine) {
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].lineId == lineId) {
      updatedLine.lineId = lineId;  // Keep original ID
      sequenceTable[i] = updatedLine;
      
      engine->info("✏️ Ligne mise à jour: ID=" + String(lineId));
      return true;
    }
  }
  
  sendError("❌ Ligne non trouvée");
  return false;
}

/**
 * Move a line up (-1) or down (+1)
 * Returns: true if moved, false if invalid
 */
bool moveSequenceLine(int lineId, int direction) {
  int idx = -1;
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].lineId == lineId) {
      idx = i;
      break;
    }
  }
  
  if (idx == -1) return false;
  
  int newIdx = idx + direction;
  if (newIdx < 0 || newIdx >= sequenceLineCount) {
    return false;  // Out of bounds
  }
  
  // Swap lines
  SequenceLine temp = sequenceTable[idx];
  sequenceTable[idx] = sequenceTable[newIdx];
  sequenceTable[newIdx] = temp;
  
  engine->info(String("↕️ Ligne déplacée: ID=") + String(lineId) + " | " + 
        String(idx + 1) + " → " + String(newIdx + 1));
  
  return true;
}

/**
 * Toggle line enabled/disabled
 */
bool toggleSequenceLine(int lineId, bool enabled) {
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].lineId == lineId) {
      sequenceTable[i].enabled = enabled;
      engine->info(String(enabled ? "✓" : "✗") + " Ligne ID=" + String(lineId) + 
            (enabled ? " activée" : " désactivée"));
      return true;
    }
  }
  return false;
}

/**
 * Duplicate a line
 * Returns: new lineId if successful, -1 if failed
 */
int duplicateSequenceLine(int lineId) {
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].lineId == lineId) {
      SequenceLine duplicate = sequenceTable[i];
      return addSequenceLine(duplicate);
    }
  }
  return -1;
}

/**
 * Validate sequence line against physical constraints
 * Returns error message if invalid, empty string if valid
 */
String validateSequenceLinePhysics(const SequenceLine& line) {
  float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  switch (line.movementType) {
    case MOVEMENT_VAET: {
      // VA-ET-VIENT validation
      float endPosition = line.startPositionMM + line.distanceMM;
      
      if (line.startPositionMM < 0) {
        return "Start position negative";
      }
      if (line.startPositionMM > effectiveMax) {
        return "Start position (" + String(line.startPositionMM, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (endPosition > effectiveMax) {
        return "End position (" + String(endPosition, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (line.distanceMM <= 0) {
        return "Distance must be positive";
      }
      break;
    }
    
    case MOVEMENT_OSC: {
      // OSCILLATION validation
      float minPos = line.oscCenterPositionMM - line.oscAmplitudeMM;
      float maxPos = line.oscCenterPositionMM + line.oscAmplitudeMM;
      
      if (minPos < 0) {
        return "Oscillation min position (" + String(minPos, 1) + "mm) is negative";
      }
      if (maxPos > effectiveMax) {
        return "Oscillation max position (" + String(maxPos, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (line.oscAmplitudeMM <= 0) {
        return "Amplitude must be positive";
      }
      break;
    }
    
    case MOVEMENT_CHAOS: {
      // CHAOS validation
      float minPos = line.chaosCenterPositionMM - line.chaosAmplitudeMM;
      float maxPos = line.chaosCenterPositionMM + line.chaosAmplitudeMM;
      
      if (minPos < 0) {
        return "Chaos min position (" + String(minPos, 1) + "mm) is negative";
      }
      if (maxPos > effectiveMax) {
        return "Chaos max position (" + String(maxPos, 1) + "mm) exceeds available course (" + String(effectiveMax, 1) + "mm)";
      }
      if (line.chaosAmplitudeMM <= 0) {
        return "Amplitude must be positive";
      }
      break;
    }
  }
  
  return "";  // Valid
}

/**
 * Parse SequenceLine fields from JSON object (Refactoring utility)
 * Eliminates duplication in addSequenceLine, updateSequenceLine, and importSequenceFromJson
 * 
 * @param obj JsonObject or JsonVariant containing sequence line fields
 * @return SequenceLine with all fields populated from JSON (with defaults)
 */
SequenceLine parseSequenceLineFromJson(JsonVariantConst obj) {
  SequenceLine line;
  
  // Common fields
  line.enabled = obj["enabled"] | true;
  line.movementType = (MovementType)(obj["movementType"] | 0);
  
  // Cycle count: always 1 for CALIBRATION, else from JSON
  if (line.movementType == MOVEMENT_CALIBRATION) {
    line.cycleCount = 1;  // Force 1 cycle for calibration
  } else {
    line.cycleCount = obj["cycleCount"] | 1;
  }
  
  // Pause after movement
  line.pauseAfterMs = obj["pauseAfterMs"] | 0;
  
  // VA-ET-VIENT fields
  line.startPositionMM = obj["startPositionMM"] | 0.0;
  line.distanceMM = obj["distanceMM"] | 100.0;
  line.speedForward = obj["speedForward"] | 5.0;
  line.speedBackward = obj["speedBackward"] | 5.0;
  line.decelStartEnabled = obj["decelStartEnabled"] | false;
  line.decelEndEnabled = obj["decelEndEnabled"] | false;
  line.decelZoneMM = obj["decelZoneMM"] | 50.0;
  line.decelEffectPercent = obj["decelEffectPercent"] | 50.0;
  line.decelMode = (DecelMode)(obj["decelMode"] | 0);
  
  // VA-ET-VIENT cycle pause
  line.vaetCyclePauseEnabled = obj["vaetCyclePauseEnabled"] | false;
  line.vaetCyclePauseIsRandom = obj["vaetCyclePauseIsRandom"] | false;
  line.vaetCyclePauseDurationSec = obj["vaetCyclePauseDurationSec"] | 0.0;
  line.vaetCyclePauseMinSec = obj["vaetCyclePauseMinSec"] | 0.5;
  line.vaetCyclePauseMaxSec = obj["vaetCyclePauseMaxSec"] | 3.0;
  
  // OSCILLATION fields (use effective max distance for default center)
  float effectiveMax = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  line.oscCenterPositionMM = obj["oscCenterPositionMM"] | (effectiveMax / 2.0);
  line.oscAmplitudeMM = obj["oscAmplitudeMM"] | 50.0;
  line.oscWaveform = (OscillationWaveform)(obj["oscWaveform"] | 0);
  line.oscFrequencyHz = obj["oscFrequencyHz"] | 1.0;
  // Note: oscSpeedLevel removed - speed is now dictated by frequency
  line.oscEnableRampIn = obj["oscEnableRampIn"] | false;
  line.oscEnableRampOut = obj["oscEnableRampOut"] | false;
  line.oscRampInDurationMs = obj["oscRampInDurationMs"] | 1000.0;
  line.oscRampOutDurationMs = obj["oscRampOutDurationMs"] | 1000.0;
  
  // OSCILLATION cycle pause
  line.oscCyclePauseEnabled = obj["oscCyclePauseEnabled"] | false;
  line.oscCyclePauseIsRandom = obj["oscCyclePauseIsRandom"] | false;
  line.oscCyclePauseDurationSec = obj["oscCyclePauseDurationSec"] | 0.0;
  line.oscCyclePauseMinSec = obj["oscCyclePauseMinSec"] | 0.5;
  line.oscCyclePauseMaxSec = obj["oscCyclePauseMaxSec"] | 3.0;
  
  // CHAOS fields (use effective max distance for default center)
  line.chaosCenterPositionMM = obj["chaosCenterPositionMM"] | (effectiveMax / 2.0);
  line.chaosAmplitudeMM = obj["chaosAmplitudeMM"] | 50.0;
  line.chaosMaxSpeedLevel = obj["chaosMaxSpeedLevel"] | 10.0;
  line.chaosCrazinessPercent = obj["chaosCrazinessPercent"] | 50.0;
  line.chaosDurationSeconds = obj["chaosDurationSeconds"] | 30UL;
  line.chaosSeed = obj["chaosSeed"] | 0UL;
  
  // Parse patterns array if present (11 patterns: ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR)
  JsonVariantConst patternsVar = obj["chaosPatternsEnabled"];
  if (patternsVar.is<JsonArrayConst>()) {
    JsonArrayConst patterns = patternsVar.as<JsonArrayConst>();
    int idx = 0;
    for (JsonVariantConst val : patterns) {
      if (idx < 11) line.chaosPatternsEnabled[idx++] = val.as<bool>();
    }
  } else {
    // Default: all patterns enabled
    for (int i = 0; i < 11; i++) {
      line.chaosPatternsEnabled[i] = true;
    }
  }
  
  return line;
}

/**
 * Generic JSON response builder
 * Standardizes JSON response format and broadcasting
 * 
 * @param type Response type (e.g., "sequenceTable", "exportData", "status")
 * @param data JSON data content (already formatted)
 */
void sendJsonResponse(const char* type, const String& data) {
  // Only broadcast if clients are connected
  if (webSocket.connectedClients() > 0) {
    String response = "{\"type\":\"" + String(type) + "\",\"data\":" + data + "}";
    webSocket.broadcastTXT(response);
  }
}

/**
 * Broadcast sequence table to all WebSocket clients
 */
void broadcastSequenceTable() {
  sendJsonResponse("sequenceTable", exportSequenceToJson());
}

/**
 * Clear entire sequence table
 */
void clearSequenceTable() {
  sequenceLineCount = 0;
  nextLineId = 1;
  engine->info("🗑️ Tableau vidé");
}

/**
 * Export sequence table to JSON (ArduinoJson optimized)
 */
String exportSequenceToJson() {
  JsonDocument doc;
  
  doc["version"] = "2.0";  // Version 2.0 with oscillation support
  doc["lineCount"] = sequenceLineCount;
  
  JsonArray linesArray = doc["lines"].to<JsonArray>();
  
  for (int i = 0; i < sequenceLineCount; i++) {
    SequenceLine* line = &sequenceTable[i];
    
    JsonObject lineObj = linesArray.add<JsonObject>();
    
    // Common fields
    lineObj["lineId"] = line->lineId;
    lineObj["enabled"] = line->enabled;
    lineObj["movementType"] = (int)line->movementType;
    
    // VA-ET-VIENT fields
    lineObj["startPositionMM"] = serialized(String(line->startPositionMM, 1));
    lineObj["distanceMM"] = serialized(String(line->distanceMM, 1));
    lineObj["speedForward"] = serialized(String(line->speedForward, 1));
    lineObj["speedBackward"] = serialized(String(line->speedBackward, 1));
    lineObj["decelStartEnabled"] = line->decelStartEnabled;
    lineObj["decelEndEnabled"] = line->decelEndEnabled;
    lineObj["decelZoneMM"] = serialized(String(line->decelZoneMM, 1));
    lineObj["decelEffectPercent"] = serialized(String(line->decelEffectPercent, 0));
    lineObj["decelMode"] = (int)line->decelMode;
    
    // VA-ET-VIENT cycle pause
    lineObj["vaetCyclePauseEnabled"] = line->vaetCyclePauseEnabled;
    lineObj["vaetCyclePauseIsRandom"] = line->vaetCyclePauseIsRandom;
    lineObj["vaetCyclePauseDurationSec"] = serialized(String(line->vaetCyclePauseDurationSec, 1));
    lineObj["vaetCyclePauseMinSec"] = serialized(String(line->vaetCyclePauseMinSec, 1));
    lineObj["vaetCyclePauseMaxSec"] = serialized(String(line->vaetCyclePauseMaxSec, 1));
    
    // OSCILLATION fields
    lineObj["oscCenterPositionMM"] = serialized(String(line->oscCenterPositionMM, 1));
    lineObj["oscAmplitudeMM"] = serialized(String(line->oscAmplitudeMM, 1));
    lineObj["oscWaveform"] = (int)line->oscWaveform;
    lineObj["oscFrequencyHz"] = serialized(String(line->oscFrequencyHz, 3));
    lineObj["oscEnableRampIn"] = line->oscEnableRampIn;
    lineObj["oscEnableRampOut"] = line->oscEnableRampOut;
    lineObj["oscRampInDurationMs"] = serialized(String(line->oscRampInDurationMs, 0));
    lineObj["oscRampOutDurationMs"] = serialized(String(line->oscRampOutDurationMs, 0));
    
    // OSCILLATION cycle pause
    lineObj["oscCyclePauseEnabled"] = line->oscCyclePauseEnabled;
    lineObj["oscCyclePauseIsRandom"] = line->oscCyclePauseIsRandom;
    lineObj["oscCyclePauseDurationSec"] = serialized(String(line->oscCyclePauseDurationSec, 1));
    lineObj["oscCyclePauseMinSec"] = serialized(String(line->oscCyclePauseMinSec, 1));
    lineObj["oscCyclePauseMaxSec"] = serialized(String(line->oscCyclePauseMaxSec, 1));
    
    // CHAOS fields
    lineObj["chaosCenterPositionMM"] = serialized(String(line->chaosCenterPositionMM, 1));
    lineObj["chaosAmplitudeMM"] = serialized(String(line->chaosAmplitudeMM, 1));
    lineObj["chaosMaxSpeedLevel"] = serialized(String(line->chaosMaxSpeedLevel, 1));
    lineObj["chaosCrazinessPercent"] = serialized(String(line->chaosCrazinessPercent, 1));
    lineObj["chaosDurationSeconds"] = line->chaosDurationSeconds;
    lineObj["chaosSeed"] = line->chaosSeed;
    
    JsonArray patternsArray = lineObj["chaosPatternsEnabled"].to<JsonArray>();
    for (int p = 0; p < 11; p++) {  // 11 patterns: ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR
      patternsArray.add(line->chaosPatternsEnabled[p]);
    }
    
    // COMMON fields
    lineObj["cycleCount"] = line->cycleCount;
    lineObj["pauseAfterMs"] = line->pauseAfterMs;
  }
  
  String output;
  serializeJson(doc, output);
  return output;
}

/**
 * Import sequence from JSON
 * Returns: number of lines imported, -1 if error
 */
int importSequenceFromJson(String jsonData) {
  // Debug: Show first 200 chars of received JSON
  engine->debug(String("📤 JSON reçu (") + String(jsonData.length()) + " chars): " + 
        jsonData.substring(0, min(200, (int)jsonData.length())));
  
  // Clear existing table
  clearSequenceTable();
  
  JsonDocument importDoc;
  if (!parseJsonCommand(jsonData, importDoc)) {
    return -1;  // Error already logged
  }
  
  // Validate lineCount
  int lineCount = importDoc["lineCount"] | 0;
  if (lineCount <= 0 || lineCount > MAX_SEQUENCE_LINES) {
    sendError("❌ JSON invalide ou trop de lignes");
    return -1;
  }
  
  // Validate lines array exists (ArduinoJson v7 API)
  if (!importDoc["lines"].is<JsonArray>()) {
    sendError("❌ Array 'lines' non trouvé ou invalide");
    return -1;
  }
  
  JsonArray linesArray = importDoc["lines"].as<JsonArray>();
  engine->info(String("📥 Import: ") + String(lineCount) + " lignes");
  
  // Reset nextLineId to find the maximum
  int maxLineId = 0;
  int importedCount = 0;
  
  // Parse each line object from array
  for (JsonObject lineObj : linesArray) {
    if (sequenceLineCount >= MAX_SEQUENCE_LINES) {
      engine->warn("⚠️ Table pleine, arrêt import");
      break;
    }
    
    // Parse line data using factorized helper
    SequenceLine newLine = parseSequenceLineFromJson(lineObj);
    
    // lineId must be parsed separately as it's not part of the standard parsing
    newLine.lineId = lineObj["lineId"] | 0;
    
    // Track maximum lineId for nextLineId update
    if (newLine.lineId > maxLineId) {
      maxLineId = newLine.lineId;
    }
    
    // Add to sequence table
    sequenceTable[sequenceLineCount] = newLine;
    sequenceLineCount++;
    importedCount++;
  }
  
  // Update nextLineId to avoid ID conflicts when adding new lines
  nextLineId = maxLineId + 1;
  
  engine->info(String("✅ ") + String(importedCount) + " lignes importées");
  engine->info(String("📢 nextLineId mis à jour: ") + String(nextLineId));
  
  return importedCount;
}

// ============================================================================
// SEQUENCE EXECUTION FUNCTIONS
// ============================================================================

/**
 * Start sequence execution
 * @param loopMode: false=single read, true=infinite loop
 */
void startSequenceExecution(bool loopMode) {
  // Check if any lines are enabled
  int enabledCount = 0;
  for (int i = 0; i < sequenceLineCount; i++) {
    if (sequenceTable[i].enabled) enabledCount++;
  }
  
  if (enabledCount == 0) {
    sendError("❌ Aucune ligne active à exécuter!");
    return;
  }
  
  if (config.currentState != STATE_READY) {
    sendError("❌ Système pas prêt (calibration requise?)");
    return;
  }
  
  // Initialize execution state
  seqState.isRunning = true;
  seqState.isLoopMode = loopMode;
  seqState.currentLineIndex = 0;
  seqState.currentCycleInLine = 0;
  seqState.isPaused = false;
  seqState.isWaitingPause = false;
  seqState.loopCount = 0;
  seqState.sequenceStartTime = millis();
  
  // Set execution context
  config.executionContext = CONTEXT_SEQUENCER;
  currentMovement = MOVEMENT_VAET;
  
  // Skip to first enabled line
  while (seqState.currentLineIndex < sequenceLineCount && 
         !sequenceTable[seqState.currentLineIndex].enabled) {
    seqState.currentLineIndex++;
  }
  
  engine->info(String("═══════════════════════════════════════════\n") +
        "▶️ SÉQUENCE DÉMARRÉE - Mode: " + (loopMode ? "BOUCLE INFINIE" : "LECTURE UNIQUE") + "\n" +
        "   isLoopMode = " + (seqState.isLoopMode ? "TRUE" : "FALSE") + "\n" +
        "   Lignes actives: " + String(enabledCount) + " / " + String(sequenceLineCount) + "\n" +
        "═══════════════════════════════════════════");
}

/**
 * ═══════════════════════════════════════════════════════════════════════════
 * UNIFIED MOVEMENT COMPLETION HANDLER (NEW ARCHITECTURE)
 * ═══════════════════════════════════════════════════════════════════════════
 * This function unifies the "end of movement" logic for all movement types.
 * Replaces scattered if(isFromSequencer) blocks.
 * 
 * SEQUENCER context: Increment cycle counter and set STATE_READY
 * STANDALONE context: Set STATE_IDLE (movement complete, stop)
 */
void onMovementComplete() {
  if (config.executionContext == CONTEXT_SEQUENCER) {
    // Running from sequencer: increment cycle counter
    seqState.currentCycleInLine++;
    config.currentState = STATE_READY;  // Signal sequencer that cycle is complete
    
    engine->info("✅ Cycle terminé - retour séquenceur");
    sendSequenceStatus();
  } else {
    // Standalone mode: movement is complete, return to ready state
    config.currentState = STATE_READY;
    
    // Auto-increment daily statistics with distance traveled
    if (motion.targetDistanceMM > 0) {
      incrementDailyStats(motion.targetDistanceMM);
    }
    
    engine->info("✅ Mouvement terminé (STANDALONE)");
  }
}

/**
 * ═══════════════════════════════════════════════════════════════════════════
 * SEQUENCE POSITIONING HELPER
 * ═══════════════════════════════════════════════════════════════════════════
 * Position the motor at the start of the current sequence line.
 * Called BEFORE starting the first cycle of each line.
 * 
 * Each movement type has a different "start position":
 * - VA-ET-VIENT: startPositionMM
 * - OSCILLATION: centerPositionMM - amplitudeMM (minimum position)
 * - CHAOS: centerPositionMM (center of chaos zone)
 * - CALIBRATION: No positioning needed (calibration handles it)
 */
void positionForNextLine() {
  if (!seqState.isRunning) return;
  if (seqState.currentLineIndex >= sequenceLineCount) return;
  
  SequenceLine* currentLine = &sequenceTable[seqState.currentLineIndex];
  
  float targetPositionMM = 0;
  bool needsPositioning = true;
  
  // Determine target position based on current line's movement type
  switch (currentLine->movementType) {
    case MOVEMENT_VAET:
      targetPositionMM = currentLine->startPositionMM;
      break;
      
    case MOVEMENT_OSC:
      // Start at center position - oscillation always starts from center
      targetPositionMM = currentLine->oscCenterPositionMM;
      break;
      
    case MOVEMENT_CHAOS:
      // Start at center of chaos zone
      targetPositionMM = currentLine->chaosCenterPositionMM;
      break;
      
    case MOVEMENT_CALIBRATION:
      // Calibration will handle positioning itself
      needsPositioning = false;
      break;
  }
  
  if (!needsPositioning) return;
  
  // Validate target position against effective limits
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  if (targetPositionMM < 0) {
    engine->warn("⚠️ Position cible négative (" + String(targetPositionMM, 1) + "mm) - ajustée à 0mm");
    targetPositionMM = 0;
  }
  if (targetPositionMM > maxAllowed) {
    engine->warn("⚠️ Position cible (" + String(targetPositionMM, 1) + "mm) dépasse limite (" + String(maxAllowed, 1) + "mm) - ajustée");
    targetPositionMM = maxAllowed;
  }
  
  // Calculate current position
  float currentPosMM = currentStep / (float)STEPS_PER_MM;
  
  // Only move if we're not already at target (tolerance: 1mm)
  if (abs(currentPosMM - targetPositionMM) > 1.0) {
    engine->info("🎯 Repositionnement: " + String(currentPosMM, 1) + "mm → " + String(targetPositionMM, 1) + "mm");
    
    // ⚠️ CRITICAL: Stop previous movement completely before repositioning
    chaosState.isRunning = false;
    oscillationState.isRampingIn = false;
    oscillationState.isRampingOut = false;
    currentMovement = MOVEMENT_VAET;  // Force back to VAET
    
    targetStep = (long)(targetPositionMM * STEPS_PER_MM);
    
    // Set direction for repositioning
    bool moveForward = (targetStep > currentStep);
    Motor.setDirection(moveForward);
    
    // ⚠️ CRITICAL: Keep motor ENABLED during repositioning - HSS loses position if disabled!
    // Blocking move to target position at moderate speed (990 µs = speed 5.0)
    unsigned long positioningStart = millis();
    unsigned long lastStepTime = micros();
    const unsigned long stepDelay = 990;  // Same speed as normal VAET (5.0)
    
    while (currentStep != targetStep && (millis() - positioningStart < 30000)) {  // 30s timeout
      unsigned long now = micros();
      if (now - lastStepTime >= stepDelay) {
        // Simple step without calling doStep() to avoid triggering cycle detection
        if (moveForward) {
          Motor.step();
          currentStep++;
        } else {
          Motor.step();
          currentStep--;
        }
        lastStepTime = now;
      }
      yield();  // Allow ESP32 to handle other tasks (WebSocket, etc.)
      webSocket.loop();  // Keep WebSocket alive
    }
    
    // ✅ Repositioning complete - restore state
    config.currentState = STATE_READY;
    
    if (currentStep != targetStep) {
      engine->warn("⚠️ Timeout repositionnement - position: " + String(currentStep / (float)STEPS_PER_MM, 1) + "mm");
    } else {
      engine->info("✅ Repositionnement terminé");
    }
  }
}

/**
 * Stop sequence execution
 */
void stopSequenceExecution() {
  if (!seqState.isRunning) return;
  engine->debug("stopSequenceExecution() called - seqState.currentLineIndex=" + String(seqState.currentLineIndex) + " currentCycle=" + String(seqState.currentCycleInLine));
  
  seqState.isRunning = false;
  engine->debug("seqState.isRunning set to false in stopSequenceExecution");
  seqState.isPaused = false;
  seqState.isWaitingPause = false;
  
  // NEW ARCHITECTURE: Reset execution context to standalone
  config.executionContext = CONTEXT_STANDALONE;
  
  // Stop current movement if any
  stopMovement();
  
  unsigned long elapsedSec = (millis() - seqState.sequenceStartTime) / 1000;
  
  String loopInfo = seqState.isLoopMode ? 
    String("\n   Boucles complétées: ") + String(seqState.loopCount) : "";
  
  engine->info(String("═══════════════════════════════════════════\n") +
        "⏹️ SÉQUENCE ARRÊTÉE\n" +
        "   Durée: " + String(elapsedSec) + "s" + loopInfo + "\n" +
        "═══════════════════════════════════════════");
}

/**
 * Pause/Resume sequence execution
 */
void toggleSequencePause() {
  if (!seqState.isRunning) return;
  
  seqState.isPaused = !seqState.isPaused;
  
  if (seqState.isPaused) {
    // Pause current movement
    isPaused = true;
    engine->info("⏸️ Séquence en pause");
  } else {
    // Resume movement
    isPaused = false;
    engine->info("▶️ Séquence reprise");
  }
}

/**
 * Skip to next line in sequence
 */
void skipToNextSequenceLine() {
  if (!seqState.isRunning) return;
  
  // Force current line to complete
  seqState.currentCycleInLine = sequenceTable[seqState.currentLineIndex].cycleCount;
  stopMovement();
  
  engine->info("⏭️ Ligne suivante...");
}

/**
 * Send sequence status via WebSocket (ArduinoJson optimized)
 */
void sendSequenceStatus() {
  // Only broadcast if clients are connected (early exit)
  if (webSocket.connectedClients() == 0) return;
  
  JsonDocument doc;
  
  doc["type"] = "sequenceStatus";
  doc["isRunning"] = seqState.isRunning;
  doc["isLoopMode"] = seqState.isLoopMode;
  doc["isPaused"] = seqState.isPaused;
  doc["currentLineIndex"] = seqState.currentLineIndex;
  doc["currentLineNumber"] = seqState.currentLineIndex + 1;
  doc["totalLines"] = sequenceLineCount;
  
  // Display real cycle info: for OSC, show internal oscillation cycles
  int displayCycle = seqState.currentCycleInLine + 1;  // +1 because cycles are 1-indexed for display
  if (seqState.isRunning && seqState.currentLineIndex < sequenceLineCount) {
    SequenceLine* line = &sequenceTable[seqState.currentLineIndex];
    if (line->movementType == MOVEMENT_OSC) {
      displayCycle = oscillationState.completedCycles + 1;  // Show oscillation's internal cycle count (1-indexed)
    } else if (line->movementType == MOVEMENT_CHAOS) {
      displayCycle = 1;  // Chaos is always 1 cycle (duration-based)
    }
  }
  doc["currentCycle"] = displayCycle;
  doc["loopCount"] = seqState.loopCount;
  
  // Pause remaining time
  if (seqState.isWaitingPause) {
    long remaining = seqState.pauseEndTime - millis();
    doc["pauseRemaining"] = remaining > 0 ? remaining : 0;
  } else {
    doc["pauseRemaining"] = 0;
  }
  
  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
}

/**
 * Check and handle sequence end logic (DRY refactor P2.5)
 * 
 * Handles:
 * - Line completion and advancement
 * - Loop mode restart vs single mode termination
 * - Skipping disabled lines
 * 
 * @return true if sequence continues, false if ended (single mode)
 */
bool checkAndHandleSequenceEnd() {
  // Move to next line and reset cycle counter
  seqState.currentLineIndex++;
  seqState.currentCycleInLine = 0;
  
  // Check if sequence is complete
  if (seqState.currentLineIndex >= sequenceLineCount) {
    
    // Loop mode: restart sequence
    if (seqState.isLoopMode) {
      seqState.currentLineIndex = 0;
      seqState.loopCount++;
      
      engine->info("───────────────────────────────────────────");
      engine->info("🔁 Boucle #" + String(seqState.loopCount) + " terminée - Redémarrage...");
      engine->info("───────────────────────────────────────────");
    }
    // Single read mode: stop
    else {
      unsigned long elapsedSec = (millis() - seqState.sequenceStartTime) / 1000;
      
      engine->info("═══════════════════════════════════════════");
      engine->info("✅ SÉQUENCE TERMINÉE (LECTURE UNIQUE)!");
      engine->info("   Lignes exécutées: " + String(sequenceLineCount));
      engine->info("   Durée totale: " + String(elapsedSec) + "s");
      engine->info("   Mode: " + String(seqState.isLoopMode ? "BOUCLE" : "UNIQUE"));
      engine->info("═══════════════════════════════════════════");
      
      // ⚠️ NOUVELLE ARCHITECTURE: Retour automatique à 0.0mm et nettoyage complet
      seqState.isRunning = false;
      config.executionContext = CONTEXT_STANDALONE;
      
      // Retour au contact START (position 0.0mm) si pas déjà là
      if (currentStep != 0) {
        engine->info("🏠 Retour automatique au contact START...");
        long startReturnStep = currentStep;
        
        Motor.setDirection(false);  // Backward to START
        unsigned long returnStart = millis();
        unsigned long lastStepTime = micros();
        const unsigned long stepDelay = 990;  // Speed 5.0
        
        while (currentStep > 0 && (millis() - returnStart < 30000)) {
          unsigned long now = micros();
          if (now - lastStepTime >= stepDelay) {
            Motor.step();
            currentStep--;
            lastStepTime = now;
          }
          yield();  // Allow ESP32 to handle other tasks
          webSocket.loop();  // Keep WebSocket alive
        }
        
        float returnedMM = (startReturnStep - currentStep) / STEPS_PER_MM;
        engine->info("✓ Retour terminé: " + String(returnedMM, 1) + "mm → Position 0.0mm");
      }
      
      // Nettoyage complet des variables
      currentStep = 0;
      startStep = 0;
      targetStep = 0;
      movingForward = true;
      hasReachedStartStep = false;
      config.currentState = STATE_READY;
      
      engine->info("✓ Système prêt pour le prochain cycle");
      sendSequenceStatus();
      return false;  // Sequence ended
    }
  }
  
  // Skip disabled lines
  while (seqState.currentLineIndex < sequenceLineCount && 
         !sequenceTable[seqState.currentLineIndex].enabled) {
    seqState.currentLineIndex++;
  }
  
  // Check again if we've reached the end after skipping
  if (seqState.currentLineIndex >= sequenceLineCount) {
    if (seqState.isLoopMode) {
      seqState.currentLineIndex = 0;
      seqState.loopCount++;
      engine->info("🔁 Boucle #" + String(seqState.loopCount) + " - Redémarrage...");
    } else {
      seqState.isRunning = false;
      
      // ⚠️ CRITICAL: Stop motor and reset execution context
      stopMovement();
      config.executionContext = CONTEXT_STANDALONE;
      
      sendSequenceStatus();
      return false;  // Sequence ended
    }
  }
  
  return true;  // Sequence continues
}

/**
 * Process sequence execution (called in main loop)
 * This advances the sequence through lines and manages timing
 */
void processSequenceExecution() {
  if (!seqState.isRunning || seqState.isPaused) {
    return;
  }
  
  // Handle pause between lines (temporization)
  if (seqState.isWaitingPause) {
    if (millis() >= seqState.pauseEndTime) {
      seqState.isWaitingPause = false;
      
      // Use helper to advance to next line (DRY)
      if (!checkAndHandleSequenceEnd()) {
        return;  // Sequence ended
      }
      
      // Continue to start the next line's first cycle
      // (will be handled by the code below)
    } else {
      // Still waiting, send status update every 500ms
      static unsigned long lastStatusSend = 0;
      if (millis() - lastStatusSend > SEQUENCE_STATUS_UPDATE_MS) {
        sendSequenceStatus();
        lastStatusSend = millis();
      }
      return;
    }
  }
  
  // Check if current movement is complete and we can start next action
  if (config.currentState == STATE_READY && !seqState.isWaitingPause) {
    
    SequenceLine* currentLine = &sequenceTable[seqState.currentLineIndex];
    
    // Determine effective cycle count for sequencer
    // OSC and CHAOS manage their own internal cycles, so sequencer sees them as 1 cycle
    int effectiveCycleCount = currentLine->cycleCount;
    if (currentLine->movementType == MOVEMENT_OSC || currentLine->movementType == MOVEMENT_CHAOS) {
      effectiveCycleCount = 1;  // They handle cycles internally
    }
    
    // Check if all cycles for current line are complete
    // currentCycleInLine starts at 0, incremented in onMovementComplete() after each cycle
    if (seqState.currentCycleInLine >= effectiveCycleCount) {
      // Line complete - all cycles executed

      
      // Apply pause after line if configured
      if (currentLine->pauseAfterMs > 0) {
        seqState.isWaitingPause = true;
        seqState.pauseEndTime = millis() + currentLine->pauseAfterMs;
        
        engine->info("⏸️ Pause ligne: " + String(currentLine->pauseAfterMs / 1000.0, 1) + "s");
        
        sendSequenceStatus();
        return;
      }
      
      // Move to next line
      if (!checkAndHandleSequenceEnd()) {
        return;  // Sequence ended
      }
      
      // Reload current line after moving to next
      currentLine = &sequenceTable[seqState.currentLineIndex];
    }
    // Execute next cycle for current line
    else {
      // Load current line configuration
      currentLine = &sequenceTable[seqState.currentLineIndex];
      
      // 🎯 POSITIONING: Move to start position on first cycle of each line
      if (seqState.currentCycleInLine == 0) {
        positionForNextLine();
      }
      
      // ✅ CHECK MOVEMENT TYPE: Va-et-vient OR Oscillation
      if (currentLine->movementType == MOVEMENT_VAET) {
        // ════════════════════════════════════════════════════════════════
        // VA-ET-VIENT MODE (classic back-and-forth movement)
        // ════════════════════════════════════════════════════════════════
        
        // Apply line parameters to motion configuration
        motion.startPositionMM = currentLine->startPositionMM;
        motion.targetDistanceMM = currentLine->distanceMM;
      
        // Apply speed levels directly (0.0-MAX_SPEED_LEVEL scale)
        motion.speedLevelForward = currentLine->speedForward;
        motion.speedLevelBackward = currentLine->speedBackward;
        
        // Clamp to valid range [1, MAX_SPEED_LEVEL]
        if (motion.speedLevelForward < 1.0) motion.speedLevelForward = 1.0;
        if (motion.speedLevelForward > MAX_SPEED_LEVEL) motion.speedLevelForward = MAX_SPEED_LEVEL;
        if (motion.speedLevelBackward < 1.0) motion.speedLevelBackward = 1.0;
        if (motion.speedLevelBackward > MAX_SPEED_LEVEL) motion.speedLevelBackward = MAX_SPEED_LEVEL;
        
        // Apply deceleration configuration
        decelZone.enabled = currentLine->decelStartEnabled || currentLine->decelEndEnabled;
        decelZone.enableStart = currentLine->decelStartEnabled;
        decelZone.enableEnd = currentLine->decelEndEnabled;
        decelZone.zoneMM = currentLine->decelZoneMM;
        decelZone.effectPercent = currentLine->decelEffectPercent;
        decelZone.mode = currentLine->decelMode;
        
        // Apply cycle pause configuration from sequence line
        motion.cyclePause.enabled = currentLine->vaetCyclePauseEnabled;
        motion.cyclePause.isRandom = currentLine->vaetCyclePauseIsRandom;
        motion.cyclePause.pauseDurationSec = currentLine->vaetCyclePauseDurationSec;
        motion.cyclePause.minPauseSec = currentLine->vaetCyclePauseMinSec;
        motion.cyclePause.maxPauseSec = currentLine->vaetCyclePauseMaxSec;
        
        // Validate configuration
        validateDecelZone();
        
        // Calculate step delays and start movement
        calculateStepDelay();
        
        Motor.enable();
        lastStepMicros = micros();
        
        // Calculate step positions
        startStep = (long)(motion.startPositionMM * STEPS_PER_MM);
        targetStep = (long)((motion.startPositionMM + motion.targetDistanceMM) * STEPS_PER_MM);
        
        config.currentState = STATE_RUNNING;
        isPaused = false;
        
        // Set movement type (config.executionContext already == CONTEXT_SEQUENCER)
        currentMovement = MOVEMENT_VAET;
        
        // Determine starting direction
        if (currentStep <= startStep) {
          movingForward = true;
        } else if (currentStep >= targetStep) {
          movingForward = false;
        } else {
          movingForward = true;
        }
        
        Motor.setDirection(movingForward);
        
        lastStepForDistance = currentStep;
        lastStartContactMillis = 0;
        cycleTimeMillis = 0;
        measuredCyclesPerMinute = 0;
        wasAtStart = false;
        hasReachedStartStep = (currentStep >= startStep);
        
        // DON'T increment here - will be incremented in onMovementComplete()
        seqState.lineStartTime = millis();
        
        engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
              " | 🔄 VA-ET-VIENT | Cycle " + String(seqState.currentCycleInLine + 1) + "/" + String(currentLine->cycleCount) + 
              " | " + String(currentLine->startPositionMM, 1) + "mm → " + 
              String(currentLine->startPositionMM + currentLine->distanceMM, 1) + "mm | Speed: " + 
              String(motion.speedLevelForward, 1) + "/" + String(motion.speedLevelBackward, 1));
        
        sendSequenceStatus();
      }
      else if (currentLine->movementType == MOVEMENT_OSC) {
        // ════════════════════════════════════════════════════════════════
        // OSCILLATION MODE
        // ════════════════════════════════════════════════════════════════
        
        // IMPORTANT: Oscillation manages its own internal cycle count
        // We only start it ONCE per sequence line (not per sequencer cycle)
        // The oscillation will call onMovementComplete() when ALL its cycles are done
        
        // Copy oscillation parameters from sequence line to oscillation config
        oscillation.centerPositionMM = currentLine->oscCenterPositionMM;
        oscillation.amplitudeMM = currentLine->oscAmplitudeMM;
        oscillation.waveform = currentLine->oscWaveform;
        oscillation.frequencyHz = currentLine->oscFrequencyHz;
        // Note: Speed is now dictated by frequency (no manual speed control)
        oscillation.enableRampIn = currentLine->oscEnableRampIn;
        oscillation.enableRampOut = currentLine->oscEnableRampOut;
        oscillation.rampInDurationMs = currentLine->oscRampInDurationMs;
        oscillation.rampOutDurationMs = currentLine->oscRampOutDurationMs;
        oscillation.cycleCount = currentLine->cycleCount;  // Oscillation will execute THIS many cycles
        oscillation.returnToCenter = false;  // Don't return to center in sequencer (continue to next line)
        
        // Apply cycle pause configuration from sequence line
        oscillation.cyclePause.enabled = currentLine->oscCyclePauseEnabled;
        oscillation.cyclePause.isRandom = currentLine->oscCyclePauseIsRandom;
        oscillation.cyclePause.pauseDurationSec = currentLine->oscCyclePauseDurationSec;
        oscillation.cyclePause.minPauseSec = currentLine->oscCyclePauseMinSec;
        oscillation.cyclePause.maxPauseSec = currentLine->oscCyclePauseMaxSec;
        
        seqState.lineStartTime = millis();
        
        // NEW ARCHITECTURE: No need to set flag, config.executionContext already == CONTEXT_SEQUENCER
        // (set by startSequence at beginning)
        
        // Start oscillation (will set currentMovement = MOVEMENT_OSC)
        // This will run ALL cycles internally and call onMovementComplete() ONCE when done
        startOscillation();
        
        // ✅ Skip initial positioning - positionForNextLine() already moved us to start position
        // Calculate initial phase based on current position to avoid sudden jump
        oscillationState.isInitialPositioning = false;
        
        // ✅ Skip ramp-in too - we're already positioned, no need to ramp amplitude from 0
        oscillationState.isRampingIn = false;
        
        // Calculate where we are in the wave cycle based on current position
        float currentPosMM = currentStep / (float)STEPS_PER_MM;
        float relativePos = (currentPosMM - oscillation.centerPositionMM) / oscillation.amplitudeMM;
        
        // Clamp to [-1, 1] range (in case positioning wasn't perfect)
        if (relativePos < -1.0) relativePos = -1.0;
        if (relativePos > 1.0) relativePos = 1.0;
        
        // For SINE wave: waveValue = -cos(phase * 2π), so phase = acos(-waveValue) / 2π
        // We want to start at minimum (waveValue = -1), which corresponds to phase = 0
        // If we're already at minimum, phase = 0 is correct
        float initialPhase = 0.0;
        if (currentLine->oscWaveform == OSC_SINE) {
          // For sine: waveValue = -cos(phase * 2π)
          // So: phase = acos(-relativePos) / (2π)
          initialPhase = acos(-relativePos) / (2.0 * PI);
        } else if (currentLine->oscWaveform == OSC_TRIANGLE || currentLine->oscWaveform == OSC_SQUARE) {
          // For triangle/square, approximate phase from position
          // Simple approach: if at min (-1), phase=0; if at max (+1), phase=0.5
          initialPhase = (relativePos + 1.0) / 4.0;  // Maps [-1,+1] to [0, 0.5]
        }
        
        oscillationState.accumulatedPhase = initialPhase;
        oscillationState.lastPhaseUpdateMs = millis();  // ✅ Reset timing to avoid huge deltaTime on first call
        engine->debug("📍 Oscillation démarre depuis position actuelle: " + String(currentPosMM, 1) + 
              "mm (phase initiale: " + String(initialPhase, 3) + ", relativePos: " + String(relativePos, 2) + ")");
        
        String waveformName = "SINE";
        if (currentLine->oscWaveform == OSC_TRIANGLE) waveformName = "TRIANGLE";
        if (currentLine->oscWaveform == OSC_SQUARE) waveformName = "SQUARE";
        
        engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
              " | 〰️ OSCILLATION (" + String(currentLine->cycleCount) + " cycles internes)" +
              " | Centre: " + String(currentLine->oscCenterPositionMM, 1) + "mm | Amp: ±" + 
              String(currentLine->oscAmplitudeMM, 1) + "mm | " + waveformName + " @ " + 
              String(currentLine->oscFrequencyHz, 2) + " Hz");
        
        sendSequenceStatus();
      }
      else if (currentLine->movementType == MOVEMENT_CHAOS) {
        // ════════════════════════════════════════════════════════════════
        // CHAOS MODE (durée fixe)
        // ════════════════════════════════════════════════════════════════
        
        // Copy chaos parameters from sequence line to chaos config
        chaos.centerPositionMM = currentLine->chaosCenterPositionMM;
        chaos.amplitudeMM = currentLine->chaosAmplitudeMM;
        chaos.maxSpeedLevel = currentLine->chaosMaxSpeedLevel;
        chaos.crazinessPercent = currentLine->chaosCrazinessPercent;
        chaos.durationSeconds = currentLine->chaosDurationSeconds;
        chaos.seed = currentLine->chaosSeed;
        
        // Copy patterns enabled array (11 patterns: ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, CALM, BRUTE_FORCE, LIBERATOR)
        for (int i = 0; i < 11; i++) {
          chaos.patternsEnabled[i] = currentLine->chaosPatternsEnabled[i];
        }
        
        // DON'T increment here - will be incremented in onMovementComplete()
        // (CHAOS always executes 1 cycle per line)
        seqState.lineStartTime = millis();
        
        // NEW ARCHITECTURE: No need to set flag, config.executionContext already == CONTEXT_SEQUENCER
        // (set by startSequence at beginning)
        
        // Start chaos mode (will set currentMovement = MOVEMENT_CHAOS)
        startChaos();
        
        engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
              " | 🌀 CHAOS | Cycle " + String(seqState.currentCycleInLine + 1) + "/" + String(currentLine->cycleCount) + " | Durée: " + String(currentLine->chaosDurationSeconds) + "s | Centre: " + 
              String(currentLine->chaosCenterPositionMM, 1) + "mm ±" + 
              String(currentLine->chaosAmplitudeMM, 1) + "mm | Speed: " + 
              String(currentLine->chaosMaxSpeedLevel, 1) + " | Madness: " + 
              String(currentLine->chaosCrazinessPercent, 0) + "%");
        
        sendSequenceStatus();
      }
      else if (currentLine->movementType == MOVEMENT_CALIBRATION) {
        // ════════════════════════════════════════════════════════════════
        // CALIBRATION MODE (full calibration sequence)
        // ════════════════════════════════════════════════════════════════
        
        engine->info(String("▶️ Ligne ") + String(seqState.currentLineIndex + 1) + "/" + String(sequenceLineCount) + 
              " | 📏 CALIBRATION | Lancement calibration complète...");
        
        // Mark sequence as waiting for calibration
        // The calibration will complete asynchronously and call onMovementComplete()
        seqState.lineStartTime = millis();
        
        // Start full calibration
        startCalibration();
        
        // Note: onMovementComplete() will be called when calibration finishes
        // This will increment seqState.currentCycleInLine and continue sequence
        
        sendSequenceStatus();
      }
    }  // end else (execute new cycle)
  }  // end if (STATE_READY && !isWaitingPause)
}  // end processSequenceExecution()

void sendStatus() {
  // Only broadcast if clients are connected (early exit optimization)
  if (webSocket.connectedClients() == 0) return;
  
  // ============================================================================
  // COMMON FIELDS (all modes)
  // ============================================================================
  
  // Calculate derived values
  float positionMM = currentStep / STEPS_PER_MM;
  float totalTraveledMM = totalDistanceTraveled / STEPS_PER_MM;
  
  // Validation state
  bool canStart = true;
  String errorMessage = "";
  if (config.totalDistanceMM == 0) {
    canStart = false;
    errorMessage = "Recalibration nécessaire";
  } else if (motion.targetDistanceMM <= 0) {
    canStart = false;
    errorMessage = "Définir distance";
  }
  
  bool canCalibrate = (config.currentState == STATE_READY || config.currentState == STATE_INIT || config.currentState == STATE_ERROR);
  
  // Use ArduinoJson for efficient JSON construction
  // Mode-specific optimization: Only send relevant data per movement type
  JsonDocument doc;
  
  // Root level fields (ALWAYS sent regardless of mode)
  doc["state"] = (int)config.currentState;
  doc["currentStep"] = currentStep;
  doc["positionMM"] = serialized(String(positionMM, 2));
  doc["totalDistMM"] = serialized(String(config.totalDistanceMM, 2));
  doc["maxDistLimitPercent"] = serialized(String(maxDistanceLimitPercent, 0));
  doc["effectiveMaxDistMM"] = serialized(String(effectiveMaxDistanceMM, 2));
  doc["isPaused"] = isPaused;
  doc["totalTraveled"] = serialized(String(totalTraveledMM, 2));
  doc["canStart"] = canStart;
  doc["canCalibrate"] = canCalibrate;
  doc["errorMessage"] = errorMessage;
  doc["movementType"] = (int)currentMovement;
  doc["config.executionContext"] = (int)config.executionContext;
  doc["operationMode"] = (int)currentMovement;  // Legacy
  doc["pursuitActive"] = pursuit.isMoving;
  
  // ============================================================================
  // MODE-SPECIFIC FIELDS (Phase 1 Optimization)
  // ============================================================================
  
  if (currentMovement == MOVEMENT_VAET || currentMovement == MOVEMENT_PURSUIT) {
    // ========================================================================
    // VA-ET-VIENT / PURSUIT MODE
    // ========================================================================
    
    // Motion-specific derived values
    float cyclesPerMinForward = speedLevelToCyclesPerMin(motion.speedLevelForward);
    float cyclesPerMinBackward = speedLevelToCyclesPerMin(motion.speedLevelBackward);
    float avgCyclesPerMin = (cyclesPerMinForward + cyclesPerMinBackward) / 2.0;
    float avgSpeedLevel = (motion.speedLevelForward + motion.speedLevelBackward) / 2.0;
    
    // Motion object (nested)
    JsonObject motionObj = doc["motion"].to<JsonObject>();
    motionObj["startPositionMM"] = serialized(String(motion.startPositionMM, 2));
    motionObj["targetDistanceMM"] = serialized(String(motion.targetDistanceMM, 2));
    motionObj["speedLevelForward"] = serialized(String(motion.speedLevelForward, 1));
    motionObj["speedLevelBackward"] = serialized(String(motion.speedLevelBackward, 1));
    
    // Cycle pause config & state
    JsonObject pauseObj = motionObj["cyclePause"].to<JsonObject>();
    pauseObj["enabled"] = motion.cyclePause.enabled;
    pauseObj["isRandom"] = motion.cyclePause.isRandom;
    pauseObj["pauseDurationSec"] = serialized(String(motion.cyclePause.pauseDurationSec, 1));
    pauseObj["minPauseSec"] = serialized(String(motion.cyclePause.minPauseSec, 1));
    pauseObj["maxPauseSec"] = serialized(String(motion.cyclePause.maxPauseSec, 1));
    pauseObj["isPausing"] = motionPauseState.isPausing;
    
    // Calculate remaining time (server-side)
    if (motionPauseState.isPausing) {
      unsigned long elapsedMs = millis() - motionPauseState.pauseStartMs;
      long remainingMs = (long)motionPauseState.currentPauseDuration - (long)elapsedMs;
      pauseObj["remainingMs"] = max(0L, remainingMs);
    } else {
      pauseObj["remainingMs"] = 0;
    }
    
    // Legacy fields (backward compatibility)
    doc["startPositionMM"] = serialized(String(motion.startPositionMM, 2));
    doc["targetDistMM"] = serialized(String(motion.targetDistanceMM, 2));
    doc["targetDistanceMM"] = serialized(String(motion.targetDistanceMM, 2));
    doc["speedLevelForward"] = serialized(String(motion.speedLevelForward, 1));
    doc["speedLevelBackward"] = serialized(String(motion.speedLevelBackward, 1));
    doc["speedLevelAvg"] = serialized(String(avgSpeedLevel, 1));
    doc["cyclesPerMinForward"] = serialized(String(cyclesPerMinForward, 1));
    doc["cyclesPerMinBackward"] = serialized(String(cyclesPerMinBackward, 1));
    doc["cyclesPerMinAvg"] = serialized(String(avgCyclesPerMin, 1));
    
    // Pending motion
    doc["hasPending"] = pendingMotion.hasChanges;
    if (pendingMotion.hasChanges) {
      JsonObject pendingObj = doc["pendingMotion"].to<JsonObject>();
      pendingObj["startPositionMM"] = serialized(String(pendingMotion.startPositionMM, 2));
      pendingObj["distanceMM"] = serialized(String(pendingMotion.distanceMM, 2));
      pendingObj["speedLevelForward"] = serialized(String(pendingMotion.speedLevelForward, 1));
      pendingObj["speedLevelBackward"] = serialized(String(pendingMotion.speedLevelBackward, 1));
      
      // Legacy pending fields
      doc["pendingStartPos"] = serialized(String(pendingMotion.startPositionMM, 2));
      doc["pendingDist"] = serialized(String(pendingMotion.distanceMM, 2));
      doc["pendingSpeedLevelForward"] = serialized(String(pendingMotion.speedLevelForward, 1));
      doc["pendingSpeedLevelBackward"] = serialized(String(pendingMotion.speedLevelBackward, 1));
    } else {
      doc["hasPending"] = false;
    }
    
    // Deceleration zone (always send, even if disabled, for UI sync)
    JsonObject decelObj = doc["decelZone"].to<JsonObject>();
    decelObj["enabled"] = decelZone.enabled;
    decelObj["enableStart"] = decelZone.enableStart;
    decelObj["enableEnd"] = decelZone.enableEnd;
    decelObj["zoneMM"] = serialized(String(decelZone.zoneMM, 1));
    decelObj["effectPercent"] = serialized(String(decelZone.effectPercent, 0));
    decelObj["mode"] = (int)decelZone.mode;
  }
  else if (currentMovement == MOVEMENT_OSC) {
    // ========================================================================
    // OSCILLATION MODE
    // ========================================================================
    
    // Oscillation config
    JsonObject oscObj = doc["oscillation"].to<JsonObject>();
    oscObj["centerPositionMM"] = serialized(String(oscillation.centerPositionMM, 2));
    oscObj["amplitudeMM"] = serialized(String(oscillation.amplitudeMM, 2));
    oscObj["waveform"] = (int)oscillation.waveform;
    oscObj["frequencyHz"] = serialized(String(oscillation.frequencyHz, 3));
    
    // Calculate effective frequency (capped if exceeds max speed)
    float effectiveFrequencyHz = oscillation.frequencyHz;
    if (oscillation.amplitudeMM > 0.0) {
      float maxAllowedFreq = OSC_MAX_SPEED_MM_S / (2.0 * PI * oscillation.amplitudeMM);
      if (oscillation.frequencyHz > maxAllowedFreq) {
        effectiveFrequencyHz = maxAllowedFreq;
      }
    }
    oscObj["effectiveFrequencyHz"] = serialized(String(effectiveFrequencyHz, 3));
    oscObj["actualSpeedMMS"] = serialized(String(actualOscillationSpeedMMS, 1));
    oscObj["enableRampIn"] = oscillation.enableRampIn;
    oscObj["rampInDurationMs"] = serialized(String(oscillation.rampInDurationMs, 0));
    oscObj["enableRampOut"] = oscillation.enableRampOut;
    oscObj["rampOutDurationMs"] = serialized(String(oscillation.rampOutDurationMs, 0));
    oscObj["cycleCount"] = oscillation.cycleCount;
    oscObj["returnToCenter"] = oscillation.returnToCenter;
    
    // Cycle pause config & state
    JsonObject oscPauseObj = oscObj["cyclePause"].to<JsonObject>();
    oscPauseObj["enabled"] = oscillation.cyclePause.enabled;
    oscPauseObj["isRandom"] = oscillation.cyclePause.isRandom;
    oscPauseObj["pauseDurationSec"] = serialized(String(oscillation.cyclePause.pauseDurationSec, 1));
    oscPauseObj["minPauseSec"] = serialized(String(oscillation.cyclePause.minPauseSec, 1));
    oscPauseObj["maxPauseSec"] = serialized(String(oscillation.cyclePause.maxPauseSec, 1));
    oscPauseObj["isPausing"] = oscPauseState.isPausing;
    
    // Calculate remaining time (server-side)
    if (oscPauseState.isPausing) {
      unsigned long elapsedMs = millis() - oscPauseState.pauseStartMs;
      long remainingMs = (long)oscPauseState.currentPauseDuration - (long)elapsedMs;
      oscPauseObj["remainingMs"] = max(0L, remainingMs);
    } else {
      oscPauseObj["remainingMs"] = 0;
    }
    
    // Oscillation state (minimal if no ramping, full if ramping)
    JsonObject oscStateObj = doc["oscillationState"].to<JsonObject>();
    oscStateObj["completedCycles"] = oscillationState.completedCycles;  // Always needed
    
    if (oscillation.enableRampIn || oscillation.enableRampOut) {
      // Full state if ramping enabled
      oscStateObj["currentAmplitude"] = serialized(String(oscillationState.currentAmplitude, 2));
      oscStateObj["isRampingIn"] = oscillationState.isRampingIn;
      oscStateObj["isRampingOut"] = oscillationState.isRampingOut;
    }
  }
  else if (currentMovement == MOVEMENT_CHAOS) {
    // ========================================================================
    // CHAOS MODE
    // ========================================================================
    
    // Chaos config
    JsonObject chaosObj = doc["chaos"].to<JsonObject>();
    chaosObj["centerPositionMM"] = serialized(String(chaos.centerPositionMM, 2));
    chaosObj["amplitudeMM"] = serialized(String(chaos.amplitudeMM, 2));
    chaosObj["maxSpeedLevel"] = serialized(String(chaos.maxSpeedLevel, 1));
    chaosObj["crazinessPercent"] = serialized(String(chaos.crazinessPercent, 0));
    chaosObj["durationSeconds"] = chaos.durationSeconds;
    chaosObj["seed"] = chaos.seed;
    
    JsonArray patternsArray = chaosObj["patternsEnabled"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
      patternsArray.add(chaos.patternsEnabled[i]);
    }
    
    // Chaos state
    JsonObject chaosStateObj = doc["chaosState"].to<JsonObject>();
    chaosStateObj["isRunning"] = chaosState.isRunning;
    chaosStateObj["currentPattern"] = (int)chaosState.currentPattern;
    
    const char* patternNames[] = {"ZIGZAG", "SWEEP", "PULSE", "DRIFT", "BURST", "WAVE", "PENDULUM", "SPIRAL", "BREATHING", "BRUTE_FORCE", "LIBERATOR"};
    chaosStateObj["patternName"] = patternNames[chaosState.currentPattern];
    
    chaosStateObj["targetPositionMM"] = serialized(String(chaosState.targetPositionMM, 2));
    chaosStateObj["currentSpeedLevel"] = serialized(String(chaosState.currentSpeedLevel, 1));
    chaosStateObj["minReachedMM"] = serialized(String(chaosState.minReachedMM, 2));
    chaosStateObj["maxReachedMM"] = serialized(String(chaosState.maxReachedMM, 2));
    chaosStateObj["patternsExecuted"] = chaosState.patternsExecuted;
    
    if (chaosState.isRunning && chaos.durationSeconds > 0) {
      unsigned long elapsed = (millis() - chaosState.startTime) / 1000;
      chaosStateObj["elapsedSeconds"] = elapsed;
    }
  }
  
  // ============================================================================
  // SYSTEM STATS (Phase 2 Optimization - On-Demand)
  // ============================================================================
  
  if (statsRequested) {
    JsonObject systemObj = doc["system"].to<JsonObject>();
    systemObj["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    systemObj["heapTotal"] = ESP.getHeapSize();
    systemObj["heapFree"] = ESP.getFreeHeap();
    systemObj["heapUsedPercent"] = serialized(String(100.0 - (100.0 * ESP.getFreeHeap() / ESP.getHeapSize()), 1));
    systemObj["psramTotal"] = ESP.getPsramSize();
    systemObj["psramFree"] = ESP.getFreePsram();
    systemObj["psramUsedPercent"] = serialized(String(100.0 - (100.0 * ESP.getFreePsram() / ESP.getPsramSize()), 1));
    systemObj["wifiRssi"] = WiFi.RSSI();
    systemObj["temperatureC"] = serialized(String(temperatureRead(), 1));
    systemObj["uptimeSeconds"] = millis() / 1000;
  }
  
  // Serialize to String and broadcast
  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
}
