/**
 * WiFiConfigManager.cpp - WiFi Configuration Management Implementation
 * 
 * Manages WiFi credentials in EEPROM with checksum validation.
 */

#include "communication/WiFiConfigManager.h"
#include "core/UtilityEngine.h"
#include <esp_wifi.h>

// Forward declaration
extern UtilityEngine* engine;

// ============================================================================
// CONSTRUCTOR - Ensure EEPROM is initialized
// ============================================================================

WiFiConfigManager::WiFiConfigManager() {
    // CRITICAL: Initialize EEPROM if not already done
    // This ensures WiFiConfigManager works even if constructed before UtilityEngine
    static bool eepromInitialized = false;
    if (!eepromInitialized) {
        EEPROM.begin(128);  // Same size as UtilityEngine uses
        eepromInitialized = true;
        Serial.println("[WiFiConfigManager] 🔧 EEPROM initialized in constructor");
    }
}

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

WiFiConfigManager& WiFiConfigManager::getInstance() {
    static WiFiConfigManager instance;
    return instance;
}

// ============================================================================
// CONFIGURATION CHECK
// ============================================================================

bool WiFiConfigManager::isConfigured() {
    uint8_t flag = EEPROM.read(WIFI_EEPROM_FLAG);
    if (flag != WIFI_CONFIG_MAGIC) {
        return false;
    }
    return verifyChecksum();
}

// ============================================================================
// LOAD CONFIGURATION
// ============================================================================

bool WiFiConfigManager::loadConfig(String& ssid, String& password) {
    if (!isConfigured()) {
        return false;
    }
    
    // Read SSID
    char ssidBuf[WIFI_SSID_MAX_LEN + 1] = {0};
    for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
        ssidBuf[i] = EEPROM.read(WIFI_EEPROM_SSID + i);
        if (ssidBuf[i] == 0) break;
    }
    ssidBuf[WIFI_SSID_MAX_LEN] = 0;  // Ensure null termination
    ssid = String(ssidBuf);
    
    // Read password
    char passBuf[WIFI_PASSWORD_MAX_LEN + 1] = {0};
    for (int i = 0; i < WIFI_PASSWORD_MAX_LEN; i++) {
        passBuf[i] = EEPROM.read(WIFI_EEPROM_PASSWORD + i);
        if (passBuf[i] == 0) break;
    }
    passBuf[WIFI_PASSWORD_MAX_LEN] = 0;  // Ensure null termination
    password = String(passBuf);
    
    if (engine) {
        engine->info("📶 WiFi config loaded from EEPROM: " + ssid);
    }
    
    return true;
}

// ============================================================================
// SAVE CONFIGURATION
// ============================================================================

bool WiFiConfigManager::saveConfig(const String& ssid, const String& password) {
    if (ssid.length() == 0 || ssid.length() > WIFI_SSID_MAX_LEN - 1) {
        if (engine) engine->error("❌ WiFi save: Invalid SSID length");
        return false;
    }
    if (password.length() > WIFI_PASSWORD_MAX_LEN - 1) {
        if (engine) engine->error("❌ WiFi save: Password too long");
        return false;
    }
    
    // 🛡️ PROTECTION: Set EEPROM write in progress flag
    _eepromWriteInProgress = true;
    
    if (engine) engine->info("💾 Saving WiFi config: " + ssid + " (" + String(ssid.length()) + " chars)");
    
    // Write magic flag
    EEPROM.write(WIFI_EEPROM_FLAG, WIFI_CONFIG_MAGIC);
    
    // Write SSID (with null termination)
    for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
        if (i < (int)ssid.length()) {
            EEPROM.write(WIFI_EEPROM_SSID + i, ssid[i]);
        } else {
            EEPROM.write(WIFI_EEPROM_SSID + i, 0);
        }
    }
    
    // Write password (with null termination)
    for (int i = 0; i < WIFI_PASSWORD_MAX_LEN; i++) {
        if (i < (int)password.length()) {
            EEPROM.write(WIFI_EEPROM_PASSWORD + i, password[i]);
        } else {
            EEPROM.write(WIFI_EEPROM_PASSWORD + i, 0);
        }
    }
    
    // Calculate and write checksum
    uint8_t checksum = calculateChecksum();
    EEPROM.write(WIFI_EEPROM_CHECKSUM, checksum);
    
    // 🛡️ COMMIT WITH RETRY: Flash wear-out protection
    const int maxRetries = 3;
    bool committed = false;
    
    for (int attempt = 0; attempt < maxRetries && !committed; attempt++) {
        if (attempt > 0 && engine) {
            engine->warn("⚠️ EEPROM commit retry #" + String(attempt) + "/" + String(maxRetries));
        }
        
        committed = EEPROM.commit();
        
        if (!committed) {
            // Exponential backoff: 50ms, 100ms, 150ms
            delay(50 * (attempt + 1));
        }
    }
    
    // 🛡️ PROTECTION: Clear EEPROM write flag BEFORE any other operations
    _eepromWriteInProgress = false;
    
    if (!committed) {
        if (engine) engine->error("❌ EEPROM commit failed after " + String(maxRetries) + " retries!");
        return false;
    }
    
    // 🛡️ SAFETY DELAY: Let EEPROM stabilize after commit
    delay(50);
    
    // VERIFY: Read back and check
    String verifySSID, verifyPassword;
    if (!loadConfig(verifySSID, verifyPassword)) {
        if (engine) engine->error("❌ EEPROM verify failed: couldn't read back config!");
        return false;
    }
    
    if (verifySSID != ssid) {
        if (engine) engine->error("❌ EEPROM verify failed: SSID mismatch! Saved='" + ssid + "' Read='" + verifySSID + "'");
        return false;
    }
    
    if (engine) {
        engine->info("✅ WiFi config verified in EEPROM: " + verifySSID);
    }
    
    return true;
}

