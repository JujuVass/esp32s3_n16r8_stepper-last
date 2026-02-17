/**
 * NetworkManager.cpp - WiFi Network Management Implementation
 * 
 * Three modes (GPIO 19: GND = normal, floating = AP_SETUP):
 * - AP_SETUP:  GPIO 19 floating or no credentials → Config-only (setup.html + captive portal)
 * - STA+AP:    GPIO 19 GND + WiFi connected → Full app on both interfaces (STA + AP parallel)
 * - AP_DIRECT: GPIO 19 GND + WiFi fail → AP-only with full stepper control
 */

#include "communication/NetworkManager.h"
#include "communication/WiFiConfigManager.h"
#include "core/UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "movement/SequenceExecutor.h"
#include <sys/time.h>

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
}

// Global accessor
NetworkManager& Network = NetworkManager::getInstance();

// ============================================================================
// MODE DETERMINATION - Should we enter AP_SETUP (config-only)?
// GPIO 19: GND (LOW) = normal operation, floating (HIGH via pull-up) = AP_SETUP
// Also enters AP_SETUP if no WiFi credentials are available
// ============================================================================

bool NetworkManager::shouldStartAPSetup() {
    // Setup GPIO for AP mode detection (active HIGH with internal pull-up)
    // Pin permanently wired to GND = normal mode
    // Pin disconnected/floating = HIGH via pull-up = force AP_SETUP
    pinMode(PIN_AP_MODE, INPUT_PULLUP);
    delay(10);  // Let pin stabilize
    
    int pinState = digitalRead(PIN_AP_MODE);
    engine->info("📌 GPIO" + String(PIN_AP_MODE) + " state: " + String(pinState == HIGH ? "HIGH (floating → AP_SETUP)" : "LOW (GND → normal)"));
    
    // GPIO 19 HIGH (floating, not connected to GND) = force AP_SETUP mode
    if (pinState == HIGH) {
        engine->info("🔧 GPIO " + String(PIN_AP_MODE) + " is HIGH (floating) - Forcing AP_SETUP mode");
        return true;
    }
    
    // GPIO 19 is LOW (GND) = normal mode, check credentials
    String savedSSID, savedPassword;
    bool eepromConfigured = WiFiConfig.isConfigured();
    engine->info("📦 EEPROM configured: " + String(eepromConfigured ? "YES" : "NO"));
    
    if (eepromConfigured && WiFiConfig.loadConfig(savedSSID, savedPassword)) {
        if (savedSSID.length() > 0) {
            engine->info("📶 Found EEPROM WiFi config: '" + savedSSID + "' → Try STA+AP mode");
            return false;  // Have credentials, try STA mode
        }
    }
    
    // Check hardcoded defaults from Config.h
    engine->info("📄 Config.h SSID: '" + String(ssid) + "'");
    if (strlen(ssid) > 0 && strcmp(ssid, "YOUR_WIFI_SSID") != 0) {
        engine->info("📶 Using Config.h WiFi config: '" + String(ssid) + "' → Try STA+AP mode");
        return false;  // Have defaults, try STA mode
    }
    
    // No credentials available - must use AP_SETUP for configuration
    engine->warn("⚠️ No WiFi credentials found - Entering AP_SETUP mode");
    return true;
}

// ============================================================================
// AP_SETUP MODE (Configuration only - setup.html + captive portal)
// ============================================================================

void NetworkManager::startAPSetupMode() {
    _mode = NET_AP_SETUP;
    
    // Use AP_STA mode so we can test WiFi connections without disrupting the AP
    WiFi.mode(WIFI_AP_STA);
    
    // Start Access Point
    String apName = String(otaHostname) + "-Setup";
    WiFi.softAP(apName.c_str());  // Open network for easy config
    
    WiFi.setSleep(WIFI_PS_NONE);
    
    // Start Captive Portal DNS server
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _captivePortalActive = true;
    _cachedIP = WiFi.softAPIP().toString();
    
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("🔧 AP_SETUP MODE - WiFi Configuration + Captive Portal");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   Network: " + apName);
    engine->info("   IP: " + _cachedIP);
    engine->info("   📱 Captive Portal active - auto-opens on connect!");
    engine->info("   ⚠️  Hardware NOT initialized (config mode only)");
    engine->info("═══════════════════════════════════════════════════════");
}

