/**
 * NetworkManager.h - WiFi, OTA, mDNS, NTP Configuration
 * 
 * Manages all network-related initialization:
 * - WiFi connection with retry logic
 * - mDNS for local domain (esp32-stepper.local)
 * - OTA (Over-The-Air) updates
 * - NTP time synchronization
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "Config.h"

// Forward declarations
class UtilityEngine;
extern UtilityEngine* engine;
extern void stopMovement();
extern void Motor_disable();

class NetworkManager {
public:
    static NetworkManager& getInstance();
    
    /**
     * Initialize WiFi connection
     * @return true if connected successfully
     */
    bool connectWiFi();
    
    /**
     * Setup mDNS responder
     * @return true if mDNS started successfully
     */
    bool setupMDNS();
    
    /**
     * Configure NTP time synchronization
     */
    void setupNTP();
    
    /**
     * Configure OTA update handlers
     * @param onStopMovement Callback to stop movement before OTA
     * @param onStopSequencer Callback to stop sequencer before OTA
     */
    void setupOTA(std::function<void()> onStopMovement = nullptr,
                  std::function<void()> onStopSequencer = nullptr);
    
    /**
     * Full network initialization (WiFi + mDNS + NTP + OTA)
     * @return true if WiFi connected (other services are optional)
     */
    bool begin();
    
    /**
     * Check if WiFi STA is connected
     */
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    
    /**
     * Check if running in degraded mode (AP only, STA failed)
     */
    bool isDegradedMode() const { return _degradedMode; }
    
    /**
     * Get STA IP address as string
     */
    String getIPAddress() const { return WiFi.localIP().toString(); }
    
    /**
     * Get AP IP address as string
     */
    String getAPIPAddress() const { return WiFi.softAPIP().toString(); }
    
    /**
     * Handle OTA in loop - MUST be called in every loop iteration
     */
    void handleOTA() { if (_otaConfigured) ArduinoOTA.handle(); }

private:
    NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    
    std::function<void()> _onStopMovement = nullptr;
    std::function<void()> _onStopSequencer = nullptr;
    bool _wifiConnected = false;
    bool _otaConfigured = false;
    bool _degradedMode = false;  // True if STA failed, running AP only
};

// Global access macro
#define Network NetworkManager::getInstance()
