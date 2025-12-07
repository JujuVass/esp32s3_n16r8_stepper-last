/**
 * NetworkManager.h - WiFi Network Management
 * 
 * Two exclusive modes:
 * - AP Mode: For WiFi configuration only (setup.html) + Captive Portal
 * - STA Mode: For stepper control (index.html)
 * 
 * AP Mode is activated if:
 * - PIN_AP_MODE (GPIO 18) is LOW at boot (physical switch)
 * - No WiFi credentials in EEPROM and no valid defaults
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

class NetworkManager {
public:
    static NetworkManager& getInstance();
    
    /**
     * Full network initialization
     * Determines mode (AP or STA) and starts appropriate services
     * @return true if in STA mode and connected
     */
    bool begin();
    
    /**
     * Check if running in AP mode (configuration mode)
     */
    bool isAPMode() const { return _apMode; }
    
    /**
     * Check if running in STA mode (stepper mode)
     */
    bool isSTAMode() const { return !_apMode; }
    
    /**
     * Check if WiFi STA is connected (only valid in STA mode)
     */
    bool isConnected() const { return !_apMode && WiFi.status() == WL_CONNECTED; }
    
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
     */
    String getIPAddress() const;
    
    /**
     * Handle OTA in loop - MUST be called in every loop iteration (STA mode only)
     */
    void handleOTA() { if (_otaConfigured) ArduinoOTA.handle(); }
    
    /**
     * Handle Captive Portal DNS in loop - MUST be called in AP mode
     */
    void handleCaptivePortal();

private:
    NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    
    // Mode determination
    bool shouldStartAPMode();
    
    // Mode-specific initialization
    void startAPMode();
    bool startSTAMode();
    
    // STA services
    bool setupMDNS();
    void setupNTP();
    void setupOTA();
    
    // State
    bool _apMode = false;
    bool _otaConfigured = false;
    
    // Captive Portal DNS server
    DNSServer _dnsServer;
    bool _captivePortalActive = false;
};

// Global access macro
#define Network NetworkManager::getInstance()
