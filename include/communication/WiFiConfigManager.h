/**
 * WiFiConfigManager.h - WiFi Configuration Management
 * 
 * Manages WiFi credentials stored in EEPROM:
 * - Load/Save WiFi SSID and password
 * - Scan available networks
 * - Test connection before saving
 * - Clear configuration (factory reset)
 * 
 * EEPROM Layout (starting at address 4):
 *   Addr 4      : Configured flag (0xAA = valid config)
 *   Addr 5-36   : SSID (32 bytes, null-terminated)
 *   Addr 37-100  : Password (64 bytes, null-terminated)
 *   Addr 101     : Checksum (XOR of all bytes 4-100)
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// EEPROM addresses for WiFi config
// Layout: 0=logging, 1=log_level, 2=stats_enabled, 3=sensorsInverted, 4-102=WiFi, 127=global_checksum
#define WIFI_EEPROM_START       4
#define WIFI_EEPROM_FLAG        4       // 1 byte: 0xAA = configured
#define WIFI_EEPROM_SSID        5       // 32 bytes
#define WIFI_EEPROM_PASSWORD    37      // 64 bytes
#define WIFI_EEPROM_CHECKSUM    101     // 1 byte
#define WIFI_EEPROM_END         102

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASSWORD_MAX_LEN   64
#define WIFI_CONFIG_MAGIC       0xAA    // Magic byte for valid config

// WiFi network info structure (for scan results)
struct WiFiNetworkInfo {
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    int32_t channel;
};

class WiFiConfigManager {
public:
    static WiFiConfigManager& getInstance();
    
    /**
     * 🛡️ Check if EEPROM write is in progress (safety for WiFi operations)
     * @return true if EEPROM is busy writing
     */
    bool isEEPROMBusy() const { return _eepromWriteInProgress; }
    
    /**
     * Check if WiFi is configured in EEPROM
     * @return true if valid configuration exists
     */
    bool isConfigured();
    
    /**
     * Load WiFi credentials from EEPROM
     * @param ssid Output: SSID string
     * @param password Output: Password string
     * @return true if valid config loaded
     */
    bool loadConfig(String& ssid, String& password);
    
    /**
     * Save WiFi credentials to EEPROM
     * @param ssid SSID to save (max 31 chars)
     * @param password Password to save (max 63 chars)
     * @return true if saved successfully
     */
    bool saveConfig(const String& ssid, const String& password);
    
    /**
     * Clear WiFi configuration from EEPROM
     * @return true if cleared successfully
     */
    bool clearConfig();
    
    /**
     * Scan available WiFi networks
     * @param networks Output: Vector of network info
     * @param maxNetworks Maximum networks to return
     * @return Number of networks found
     */
    int scanNetworks(WiFiNetworkInfo* networks, int maxNetworks);
    
    /**
     * Test WiFi connection with given credentials
     * Does NOT save to EEPROM - just tests
     * @param ssid SSID to test
     * @param password Password to test
     * @param timeoutMs Connection timeout in milliseconds
     * @return true if connection successful
     */
    bool testConnection(const String& ssid, const String& password, unsigned long timeoutMs = 15000);
    
    /**
     * Get stored SSID (without password for security)
     * @return SSID or empty string if not configured
     */
    String getStoredSSID();
    
    /**
     * Get encryption type as readable string
     */
    static String encryptionTypeToString(uint8_t encType);

private:
    WiFiConfigManager();  // Defined in .cpp to ensure EEPROM initialization
    WiFiConfigManager(const WiFiConfigManager&) = delete;
    WiFiConfigManager& operator=(const WiFiConfigManager&) = delete;
    
    /**
     * Calculate checksum for config data
     */
    uint8_t calculateChecksum();
    
    /**
     * Verify checksum of stored config
     */
    bool verifyChecksum();
    
    // 🛡️ PROTECTION: Track EEPROM write state
    mutable bool _eepromWriteInProgress = false;
};

// Global accessor (singleton reference)
extern WiFiConfigManager& WiFiConfig;
