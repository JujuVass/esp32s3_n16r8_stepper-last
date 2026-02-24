// ============================================================================
// API ROUTES MANAGER - Implementation
// ============================================================================
// HTTP server routes for ESP32 Stepper Controller.
// Direct LittleFS access: Justified here because APIRoutes is the HTTP
// layer responsible for serving static files and persisting JSON data
// (playlists, stats). FilesystemManager handles upload/format operations;
// this module handles read/write of specific data files as part of the API.
// ============================================================================

#include "communication/APIRoutes.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "core/UtilityEngine.h"
#include "core/TimeUtils.h"
#include "movement/SequenceTableManager.h"
#include "communication/WiFiConfigManager.h"
#include "communication/NetworkManager.h"
#include "communication/FilesystemManager.h"

// External globals
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern UtilityEngine* engine;

// External LED control (defined in main .ino)
extern void setRgbLed(uint8_t r, uint8_t g, uint8_t b);

// ============================================================================
// ASYNC BODY COLLECTION HELPERS
// ============================================================================

/**
 * Reusable body collector for the onBody callback of ESPAsyncWebServer.
 * Accumulates incoming chunks into a String stored in request->_tempObject.
 */
void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    request->_tempObject = new String();
    static_cast<String*>(request->_tempObject)->reserve(total);
  }
  static_cast<String*>(request->_tempObject)->concat(reinterpret_cast<char*>(data), len);
}

/**
 * Extract the collected body from request->_tempObject and clean up.
 * Returns an empty String if no body was collected.
 */
String getBody(AsyncWebServerRequest* request) {
  String body;
  if (request->_tempObject) {
    body = *static_cast<String*>(request->_tempObject);
    delete static_cast<String*>(request->_tempObject);
    request->_tempObject = nullptr;
  }
  return body;
}

// ============================================================================
// STATIC FILE SERVER - Auto-serves any file from LittleFS
// ============================================================================

bool serveStaticFile(AsyncWebServerRequest* request, const String& path) {
  String filePath = path;

  // Handle root -> index.html
  if (filePath == "/") filePath = "/index.html";

  // Check if file exists
  if (!LittleFS.exists(filePath)) {
    return false;
  }

  String mimeType = FilesystemManager::getContentType(filePath);

  // Create async response from LittleFS
  AsyncWebServerResponse* response = request->beginResponse(LittleFS, filePath, mimeType);

  // Set cache headers based on file type
  if (filePath.endsWith(".html") || filePath.endsWith(".json")) {
    // HTML/JSON: no cache (always fresh)
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
  } else {
    // CSS/JS/assets: cache 24h (script loader adds cache-busting params for updates)
    response->addHeader("Cache-Control", "public, max-age=86400");
  }

  request->send(response);

  engine->debug("✅ Served: " + filePath + " (" + mimeType + ")");
  return true;
}

// ============================================================================
// CORS HELPER FUNCTIONS
// ============================================================================

void sendCORSHeaders(AsyncWebServerResponse* response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

void handleCORSPreflight(AsyncWebServerRequest* request) {
  AsyncWebServerResponse* response = request->beginResponse(204);
  sendCORSHeaders(response);
  request->send(response);
}

// ============================================================================
// HELPER FUNCTIONS IMPLEMENTATION
// ============================================================================

void sendJsonError(AsyncWebServerRequest* request, int code, const String& message) {
  JsonDocument doc;
  doc["success"] = false;
  doc["error"] = message;
  String json;
  serializeJson(doc, json);
  AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
  sendCORSHeaders(response);
  request->send(response);
}

void sendJsonSuccess(AsyncWebServerRequest* request, const String& message) {
  JsonDocument doc;
  doc["success"] = true;
  if (!message.isEmpty()) {
    doc["message"] = message;
  }
  String json;
  serializeJson(doc, json);
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
  sendCORSHeaders(response);
  request->send(response);
}

void sendJsonSuccessWithId(AsyncWebServerRequest* request, int id) {
  JsonDocument doc;
  doc["success"] = true;
  doc["id"] = id;
  String json;
  serializeJson(doc, json);
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
  sendCORSHeaders(response);
  request->send(response);
}

void sendEmptyPlaylistStructure(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["simple"] = JsonArray();
  doc["oscillation"] = JsonArray();
  doc["chaos"] = JsonArray();
  doc["pursuit"] = JsonArray();
  sendJsonDoc(request, doc);
}

bool parseJsonBody(AsyncWebServerRequest* request, JsonDocument& doc) {
  String body = getBody(request);
  if (body.isEmpty()) {
    sendJsonError(request, 400, "Missing JSON body");
    return false;
  }
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    sendJsonError(request, 400, "Invalid JSON");
    return false;
  }
  return true;
}

void sendJsonDoc(AsyncWebServerRequest* request, JsonDocument& doc, int code) {
  String json;
  serializeJson(doc, json);
  AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
  sendCORSHeaders(response);
  request->send(response);
}

bool requireFilesystem(AsyncWebServerRequest* request) {
  if (!engine || !engine->isFilesystemReady()) {
    sendJsonError(request, 500, "LittleFS not mounted");
    return false;
  }
  return true;
}

// Helper: Create empty file with initial content if it doesn't exist
bool ensureFileExists(const char* path, const char* defaultContent) {
  if (LittleFS.exists(path)) return true;  // Already exists

  File file = LittleFS.open(path, "w");
  if (!file) {
    engine->error("❌ Failed to create file: " + String(path));
    return false;
  }

  size_t written = file.print(defaultContent);

  // 🛡️ PROTECTION: Flush before closing
  file.flush();

  bool success = (file && written > 0);
  file.close();

  if (success) {
    engine->info("📁 Created missing file: " + String(path));
  } else {
    engine->error("❌ Failed to initialize file: " + String(path));
  }

  return success;
}

