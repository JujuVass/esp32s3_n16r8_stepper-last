// ============================================================================
// API ROUTES MANAGER - PHASE 3.4
// ============================================================================
// Extracted HTTP server routes from main .ino
// Reduces main file by ~550 lines
// ============================================================================

#ifndef API_ROUTES_H
#define API_ROUTES_H

#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "UtilityEngine.h"

// Forward declarations for global context
extern WebServer server;
extern WebSocketsServer webSocket;
extern UtilityEngine* engine;

// ============================================================================
// HELPER FUNCTIONS (JSON responses)
// ============================================================================

inline void sendJsonError(int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  String json;
  serializeJson(doc, json);
  server.send(code, "application/json", json);
}

inline void sendJsonSuccess(const String& message = "") {
  JsonDocument doc;
  doc["success"] = true;
  if (message.length() > 0) {
    doc["message"] = message;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

inline void sendJsonSuccessWithId(int id) {
  JsonDocument doc;
  doc["success"] = true;
  doc["id"] = id;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

inline void sendJsonApiError(int code, const String& message) {
  sendJsonError(code, message);
}

inline void sendEmptyPlaylistStructure() {
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
  // Main route: serve index.html from LittleFS
  server.on("/", HTTP_GET, []() {
    if (LittleFS.exists("/index.html")) {
      File file = LittleFS.open("/index.html", "r");
      if (file) {
        // Disable browser caching to ensure latest version is always loaded
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
        server.streamFile(file, "text/html");
        file.close();
        engine->debug("✅ Served /index.html from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening index.html");
        engine->error("❌ Error opening /index.html");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: index.html\nPlease upload filesystem using: platformio run --target uploadfs");
      engine->error("❌ /index.html not found in LittleFS");
    }
  });
  
  // CSS route: serve style.css from LittleFS with caching
  server.on("/style.css", HTTP_GET, []() {
    if (LittleFS.exists("/style.css")) {
      File file = LittleFS.open("/style.css", "r");
      if (file) {
        // Enable browser caching for CSS (24 hours)
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "text/css");
        file.close();
        engine->debug("✅ Served /style.css from LittleFS (cached 24h)");
      } else {
        server.send(500, "text/plain", "❌ Error opening style.css");
        engine->error("❌ Error opening /style.css");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: style.css");
      engine->error("❌ /style.css not found in LittleFS");
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
    server.send(200, "application/json", content);
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
    
    // Save back to file using UtilityEngine (Phase 3.1 - JSON Manager)
    if (!engine->saveJsonFile("/stats.json", statsDoc)) {
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
    } else {
      engine->debug("✅ Returning playlist content: " + content.substring(0, 100) + "...");
      server.send(200, "application/json", content);
    }
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
    engine->loadJsonFile(PLAYLIST_FILE_PATH, playlistDoc);
    
    // Initialize mode array if doesn't exist
    JsonArray modeArray;
    if (!playlistDoc[mode].is<JsonArray>() || playlistDoc[mode].isNull()) {
      modeArray = playlistDoc.to<JsonObject>()[mode].to<JsonArray>();
      engine->debug("📋 Created new array for mode: " + String(mode));
    } else {
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
    
    // Save updated playlists
    file = LittleFS.open(PLAYLIST_FILE_PATH, "w");
    if (!file) {
      sendJsonApiError(500, "Failed to save");
      return;
    }
    
    serializeJson(playlistDoc, file);
    file.close();
    
    engine->info("🗑️ Preset deleted: ID " + String(id) + " (mode: " + String(mode) + ")");
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
    
    // Save updated playlists
    file = LittleFS.open(PLAYLIST_FILE_PATH, "w");
    if (!file) {
      sendJsonApiError(500, "Failed to save");
      return;
    }
    
    serializeJson(playlistDoc, file);
    file.close();
    
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
    // Delete all files in /logs directory
    File logsDir = LittleFS.open("/logs");
    if (logsDir && logsDir.isDirectory()) {
      File logFile = logsDir.openNextFile();
      while (logFile) {
        if (!logFile.isDirectory()) {
          String fullPath = "/logs/" + String(logFile.name());
          LittleFS.remove(fullPath);
          engine->info("🗑️ Deleted log: " + fullPath);
        }
        logFile = logsDir.openNextFile();
      }
      logsDir.close();
    }
    
    engine->info("📋 All log files cleared");
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"All logs cleared\"}");
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
    
    // Disconnect and reconnect WiFi
    WiFi.disconnect();
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

  // Handle 404 - try to serve files from LittleFS (including /logs/*)
  server.onNotFound([]() {
    String uri = server.uri();
    
    // Try to serve the file from LittleFS
    if (LittleFS.exists(uri)) {
      File f = LittleFS.open(uri, "r");
      if (f) {
        String contentType = "text/plain; charset=UTF-8";
        if (uri.endsWith(".html")) contentType = "text/html; charset=UTF-8";
        else if (uri.endsWith(".css")) contentType = "text/css";
        else if (uri.endsWith(".js")) contentType = "application/javascript";
        else if (uri.endsWith(".json")) contentType = "application/json";
        
        server.streamFile(f, contentType);
        f.close();
        return;
      }
    }
    
    // File not found
    server.send(404, "text/plain", "Not found: " + uri);
  });
}

#endif // API_ROUTES_H
