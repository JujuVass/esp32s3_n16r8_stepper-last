/**
 * NetworkManager.cpp - WiFi Network Management Implementation
 * 
 * Two exclusive modes:
 * - AP Mode: WiFi configuration only (192.168.4.1/setup.html)
 * - STA Mode: Stepper control (connected to home WiFi)
 */

#include "communication/NetworkManager.h"
#include "communication/WiFiConfigManager.h"
#include "core/UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "movement/SequenceExecutor.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
}

// ============================================================================
// MODE DETERMINATION
// ============================================================================

bool NetworkManager::shouldStartAPMode() {
    // Setup GPIO for AP mode detection (active LOW with internal pull-up)
    pinMode(PIN_AP_MODE, INPUT_PULLUP);
    delay(10);  // Let pin stabilize
    
    int pinState = digitalRead(PIN_AP_MODE);
    engine->info("📌 GPIO" + String(PIN_AP_MODE) + " state: " + String(pinState == LOW ? "LOW (force AP)" : "HIGH"));
    
    // Check GPIO 18 - if LOW, force AP mode
    if (pinState == LOW) {
        engine->info("🔧 GPIO " + String(PIN_AP_MODE) + " is LOW - Forcing AP mode");
        return true;
    }
    
    // Check if we have valid WiFi credentials in EEPROM
    String savedSSID, savedPassword;
    bool eepromConfigured = WiFiConfig.isConfigured();
    engine->info("📦 EEPROM configured: " + String(eepromConfigured ? "YES" : "NO"));
    
    if (eepromConfigured && WiFiConfig.loadConfig(savedSSID, savedPassword)) {
        if (savedSSID.length() > 0) {
            engine->info("📶 Found EEPROM WiFi config: '" + savedSSID + "' → Try STA mode");
            return false;  // Have credentials, try STA mode
        }
    }
    
    // Check hardcoded defaults from Config.h
    engine->info("📄 Config.h SSID: '" + String(ssid) + "'");
    if (strlen(ssid) > 0 && strcmp(ssid, "YOUR_WIFI_SSID") != 0) {
        engine->info("📶 Using Config.h WiFi config: '" + String(ssid) + "' → Try STA mode");
        return false;  // Have defaults, try STA mode
    }
    
    // No credentials available - must use AP mode
    engine->warn("⚠️ No WiFi credentials found - Starting AP mode");
    return true;
}

// ============================================================================
// AP MODE (Configuration)
// ============================================================================

void NetworkManager::startAPMode() {
    _apMode = true;
    
    // Use AP_STA mode so we can test WiFi connections without disrupting the AP!
    // The STA part won't connect to anything until testConnection() is called
    WiFi.mode(WIFI_AP_STA);
    
    // Start Access Point
    String apName = String(otaHostname) + "-AP";
    WiFi.softAP(apName.c_str());  // Open network, channel 1, max 4 clients
    
    // Disable WiFi power saving in AP mode too
    WiFi.setSleep(WIFI_PS_NONE);
    
    // Start Captive Portal DNS server
    // This makes all DNS queries return our IP, triggering auto-open on mobile/Windows
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _captivePortalActive = true;
    
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("🌐 AP MODE - WiFi Configuration + Captive Portal");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   Network: " + apName);
    engine->info("   IP: " + WiFi.softAPIP().toString());
    engine->info("   📱 Captive Portal active - auto-opens on connect!");
    engine->info("   ⚡ Power save: DISABLED");
    engine->info("═══════════════════════════════════════════════════════");
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
// STA MODE (Normal Operation)
// ============================================================================

bool NetworkManager::startSTAMode() {
    _apMode = false;
    
    // Set WiFi to STA only mode
    WiFi.mode(WIFI_STA);
    
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
        engine->warn("⚠️ Credentials from " + credentialSource + " - Switching to AP mode...");
        
        // Failed to connect - switch to AP mode
        WiFi.disconnect();
        startAPMode();
        return false;
    }
    
    // Connected successfully
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("✅ STA MODE - Stepper Controller Active");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   WiFi: " + targetSSID + " [" + credentialSource + "]");
    engine->info("   IP: " + WiFi.localIP().toString());
    engine->info("   Hostname: http://" + String(otaHostname) + ".local");
    engine->info("═══════════════════════════════════════════════════════");
    
    // Disable WiFi power saving - keep ESP32 always responsive
    // Without this, mDNS/WebSocket can have 100-300ms latency spikes
    WiFi.setSleep(WIFI_PS_NONE);
    engine->info("⚡ WiFi power save: DISABLED (always active)");
    
    // Setup additional services
    setupMDNS();
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
// GET IP ADDRESS
// ============================================================================

String NetworkManager::getIPAddress() const {
    if (_apMode) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

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
    
    if (shouldStartAPMode()) {
        startAPMode();
        return false;  // AP mode = not connected to home WiFi
    } else {
        bool connected = startSTAMode();  // Returns true if connected
        _wasConnected = connected;
        return connected;
    }
}

// ============================================================================
// CONNECTION HEALTH CHECK (STA mode)
// - Auto-reconnect WiFi if connection lost
// - Re-announces mDNS after WiFi reconnection for stable .local resolution
// ============================================================================

void NetworkManager::checkConnectionHealth() {
    // Only in STA mode
    if (_apMode) return;
    
    // Rate limit to once per second
    unsigned long now = millis();
    if (now - _lastHealthCheck < 1000) return;
    _lastHealthCheck = now;
    
    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);
    
    // ═══════════════════════════════════════════════════════════════════════
    // CASE 1: Lost connection → Attempt auto-reconnect
    // ═══════════════════════════════════════════════════════════════════════
    if (!currentlyConnected && _wasConnected) {
        engine->warn("⚠️ WiFi connection lost!");
        _reconnectAttempts = 0;
    }
    
    if (!currentlyConnected) {
        // Attempt reconnect every WIFI_RECONNECT_INTERVAL_MS (5s default)
        if (now - _lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            _reconnectAttempts++;
            
            if (_reconnectAttempts <= 10) {
                engine->info("🔄 WiFi reconnect attempt " + String(_reconnectAttempts) + "/10...");
                WiFi.reconnect();
            } else if (_reconnectAttempts == 11) {
                // After 10 failed attempts (~50s), log once and keep trying silently
                engine->warn("⚠️ WiFi reconnect failed 10 times, continuing in background...");
            }
            // Keep trying indefinitely but silently after 10 attempts
            if (_reconnectAttempts > 10) {
                WiFi.reconnect();
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CASE 2: Reconnected → Re-announce mDNS
    // ═══════════════════════════════════════════════════════════════════════
    if (currentlyConnected && !_wasConnected) {
        engine->info("✅ WiFi reconnected! IP: " + WiFi.localIP().toString());
        engine->info("🔄 Re-announcing mDNS...");
        _reconnectAttempts = 0;
        
        // Re-initialize mDNS after reconnection
        MDNS.end();
        delay(50);
        setupMDNS();
    }
    
    _wasConnected = currentlyConnected;
}