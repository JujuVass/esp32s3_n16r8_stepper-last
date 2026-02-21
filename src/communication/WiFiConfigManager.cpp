/**
 * WiFiConfigManager.cpp - WiFi Configuration Management Implementation
 *
 * Manages WiFi credentials in NVS via Preferences API.
 */

#include "communication/WiFiConfigManager.h"
#include "core/UtilityEngine.h"
#include <esp_wifi.h>

// Forward declaration
extern UtilityEngine* engine;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

// NVS initialization is deferred to ensureInitialized()
// because this constructor runs during global static init,
// before NVS flash is ready (ESP-IDF 5.x / pioarduino).
WiFiConfigManager::WiFiConfigManager() = default;

void WiFiConfigManager::ensureInitialized() {
    if (_initialized) return;
    _initialized = _prefs.begin(NVS_NAMESPACE, false);
    if (_initialized) {
        if (engine) engine->info("[WiFiConfigManager] 🔧 Preferences (NVS) initialized");
    } else {
        if (engine) engine->error("[WiFiConfigManager] ❌ Failed to initialize NVS!");
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
    ensureInitialized();
    return _prefs.getBool("configured", false);
}

// ============================================================================
// LOAD CONFIGURATION
// ============================================================================

bool WiFiConfigManager::loadConfig(String& outSsid, String& outPassword) {
    ensureInitialized();
    if (!isConfigured()) {
        return false;
    }

    outSsid = _prefs.getString("ssid", "");
    outPassword = _prefs.getString("password", "");

    if (outSsid.isEmpty()) {
        return false;
    }

    if (engine) {
        engine->info("📶 WiFi config loaded from NVS: " + outSsid);
    }

    return true;
}

// ============================================================================
// SAVE CONFIGURATION
// ============================================================================

bool WiFiConfigManager::saveConfig(const String& newSsid, const String& newPassword) {
    if (newSsid.isEmpty() || newSsid.length() > WIFI_SSID_MAX_LEN - 1) {
        if (engine) engine->error("❌ WiFi save: Invalid SSID length");
        return false;
    }
    if (newPassword.length() > WIFI_PASSWORD_MAX_LEN - 1) {
        if (engine) engine->error("❌ WiFi save: Password too long");
        return false;
    }

    if (engine) engine->info("💾 Saving WiFi config: " + newSsid + " (" + String(newSsid.length()) + " chars)");

    ensureInitialized();

    // Write all keys and check return values
    size_t ssidWritten = _prefs.putString("ssid", newSsid);
    _prefs.putString("password", newPassword);
    _prefs.putBool("configured", true);

    if (ssidWritten == 0) {
        if (engine) engine->error("❌ NVS putString('ssid') failed! NVS may be full or corrupted");
        return false;
    }

    // Verify: Read back and check
    String verifySSID = _prefs.getString("ssid", "");

    if (verifySSID != newSsid) {
        if (engine) engine->error("❌ NVS verify failed: SSID mismatch! Saved='" + newSsid + "' Read='" + verifySSID + "'");
        return false;
    }

    if (engine) {
        engine->info("✅ WiFi config verified in NVS: " + verifySSID);
    }

    return true;
}

// ============================================================================
// CLEAR CONFIGURATION
// ============================================================================

bool WiFiConfigManager::clearConfig() {
    ensureInitialized();
    _prefs.clear();  // Remove all keys in this namespace

    if (engine) {
        engine->info("🗑️ WiFi config cleared from NVS");
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
        if (currentSSID.isEmpty()) continue;

        // Find existing entry with same SSID
        int existingIdx = -1;
        for (int j = 0; j < uniqueCount; j++) {
            if (networks[j].ssid == currentSSID) {
                existingIdx = j;
                break;
            }
        }

        // Duplicate: keep strongest signal
        if (existingIdx >= 0) {
            if (currentRSSI > networks[existingIdx].rssi) {
                networks[existingIdx].rssi = currentRSSI;
                networks[existingIdx].encryptionType = WiFi.encryptionType(i);
                networks[existingIdx].channel = WiFi.channel(i);
            }
            continue;
        }

        // New network, add to list
        networks[uniqueCount].ssid = currentSSID;
        networks[uniqueCount].rssi = currentRSSI;
        networks[uniqueCount].encryptionType = WiFi.encryptionType(i);
        networks[uniqueCount].channel = WiFi.channel(i);
        uniqueCount++;
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

bool WiFiConfigManager::testConnection(const String& testSsid, const String& testPassword, unsigned long timeoutMs) {
    if (engine) {
        engine->info("🔌 Testing WiFi connection to: " + testSsid);
    }

    // We're already in AP_STA mode (set in startAPMode), so we can just use
    // WiFi.begin() without changing modes - this keeps the AP stable!

    // Try to connect as STA (AP stays active and stable)
    WiFi.begin(testSsid.c_str(), testPassword.c_str());

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
            engine->warn("❌ WiFi test failed for: " + testSsid);
        }
    }

    // DON'T disconnect - it can disrupt the AP on some ESP32 firmware versions
    // The STA connection will be cleared on reboot anyway
    // WiFi.disconnect(false);  // REMOVED - was causing AP client disconnection

    if (engine) {
        engine->info("📡 AP still active: " + WiFi.softAPIP().toString());
        if (connected) {
            engine->info("📡 STA also connected to: " + testSsid + " (will disconnect on reboot)");
        }
    }

    return connected;
}

// ============================================================================
// GET STORED SSID
// ============================================================================

String WiFiConfigManager::getStoredSSID() {
    ensureInitialized();
    if (!isConfigured()) {
        return "";
    }

    return _prefs.getString("ssid", "");
}

// ============================================================================
// ENCRYPTION TYPE STRING
// ============================================================================

String WiFiConfigManager::encryptionTypeToString(wifi_auth_mode_t encType) {
    switch (encType) {
        case WIFI_AUTH_OPEN:            return "Open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
        case WIFI_AUTH_OWE:             return "OWE";
        case WIFI_AUTH_WPA3_ENT_192:    return "WPA3-Enterprise-192";
        case WIFI_AUTH_DPP:             return "DPP";
        default:                        return "Unknown";
    }
}


