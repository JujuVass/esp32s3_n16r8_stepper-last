// ============================================================================
// API ROUTES MANAGER - Implementation
// ============================================================================
// HTTP server routes for ESP32 Stepper Controller
// ============================================================================

#include "web/APIRoutes.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "UtilityEngine.h"
#include "sequencer/SequenceTableManager.h"
#include "communication/WiFiConfigManager.h"

// External globals
extern WebServer server;
extern WebSocketsServer webSocket;
extern UtilityEngine* engine;

// ============================================================================
// MIME TYPE DETECTION
// ============================================================================

String getMimeType(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=UTF-8";
  if (path.endsWith(".css"))  return "text/css; charset=UTF-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=UTF-8";
  if (path.endsWith(".json")) return "application/json; charset=UTF-8";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif"))  return "image/gif";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".txt"))  return "text/plain; charset=UTF-8";
  if (path.endsWith(".log"))  return "text/plain; charset=UTF-8";
  return "application/octet-stream";
}

// ============================================================================
// STATIC FILE SERVER - Auto-serves any file from LittleFS
// ============================================================================

bool serveStaticFile(const String& path) {
  String filePath = path;
  
  // Handle root -> index.html
  if (filePath == "/") filePath = "/index.html";
  
  // Check if file exists
  if (!LittleFS.exists(filePath)) {
    return false;
  }
  
  File file = LittleFS.open(filePath, "r");
  if (!file) {
    engine->error("❌ Error opening: " + filePath);
    return false;
  }
  
  String mimeType = getMimeType(filePath);
  
  // Set cache headers based on file type
  if (filePath.endsWith(".html")) {
    // HTML: no cache (always fresh)
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
  } else {
    // CSS/JS/images: cache 24h
    server.sendHeader("Cache-Control", "public, max-age=86400");
  }
  
  server.streamFile(file, mimeType);
  file.close();
  
  engine->debug("✅ Served: " + filePath + " (" + mimeType + ")");
  return true;
}

// ============================================================================
// CORS HELPER FUNCTIONS
// ============================================================================

void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

void handleCORSPreflight() {
  sendCORSHeaders();
  server.send(204);  // No Content
}

// ============================================================================
// HELPER FUNCTIONS IMPLEMENTATION
// ============================================================================

String getFormattedDate() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
  return String(dateStr);
}

String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
  return String(timeStr);
}

void sendJsonError(int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  String json;
  serializeJson(doc, json);
  sendCORSHeaders();
  server.send(code, "application/json", json);
}

void sendJsonSuccess(const String& message) {
  JsonDocument doc;
  doc["success"] = true;
  if (message.length() > 0) {
    doc["message"] = message;
  }
  String json;
  serializeJson(doc, json);
  sendCORSHeaders();
  server.send(200, "application/json", json);
}

void sendJsonSuccessWithId(int id) {
  JsonDocument doc;
  doc["success"] = true;
  doc["id"] = id;
  String json;
  serializeJson(doc, json);
  sendCORSHeaders();
  server.send(200, "application/json", json);
}

void sendJsonApiError(int code, const String& message) {
  sendJsonError(code, message);
}

