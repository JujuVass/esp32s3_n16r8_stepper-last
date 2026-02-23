// ============================================================================
// API ROUTES MANAGER
// ============================================================================
// HTTP server routes
// Header file: declarations only
// Implementation in src/communication/APIRoutes.cpp
// ============================================================================

#ifndef API_ROUTES_H
#define API_ROUTES_H

#include <ESPAsyncWebServer.h>

// Forward declarations for global context
extern AsyncWebServer server;
extern AsyncWebSocket ws;

// ============================================================================
// MAIN SETUP FUNCTION
// ============================================================================

/**
 * Setup all API routes on the HTTP server
 * Must be called in setup() after server and webSocket are initialized
 */
void setupAPIRoutes();

// ============================================================================
// HELPER FUNCTIONS (also used by other modules)
// ============================================================================

/**
 * Send JSON error response
 * @param request The async request to respond to
 * @param code HTTP status code
 * @param message Error message
 */
void sendJsonError(AsyncWebServerRequest* request, int code, const String& message);

/**
 * Send JSON success response
 * @param request The async request to respond to
 * @param message Optional success message
 */
void sendJsonSuccess(AsyncWebServerRequest* request, const String& message = "");

/**
 * Send JSON success response with ID
 * @param request The async request to respond to
 * @param id The ID to include in response
 */
void sendJsonSuccessWithId(AsyncWebServerRequest* request, int id);

/**
 * Send empty playlist structure
 * @param request The async request to respond to
 */
void sendEmptyPlaylistStructure(AsyncWebServerRequest* request);

#endif // API_ROUTES_H