// ============================================================================
// PLAYLIST FILE HELPERS (DRY)
// ============================================================================

/**
 * Load and parse the playlist JSON file.
 * Sends appropriate error responses on failure.
 * @return true if playlistDoc is populated and ready to use
 */
bool loadPlaylistDoc(AsyncWebServerRequest* request, JsonDocument& playlistDoc) {
  if (!LittleFS.exists(PLAYLIST_FILE_PATH)) {
    sendJsonError(request, 404, "No playlists found");
    return false;
  }

  File file = LittleFS.open(PLAYLIST_FILE_PATH, "r");
  if (!file) {
    sendJsonError(request, 500, "Failed to read playlists");
    return false;
  }

  deserializeJson(playlistDoc, file);
  file.close();
  return true;
}

/**
 * Find a preset by mode and id in a loaded playlist document.
 * Sends 404 error if mode or preset not found.
 * @param outArray  Populated with the mode's JsonArray on success
 * @param outIndex  Set to the index of the found preset (-1 if not applicable)
 * @return true if the preset was found
 */
bool findPresetInMode(AsyncWebServerRequest* request, JsonDocument& playlistDoc, const char* mode, int id,
                      JsonArray& outArray, int& outIndex) {
  if (!playlistDoc[mode].is<JsonArray>()) {
    sendJsonError(request, 404, "Mode not found");
    return false;
  }

  outArray = playlistDoc[mode].as<JsonArray>();

  for (size_t i = 0; i < outArray.size(); i++) {
    JsonObject preset = outArray[i];
    if (preset["id"] == id) {
      outIndex = (int)i;
      return true;
    }
  }

  sendJsonError(request, 404, "Preset not found");
  return false;
}

// ============================================================================
// STATS FILE HELPERS (DRY)
// ============================================================================

/**
 * Read stats from /stats.json, detecting old format (direct array) vs new format.
 * @param statsDoc  Document to hold the parsed JSON (caller must keep alive)
 * @param outArray  Populated with the stats JsonArray on success
 * @return true if stats were loaded successfully
 */
bool readStatsArray(JsonDocument& statsDoc, JsonArray& outArray) {
  if (!engine->loadJsonFile("/stats.json", statsDoc)) return false;

  if (statsDoc.is<JsonArray>()) {
    // OLD FORMAT: Direct array [{"date":"...","distanceMM":...}, ...]
    engine->debug("📥 Detected OLD stats format (direct array)");
    outArray = statsDoc.as<JsonArray>();
    return true;
  } else if (statsDoc["stats"].is<JsonArray>()) {
    // NEW FORMAT: {"stats": [...]}
    outArray = statsDoc["stats"].as<JsonArray>();
    return true;
  }

  return false;
}


// ============================================================================
// ROUTE HANDLER FUNCTIONS (extracted from setupAPIRoutes lambdas)
// ============================================================================

// --- Stats handlers ---

static void handleGetStats(AsyncWebServerRequest* request) {
  if (!LittleFS.exists("/stats.json")) {
    // Create empty stats file
    ensureFileExists("/stats.json", "[]");
    request->send(200, "application/json", "[]");
    return;
  }

  // Load via facade (consistent error handling + logging)
  JsonDocument doc;
  if (!engine->loadJsonFile("/stats.json", doc)) {
    sendJsonError(request, 500, "Failed to read stats file");
    return;
  }

  // Normalize to old format (direct array) for frontend compatibility
  String response;
  if (doc.is<JsonArray>()) {
    // Already old format - serialize directly
    serializeJson(doc, response);
  } else if (doc["stats"].is<JsonArray>()) {
    // New format - extract stats array and send only that
    JsonArray statsArray = doc["stats"].as<JsonArray>();
    serializeJson(statsArray, response);
  } else {
    sendJsonError(request, 500, "Invalid stats file structure");
    return;
  }

  request->send(200, "application/json", response);
}

static void handleIncrementStats(AsyncWebServerRequest* request) {
  JsonDocument requestDoc;
  if (!parseJsonBody(request, requestDoc)) return;

  float distanceMM = requestDoc["distanceMM"] | 0.0f;
  if (distanceMM <= 0) {
    sendJsonError(request, 400, "Invalid distance");
    return;
  }

  // DRY: Delegate to StatsManager which handles date, file I/O, and format
  engine->incrementDailyStats(distanceMM);
  engine->info(String("📊 Stats updated via API: +") + String(distanceMM, 1) + "mm");
  sendJsonSuccess(request);
}