// ============================================================================
// CLEAR CONFIGURATION
// ============================================================================

bool WiFiConfigManager::clearConfig() {
    // 🛡️ PROTECTION: Set EEPROM write in progress
    _eepromWriteInProgress = true;
    
    // Clear magic flag
    EEPROM.write(WIFI_EEPROM_FLAG, 0x00);
    
    // Clear SSID
    for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
        EEPROM.write(WIFI_EEPROM_SSID + i, 0);
    }
    
    // Clear password
    for (int i = 0; i < WIFI_PASSWORD_MAX_LEN; i++) {
        EEPROM.write(WIFI_EEPROM_PASSWORD + i, 0);
    }
    
    // Clear checksum
    EEPROM.write(WIFI_EEPROM_CHECKSUM, 0);
    
    // 🛡️ COMMIT WITH RETRY
    const int maxRetries = 3;
    bool committed = false;
    
    for (int attempt = 0; attempt < maxRetries && !committed; attempt++) {
        if (attempt > 0 && engine) {
            engine->warn("⚠️ EEPROM clear retry #" + String(attempt));
        }
        committed = EEPROM.commit();
        if (!committed) delay(50 * (attempt + 1));
    }
    
    // 🛡️ PROTECTION: Clear flag and stabilize
    _eepromWriteInProgress = false;
    delay(50);
    
    if (!committed && engine) {
        engine->error("❌ EEPROM clear commit failed after retries");
    }
    
    if (engine) {
        engine->info("🗑️ WiFi config cleared from EEPROM");
    }
    
    return true;
}

// ============================================================================
// SCAN NETWORKS
// ============================================================================

