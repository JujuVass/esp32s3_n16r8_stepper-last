// ============================================================================
// UTILITY ENGINE - Unified System for WebSocket, Logging, and LittleFS
// ============================================================================
// Purpose: Centralized management of all system utilities
// - WebSocket server lifecycle and message broadcasting
// - Multi-level structured logging (ERROR, WARN, INFO, DEBUG)
// - LittleFS filesystem operations (read, write, delete)
// - Circular log buffer with async file flushing
//
// Benefits:
// ✓ Single initialization point (setup)
// ✓ Consistent error handling across modules
// ✓ Reusable in other ESP32 projects
// ✓ Reduces global state variables (8 → 1)
// ✓ Type-safe logging with compile-time level checking
// ============================================================================

#ifndef UTILITY_ENGINE_H
#define UTILITY_ENGINE_H

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <string>
#include "Types.h"  // For SystemConfig struct members

// ============================================================================
// CONFIGURATION
// ============================================================================
#define LOG_BUFFER_SIZE 100
#define LOG_FILE_PATTERN "/logs/log_"
#define LOG_FILE_SUFFIX "_session"
#define LOG_FILE_EXTENSION ".txt"

// ============================================================================
// LOG LEVEL ENUM
// ============================================================================
enum LogLevel {
  LOG_ERROR = 0,
  LOG_WARNING = 1,
  LOG_INFO = 2,
  LOG_DEBUG = 3
};

// ============================================================================
// LOG ENTRY STRUCTURE
// ============================================================================
struct LogEntry {
  unsigned long timestamp;  // millis() when log was created
  LogLevel level;
  String message;
  bool valid;  // true if entry contains data
  
  LogEntry() : timestamp(0), level(LOG_INFO), message(""), valid(false) {}
};

// ============================================================================
// SYSTEM CONFIGURATION STRUCT - Phase 3.2
// ============================================================================
// Central aggregation of all critical system state variables
// Provides unified load/save interface for persistence (config.json)
// Maps 33+ global variables into coherent configuration object
// ============================================================================
struct SystemConfig {
  
  // ========================================================================
  // SYSTEM STATE
  // ========================================================================
  SystemState currentState;         // Current operation state (INIT, RUNNING, PAUSED, etc.)
  ExecutionContext executionContext; // Execution context (STANDALONE vs SEQUENCER)
  MovementType currentMovement;      // Current movement type (VAET, OSC, CHAOS, PURSUIT, CALIB)
  bool needsInitialCalibration;      // Flag: system requires recalibration
  
  // ========================================================================
  // POSITION LIMITS & CALIBRATION
  // ========================================================================
  long minStep;                      // Minimum position (after START contact)
  long maxStep;                      // Maximum position (before END contact)
  float totalDistanceMM;             // Total travel distance (mm)
  float maxDistanceLimitPercent;     // Safety limit on motion (default 100%)
  float effectiveMaxDistanceMM;      // Calculated limit based on percentage
  
  // ========================================================================
  // CONTACT SENSING & DRIFT DETECTION
  // ========================================================================
  int lastStartContactState;         // Last read START contact state
  int lastEndContactState;           // Last read END contact state
  bool currentMotorDirection;        // Current motor direction (HIGH/LOW)
  bool wasAtStart;                   // Flag: was at START position
  float measuredCyclesPerMinute;     // Measured speed (cycles/min)
  
  // ========================================================================
  // MOTION EXECUTION STATE
  // ========================================================================
  bool isPaused;                     // Is motion currently paused?
  bool movingForward;                // Direction of current motion
  long startStep;                    // Start position for current move
  long targetStep;                   // Target position for current move
  bool hasReachedStartStep;          // Has reached startStep at least once
  long lastStepForDistance;          // Last step for distance calculation
  
  // ========================================================================
  // VA-ET-VIENT MODE
  // ========================================================================
  MotionConfig motion;               // Current motion configuration
  