static void handleExportStats(AsyncWebServerRequest* request) {
  if (!LittleFS.exists("/stats.json")) {
    // Return empty structure if no stats
    JsonDocument doc;
    doc["exportDate"] = engine->getFormattedTime("%Y-%m-%d");
    doc["totalDistanceMM"] = 0;
    doc["stats"].to<JsonArray>();

    sendJsonDoc(request, doc);
    engine->info("📥 Stats export: empty (no data)");
    return;
  }

  // Read stats using shared helper (handles old/new format)
  JsonDocument statsDoc;
  JsonArray sourceStats;
  if (!readStatsArray(statsDoc, sourceStats)) {
    sendJsonError(request, 500, "Stats file corrupted or invalid structure");
    return;
  }

  // Build export structure with metadata
  JsonDocument exportDoc;
  exportDoc["exportDate"] = engine->getFormattedTime("%Y-%m-%d");
  exportDoc["exportTime"] = engine->getFormattedTime("%H:%M:%S");
  exportDoc["version"] = "1.0";

  JsonArray statsArray = exportDoc["stats"].to<JsonArray>();
  float totalMM = 0;
  for (JsonVariant entry : sourceStats) {
    statsArray.add(entry);
    totalMM += entry["distanceMM"].as<float>();
  }

  sendJsonDoc(request, exportDoc);

  engine->info("📥 Stats exported: " + String(statsArray.size()) + " entries, " +
               String(totalMM / 1000000.0, 3) + " km total");
}

static void handleImportStats(AsyncWebServerRequest* request) {
  String body = getBody(request);

  engine->info("📥 Stats import request - body size: " + String(body.length()) + " bytes");

  if (body.isEmpty()) {
    engine->error("❌ Empty body received for stats import");
    sendJsonError(request, 400, "Empty request body");
    return;
  }

  JsonDocument importDoc;
  DeserializationError error = deserializeJson(importDoc, body);

  if (error) {
    sendJsonError(request, 400, "Invalid JSON format");
    return;
  }

  // Validate structure
  if (!importDoc["stats"].is<JsonArray>()) {
    sendJsonError(request, 400, "Missing or invalid 'stats' array in import data");
    return;
  }

  JsonArray importStats = importDoc["stats"].as<JsonArray>();
  if (importStats.size() == 0) {
    sendJsonError(request, 400, "No stats to import");
    return;
  }

  // Validate each entry has required fields
  for (JsonVariant entry : importStats) {
    if (!entry["date"].is<const char*>() || !entry["distanceMM"].is<float>()) {
      sendJsonError(request, 400, "Invalid entry format (missing date or distanceMM)");
      return;
    }
  }

  // ============================================================================
  // IMPORTANT: Always save as OLD FORMAT (direct array) to /stats.json
  // The NEW FORMAT (object with metadata) is only used for exports
  // This ensures compatibility and avoids format confusion during increments
  // ============================================================================
  JsonDocument saveDoc;
  JsonArray saveArray = saveDoc.to<JsonArray>();

  float totalMM = 0;
  for (JsonVariant entry : importStats) {
    saveArray.add(entry);
    totalMM += entry["distanceMM"].as<float>();
  }

  // Save via facade (consistent flush + verification)
  if (!engine->saveJsonFile("/stats.json", saveDoc)) {
    sendJsonError(request, 500, "Failed to create stats file");
    return;
  }

  engine->info("📤 Stats imported: " + String(saveArray.size()) + " entries, " +
               String(totalMM / 1000000.0, 3) + " km total");

  // Return success response with import summary
  JsonDocument responseDoc;
  responseDoc["success"] = true;
  responseDoc["entriesImported"] = saveArray.size();
  responseDoc["totalDistanceMM"] = totalMM;

  sendJsonDoc(request, responseDoc);
}

// --- Playlist handlers ---

static void handleGetPlaylists(AsyncWebServerRequest* request) {
  if (!engine) {
    sendJsonError(request, 500, "Engine not initialized");
    return;
  }
  if (!engine->isFilesystemReady()) {
    engine->error("❌ GET /api/playlists: LittleFS not mounted");
    sendJsonError(request, 500, "LittleFS not mounted");
    return;
  }

  if (!LittleFS.exists(PLAYLIST_FILE_PATH)) {
    // Create empty playlists file
    const char* emptyPlaylists = R"({"simple":[],"oscillation":[],"chaos":[],"pursuit":[]})";
    ensureFileExists(PLAYLIST_FILE_PATH, emptyPlaylists);
    sendEmptyPlaylistStructure(request);
    return;
  }

  File file = LittleFS.open(PLAYLIST_FILE_PATH, "r");
  if (!file) {
    engine->error("❌ GET /api/playlists: Failed to open file");
    sendJsonError(request, 500, "Failed to open playlists file");
    return;
  }

  String content = file.readString();
  size_t fileSize = file.size();
  file.close();

  engine->debug("📋 GET /api/playlists: File size=" + String(fileSize) + ", content length=" + String(content.length()));

  if (content.isEmpty()) {
    engine->warn("⚠️ Playlist file exists but is empty");
    sendEmptyPlaylistStructure(request);
    return;
  }

  // Validate JSON integrity
  JsonDocument testDoc;
  DeserializationError testError = deserializeJson(testDoc, content);
  if (testError) {
    engine->error("❌ Playlist JSON corrupted! Error: " + String(testError.c_str()));
    engine->warn("🔧 Backing up corrupted file and resetting playlists");

    // Backup corrupted file
    String backupPath = String(PLAYLIST_FILE_PATH) + ".corrupted";
    LittleFS.rename(PLAYLIST_FILE_PATH, backupPath.c_str());

    sendEmptyPlaylistStructure(request);
    return;
  }

  engine->debug("✅ Returning playlist content: " + content.substring(0, 100) + "...");
  request->send(200, "application/json", content);
}

