/**
 * ============================================================================
 * StatusBroadcaster.h - WebSocket Status Broadcasting
 * ============================================================================
 *
 * Manages the broadcasting of system status via WebSocket.
 * Handles mode-specific JSON construction and optimization.
 */

#ifndef STATUS_BROADCASTER_H
#define STATUS_BROADCASTER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/UtilityEngine.h"
#include "core/GlobalState.h"

// ============================================================================
// STATUS BROADCASTER CLASS
// ============================================================================

class StatusBroadcaster {
public:
    // Singleton pattern
    static StatusBroadcaster& getInstance();

    // ========================================================================
    // INITIALIZATION
    // ========================================================================

    /**
     * Initialize the broadcaster with WebSocket reference
     * @param ws Pointer to WebSocketsServer for broadcasting
     */
    void begin(AsyncWebSocket* ws);

    // ========================================================================
    // BROADCASTING
    // ========================================================================

    /**
     * Send complete system status via WebSocket
     * Optimized for current movement type (only sends relevant data)
     */
    void send();

    /**
     * Get adaptive broadcast interval based on current system state.
     * Returns faster rate during active movement, slower when idle.
     * @return Interval in milliseconds
     */
    unsigned long getAdaptiveBroadcastInterval() const;

    /**
     * Reset broadcast deduplication hash.
     * Call when a new client connects so the next send() is guaranteed
     * to broadcast even if the payload hasn't changed.
     */
    void resetHash() { _lastBroadcastHash = 0; }

    /**
     * Send error message via WebSocket AND Serial
     * Ensures user sees errors even without Serial monitor
     * @param message Error message to broadcast
     */
    void sendError(const String& message);

    /**
     * Check if a file upload is currently in progress
     * Based on timestamp expiry (UPLOAD_ACTIVITY_TIMEOUT_MS after last activity)
     * @return true if upload batch is active
     */
    bool isUploadActive() const;

private:
    StatusBroadcaster() = default;
    StatusBroadcaster(const StatusBroadcaster&) = delete;
    StatusBroadcaster& operator=(const StatusBroadcaster&) = delete;

    AsyncWebSocket* _webSocket = nullptr;
    uint32_t _lastBroadcastHash = 0;  // FNV-1a hash of last broadcast payload (dedup)

    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================

    /**
     * Add VA-ET-VIENT / Pursuit mode specific fields to JSON
     */
    void addVaEtVientFields(JsonDocument& doc);

    /**
     * Add Oscillation mode specific fields to JSON
     */
    void addOscillationFields(JsonDocument& doc);

    /**
     * Add Chaos mode specific fields to JSON
     */
    void addChaosFields(JsonDocument& doc);

    /**
     * Add system stats fields to JSON (on-demand)
     */
    void addSystemStats(JsonDocument& doc);
};

// Global accessor (singleton)
inline StatusBroadcaster& Status = StatusBroadcaster::getInstance();

#endif // STATUS_BROADCASTER_H