  // ========================================================================
  // OSCILLATION MODE
  // ========================================================================
  OscillationConfig oscillation;     // Oscillation configuration
  OscillationState oscillationState; // Oscillation runtime state
  float actualOscillationSpeedMMS;   // Actual oscillation speed (mm/s)
  
  // ========================================================================
  // PURSUIT MODE
  // ========================================================================
  PursuitState pursuit;              // Pursuit state and configuration
  
  // ========================================================================
  // DECELERATION ZONE
  // ========================================================================
  DecelZoneConfig decelZone;         // Deceleration configuration
  
  // ========================================================================
  // CHAOS MODE
  // ========================================================================
  ChaosRuntimeConfig chaos;          // Chaos mode configuration
  ChaosExecutionState chaosState;    // Chaos mode runtime state
  
  // ========================================================================
  // SEQUENCER
  // ========================================================================
  int sequenceLineCount;             // Number of loaded sequence lines
  int nextLineId;                    // Auto-increment ID counter
  SequenceExecutionState seqState;   // Sequencer runtime state
  
  // ========================================================================
  // CONSTRUCTOR - Initialize all fields to defaults
  // ========================================================================
  SystemConfig() :
    currentState(STATE_INIT),
    executionContext(CONTEXT_STANDALONE),
    currentMovement(MOVEMENT_VAET),
    needsInitialCalibration(true),
    minStep(0),
    maxStep(0),
    totalDistanceMM(0),
    maxDistanceLimitPercent(100.0),
    effectiveMaxDistanceMM(0.0),
    lastStartContactState(HIGH),
    lastEndContactState(HIGH),
    currentMotorDirection(HIGH),
    wasAtStart(false),
    measuredCyclesPerMinute(0),
    isPaused(false),
    movingForward(true),
    startStep(0),
    targetStep(0),
    hasReachedStartStep(false),
    lastStepForDistance(0),
    motion(),
    oscillation(),
    oscillationState(),
    actualOscillationSpeedMMS(0.0),
    pursuit(),
    decelZone(),
    chaos(),
    chaosState(),
    sequenceLineCount(0),
    nextLineId(1),
    seqState() {}
};

// ============================================================================
// UTILITY ENGINE CLASS
// ============================================================================
class UtilityEngine {
  
public:
  // ========================================================================
  // CONSTRUCTOR & LIFECYCLE
  // ========================================================================
  
  /**
   * Initialize UtilityEngine with WebSocket server reference
   * @param webSocketServer Reference to global WebSocketsServer instance
   */
  UtilityEngine(WebSocketsServer& webSocketServer);
  
  /**
   * Full initialization (must be called in setup())
   * - Mounts LittleFS
   * - Creates logs directory
   * - Opens/creates log file
   * - Configures NTP time
   * @return true if successful, false if critical failure
   */
  bool initialize();
  
  /**
   * Cleanup before shutdown or OTA update
   * - Flushes pending logs to disk
   * - Closes log file
   */
  void shutdown();
  
  // ========================================================================
  // LOGGING INTERFACE
  // ========================================================================
  
  /**
   * Main logging function (used by macros and direct calls)
   * Outputs to 3 channels: Serial, WebSocket (if clients), File (buffered)
   * @param level Severity level (ERROR, WARN, INFO, DEBUG)
   * @param message Log message
   */
  void log(LogLevel level, const String& message);
  
  // Convenience methods (same as macros but class-based)
  void error(const String& message)   { log(LOG_ERROR, message); }
  void warn(const String& message)    { log(LOG_WARNING, message); }
  void info(const String& message)    { log(LOG_INFO, message); }
  void debug(const String& message)   { log(LOG_DEBUG, message); }
  
  /**
   * Flush log buffer to disk (called every 5-10 seconds)
   * - CRITICAL: Skips flush if motor is actively moving (avoid jitter)
   * - EXCEPTION: Forces flush if buffer reaches 80% capacity
   * @param forceFlush Force flush even during movement (true = emergency only)
   */
  void flushLogBuffer(bool forceFlush = false);
  