static void handleAddPreset(AsyncWebServerRequest* request) {
  if (!requireFilesystem(request)) return;

  JsonDocument reqDoc;
  if (!parseJsonBody(request, reqDoc)) return;

  const char* mode = reqDoc["mode"];
  const char* name = reqDoc["name"];
  JsonObject configData = reqDoc["config"];

  if (!mode || !name || configData.isNull()) {
    sendJsonError(request, 400, "Missing required fields");
    return;
  }

  // Validation: refuse infinite durations
  if (strcmp(mode, "oscillation") == 0) {
    int cycleCount = configData["cycleCount"] | -1;
    if (cycleCount == 0) {
      sendJsonError(request, 400, "Infinite cycles not allowed in playlist");
      return;
    }
  } else if (strcmp(mode, "chaos") == 0) {
    int duration = configData["durationSeconds"] | -1;
    if (duration == 0) {
      sendJsonError(request, 400, "Infinite duration not allowed in playlist");
      return;
    }
  }

  // Load existing playlists
  JsonDocument playlistDoc;
  bool fileLoaded = engine->loadJsonFile(PLAYLIST_FILE_PATH, playlistDoc);

  // If file doesn't exist or is empty, initialize empty structure
  if (!fileLoaded) {
    engine->info("📋 Playlist file not found, creating new structure");
    // Create empty arrays properly attached to document
    playlistDoc["simple"].to<JsonArray>();
    playlistDoc["oscillation"].to<JsonArray>();
    playlistDoc["chaos"].to<JsonArray>();
  }

  // Debug: Check what's in the loaded document
  auto jsonTypeOf = [](JsonVariant v) {
    if (v.isNull()) return "null";
    if (v.is<JsonArray>()) return "array";
    return "other";
  };
  engine->debug("📋 Loaded doc - simple: " + String(jsonTypeOf(playlistDoc["simple"])));
  engine->debug("📋 Loaded doc - oscillation: " + String(jsonTypeOf(playlistDoc["oscillation"])));
  engine->debug("📋 Loaded doc - chaos: " + String(jsonTypeOf(playlistDoc["chaos"])));

  // Get or create mode array
  JsonArray modeArray;
  if (playlistDoc[mode].isNull() || !playlistDoc[mode].is<JsonArray>()) {
    // Mode doesn't exist yet, create it
    modeArray = playlistDoc[mode].to<JsonArray>();
    engine->debug("📋 Created new array for mode: " + String(mode));
  } else {
    // Mode exists, use it
    modeArray = playlistDoc[mode].as<JsonArray>();
    engine->debug("📋 Using existing array for mode: " + String(mode) + ", size: " + String(modeArray.size()));
  }

  // Check limit
  if (modeArray.size() >= MAX_PRESETS_PER_MODE) {
    sendJsonError(request, 400, "Maximum 20 presets reached");
    return;
  }

  // Find next available ID
  int nextId = 1;
  for (JsonObject preset : modeArray) {
    int id = preset["id"] | 0;
    if (id >= nextId) {
      nextId = id + 1;
    }
  }

  // Create new preset
  JsonObject newPreset = modeArray.add<JsonObject>();
  newPreset["id"] = nextId;
  newPreset["name"] = name;
  newPreset["timestamp"] = static_cast<unsigned long>(TimeUtils::epochSeconds());
  newPreset["config"] = configData;

  engine->debug("📋 Adding preset: mode=" + String(mode) + ", id=" + String(nextId) + ", name=" + String(name));
  engine->debug("📋 Array size after add: " + String(modeArray.size()));

  // Save to file
  if (!engine->saveJsonFile(PLAYLIST_FILE_PATH, playlistDoc)) {
    engine->error("❌ Failed to save playlist");
    sendJsonError(request, 500, "Failed to save");
    return;
  }

  engine->info("📋 Preset added: " + String(name) + " (mode: " + String(mode) + ")");
  sendJsonSuccessWithId(request, nextId);
}

static void handleDeletePreset(AsyncWebServerRequest* request) {
  if (!requireFilesystem(request)) return;

  JsonDocument reqDoc;
  if (!parseJsonBody(request, reqDoc)) return;

  const char* mode = reqDoc["mode"];
  int id = reqDoc["id"] | 0;

  if (!mode || id == 0) {
    sendJsonError(request, 400, "Missing mode or id");
    return;
  }

  // Load and find preset using shared helpers
  JsonDocument playlistDoc;
  if (!loadPlaylistDoc(request, playlistDoc)) return;

  JsonArray modeArray;
  int index = -1;
  if (!findPresetInMode(request, playlistDoc, mode, id, modeArray, index)) return;

  modeArray.remove(index);

  if (!engine->saveJsonFile(PLAYLIST_FILE_PATH, playlistDoc)) {
    engine->error("❌ Failed to save playlist after delete");
    sendJsonError(request, 500, "Failed to save");
    return;
  }

  engine->info("🗑️ Preset deleted: ID " + String(id) + " (mode: " + String(mode) + "), " + String(modeArray.size()) + " remaining");
  sendJsonSuccess(request);
}

static void handleUpdatePreset(AsyncWebServerRequest* request) {
  if (!requireFilesystem(request)) return;

  JsonDocument reqDoc;
  if (!parseJsonBody(request, reqDoc)) return;

  const char* mode = reqDoc["mode"];
  int id = reqDoc["id"] | 0;
  const char* newName = reqDoc["name"];

  if (!mode || id == 0 || !newName) {
    sendJsonError(request, 400, "Missing required fields");
    return;
  }

  // Load and find preset using shared helpers
  JsonDocument playlistDoc;
  if (!loadPlaylistDoc(request, playlistDoc)) return;

  JsonArray modeArray;
  int index = -1;
  if (!findPresetInMode(request, playlistDoc, mode, id, modeArray, index)) return;

  modeArray[index]["name"] = newName;

  if (!engine->saveJsonFile(PLAYLIST_FILE_PATH, playlistDoc)) {
    engine->error("❌ Failed to save playlist after rename");
    sendJsonError(request, 500, "Failed to save");
    return;
  }

  engine->info("✏️ Preset renamed: ID " + String(id) + " -> " + String(newName));
  sendJsonSuccess(request);
}