int WiFiConfigManager::scanNetworks(WiFiNetworkInfo* networks, int maxNetworks) {
    if (engine) {
        engine->info("📡 Scanning WiFi networks...");
    }
    
    // We're already in AP_STA mode (set in startAPMode), so we can scan directly
    // DO NOT change WiFi mode here - it disconnects all AP clients!
    
    // Perform scan (blocking, with longer timeout for stability)
    if (engine) engine->info("📡 Starting network scan (AP stays active)...");
    int numNetworks = WiFi.scanNetworks(false, false, false, 300);  // async=false, showHidden=false, passive=false, max_ms_per_chan=300
    
    if (numNetworks < 0) {
        if (engine) engine->error("❌ WiFi scan failed with code: " + String(numNetworks));
        // Try again with different settings
        delay(500);
        numNetworks = WiFi.scanNetworks(false, false);
    }
    
    if (numNetworks < 0) {
        if (engine) engine->error("❌ WiFi scan failed after retry");
        return 0;
    }
    
    if (engine) engine->info("📡 Raw scan found " + String(numNetworks) + " networks");
    
    // First pass: collect all networks with deduplication (mesh support)
    // For duplicate SSIDs, keep only the one with strongest signal
    int uniqueCount = 0;
    
    for (int i = 0; i < numNetworks && uniqueCount < maxNetworks; i++) {
        String currentSSID = WiFi.SSID(i);
        int32_t currentRSSI = WiFi.RSSI(i);
        
        // Skip empty SSIDs
        if (currentSSID.length() == 0) continue;
        
        // Check if this SSID already exists in our list
        bool isDuplicate = false;
        for (int j = 0; j < uniqueCount; j++) {
            if (networks[j].ssid == currentSSID) {
                // Duplicate found - keep the one with better signal
                isDuplicate = true;
                if (currentRSSI > networks[j].rssi) {
                    // This one is stronger, replace
                    networks[j].rssi = currentRSSI;
                    networks[j].encryptionType = WiFi.encryptionType(i);
                    networks[j].channel = WiFi.channel(i);
                }
                break;
            }
        }
        
        // Not a duplicate, add to list
        if (!isDuplicate) {
            networks[uniqueCount].ssid = currentSSID;
            networks[uniqueCount].rssi = currentRSSI;
            networks[uniqueCount].encryptionType = WiFi.encryptionType(i);
            networks[uniqueCount].channel = WiFi.channel(i);
            uniqueCount++;
        }
    }
    
    // Sort by signal strength (strongest first)
    for (int i = 0; i < uniqueCount - 1; i++) {
        for (int j = i + 1; j < uniqueCount; j++) {
            if (networks[j].rssi > networks[i].rssi) {
                WiFiNetworkInfo temp = networks[i];
                networks[i] = networks[j];
                networks[j] = temp;
            }
        }
    }
    
    // Clean up scan results
    WiFi.scanDelete();
    
    // NO mode change here - we stay in AP_STA to keep clients connected
    
    if (engine) {
        engine->info("📡 Found " + String(uniqueCount) + " unique WiFi networks");
    }
    
    return uniqueCount;
}

// ============================================================================
// TEST CONNECTION
// ============================================================================

bool WiFiConfigManager::testConnection(const String& ssid, const String& password, unsigned long timeoutMs) {
    if (engine) {
        engine->info("🔌 Testing WiFi connection to: " + ssid);
    }
    
    // We're already in AP_STA mode (set in startAPMode), so we can just use
    // WiFi.begin() without changing modes - this keeps the AP stable!
    
    // Try to connect as STA (AP stays active and stable)
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(250);
    }
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    String assignedIP = "";
    
    if (connected) {
        assignedIP = WiFi.localIP().toString();
        if (engine) {
            engine->info("✅ WiFi test successful! IP: " + assignedIP);
        }
    } else {
        if (engine) {
            engine->warn("❌ WiFi test failed for: " + ssid);
        }
    }
    
    // DON'T disconnect - it can disrupt the AP on some ESP32 firmware versions
    // The STA connection will be cleared on reboot anyway
    // WiFi.disconnect(false);  // REMOVED - was causing AP client disconnection
    
    if (engine) {
        engine->info("📡 AP still active: " + WiFi.softAPIP().toString());
        if (connected) {
            engine->info("📡 STA also connected to: " + ssid + " (will disconnect on reboot)");
        }
    }
    
    return connected;
}

// ============================================================================
// GET STORED SSID
// ============================================================================

String WiFiConfigManager::getStoredSSID() {
    if (!isConfigured()) {
        return "";
    }
    
    char ssidBuf[WIFI_SSID_MAX_LEN + 1] = {0};
    for (int i = 0; i < WIFI_SSID_MAX_LEN; i++) {
        ssidBuf[i] = EEPROM.read(WIFI_EEPROM_SSID + i);
        if (ssidBuf[i] == 0) break;
    }
    return String(ssidBuf);
}

// ============================================================================
// ENCRYPTION TYPE STRING
// ============================================================================

String WiFiConfigManager::encryptionTypeToString(uint8_t encType) {
    switch (encType) {
        case WIFI_AUTH_OPEN:            return "Open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        default:                        return "Unknown";
    }
}

// ============================================================================
// CHECKSUM HELPERS
// ============================================================================

uint8_t WiFiConfigManager::calculateChecksum() {
    uint8_t checksum = 0;
    for (int i = WIFI_EEPROM_FLAG; i < WIFI_EEPROM_CHECKSUM; i++) {
        checksum ^= EEPROM.read(i);
    }
    return checksum;
}

bool WiFiConfigManager::verifyChecksum() {
    uint8_t calculated = calculateChecksum();
    uint8_t stored = EEPROM.read(WIFI_EEPROM_CHECKSUM);
    return (calculated == stored);
}