  /**
   * Set current log level (filters what gets logged)
   * @param level Minimum level to log (LOG_ERROR, LOG_WARNING, LOG_INFO, LOG_DEBUG)
   */
  void setLogLevel(LogLevel level) { currentLogLevel = level; }
  
  /**
   * Get current log level
   */
  LogLevel getLogLevel() const { return currentLogLevel; }
  
  // ========================================================================
  // FILESYSTEM INTERFACE
  // ========================================================================
  
  /**
   * Check if filesystem is mounted and ready
   */
  bool isFilesystemReady() const { return littleFsMounted; }
  
  /**
   * Check if file exists in LittleFS
   * @param path Absolute path (e.g., "/index.html", "/logs/log_20250102_0.txt")
   * @return true if file exists and is not a directory
   */
  bool fileExists(const String& path) const;
  
  /**
   * Check if directory exists in LittleFS
   * @param path Absolute path to directory (e.g., "/logs", "/www")
   * @return true if directory exists
   */
  bool directoryExists(const String& path) const;
  
  /**
   * Read file contents as string (text files only)
   * Binary files will return garbled strings - use download for those
   * @param path Absolute file path
   * @param maxSize Maximum bytes to read (safety limit, default 1MB)
   * @return File contents as String, or empty String if error
   */
  String readFileAsString(const String& path, size_t maxSize = 1024 * 1024);
  
  /**
   * Write/overwrite file with string contents
   * @param path Absolute file path
   * @param data String to write
   * @return true if write successful, false otherwise
   */
  bool writeFileAsString(const String& path, const String& data);
  
  /**
   * Delete file from filesystem
   * @param path Absolute file path
   * @return true if deletion successful, false otherwise
   */
  bool deleteFile(const String& path);
  
  /**
   * Create directory in filesystem
   * @param path Absolute directory path (e.g., "/logs", "/www/assets")
   * @return true if created or already exists, false on error
   */
  bool createDirectory(const String& path);
  
  /**
   * Get total filesystem capacity in bytes
   */
  uint32_t getTotalBytes() const;
  
  /**
   * Get used filesystem space in bytes
   */
  uint32_t getUsedBytes() const;
  
  /**
   * Get available filesystem space in bytes
   */
  uint32_t getAvailableBytes() const { return getTotalBytes() - getUsedBytes(); }
  
  /**
   * Get disk usage as percentage (0-100)
   */
  float getDiskUsagePercent() const;
  
  // ========================================================================
  // JSON HELPERS - Phase 3.1
  // ========================================================================
  
  /**
   * Load JSON from file (deserialize)
   * Automatically handles file opening/closing and error checking
   * @param path File path (e.g., "/playlist.json", "/stats.json")
   * @param doc Reference to JsonDocument to populate
   * @return true if successful, false on error (file not found, invalid JSON, etc.)
   */
  bool loadJsonFile(const String& path, JsonDocument& doc);
  
  /**
   * Save JSON to file (serialize)
   * Automatically handles file opening/closing and error checking
   * @param path File path
   * @param doc JsonDocument to save
   * @return true if successful, false on error
   */
  bool saveJsonFile(const String& path, const JsonDocument& doc);
  
  /**
   * Validate JSON schema (basic key presence check)
   * Verifies that all required keys are present in the document
   * @param doc Document to validate
   * @param requiredKeys Space-separated or comma-separated keys (e.g., "mode speed distance")
   * @return true if all required keys present, false otherwise
   */
  bool validateJsonSchema(const JsonDocument& doc, const String& requiredKeys);
  
  /**
   * Clear all files from filesystem (WARNING: destructive!)
   * @return Number of files deleted
   */
  int clearAllFiles();
  