// --- Logs & System handlers ---

static void handleClearLogs(AsyncWebServerRequest* request) {
  int deletedCount = 0;

  // Delete all files in /logs directory
  File logsDir = LittleFS.open("/logs");
  if (logsDir && logsDir.isDirectory()) {
    for (File logFile = logsDir.openNextFile(); logFile; logFile = logsDir.openNextFile()) {
      if (logFile.isDirectory()) continue;

      auto fileName = String(logFile.name());
      logFile.close();

      String fullPath = "/logs/" + fileName;
      if (LittleFS.remove(fullPath)) {
        engine->info("🗑️ Deleted: " + fullPath);
        deletedCount++;
      } else {
        engine->error("❌ Failed to delete: " + fullPath);
      }
    }
    logsDir.close();
  }

  engine->info("📋 Deleted " + String(deletedCount) + " log files");
  JsonDocument doc;
  doc["status"] = "ok";
  doc["message"] = String(deletedCount) + " logs deleted";
  doc["count"] = deletedCount;
  sendJsonDoc(request, doc);
}

static void handleSetLoggingPreferences(AsyncWebServerRequest* request) {
  JsonDocument doc;
  if (!parseJsonBody(request, doc)) return;

  // Update logging enabled state
  if (doc["loggingEnabled"].is<bool>()) {
    bool enabled = doc["loggingEnabled"];
    engine->setLoggingEnabled(enabled);
    engine->info(enabled ? "✅ Logging ENABLED" : "❌ Logging DISABLED");
  }

  // Update log level (ERROR=0, WARN=1, INFO=2, DEBUG=3)
  if (doc["logLevel"].is<int>()) {
    int level = doc["logLevel"];
    if (level >= 0 && level <= 3) {
      engine->setLogLevel((LogLevel)level);
      engine->info("📊 Log level set to: " + String(level));
    }
  }

  // Save to NVS
  engine->saveLoggingPreferences();

  sendJsonSuccess(request, "Logging preferences saved");
}

// --- Dumps handlers ---

static void handleListDumps(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();

  File dir = LittleFS.open("/dumps");
  if (dir && dir.isDirectory()) {
    File f = dir.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        JsonObject entry = files.add<JsonObject>();
        entry["name"] = String(f.name());
        entry["size"] = f.size();
        entry["path"] = "/dumps/" + String(f.name());
      }
      f = dir.openNextFile();
    }
  }

  String responseBody;
  serializeJson(doc, responseBody);
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", responseBody);
  sendCORSHeaders(response);
  request->send(response);
}

static void handleGetLatestDump(AsyncWebServerRequest* request) {
  String latestName;

  File dir = LittleFS.open("/dumps");
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      String name = f.name();
      if (name > latestName) latestName = name;  // Lexicographic = chronological (YYYYMMDD_HHMMSS)
    }
  }

  if (latestName.isEmpty()) {
    AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", "No crash dumps found.");
    sendCORSHeaders(response);
    request->send(response);
    return;
  }

  String content = engine->readFileAsString("/dumps/" + latestName);
  AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", content);
  sendCORSHeaders(response);
  request->send(response);
}

// --- WiFi handlers ---

static void handleWifiScan(AsyncWebServerRequest* request) {
  engine->info("📡 WiFi scan requested via API");

  // In AP-only mode, we need to be careful with scanning
  bool wasAPOnly = StepperNetwork.isAPSetupMode() || StepperNetwork.isAPDirectMode();

  if (wasAPOnly) {
    engine->info("📡 AP mode: preparing for scan...");
    // Brief delay to let AP stabilize
    delay(100);
  }

  std::array<WiFiNetworkInfo, 15> networks{};
  int count = WiFiConfig.scanNetworks(networks.data(), 15);

  JsonDocument doc;
  JsonArray networksArray = doc["networks"].to<JsonArray>();

  for (int i = 0; i < count; i++) {
    JsonObject net = networksArray.add<JsonObject>();
    net["ssid"] = networks[i].ssid;
    net["rssi"] = networks[i].rssi;
    net["encryption"] = WiFiConfigManager::encryptionTypeToString(networks[i].encryptionType);
    net["channel"] = networks[i].channel;
    net["secure"] = (networks[i].encryptionType != WIFI_AUTH_OPEN);
  }

  doc["count"] = count;
  doc["apMode"] = wasAPOnly;

  sendJsonDoc(request, doc);
}

static void handleWifiSave(AsyncWebServerRequest* request) {
  JsonDocument doc;
  if (!parseJsonBody(request, doc)) return;

  String wifiSsid = doc["ssid"] | "";
  String wifiPassword = doc["password"] | "";

  if (wifiSsid.isEmpty()) {
    sendJsonError(request, 400, "SSID required");
    return;
  }

  engine->info("💾 Saving WiFi config to NVS: " + wifiSsid);

  bool saved = WiFiConfig.saveConfig(wifiSsid, wifiPassword);

  if (saved) {
    JsonDocument respDoc;
    respDoc["success"] = true;
    respDoc["message"] = "WiFi configuration saved";
    respDoc["ssid"] = wifiSsid;
    respDoc["rebootRequired"] = true;

    sendJsonDoc(request, respDoc);

    engine->info("✅ WiFi config saved successfully");
  } else {
    sendJsonError(request, 500, "Failed to save WiFi config");
  }
}