void sendEmptyPlaylistStructure() {
  JsonDocument doc;
  doc["vaet"] = JsonArray();
  doc["oscillation"] = JsonArray();
  doc["chaos"] = JsonArray();
  doc["pursuit"] = JsonArray();
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// ============================================================================
// API ROUTES SETUP
// ============================================================================

void setupAPIRoutes() {
  // ============================================================================
  // CORS PREFLIGHT HANDLER - Must be first!
  // ============================================================================
  // Handle OPTIONS preflight for all /api/* routes
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
  // - Only API routes (/api/*) need explicit handlers below
  // ============================================================================
  
  // Root route explicitly for faster response
  server.on("/", HTTP_GET, []() {
    if (!serveStaticFile("/index.html")) {
      server.send(404, "text/plain", "❌ File not found: index.html\nPlease upload filesystem using: platformio run --target uploadfs");
    }
  });

  // ========================================================================
  // STATISTICS API ROUTES
  // ========================================================================
  
  // GET /api/stats - Retrieve all daily stats
  server.on("/api/stats", HTTP_GET, []() {
    if (!LittleFS.exists("/stats.json")) {
      server.send(200, "application/json", "[]");
      return;
    }
    
    File file = LittleFS.open("/stats.json", "r");
    if (!file) {
      sendJsonError(500, "Failed to open stats file");
      return;
    }
    
    String content = file.readString();
    file.close();
    
    // Parse to detect format
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
      sendJsonError(500, "Stats file corrupted");
      return;
    }
    
    // Normalize to old format (direct array) for frontend compatibility
    String response;
    if (doc.is<JsonArray>()) {
      // Already old format - send as-is
      response = content;
    } else if (doc["stats"].is<JsonArray>()) {
      // New format - extract stats array and send only that
      JsonArray statsArray = doc["stats"].as<JsonArray>();
      serializeJson(statsArray, response);
    } else {
      sendJsonError(500, "Invalid stats file structure");
      return;
    }
    
    server.send(200, "application/json", response);
  });
  
  // POST /api/stats/increment - Add distance to today's stats
  server.on("/api/stats/increment", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      sendJsonError(400, "Missing JSON body");
      return;
    }
    
    String body = server.arg("plain");
    JsonDocument requestDoc;
    DeserializationError error = deserializeJson(requestDoc, body);
    
    if (error) {
      sendJsonError(400, "Invalid JSON");
      return;
    }
    
    float distanceMM = requestDoc["distanceMM"] | 0.0;
    if (distanceMM <= 0) {
      sendJsonError(400, "Invalid distance");
      return;
    }
    
    // Get current date (YYYY-MM-DD format)
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char dateStr[11];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
    
    // Load existing stats using UtilityEngine (Phase 3.1 - JSON Manager)
    JsonDocument statsDoc;
    engine->loadJsonFile("/stats.json", statsDoc);
    
    // Handle both old format (direct array) and new format (object with "stats" key)
    JsonArray statsArray;
    if (statsDoc.is<JsonArray>()) {
      // OLD FORMAT: Direct array
      statsArray = statsDoc.as<JsonArray>();
    } else if (statsDoc["stats"].is<JsonArray>()) {
      // NEW FORMAT: Object with "stats" key - extract the array
      statsArray = statsDoc["stats"].as<JsonArray>();
    } else {
      // Neither format - create new array (old format for backward compatibility)
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
    
    // ============================================================================
    // IMPORTANT: Always save as OLD FORMAT (direct array) to /stats.json
    // The NEW FORMAT (object with metadata) is only used for exports
    // This ensures compatibility and avoids format confusion during increments
    // ============================================================================
    JsonDocument saveDoc;
    JsonArray saveArray = saveDoc.to<JsonArray>();
    
    // Copy all entries to the save document (ensures clean array format)
    for (JsonVariant entry : statsArray) {
      saveArray.add(entry);
    }
    
    // Save back to file using UtilityEngine (Phase 3.1 - JSON Manager)
    if (!engine->saveJsonFile("/stats.json", saveDoc)) {
      sendJsonError(500, "Failed to write stats");
      return;
    }
    
    engine->info(String("📊 Stats updated: +") + String(distanceMM, 1) + "mm on " + String(dateStr));
    sendJsonSuccess();
  });
  
  // POST /api/stats/clear - Delete all stats
  server.on("/api/stats/clear", HTTP_POST, []() {
    if (LittleFS.exists("/stats.json")) {
      if (LittleFS.remove("/stats.json")) {
        engine->info("🗑️ Statistics cleared");
        sendJsonSuccess();
      } else {
        sendJsonError(500, "Failed to delete stats");
      }
    } else {
      sendJsonSuccess("No stats to clear");
    }
  });
  
  // GET /api/stats/export - Export all stats as JSON
  server.on("/api/stats/export", HTTP_GET, []() {
    if (!LittleFS.exists("/stats.json")) {
      // Return empty structure if no stats
      JsonDocument doc;
      doc["exportDate"] = getFormattedDate();
      doc["totalDistanceMM"] = 0;
      JsonArray statsArray = doc["stats"].to<JsonArray>();
      
      String json;
      serializeJson(doc, json);
      server.send(200, "application/json", json);
      engine->info("📥 Stats export: empty (no data)");
      return;
    }
    
    File file = LittleFS.open("/stats.json", "r");
    if (!file) {
      sendJsonError(500, "Failed to open stats file");
      return;
    }
    
    // Read existing stats
    String content = file.readString();
    file.close();
    
    JsonDocument statsDoc;
    DeserializationError error = deserializeJson(statsDoc, content);
    if (error) {
      sendJsonError(500, "Stats file corrupted");
      return;
    }
    
    // Build export structure with metadata
    JsonDocument exportDoc;
    exportDoc["exportDate"] = getFormattedDate();
    exportDoc["exportTime"] = getFormattedTime();
    exportDoc["version"] = "1.0";
    
    // Copy stats array - Handle both old format (direct array) and new format (object with "stats" key)
    JsonArray statsArray = exportDoc["stats"].to<JsonArray>();
    JsonArray sourceStats;
    
    // Check if old format (direct array) or new format (object with "stats")
    if (statsDoc.is<JsonArray>()) {
      // OLD FORMAT: Direct array [{"date":"2025-11-02","distanceMM":15489.83}, ...]
      engine->debug("📥 Detected OLD stats format (direct array) - migrating...");
      sourceStats = statsDoc.as<JsonArray>();
    } else if (statsDoc["stats"].is<JsonArray>()) {
      // NEW FORMAT: {"stats": [...]}
      sourceStats = statsDoc["stats"].as<JsonArray>();
    } else {
      sendJsonError(500, "Invalid stats file structure");
      return;
    }
    
    float totalMM = 0;
    for (JsonVariant entry : sourceStats) {
      statsArray.add(entry);
      totalMM += entry["distanceMM"].as<float>();
    }
    
    exportDoc["totalDistanceMM"] = totalMM;
    exportDoc["entriesCount"] = statsArray.size();
    
    String json;
    serializeJson(exportDoc, json);
    server.send(200, "application/json", json);
    
    engine->info("📥 Stats exported: " + String(statsArray.size()) + " entries, " + 
                 String(totalMM / 1000000.0, 3) + " km total");
  });
  
  // POST /api/stats/import - Import stats from JSON
  server.on("/api/stats/import", HTTP_POST, []() {
    String body = server.arg("plain");
    
    engine->info("📥 Stats import request - body size: " + String(body.length()) + " bytes");
    
    if (body.length() == 0) {
      engine->error("❌ Empty body received for stats import");
      sendJsonApiError(400, "Empty request body");
      return;
    }
    
    JsonDocument importDoc;
    DeserializationError error = deserializeJson(importDoc, body);
    
    if (error) {
      sendJsonApiError(400, "Invalid JSON format");
      return;
    }
    
    // Validate structure
    if (!importDoc["stats"].is<JsonArray>()) {
      sendJsonApiError(400, "Missing or invalid 'stats' array in import data");
      return;
    }
    
    JsonArray importStats = importDoc["stats"].as<JsonArray>();
    if (importStats.size() == 0) {
      sendJsonApiError(400, "No stats to import");
      return;
    }
    
    // Validate each entry has required fields
    for (JsonVariant entry : importStats) {
      if (!entry["date"].is<const char*>() || !entry["distanceMM"].is<float>()) {
        sendJsonApiError(400, "Invalid entry format (missing date or distanceMM)");
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
    
    // Save to file
    File file = LittleFS.open("/stats.json", "w");
    if (!file) {
      sendJsonApiError(500, "Failed to create stats file");
      return;
    }
    
    serializeJson(saveDoc, file);
    file.close();
    
    engine->info("📤 Stats imported: " + String(saveArray.size()) + " entries, " + 
                 String(totalMM / 1000000.0, 3) + " km total");
    
    // Return success response with import summary
    JsonDocument responseDoc;
    responseDoc["success"] = true;
    responseDoc["entriesImported"] = saveArray.size();
    responseDoc["totalDistanceMM"] = totalMM;
    
    String json;
    serializeJson(responseDoc, json);
    sendCORSHeaders();
    server.send(200, "application/json", json);
  });

  // ============================================================================
  // PLAYLIST API ENDPOINTS
  // ============================================================================
  
  // GET /api/playlists - Get all playlists
  server.on("/api/playlists", HTTP_GET, []() {
    if (!engine || !engine->isFilesystemReady()) {
      engine->error("❌ GET /api/playlists: LittleFS not mounted");
      sendJsonError(500, "LittleFS not mounted");
      return;
    }
    
    if (!LittleFS.exists(PLAYLIST_FILE_PATH)) {
      engine->debug("📋 GET /api/playlists: File not found, returning empty");
      sendEmptyPlaylistStructure();
      return;
    }
    
    File file = LittleFS.open(PLAYLIST_FILE_PATH, "r");
    if (!file) {
      engine->error("❌ GET /api/playlists: Failed to open file");
      sendJsonError(500, "Failed to open playlists file");
      return;
    }
    
    String content = file.readString();
    size_t fileSize = file.size();
    file.close();
    
    engine->debug("📋 GET /api/playlists: File size=" + String(fileSize) + ", content length=" + String(content.length()));
    
    if (content.length() == 0) {
      engine->warn("⚠️ Playlist file exists but is empty");
      sendEmptyPlaylistStructure();
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
      
      sendEmptyPlaylistStructure();
      return;
    }
    
    engine->debug("✅ Returning playlist content: " + content.substring(0, 100) + "...");
    server.send(200, "application/json", content);
  });
  
  // POST /api/playlists/add - Add a preset to playlist
  server.on("/api/playlists/add", HTTP_POST, []() {
    if (!engine || !engine->isFilesystemReady()) {
      sendJsonApiError(500, "LittleFS not mounted");
      return;
    }
    
    String body = server.arg("plain");
    JsonDocument reqDoc;
    DeserializationError error = deserializeJson(reqDoc, body);
    
    if (error) {
      sendJsonApiError(400, "Invalid JSON");
      return;
    }
    
    const char* mode = reqDoc["mode"];
    const char* name = reqDoc["name"];
    JsonObject configData = reqDoc["config"];
    
    if (!mode || !name || configData.isNull()) {
      sendJsonApiError(400, "Missing required fields");
      return;
    }
    
    // Validation: refuse infinite durations
    if (strcmp(mode, "oscillation") == 0) {
      int cycleCount = configData["cycleCount"] | -1;
      if (cycleCount == 0) {
        sendJsonApiError(400, "Infinite cycles not allowed in playlist");
        return;
      }
    } else if (strcmp(mode, "chaos") == 0) {
      int duration = configData["durationSeconds"] | -1;
      if (duration == 0) {
        sendJsonApiError(400, "Infinite duration not allowed in playlist");
        return;
      }
    }
    
    // Load existing playlists using UtilityEngine (Phase 3.1 - JSON Manager)
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
    engine->debug("📋 Loaded doc - simple: " + String(playlistDoc["simple"].isNull() ? "null" : (playlistDoc["simple"].is<JsonArray>() ? "array" : "other")));
    engine->debug("📋 Loaded doc - oscillation: " + String(playlistDoc["oscillation"].isNull() ? "null" : (playlistDoc["oscillation"].is<JsonArray>() ? "array" : "other")));
    engine->debug("📋 Loaded doc - chaos: " + String(playlistDoc["chaos"].isNull() ? "null" : (playlistDoc["chaos"].is<JsonArray>() ? "array" : "other")));
    
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
      sendJsonApiError(400, "Maximum 20 presets reached");
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
    newPreset["timestamp"] = (unsigned long)time(nullptr);
    newPreset["config"] = configData;
    
    engine->debug("📋 Adding preset: mode=" + String(mode) + ", id=" + String(nextId) + ", name=" + String(name));
    engine->debug("📋 Array size after add: " + String(modeArray.size()));
    
    // Save to file using UtilityEngine (Phase 3.1 - JSON Manager)
    if (!engine->saveJsonFile(PLAYLIST_FILE_PATH, playlistDoc)) {
      engine->error("❌ Failed to save playlist");
      sendJsonApiError(500, "Failed to save");
      return;
    }
    
    engine->info("📋 Preset added: " + String(name) + " (mode: " + String(mode) + ")");
    sendJsonSuccessWithId(nextId);
  });
  
  // POST /api/playlists/delete - Delete a preset
  server.on("/api/playlists/delete", HTTP_POST, []() {
    if (!engine || !engine->isFilesystemReady()) {
      sendJsonApiError(500, "LittleFS not mounted");
      return;
    }
    
    String body = server.arg("plain");
    JsonDocument reqDoc;
    DeserializationError error = deserializeJson(reqDoc, body);
    
    if (error) {
      sendJsonApiError(400, "Invalid JSON");
      return;
    }
    
    const char* mode = reqDoc["mode"];
    int id = reqDoc["id"] | 0;
    
    if (!mode || id == 0) {
      sendJsonApiError(400, "Missing mode or id");
      return;
    }
    
    // Load existing playlists
    if (!LittleFS.exists(PLAYLIST_FILE_PATH)) {
      sendJsonApiError(404, "No playlists found");
      return;
    }
    
    File file = LittleFS.open(PLAYLIST_FILE_PATH, "r");
    if (!file) {
      sendJsonApiError(500, "Failed to read playlists");
      return;
    }
    
    JsonDocument playlistDoc;
    deserializeJson(playlistDoc, file);
    file.close();
    
    // Find and remove preset
    if (!playlistDoc[mode].is<JsonArray>()) {
      sendJsonApiError(404, "Mode not found");
      return;
    }
    
    JsonArray modeArray = playlistDoc[mode].as<JsonArray>();
    bool found = false;
    
    for (size_t i = 0; i < modeArray.size(); i++) {
      JsonObject preset = modeArray[i];
      if (preset["id"] == id) {
        modeArray.remove(i);
        found = true;
        break;
      }
    }
    
    if (!found) {
      sendJsonApiError(404, "Preset not found");
      return;
    }
    
    // Save updated playlists using UtilityEngine (ensures proper flush + logs)
    if (!engine->saveJsonFile(PLAYLIST_FILE_PATH, playlistDoc)) {
      engine->error("❌ Failed to save playlist after delete");
      sendJsonApiError(500, "Failed to save");
      return;
    }
    
    engine->info("🗑️ Preset deleted: ID " + String(id) + " (mode: " + String(mode) + "), " + String(modeArray.size()) + " remaining");
    sendJsonSuccess();
  });
  
  // POST /api/playlists/update - Update (rename) a preset
  server.on("/api/playlists/update", HTTP_POST, []() {
    if (!engine || !engine->isFilesystemReady()) {
      sendJsonApiError(500, "LittleFS not mounted");
      return;
    }
    
    String body = server.arg("plain");
    JsonDocument reqDoc;
    DeserializationError error = deserializeJson(reqDoc, body);
    
    if (error) {
      sendJsonApiError(400, "Invalid JSON");
      return;
    }
    
    const char* mode = reqDoc["mode"];
    int id = reqDoc["id"] | 0;
    const char* newName = reqDoc["name"];
    
    if (!mode || id == 0 || !newName) {
      sendJsonApiError(400, "Missing required fields");
      return;
    }
    
    // Load existing playlists
    if (!LittleFS.exists(PLAYLIST_FILE_PATH)) {
      sendJsonApiError(404, "No playlists found");
      return;
    }
    
    File file = LittleFS.open(PLAYLIST_FILE_PATH, "r");
    if (!file) {
      sendJsonApiError(500, "Failed to read playlists");
      return;
    }
    
    JsonDocument playlistDoc;
    deserializeJson(playlistDoc, file);
    file.close();
    
    // Find and update preset
    if (!playlistDoc[mode].is<JsonArray>()) {
      sendJsonApiError(404, "Mode not found");
      return;
    }
    
    JsonArray modeArray = playlistDoc[mode].as<JsonArray>();
    bool found = false;
    
    for (JsonObject preset : modeArray) {
      if (preset["id"] == id) {
        preset["name"] = newName;
        found = true;
        break;
      }
    }
    
    if (!found) {
      sendJsonApiError(404, "Preset not found");
      return;
    }
    
    // Save updated playlists using UtilityEngine (ensures proper flush + logs)
    if (!engine->saveJsonFile(PLAYLIST_FILE_PATH, playlistDoc)) {
      engine->error("❌ Failed to save playlist after rename");
      sendJsonApiError(500, "Failed to save");
      return;
    }
    
    engine->info("✏️ Preset renamed: ID " + String(id) + " -> " + String(newName));
    sendJsonSuccess();
  });

  // ============================================================================
  // LOGS MANAGEMENT ROUTES
  // ============================================================================
  
  // GET /logs - List all log files as HTML directory browser
  server.on("/logs", HTTP_GET, []() {
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
    
    server.send(200, "text/html; charset=UTF-8", html);
  });
  
  // POST /logs/clear - Clear all log files
  server.on("/logs/clear", HTTP_POST, []() {
    int deletedCount = 0;
    
    // Delete all files in /logs directory
    File logsDir = LittleFS.open("/logs");
    if (logsDir && logsDir.isDirectory()) {
      File logFile = logsDir.openNextFile();
      while (logFile) {
        if (!logFile.isDirectory()) {
          // Get file name (without directory path)
          String fileName = String(logFile.name());
          
          // Close file before deleting
          logFile.close();
          
          // Build full path and delete
          String fullPath = "/logs/" + fileName;
          if (LittleFS.remove(fullPath)) {
            engine->info("🗑️ Deleted: " + fullPath);
            deletedCount++;
          } else {
            engine->error("❌ Failed to delete: " + fullPath);
          }
        }
        
        // Get next file
        logFile = logsDir.openNextFile();
      }
      logsDir.close();
    }
    
    engine->info("📋 Deleted " + String(deletedCount) + " log files");
    server.send(200, "application/json", 
      "{\"status\":\"ok\",\"message\":\"" + String(deletedCount) + " logs deleted\",\"count\":" + String(deletedCount) + "}");
  });

  // ============================================================================
  // SYSTEM MANAGEMENT ROUTES
  // ============================================================================
  
  // GET /api/ping - Simple health check endpoint
  server.on("/api/ping", HTTP_GET, []() {
    server.send(200, "application/json", "{\"status\":\"ok\",\"uptime\":" + String(millis()) + "}");
  });
  
  // POST /api/system/reboot - Reboot ESP32
  server.on("/api/system/reboot", HTTP_POST, []() {
    engine->info("🔄 Reboot requested via API");
    
    // Send success response before rebooting
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting ESP32...\"}");
    
    // Flush logs before reboot
    engine->flushLogBuffer(true);
    
    // Small delay to ensure response is sent
    delay(500);
    
    // Reboot ESP32
    ESP.restart();
  });
  
  // POST /api/system/wifi/reconnect - Reconnect WiFi
  server.on("/api/system/wifi/reconnect", HTTP_POST, []() {
    engine->info("📶 WiFi reconnect requested via API");
    
    // Send success response before disconnecting
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Reconnecting WiFi...\"}");
    
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
  server.on("/api/system/logging/preferences", HTTP_GET, []() {
    JsonDocument doc;
    doc["loggingEnabled"] = engine->isLoggingEnabled();
    doc["logLevel"] = (int)engine->getLogLevel();
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // POST /api/system/logging/preferences - Update logging preferences
  server.on("/api/system/logging/preferences", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    
    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    
    if (err) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    // Update logging enabled state
    if (doc["loggingEnabled"].is<bool>()) {
      bool enabled = doc["loggingEnabled"];
      engine->setLoggingEnabled(enabled);
      engine->info(enabled ? "✅ Logging ENABLED" : "❌ Logging DISABLED");
    }
    
    // Update log level (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
    if (doc["logLevel"].is<int>()) {
      int level = doc["logLevel"];
      if (level >= 0 && level <= 3) {
        engine->setLogLevel((LogLevel)level);
        engine->info("📊 Log level set to: " + String(level));
      }
    }
    
    // Save to EEPROM
    engine->saveLoggingPreferences();
    
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Logging preferences saved\"}");
  });

  // ============================================================================
  // WIFI CONFIGURATION API
  // ============================================================================
  
  // GET /api/wifi/scan - Scan available WiFi networks
  server.on("/api/wifi/scan", HTTP_GET, []() {
    engine->info("📡 WiFi scan requested via API");
    
    WiFiNetworkInfo networks[15];
    int count = WiFiConfig.scanNetworks(networks, 15);
    
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
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // GET /api/wifi/config - Get current WiFi configuration (without password)
  server.on("/api/wifi/config", HTTP_GET, []() {
    JsonDocument doc;
    doc["configured"] = WiFiConfig.isConfigured();
    doc["ssid"] = WiFiConfig.getStoredSSID();
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ip"] = WiFi.localIP().toString();
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // POST /api/wifi/connect - Test and save WiFi credentials
  // IMPORTANT: Only allowed in AP-only mode (degraded mode) to avoid connection issues
  server.on("/api/wifi/connect", HTTP_POST, []() {
    // Block if already connected in STA mode - must use AP only
    if (WiFi.status() == WL_CONNECTED) {
      server.send(403, "application/json", 
        "{\"success\":false,\"error\":\"WiFi config disabled when connected. Use AP mode (192.168.4.1) to change settings.\",\"hint\":\"Reboot with wrong credentials or use 'Forget WiFi' first.\"}");
      return;
    }
    
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing body\"}");
      return;
    }
    
    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    
    if (err) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }
    
    String ssid = doc["ssid"] | "";
    String password = doc["password"] | "";
    
    if (ssid.length() == 0) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
      return;
    }
    
    engine->info("🔌 WiFi connect request for: " + ssid);
    
    // Test connection first
    bool connected = WiFiConfig.testConnection(ssid, password, 15000);
    
    if (connected) {
      // Save to EEPROM
      WiFiConfig.saveConfig(ssid, password);
      
      JsonDocument respDoc;
      respDoc["success"] = true;
      respDoc["message"] = "WiFi configured successfully!";
      respDoc["ip"] = WiFi.localIP().toString();
      respDoc["ssid"] = ssid;
      respDoc["rebootRequired"] = true;
      respDoc["hostname"] = String(otaHostname) + ".local";
      
      String response;
      serializeJson(respDoc, response);
      server.send(200, "application/json", response);
      
      // Don't auto-reboot - let the UI handle it with user confirmation
    } else {
      server.send(200, "application/json", "{\"success\":false,\"error\":\"Connection failed. Check password.\"}");
    }
  });
  
  // POST /api/wifi/reboot - Explicit reboot after config
  server.on("/api/wifi/reboot", HTTP_POST, []() {
    engine->info("🔄 WiFi config reboot requested");
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
    delay(500);
    ESP.restart();
  });
  
  // POST /api/wifi/forget - Clear WiFi configuration
  server.on("/api/wifi/forget", HTTP_POST, []() {
    engine->info("🗑️ WiFi forget requested via API");
    
    WiFiConfig.clearConfig();
    
    server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi configuration cleared. Rebooting...\"}");
    
    // Reboot to enter setup mode
    delay(1000);
    ESP.restart();
  });

  // ===== IMPORT SEQUENCE VIA HTTP POST (Bypass WebSocket size limit) =====
  server.on("/api/sequence/import", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No JSON body provided\"}");
      return;
    }

    String jsonData = server.arg("plain");
    engine->info("📥 HTTP Import received: " + String(jsonData.length()) + " bytes");

    // Call the SequenceTableManager import function
    SeqTable.importFromJson(jsonData);

    server.send(200, "application/json", "{\"success\":true,\"message\":\"Sequence imported successfully\"}");
  });

  // ========================================================================
  // FALLBACK - Auto-serve static files from LittleFS
  // ========================================================================
  // This handler serves ANY file from LittleFS automatically:
  // - /css/*.css, /js/*.js, /js/core/*.js, /js/modules/*.js, etc.
  // - No need to manually add routes when adding/removing JS files
  // - Just update index.html and upload filesystem
  // ========================================================================
  server.onNotFound([]() {
    String uri = server.uri();
    
    // Try to serve the file using our helper (handles caching)
    if (serveStaticFile(uri)) {
      return;  // File served successfully
    }
    
    // File not found
    engine->warn("⚠️ 404: " + uri);
    server.send(404, "text/plain", "Not found: " + uri);
  });
}
