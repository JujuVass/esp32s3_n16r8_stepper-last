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

// External globals
extern WebServer server;
extern WebSocketsServer webSocket;
extern UtilityEngine* engine;

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
  
  // CSS route: serve /css/styles.css from LittleFS with caching
  server.on("/css/styles.css", HTTP_GET, []() {
    if (LittleFS.exists("/css/styles.css")) {
      File file = LittleFS.open("/css/styles.css", "r");
      if (file) {
        // Enable browser caching for CSS (24 hours)
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "text/css");
        file.close();
        engine->debug("✅ Served /css/styles.css from LittleFS (cached 24h)");
      } else {
        server.send(500, "text/plain", "❌ Error opening /css/styles.css");
        engine->error("❌ Error opening /css/styles.css");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /css/styles.css");
      engine->error("❌ /css/styles.css not found in LittleFS");
    }
  });

  // JS routes: serve JavaScript modules from /js/ folder
  // Cache 24h (86400s) - these files rarely change, reduces ESP32 load
  server.on("/js/app.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/app.js")) {
      File file = LittleFS.open("/js/app.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/app.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/app.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/app.js");
      engine->error("❌ /js/app.js not found in LittleFS");
    }
  });

  server.on("/js/utils.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/utils.js")) {
      File file = LittleFS.open("/js/utils.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/utils.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/utils.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/utils.js");
      engine->error("❌ /js/utils.js not found in LittleFS");
    }
  });

  server.on("/js/milestones.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/milestones.js")) {
      File file = LittleFS.open("/js/milestones.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/milestones.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/milestones.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/milestones.js");
      engine->error("❌ /js/milestones.js not found in LittleFS");
    }
  });

  server.on("/js/websocket.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/websocket.js")) {
      File file = LittleFS.open("/js/websocket.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/websocket.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/websocket.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/websocket.js");
      engine->error("❌ /js/websocket.js not found in LittleFS");
    }
  });

  server.on("/js/stats.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/stats.js")) {
      File file = LittleFS.open("/js/stats.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/stats.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/stats.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/stats.js");
      engine->error("❌ /js/stats.js not found in LittleFS");
    }
  });

  server.on("/js/context.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/context.js")) {
      File file = LittleFS.open("/js/context.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/context.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/context.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/context.js");
      engine->error("❌ /js/context.js not found in LittleFS");
    }
  });

  server.on("/js/chaos.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/chaos.js")) {
      File file = LittleFS.open("/js/chaos.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/chaos.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/chaos.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/chaos.js");
      engine->error("❌ /js/chaos.js not found in LittleFS");
    }
  });

  server.on("/js/oscillation.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/oscillation.js")) {
      File file = LittleFS.open("/js/oscillation.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/oscillation.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/oscillation.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/oscillation.js");
      engine->error("❌ /js/oscillation.js not found in LittleFS");
    }
  });

  server.on("/js/sequencer.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/sequencer.js")) {
      File file = LittleFS.open("/js/sequencer.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/sequencer.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/sequencer.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/sequencer.js");
      engine->error("❌ /js/sequencer.js not found in LittleFS");
    }
  });

  server.on("/js/presets.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/presets.js")) {
      File file = LittleFS.open("/js/presets.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/presets.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/presets.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/presets.js");
      engine->error("❌ /js/presets.js not found in LittleFS");
    }
  });

  server.on("/js/formatting.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/formatting.js")) {
      File file = LittleFS.open("/js/formatting.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/formatting.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/formatting.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/formatting.js");
      engine->error("❌ /js/formatting.js not found in LittleFS");
    }
  });

  server.on("/js/validation.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/validation.js")) {
      File file = LittleFS.open("/js/validation.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/validation.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/validation.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/validation.js");
      engine->error("❌ /js/validation.js not found in LittleFS");
    }
  });

  server.on("/js/main.js", HTTP_GET, []() {
    if (LittleFS.exists("/js/main.js")) {
      File file = LittleFS.open("/js/main.js", "r");
      if (file) {
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(file, "application/javascript");
        file.close();
        engine->debug("✅ Served /js/main.js from LittleFS");
      } else {
        server.send(500, "text/plain", "❌ Error opening /js/main.js");
      }
    } else {
      server.send(404, "text/plain", "❌ File not found: /js/main.js");
      engine->error("❌ /js/main.js not found in LittleFS");
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
