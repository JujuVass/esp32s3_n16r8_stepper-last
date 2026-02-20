/**
 * NetworkManager.h - WiFi StepperNetwork Management
 * 
 * Three network modes:
 * - AP_SETUP:  GPIO 19 floating (HIGH) or no credentials → Captive Portal + setup.html only
 * - STA+AP:    GPIO 19 GND + credentials OK → Connected to router + AP parallel (192.168.4.1)
 * - AP_DIRECT: GPIO 19 GND + credentials but WiFi fail → AP only with full stepper control
 * 
 * GPIO 19: permanently wired to GND = normal operation, disconnected = AP_SETUP
 * In STA+AP and AP_DIRECT modes, the full stepper app (index.html) is served.
 * Hardware is initialized in both STA+AP and AP_DIRECT modes.
 * Only AP_SETUP skips hardware init (configuration-only mode).
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "core/Config.h"
#include "communication/WiFiConfigManager.h"

// Forward declarations
class UtilityEngine;
extern UtilityEngine* engine;
extern void stopMovement();
extern void Motor_disable();

// ============================================================================
// NETWORK MODE ENUM
// ============================================================================
enum NetworkMode {
    NET_AP_SETUP,    // GPIO 19 floating or no credentials → Config-only (setup.html + captive portal)
    NET_STA_AP,      // GPIO 19 GND + WiFi connected → Full app on both interfaces (STA + AP parallel)
    NET_AP_DIRECT    // GPIO 19 GND + WiFi fail → AP-only with full stepper control
};

// ============================================================================
// WATCHDOG STATE ENUM
// ============================================================================
enum WatchdogState : uint8_t {
    WD_HEALTHY          = 0,   // WiFi + DNS + mDNS all OK
    WD_RECOVERING_SOFT  = 1,   // Tier 1: WiFi.reconnect() attempts
    WD_RECOVERING_HARD  = 2,   // Tier 2: Full WiFi disconnect + re-associate
    WD_REBOOTING        = 3    // Tier 3: About to ESP.restart()
};

class StepperNetworkManager {
public:
    static StepperNetworkManager& getInstance();
    
    /**
     * Full network initialization
     * Determines mode and starts appropriate services
     * @return true if STA is connected (NET_STA_AP mode)
     */
    bool begin();
    
    /**
     * Get current network mode
     */
    NetworkMode getMode() const { return _mode; }
    
    /**
     * Check if running in AP_SETUP mode (config only - setup.html)
     * This is the ONLY mode where hardware is NOT initialized
     */
    bool isAPSetupMode() const { return _mode == NET_AP_SETUP; }
    
    /**
     * Check if running in AP_DIRECT mode (full app via AP, no router)
     */
    bool isAPDirectMode() const { return _mode == NET_AP_DIRECT; }
    
    /**
     * Check if running in STA+AP mode (router + AP parallel)
     */
    bool isSTAMode() const { return _mode == NET_STA_AP; }
    
    /**
     * Legacy compatibility: isAPMode() returns true ONLY for AP_SETUP
     * Used by APIRoutes to decide setup.html vs index.html redirect
     */
    bool isAPMode() const { return _mode == NET_AP_SETUP; }
    
    /**
     * Check if STA is connected to a router
     */
    bool isConnected() const { return _mode == NET_STA_AP && WiFi.status() == WL_CONNECTED; }
    
    /**
     * Check if hardware should be initialized (all modes except AP_SETUP)
     */
    bool shouldInitHardware() const { return _mode != NET_AP_SETUP; }
    
    /**
     * Check if WiFi is configured in EEPROM
     */
    bool isWiFiConfigured() const { return WiFiConfig.isConfigured(); }
    
    /**
     * Get configured SSID (from EEPROM or default)
     */
    String getConfiguredSSID() const;
    
    /**
     * Get IP address as string (AP or STA depending on mode)
     * Uses cached value for performance (updated on connect/reconnect)
     */
    const String& getIPAddress() const { return _cachedIP; }
    
    /**
     * Get AP IP address (192.168.4.1)
     */
    String getAPIPAddress() const { return WiFi.softAPIP().toString(); }
    
    /**
     * Get number of clients connected to AP
     */
    uint8_t getAPClientCount() const { return WiFi.softAPgetStationNum(); }
    
    /**
     * Handle OTA in loop - MUST be called in every loop iteration (STA mode only)
     */
    void handleOTA() { if (_otaConfigured) ArduinoOTA.handle(); }
    
    /**
     * Handle Captive Portal DNS in loop - MUST be called in AP_SETUP mode
     */
    void handleCaptivePortal();
    
    /**
     * Connection watchdog: WiFi + DNS + mDNS health check with 3-tier escalation
     * Tier 1: WiFi.reconnect() | Tier 2: Full re-association | Tier 3: ESP.restart()
     * Call this periodically in loop() (runs on Core 0 networkTask)
     */
    void checkConnectionHealth();
    
    /**
     * Get current watchdog state (for status broadcast)
     */
    WatchdogState getWatchdogState() const { return _wdState; }
    
    /**
     * Safe shutdown: stop movement, disable motor, flush logs
     * Called before ESP.restart() (watchdog, OTA, API reboot)
     */
    void safeShutdown();
    
    /**
     * Sync system time from client timestamp (used when NTP unavailable)
     * @param epochMs JavaScript Date.now() value (milliseconds since epoch)
     */
    void syncTimeFromClient(uint64_t epochMs);
    
    /**
     * Check if time has been synced (NTP or client)
     */
    bool isTimeSynced() const { return _timeSynced; }
    
    /**
     * AP Mode LED blink control (can be disabled after successful config)
     */
    volatile bool apLedBlinkEnabled = true;

private:
    StepperNetworkManager() = default;
    StepperNetworkManager(const StepperNetworkManager&) = delete;
    StepperNetworkManager& operator=(const StepperNetworkManager&) = delete;
    
    // Mode determination
    bool shouldStartAPSetup();
    
    // Mode-specific initialization
    void startAPSetupMode();
    void startAPDirectMode();
    bool startSTAMode();
    void startParallelAP();
    
    // STA services
    bool setupMDNS(bool includeOtaService = true);
    void setupNTP();
    void setupOTA();
    
    // State
    NetworkMode _mode = NET_AP_DIRECT;
    bool _otaConfigured = false;
    bool _timeSynced = false;
    bool _wasConnected = false;              // Track connection state for recovery detection
    unsigned long _lastHealthCheck = 0;      // Last health check timestamp
    unsigned long _lastMdnsRefresh = 0;      // Last mDNS refresh timestamp
    bool _mdnsBootReannounced = false;         // One-shot delayed re-announce after boot
    String _cachedIP;                        // Cached IP address string (avoids WiFi.localIP() calls)
    
    // Connection Watchdog state
    WatchdogState _wdState = WD_HEALTHY;
    uint8_t _wdSoftRetries = 0;              // Tier 1 attempt counter
    uint8_t _wdHardRetries = 0;              // Tier 2 attempt counter
    uint8_t _pingFailCount = 0;              // Consecutive gateway ping failures
    
    // Captive Portal DNS server (AP_SETUP mode only)
    DNSServer _dnsServer;
    bool _captivePortalActive = false;
};

// Global accessor (singleton reference)
extern StepperNetworkManager& StepperNetwork;