static void handleWifiConnect(AsyncWebServerRequest* request) {
  // Block if in STA+AP mode - must use AP_SETUP mode to configure
  if (StepperNetwork.isSTAMode()) {
    sendJsonError(request, 403, "WiFi config disabled when connected. Use AP_SETUP mode (GPIO 19 to GND) to change settings.");
    return;
  }

  JsonDocument doc;
  if (!parseJsonBody(request, doc)) return;

  String wifiSsid = doc["ssid"] | "";
  String wifiPassword = doc["password"] | "";

  if (wifiSsid.isEmpty()) {
    sendJsonError(request, 400, "SSID required");
    return;
  }

  engine->info("🔌 Testing WiFi: " + wifiSsid);

  // Save credentials to NVS before testing
  engine->info("💾 Saving WiFi credentials...");
  bool saved = WiFiConfig.saveConfig(wifiSsid, wifiPassword);

  if (!saved) {
    sendJsonError(request, 500, "Failed to save WiFi config");
    return;
  }

  engine->info("✅ WiFi credentials saved - now testing connection...");

  // Test connection (we're in AP_STA mode so AP stays stable)
  bool connected = WiFiConfig.testConnection(wifiSsid, wifiPassword, 15000);

  if (connected) {
    // LED GREEN = Success! Stop blinking
    StepperNetwork.apLedBlinkEnabled = false;
    setRgbLed(0, 50, 0);

    JsonDocument respDoc;
    respDoc["success"] = true;
    respDoc["message"] = "WiFi configured successfully!";
    respDoc["ssid"] = wifiSsid;
    respDoc["rebootRequired"] = true;
    respDoc["hostname"] = String(otaHostname) + ".local";

    sendJsonDoc(request, respDoc);

    engine->info("✅ WiFi config saved AND tested successfully - waiting for reboot");
  } else {
    // LED ORANGE = Saved but connection test failed
    StepperNetwork.apLedBlinkEnabled = false;
    setRgbLed(25, 10, 0);  // Orange = Warning

    // Credentials already saved, reboot will try to connect
    JsonDocument respDoc;
    respDoc["success"] = true;  // Config IS saved even though test failed
    respDoc["warning"] = "WiFi credentials saved but connection test failed";
    respDoc["message"] = "Configuration saved. Reboot to try connecting.";
    respDoc["details"] = "Connection test timed out - password may be wrong or signal too weak";
    respDoc["ssid"] = wifiSsid;
    respDoc["rebootRequired"] = true;

    sendJsonDoc(request, respDoc);

    engine->warn("⚠️ WiFi saved but test failed: " + wifiSsid + " (will retry on reboot)");
  }
}

// --- Fallback handler ---

static void handleNotFound(AsyncWebServerRequest* request) {
  String uri = request->url();
  String method;
  if (request->method() == HTTP_GET) method = "GET";
  else if (request->method() == HTTP_POST) method = "POST";
  else if (request->method() == HTTP_OPTIONS) method = "OPTIONS";
  else method = "OTHER";
  engine->debug("📥 Request: " + method + " " + uri);

  // In AP_SETUP mode, redirect everything except /setup.html and /api/wifi/ to /setup.html
  if (StepperNetwork.isAPSetupMode()) {
    if (uri != "/setup.html" && !uri.startsWith("/api/wifi")) {
      AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "Redirecting to setup.html");
      response->addHeader("Location", "http://192.168.4.1/setup.html");
      request->send(response);
      return;
    }
  } else {
    // In STA+AP or AP_DIRECT mode, block access to setup.html
    if (uri == "/setup.html") {
      request->send(404, "text/plain", "Not found: setup.html (use GPIO 19 to GND for setup mode)");
      return;
    }
  }
  // Try to serve the file using our helper (handles caching)
  if (serveStaticFile(request, uri)) {
    return;  // File served successfully
  }
  // File not found
  engine->warn("⚠️ 404: " + uri);
  request->send(404, "text/plain", "Not found: " + uri);
}

// ============================================================================
// API ROUTES SETUP
// ============================================================================