// ============================================================================
// AP_DIRECT MODE (Full stepper control via AP, no router)
// ============================================================================

void NetworkManager::startAPDirectMode() {
    _mode = NET_AP_DIRECT;
    
    WiFi.mode(WIFI_AP);
    
    // Start Access Point with optional password
    String apName = String(otaHostname) + "-AP";
    if (strlen(AP_DIRECT_PASSWORD) > 0) {
        WiFi.softAP(apName.c_str(), AP_DIRECT_PASSWORD, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    } else {
        WiFi.softAP(apName.c_str(), NULL, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    }
    
    WiFi.setSleep(WIFI_PS_NONE);
    _cachedIP = WiFi.softAPIP().toString();
    
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("📡 AP_DIRECT MODE - Full Stepper Control via WiFi AP");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   Network: " + apName);
    engine->info("   IP: " + _cachedIP);
    engine->info("   Password: " + String(strlen(AP_DIRECT_PASSWORD) > 0 ? "YES" : "OPEN"));
    engine->info("   Channel: " + String(AP_DIRECT_CHANNEL));
    engine->info("   Max clients: " + String(AP_DIRECT_MAX_CLIENTS));
    engine->info("   🎮 Full stepper app available!");
    engine->info("   ⚠️  No OTA, no NTP (use client time sync)");
    engine->info("═══════════════════════════════════════════════════════");
}

// ============================================================================
// START PARALLEL AP (called after STA connects successfully)
// ============================================================================

void NetworkManager::startParallelAP() {
    String apName = String(otaHostname) + "-AP";
    if (strlen(AP_DIRECT_PASSWORD) > 0) {
        WiFi.softAP(apName.c_str(), AP_DIRECT_PASSWORD, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    } else {
        WiFi.softAP(apName.c_str(), NULL, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    }
    
    engine->info("📡 Parallel AP started: " + apName + " (IP: " + WiFi.softAPIP().toString() + ")");
}

// ============================================================================
// CAPTIVE PORTAL HANDLER
// ============================================================================

void NetworkManager::handleCaptivePortal() {
    if (_captivePortalActive) {
        _dnsServer.processNextRequest();
    }
}

// ============================================================================
// STA MODE (Normal Operation + Parallel AP)
// ============================================================================

bool NetworkManager::startSTAMode() {
    _mode = NET_STA_AP;
    
    // Set WiFi to AP+STA mode for parallel AP
    WiFi.mode(WIFI_AP_STA);
    
    // Get credentials - check EEPROM first, then Config.h defaults
    String targetSSID, targetPassword;
    String credentialSource;
    
    if (WiFiConfig.isConfigured() && WiFiConfig.loadConfig(targetSSID, targetPassword)) {
        credentialSource = "EEPROM";
        engine->info("🔑 WiFi credentials from: EEPROM (saved config)");
    } else {
        // Use hardcoded defaults from Config.h
        targetSSID = ssid;
        targetPassword = password;
        credentialSource = "Config.h";
        engine->info("🔑 WiFi credentials from: Config.h (hardcoded defaults)");
    }
    
    // Connect to WiFi
    // Set hostname BEFORE WiFi.begin() — registers with DHCP & helps mDNS reliability
    WiFi.setHostname(otaHostname);
    WiFi.begin(targetSSID.c_str(), targetPassword.c_str());
    engine->info("📶 Connecting to WiFi: " + targetSSID + " [" + credentialSource + "]");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {  // 20 seconds timeout
        delay(500);
        Serial.print(".");
        attempts++;
        
        if (attempts % 10 == 0) {
            engine->info("\n[" + String(attempts) + "/40] Still connecting...");
        }
    }
    Serial.println();
    
    if (WiFi.status() != WL_CONNECTED) {
        engine->error("❌ WiFi connection failed after " + String(attempts * 500 / 1000) + "s");
        engine->warn("⚠️ Credentials from " + credentialSource + " - Switching to AP_DIRECT mode...");
        
        // 🛡️ PROTECTION: Don't disconnect if EEPROM write in progress
        if (WiFiConfig.isEEPROMBusy()) {
            engine->warn("⚠️ EEPROM write in progress - delaying disconnect...");
            delay(100);
        }
        
        // Failed to connect - switch to AP_DIRECT (full app, not setup!)
        WiFi.disconnect();
        startAPDirectMode();
        return false;
    }
    
    // Connected successfully - cache IP
    _cachedIP = WiFi.localIP().toString();
    
    // Start parallel AP so device is also accessible via 192.168.4.1
    startParallelAP();
    
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("✅ STA+AP MODE - Stepper Controller Active");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   WiFi: " + targetSSID + " [" + credentialSource + "]");
    engine->info("   STA IP: " + _cachedIP);
    engine->info("   AP IP:  " + WiFi.softAPIP().toString());
    engine->info("   Hostname: http://" + String(otaHostname) + ".local");
    engine->info("   🎮 App accessible on BOTH interfaces!");
    engine->info("═══════════════════════════════════════════════════════");
    
    // Disable WiFi power saving
    WiFi.setSleep(WIFI_PS_NONE);
    engine->info("⚡ WiFi power save: DISABLED (always active)");
    
    // Setup additional services (only available with router connection)
    setupMDNS();
    _lastMdnsRefresh = millis();
    setupNTP();
    setupOTA();
    
    return true;
}

// ============================================================================
// GET CONFIGURED SSID
// ============================================================================

String NetworkManager::getConfiguredSSID() const {
    String savedSSID, savedPassword;
    if (WiFiConfig.isConfigured() && WiFiConfig.loadConfig(savedSSID, savedPassword)) {
        return savedSSID;
    }
    return String(ssid);  // Default from Config.h
}

// ============================================================================
// GET IP ADDRESS - REMOVED (now inline in header using cached value)
// ============================================================================

// ============================================================================
// MDNS SETUP (STA mode only)
// ============================================================================

bool NetworkManager::setupMDNS() {
    if (MDNS.begin(otaHostname)) {
        engine->info("✅ mDNS: http://" + String(otaHostname) + ".local");
        
        // Add services for discovery
        MDNS.addService("http", "tcp", 80);        // Web server
        MDNS.addService("ws", "tcp", 81);          // WebSocket
        MDNS.addService("arduino", "tcp", 3232);   // OTA service (standard Arduino port)
        
        // Add TXT record with device info for better discovery
        MDNS.addServiceTxt("http", "tcp", "board", "ESP32-S3");
        MDNS.addServiceTxt("http", "tcp", "path", "/");
        
        return true;
    }
    engine->error("❌ mDNS failed to start");
    return false;
}

// ============================================================================
// NTP TIME SYNC (STA mode only)
// ============================================================================

void NetworkManager::setupNTP() {
    configTime(3600, 0, "pool.ntp.org", "time.nist.gov");  // GMT+1
    engine->info("⏰ NTP configured (GMT+1)");
    
    delay(1000);
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo->tm_year > (2020 - 1900)) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        engine->info("✓ Time: " + String(timeStr));
    }
}

// ============================================================================
// OTA CONFIGURATION (STA mode only)
// ============================================================================

void NetworkManager::setupOTA() {
    ArduinoOTA.setHostname(otaHostname);
    
    if (strlen(otaPassword) > 0) {
        ArduinoOTA.setPassword(otaPassword);
    }
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        engine->info("🔄 OTA Update: " + type);
        
        if (engine) engine->flushLogBuffer(true);
        stopMovement();
        Motor.disable();
        if (seqState.isRunning) SeqExecutor.stop();
    });
    
    ArduinoOTA.onEnd([]() {
        engine->info("✅ OTA Complete - Rebooting...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastPercent = 0;
        unsigned int percent = (progress * 100) / total;
        if (percent >= lastPercent + 10) {
            engine->info("📥 OTA: " + String(percent) + "%");
            lastPercent = percent;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        engine->error("❌ OTA Error [" + String(error) + "]");
    });
    
    ArduinoOTA.begin();
    engine->info("✅ OTA Ready");
    _otaConfigured = true;
}

// ============================================================================
// FULL INITIALIZATION
// ============================================================================

bool NetworkManager::begin() {
    engine->info("🌐 Network initialization...");
    
    // Check if we should enter AP_SETUP (no credentials available)
    if (shouldStartAPSetup()) {
        startAPSetupMode();
        return false;  // AP_SETUP mode = no stepper control
    }
    
    // We have credentials → try STA+AP mode (falls back to AP_DIRECT on failure)
    bool connected = startSTAMode();
    _wasConnected = connected;
    return connected;
}

// ============================================================================
// CONNECTION HEALTH CHECK (STA+AP mode only)
// - Auto-reconnect WiFi if connection lost
// - Re-announces mDNS after WiFi reconnection for stable .local resolution
// ============================================================================

void NetworkManager::checkConnectionHealth() {
    // Only in STA+AP mode (AP_DIRECT and AP_SETUP don't need health checks)
    if (_mode != NET_STA_AP) return;
    
    // Rate limit health checks to once per 5 seconds
    unsigned long now = millis();
    if (now - _lastHealthCheck < 5000) return;
    _lastHealthCheck = now;
    
    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);
    
    // ═══════════════════════════════════════════════════════════════════════
    // CASE 1: Lost connection → Attempt auto-reconnect
    // ═══════════════════════════════════════════════════════════════════════
    if (!currentlyConnected && _wasConnected) {
        engine->warn("⚠️ WiFi connection lost! (AP still active at " + WiFi.softAPIP().toString() + ")");
        _reconnectAttempts = 0;
    }
    
    if (!currentlyConnected) {
        // Attempt reconnect every WIFI_RECONNECT_INTERVAL_MS (5s default)
        if (now - _lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            _reconnectAttempts++;
            
            // 🛡️ PROTECTION: Don't reconnect if EEPROM write in progress
            if (WiFiConfig.isEEPROMBusy()) {
                engine->warn("⚠️ EEPROM write in progress - skipping WiFi reconnect");
                return;
            }
            
            if (_reconnectAttempts <= 10) {
                engine->info("🔄 WiFi reconnect attempt " + String(_reconnectAttempts) + "/10...");
                WiFi.reconnect();
            } else if (_reconnectAttempts == 11) {
                engine->warn("⚠️ WiFi reconnect failed 10 times, continuing in background...");
            }
            if (_reconnectAttempts > 10) {
                WiFi.reconnect();
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CASE 2: Reconnected → Re-announce mDNS
    // ═══════════════════════════════════════════════════════════════════════
    if (currentlyConnected && !_wasConnected) {
        _cachedIP = WiFi.localIP().toString();
        engine->info("✅ WiFi reconnected! IP: " + _cachedIP);
        engine->info("🔄 Re-announcing mDNS...");
        _reconnectAttempts = 0;
        
        MDNS.end();
        delay(50);
        setupMDNS();
        _lastMdnsRefresh = now;
    }
    
    _wasConnected = currentlyConnected;
}

// ============================================================================
// CLIENT TIME SYNC (for AP_DIRECT mode without NTP)
// ============================================================================

void NetworkManager::syncTimeFromClient(uint64_t epochMs) {
    if (_timeSynced && _mode == NET_STA_AP) {
        return;  // NTP already synced in STA mode, ignore client time
    }
    
    struct timeval tv;
    tv.tv_sec = epochMs / 1000;
    tv.tv_usec = (epochMs % 1000) * 1000;
    settimeofday(&tv, NULL);
    _timeSynced = true;
    
    time_t now = epochMs / 1000;
    struct tm *timeinfo = localtime(&now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    engine->info("⏰ Time synced from client: " + String(timeStr));
}