/**
 * NetworkManager.cpp - WiFi, OTA, mDNS, NTP Implementation
 */

#include "communication/NetworkManager.h"
#include "communication/WiFiConfigManager.h"
#include "UtilityEngine.h"
#include "hardware/MotorDriver.h"
#include "sequencer/SequenceExecutor.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
}

// ============================================================================
// WIFI CONNECTION
// ============================================================================

bool NetworkManager::connectWiFi() {
    // AP+STA dual mode: WiFi client + Access Point simultaneously
    WiFi.mode(WIFI_AP_STA);
    
    // Get WiFi credentials - prefer EEPROM config, fallback to Config.h defaults
    String targetSSID;
    String targetPassword;
    String targetHostname = otaHostname;  // Default hostname
    
    if (WiFiConfig.isConfigured() && WiFiConfig.loadConfig(targetSSID, targetPassword)) {
        // Use EEPROM stored credentials
        engine->info("📶 Using saved WiFi config: " + targetSSID);
    } else {
        // Use hardcoded defaults from Config.h
        targetSSID = ssid;
        targetPassword = password;
        engine->info("📶 Using default WiFi config: " + targetSSID);
    }
    
    // Start STA (client) connection
    WiFi.begin(targetSSID.c_str(), targetPassword.c_str());
    
    engine->info("Connecting to WiFi: " + targetSSID);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 60) {
        delay(500);
        Serial.print(".");
        attempts++;
        
        if (attempts % 10 == 0) {
            engine->info(String("\n[") + String(attempts) + "/60] WiFi connecting...");
        }
    }
    Serial.println();
    
    _wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    if (_wifiConnected) {
        engine->info("✅ WiFi connected!");
        engine->info("🌐 STA IP Address: " + WiFi.localIP().toString());
        engine->info("🔄 OTA Mode: ACTIVE - Updates via WiFi enabled!");
    } else {
        engine->error("❌ WiFi connection failed!");
    }
    
    // Start AP (Access Point) - always available even if STA fails
    String apName = String(targetHostname) + "-AP";
    WiFi.softAP(apName.c_str());  // Open network (no password)
    
    engine->info("📡 AP Mode started: " + apName);
    engine->info("🌐 AP IP Address: " + WiFi.softAPIP().toString());
    
    return _wifiConnected;
}

// ============================================================================
// GET CONFIGURED SSID
// ============================================================================

String NetworkManager::getConfiguredSSID() const {
    if (WiFiConfig.isConfigured()) {
        return WiFiConfig.getStoredSSID();
    }
    return String(ssid);  // Default from Config.h
}

// ============================================================================
// MDNS SETUP
// ============================================================================

bool NetworkManager::setupMDNS() {
    if (!_wifiConnected) return false;
    
    if (MDNS.begin(otaHostname)) {
        engine->info("✅ mDNS responder started: http://" + String(otaHostname) + ".local");
        MDNS.addService("http", "tcp", 80);
        return true;
    } else {
        engine->error("❌ Error starting mDNS responder");
        return false;
    }
}

// ============================================================================
// NTP TIME SYNC
// ============================================================================

void NetworkManager::setupNTP() {
    if (!_wifiConnected) return;
    
    // Configure time with NTP (GMT+1 = 3600 seconds, daylight saving = 0)
    configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
    engine->info("⏰ NTP time configured (GMT+1)");
    
    // Wait a bit for time sync
    delay(1000);
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo->tm_year > (2020 - 1900)) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        engine->info("✓ Time synchronized: " + String(timeStr));
    }
}

// ============================================================================
// OTA CONFIGURATION
// ============================================================================

void NetworkManager::setupOTA(std::function<void()> onStopMovement,
                              std::function<void()> onStopSequencer) {
    if (!_wifiConnected) return;
    
    _onStopMovement = onStopMovement;
    _onStopSequencer = onStopSequencer;
    
    ArduinoOTA.setHostname(otaHostname);
    
    if (strlen(otaPassword) > 0) {
        ArduinoOTA.setPassword(otaPassword);
    }
    
    ArduinoOTA.onStart([this]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        engine->info("🔄 OTA Update starting: " + type);
        
        // Flush logs before OTA
        if (engine) {
            engine->flushLogBuffer(true);
        }
        
        // Stop movements
        if (_onStopMovement) _onStopMovement();
        Motor.disable();
        
        // Stop sequencer
        if (_onStopSequencer) _onStopSequencer();
    });
    
    ArduinoOTA.onEnd([]() {
        engine->info("✅ OTA Update complete - Rebooting...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastPercent = 0;
        unsigned int percent = (progress * 100) / total;
        
        if (percent >= lastPercent + 10) {
            engine->info("📥 OTA Progress: " + String(percent) + "%");
            lastPercent = percent;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        engine->error("❌ OTA Error [" + String(error) + "]");
        switch (error) {
            case OTA_AUTH_ERROR:    engine->error("   Authentication Failed"); break;
            case OTA_BEGIN_ERROR:   engine->error("   Begin Failed"); break;
            case OTA_CONNECT_ERROR: engine->error("   Connect Failed"); break;
            case OTA_RECEIVE_ERROR: engine->error("   Receive Failed"); break;
            case OTA_END_ERROR:     engine->error("   End Failed"); break;
        }
    });
    
    ArduinoOTA.begin();
    engine->info("✅ OTA Ready - Hostname: " + String(otaHostname));
    _otaConfigured = true;
}

// ============================================================================
// FULL INITIALIZATION
// ============================================================================

bool NetworkManager::begin() {
    bool connected = connectWiFi();
    
    if (connected) {
        // Full mode: STA + AP + all services
        _degradedMode = false;
        setupMDNS();
        setupNTP();
        setupOTA(
            []() { stopMovement(); },
            []() { if (seqState.isRunning) SeqExecutor.stop(); }
        );
        engine->info("✅ Network: FULL MODE (STA + AP + OTA + mDNS)");
    } else {
        // Degraded mode: AP only - service minimum
        _degradedMode = true;
        engine->warn("⚠️ Network: DEGRADED MODE (AP only at 192.168.4.1)");
        engine->warn("⚠️ Connect to WiFi '" + String(otaHostname) + "-AP' for access");
        engine->warn("⚠️ OTA and mDNS disabled in degraded mode");
    }
    
    return connected;
}