  // ========================================================================
  // SYSTEM CONFIGURATION PERSISTENCE - Phase 3.2
  // ========================================================================
  
  /**
   * Load system configuration from config.json
   * Deserializes and populates SystemConfig struct from persistent storage
   * @param config Reference to SystemConfig struct to populate
   * @param configPath Path to config file (default: "/config.json")
   * @return true if successful, false on error (file not found, parse error, etc.)
   */
  bool loadSystemConfig(SystemConfig& config, const String& configPath = "/config.json");
  
  /**
   * Save system configuration to config.json
   * Serializes SystemConfig struct to persistent storage
   * @param config Reference to SystemConfig struct to save
   * @param configPath Path to config file (default: "/config.json")
   * @return true if successful, false on error
   */
  bool saveSystemConfig(const SystemConfig& config, const String& configPath = "/config.json");
  
  // ========================================================================
  // WEBSOCKET INTERFACE (Relay functions)
  // ========================================================================
  
  /**
   * Broadcast message to all connected WebSocket clients
   * @param message JSON string or text message
   */
  void broadcastWebSocket(const String& message);
  
  /**
   * Get number of connected WebSocket clients
   */
  uint8_t getConnectedClients() const;
  
  /**
   * Check if any clients are connected
   */
  bool hasConnectedClients() const { return getConnectedClients() > 0; }
  
  // ========================================================================
  // UTILITY METHODS
  // ========================================================================
  
  /**
   * Get current log filename
   */
  String getCurrentLogFile() const { return currentLogFileName; }
  
  /**
   * Get system uptime in seconds
   */
  unsigned long getUptimeSeconds() const { return millis() / 1000; }
  
  /**
   * Get formatted current time (requires NTP sync)
   * @param format strftime format string (e.g., "%Y-%m-%d %H:%M:%S")
   * @return Formatted time string
   */
  String getFormattedTime(const char* format = "%Y-%m-%d %H:%M:%S") const;
  
  /**
   * Check if system time is synchronized (NTP)
   */
  bool isTimeSynchronized() const;
  
  // ========================================================================
  // STATE INSPECTION (for debugging)
  // ========================================================================
  
  struct EngineStatus {
    bool filesystemReady;
    bool fileOpen;
    bool timeSynced;
    uint8_t connectedClients;
    uint32_t totalBytes;
    uint32_t usedBytes;
    float diskUsagePercent;
    LogLevel currentLogLevel;
    String currentLogFile;
  };
  
  /**
   * Get complete engine status snapshot
   */
  EngineStatus getStatus() const;
  
  /**
   * Print status to Serial (for debugging)
   */
  void printStatus() const;

private:
  
  // ========================================================================
  // PRIVATE MEMBERS
  // ========================================================================
  
  // WebSocket reference (not owned)
  WebSocketsServer& webSocket;
  
  // Filesystem state
  bool littleFsMounted;
  File globalLogFile;
  String currentLogFileName;
  
  // Logging state
  LogLevel currentLogLevel;
  LogEntry logBuffer[LOG_BUFFER_SIZE];
  int logBufferWriteIndex;
  unsigned long lastLogFlush;
  
  // ========================================================================
  // PRIVATE HELPER METHODS
  // ========================================================================
  
  /**
   * Initialize log file (creates /logs directory, opens session file)
   */
  bool initializeLogFile();
  
  /**
   * Generate log filename with session suffix
   * @return Filename like "/logs/log_20250102_1.txt"
   */
  String generateLogFilename();
  
  /**
   * Get log level prefix string ([ERROR], [WARN], etc.)
   */
  const char* getLevelPrefix(LogLevel level) const;
  
  /**
   * Escape JSON special characters in strings
   */
  String escapeJsonString(const String& input) const;
  
  /**
   * List files in directory for debugging
   * @param path Directory path
   */
  void listDirectoryContents(const String& path) const;
  
}; // class UtilityEngine

#endif // UTILITY_ENGINE_H