void setupAPIRoutes() {  // NOSONAR(cpp:S3776) — sequential route registration, no branching depth
  // ============================================================================
  // CORS PREFLIGHT HANDLER - Must be first!
  // ============================================================================
  // Handle OPTIONS preflight for all /api/ routes
  server.on("/api/stats/import", HTTP_OPTIONS, handleCORSPreflight);
  server.on("/api/playlists", HTTP_OPTIONS, handleCORSPreflight);
  server.on("/api/command", HTTP_OPTIONS, handleCORSPreflight);

  // ============================================================================
  // AUTOMATIC STATIC FILE SERVING
  // ============================================================================
  // Instead of manually declaring each CSS/JS route, we use onNotFound to
  // automatically serve any file from LittleFS. This means:
  // - Add/remove/rename JS files freely - no code changes needed
  // - Subdirectories (js/core/, js/modules/) work automatically
  // - Only API routes (/api/) need explicit handlers below
  // ============================================================================

  // Root route explicitly for faster response
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      if (StepperNetwork.isAPSetupMode()) {
        AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "Redirecting to setup.html");
        response->addHeader("Location", "/setup.html");
        request->send(response);
        return;
      }
      if (!serveStaticFile(request, "/index.html")) {
        request->send(404, "text/plain", "❌ File not found: index.html\nPlease upload filesystem using: platformio run --target uploadfs");
      }
    });

  // ========================================================================
  // IP RESOLUTION ENDPOINT (avoids mDNS for WebSocket)
  // ========================================================================

  // GET /api/ip - Returns ESP32 IP address for direct WebSocket connection
  // Returns the IP matching the interface the client connected through:
  // - Client on AP (192.168.4.x) → returns AP IP (192.168.4.1)
  // - Client on STA (router network) → returns STA IP
  server.on("/api/ip", HTTP_GET, [](AsyncWebServerRequest* request) {
    // Determine which interface the client is connected to
    String clientIP = request->client()->remoteIP().toString();
    String responseIP;
    if (clientIP.startsWith("192.168.4.")) {
      // Client is on the AP network → return AP IP
      responseIP = WiFi.softAPIP().toString();
    } else {
      // Client is on the STA/router network → return STA IP
      responseIP = StepperNetwork.getIPAddress();
    }
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", R"({"ip":")" + responseIP + R"("})");
    sendCORSHeaders(response);
    request->send(response);
  });

  // ========================================================================
  // STATISTICS API ROUTES
  // ========================================================================

  // GET /api/stats - Retrieve all daily stats
  server.on("/api/stats", HTTP_GET, handleGetStats);

  // POST /api/stats/increment - Add distance to today's stats
  server.on("/api/stats/increment", HTTP_POST, handleIncrementStats, NULL, collectBody);

  // POST /api/stats/clear - Delete all stats
  server.on("/api/stats/clear", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (LittleFS.exists("/stats.json")) {
      if (LittleFS.remove("/stats.json")) {
        engine->info("🗑️ Statistics cleared");
        sendJsonSuccess(request);
      } else {
        sendJsonError(request, 500, "Failed to delete stats");
      }
    } else {
      sendJsonSuccess(request, "No stats to clear");
    }
  });

  // GET /api/stats/export - Export all stats as JSON
  server.on("/api/stats/export", HTTP_GET, handleExportStats);

  // POST /api/stats/import - Import stats from JSON
  server.on("/api/stats/import", HTTP_POST, handleImportStats, NULL, collectBody);

  // ============================================================================
  // PLAYLIST API ENDPOINTS
  // ============================================================================

  // GET /api/playlists - Get all playlists
  server.on("/api/playlists", HTTP_GET, handleGetPlaylists);

  // POST /api/playlists/add - Add a preset to playlist
  server.on("/api/playlists/add", HTTP_POST, handleAddPreset, NULL, collectBody);

  // POST /api/playlists/delete - Delete a preset
  server.on("/api/playlists/delete", HTTP_POST, handleDeletePreset, NULL, collectBody);

  // POST /api/playlists/update - Update (rename) a preset
  server.on("/api/playlists/update", HTTP_POST, handleUpdatePreset, NULL, collectBody);

  // ============================================================================
  // LOGS MANAGEMENT ROUTES
  // ============================================================================

  // GET /logs - List all log files as HTML directory browser
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
    String html = R"(
      <html>
      <head>
        <title>Log Files</title>
        <style>
          body { font-family: monospace; margin: 20px; }
          a { display: block; padding: 5px; color: #2196F3; text-decoration: none; }
          a:hover { text-decoration: underline; }
        </style>
      </head>
      <body>
        <h1>📋 Log Files</h1>
        <ul>
    )";

    // List all files in /logs directory
    File logsDir = LittleFS.open("/logs");
    if (logsDir && logsDir.isDirectory()) {
      File logFile = logsDir.openNextFile();
      while (logFile) {
        if (!logFile.isDirectory()) {
          String filename = logFile.name();
          html += "<li><a href='/logs/" + filename + "' download>" + filename + "</a></li>";
        }
        logFile = logsDir.openNextFile();
      }
      logsDir.close();
    }

    html += R"(
        </ul>
      </body>
      </html>
    )";

    request->send(200, "text/html; charset=UTF-8", html);
  });

  // POST /logs/clear - Clear all log files
  server.on("/logs/clear", HTTP_POST, handleClearLogs);

  // ============================================================================
  // SYSTEM MANAGEMENT ROUTES
  // ============================================================================

  // GET /api/ping - Simple health check endpoint
  server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["uptime"] = millis();
    sendJsonDoc(request, doc);
  });

  // POST /api/system/reboot - Reboot ESP32
  server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
    engine->info("🔄 Reboot requested via API");

    // Send success response before rebooting
    sendJsonSuccess(request, "Rebooting ESP32...");

    // Safe shutdown: stop movement, disable motor, flush logs
    StepperNetwork.safeShutdown();

    // Small delay to ensure response is sent
    delay(500);

    // Reboot ESP32
    ESP.restart();
  });

  // POST /api/system/wifi/reconnect - Reconnect WiFi
  server.on("/api/system/wifi/reconnect", HTTP_POST, [](AsyncWebServerRequest* request) {
    engine->info("📶 WiFi reconnect requested via API");

    // Send success response before disconnecting
    sendJsonSuccess(request, "Reconnecting WiFi...");

    // Simple reconnect - WiFi.reconnect() handles everything
    // Don't call disconnect() first - it can cause issues
    delay(100);
    WiFi.reconnect();

    engine->info("📶 WiFi reconnection initiated");
  });

  // ========================================================================
  // LOGGING PREFERENCES API
  // ========================================================================

  // GET /api/system/logging/preferences - Get current logging preferences
  server.on("/api/system/logging/preferences", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["loggingEnabled"] = engine->isLoggingEnabled();
    doc["logLevel"] = (int)engine->getLogLevel();

    sendJsonDoc(request, doc);
  });

  // POST /api/system/logging/preferences - Update logging preferences
  server.on("/api/system/logging/preferences", HTTP_POST, handleSetLoggingPreferences, NULL, collectBody);

  // ========================================================================
  // CRASH DUMPS API (readable over OTA — no USB needed)
  // ========================================================================

  // GET /api/system/dumps - List all crash dump files
  server.on("/api/system/dumps", HTTP_GET, handleListDumps);

  // GET /api/system/dumps/latest - Get most recent crash dump content
  server.on("/api/system/dumps/latest", HTTP_GET, handleGetLatestDump);

  // ============================================================================
  // WIFI CONFIGURATION API
  // ============================================================================

  // GET /api/wifi/scan - Scan available WiFi networks
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);

  // GET /api/wifi/config - Get current WiFi configuration (without password)
  server.on("/api/wifi/config", HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["configured"] = WiFiConfig.isConfigured();
    doc["ssid"] = WiFiConfig.getStoredSSID();
    doc["apMode"] = StepperNetwork.isAPMode();
    doc["staMode"] = StepperNetwork.isSTAMode();
    doc["ip"] = WiFi.localIP().toString();
    doc["apIp"] = WiFi.softAPIP().toString();

    sendJsonDoc(request, doc);
  });

  // POST /api/wifi/save - Save WiFi credentials to NVS without testing
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave, NULL, collectBody);

  // POST /api/wifi/connect - Test and save WiFi credentials
  // We start in AP_STA mode, so testing won't disrupt the AP connection
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect, NULL, collectBody);

  // POST /api/wifi/reboot - Explicit reboot after config
  server.on("/api/wifi/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
    engine->info("🔄 WiFi config reboot requested");
    sendJsonSuccess(request, "Rebooting...");
    delay(500);
    ESP.restart();
  });

  // POST /api/wifi/forget - Clear WiFi configuration
  server.on("/api/wifi/forget", HTTP_POST, [](AsyncWebServerRequest* request) {
    engine->info("🗑️ WiFi forget requested via API");

    WiFiConfig.clearConfig();

    sendJsonSuccess(request, "WiFi configuration cleared. Rebooting...");

    // Reboot to enter setup mode
    delay(1000);
    ESP.restart();
  });

  // ===== IMPORT SEQUENCE VIA HTTP POST (Bypass WebSocket size limit) =====
  server.on("/api/sequence/import", HTTP_POST, [](AsyncWebServerRequest* request) {
    String jsonData = getBody(request);
    if (jsonData.isEmpty()) {
      sendJsonError(request, 400, "No JSON body provided");
      return;
    }

    engine->info("📥 HTTP Import received: " + String(jsonData.length()) + " bytes");

    // Call the SequenceTableManager import function
    SeqTable.importFromJson(jsonData);

    sendJsonSuccess(request, "Sequence imported successfully");
  }, NULL, collectBody);

  // ========================================================================
  // CAPTIVE PORTAL DETECTION - Handle standard connectivity check URLs
  // Only active in AP_SETUP mode (AP_DIRECT serves the full app instead)
  // ========================================================================

  // Helper: Send captive portal redirect page (only in AP_SETUP mode)
  auto sendCaptivePortalRedirect = [](AsyncWebServerRequest* request) {
    // In AP_DIRECT or STA+AP mode, don't redirect - let OS think we have internet
    if (!StepperNetwork.isAPSetupMode()) {
      request->send(204);  // 204 No Content = "we have internet" for most OS
      return;
    }
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta http-equiv='refresh' content='0;url=http://192.168.4.1/setup.html'>";
    html += "<title>ESP32 WiFi Setup</title>";
    html += "</head><body>";
    html += "<h1>ESP32 Stepper Controller</h1>";
    html += "<p>Redirecting to WiFi configuration...</p>";
    html += "<p><a href='http://192.168.4.1/setup.html'>Click here if not redirected</a></p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  };

  // Android - expects 204 response, but we give HTML to trigger captive portal popup
  server.on("/generate_204", HTTP_GET, sendCaptivePortalRedirect);
  server.on("/gen_204", HTTP_GET, sendCaptivePortalRedirect);

  // Windows (NCSI - StepperNetwork Connectivity Status Indicator)
  // Windows expects specific text, returning anything else triggers captive portal
  server.on("/connecttest.txt", HTTP_GET, sendCaptivePortalRedirect);
  server.on("/ncsi.txt", HTTP_GET, sendCaptivePortalRedirect);
  server.on("/redirect", HTTP_GET, [sendCaptivePortalRedirect](AsyncWebServerRequest* request) {
    // Windows redirect page after detection
    sendCaptivePortalRedirect(request);
  });

  // Apple iOS/macOS
  server.on("/hotspot-detect.html", HTTP_GET, sendCaptivePortalRedirect);
  server.on("/library/test/success.html", HTTP_GET, sendCaptivePortalRedirect);

  // Firefox
  server.on("/success.txt", HTTP_GET, sendCaptivePortalRedirect);

  // Generic fallback for captive portal (Microsoft fwlink)
  server.on("/fwlink", HTTP_GET, sendCaptivePortalRedirect);

  // ========================================================================
  // FALLBACK - Auto-serve static files from LittleFS
  // ========================================================================
  server.onNotFound(handleNotFound);
}
