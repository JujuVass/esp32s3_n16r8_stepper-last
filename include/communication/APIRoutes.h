// ============================================================================
// API ROUTES MANAGER
// ============================================================================
// HTTP server routes
// Header file: declarations only
// Implementation in src/communication/APIRoutes.cpp
// ============================================================================

#ifndef API_ROUTES_H
#define API_ROUTES_H

#include <WebServer.h>
#include <WebSocketsServer.h>

// Forward declarations for global context
extern WebServer server;
extern WebSocketsServer webSocket;

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
 * Get formatted date string (YYYY-MM-DD)
 * @return Formatted date string
 */
String getFormattedDate();

/**
 * Get formatted time string (HH:MM:SS)
 * @return Formatted time string
 */
String getFormattedTime();

/**
 * Send JSON error response
 * @param code HTTP status code
 * @param message Error message
 */
void sendJsonError(int code, const String& message);

/**
 * Send JSON success response
 * @param message Optional success message
 */
void sendJsonSuccess(const String& message = "");

/**
 * Send JSON success response with ID
 * @param id The ID to include in response
 */
void sendJsonSuccessWithId(int id);

/**
 * Send empty playlist structure
 */
void sendEmptyPlaylistStructure();

#endif // API_ROUTES_H
