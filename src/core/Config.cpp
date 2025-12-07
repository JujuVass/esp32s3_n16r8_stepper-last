// ============================================================================
// CONFIG.CPP - Configuration Variable Definitions
// ============================================================================
// Definitions for extern variables declared in Config.h
// This file must be compiled once to avoid multiple definition errors
// ============================================================================

#include "core/Config.h"

// ============================================================================
// WIFI CREDENTIALS
// ============================================================================
const char* ssid = "your_ssid";
const char* password = "REDACTED_WIFI_PASSWORD";

// ============================================================================
// OTA CREDENTIALS
// ============================================================================
const char* otaHostname = "esp32-stepper";  // Also used for mDNS (http://esp32-stepper.local)
const char* otaPassword = "REDACTED_OTA_PASSWORD";  // OTA password protection
