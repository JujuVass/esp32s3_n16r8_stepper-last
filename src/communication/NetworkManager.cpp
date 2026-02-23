/**
 * NetworkManager.cpp - WiFi StepperNetwork Management Implementation
 *
 * Three modes (GPIO 19: GND = normal, floating = AP_SETUP):
 * - AP_SETUP:  GPIO 19 floating or no credentials → Config-only (setup.html + captive portal)
 * - STA+AP:    GPIO 19 GND + WiFi connected → Full app on both interfaces (STA + AP parallel)
 * - AP_DIRECT: GPIO 19 GND + WiFi fail → AP-only with full stepper control
 */

#include "communication/NetworkManager.h"
#include "communication/WiFiConfigManager.h"
#include "core/UtilityEngine.h"
#include "core/TimeUtils.h"
#include "hardware/MotorDriver.h"
#include "movement/SequenceExecutor.h"
#include <sys/time.h>

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

StepperNetworkManager& StepperNetworkManager::getInstance() {
    static StepperNetworkManager instance; // NOSONAR(cpp:S6018)
    return instance;
}

// ============================================================================
// MODE DETERMINATION - Should we enter AP_SETUP (config-only)?
// GPIO 19: GND (LOW) = normal operation, floating (HIGH via pull-up) = AP_SETUP
// Also enters AP_SETUP if no WiFi credentials are available
// ============================================================================

bool StepperNetworkManager::shouldStartAPSetup() {
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
    String savedSSID;
    String savedPassword;
    bool eepromConfigured = WiFiConfig.isConfigured();
    engine->info("📦 NVS configured: " + String(eepromConfigured ? "YES" : "NO"));

    if (eepromConfigured && WiFiConfig.loadConfig(savedSSID, savedPassword) && !savedSSID.isEmpty()) {
        engine->info("📡 Found saved WiFi config: '" + savedSSID + "' → Try STA+AP mode");
        return false;  // Have credentials, try STA mode
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

void StepperNetworkManager::startAPSetupMode() {
    _mode = NetworkMode::NET_AP_SETUP;

    // Use AP_STA mode so we can test WiFi connections without disrupting the AP
    WiFi.mode(WIFI_AP_STA);

    // Start Access Point
    String apName = String(otaHostname) + "-Setup";
    WiFi.softAP(apName.c_str(), nullptr, 1);  // Open network, explicit channel 1

    WiFi.setSleep(WIFI_PS_NONE);

    // Start Captive Portal DNS server
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _captivePortalActive = true;
    _cachedIP = WiFi.softAPIP().toString();

    engine->info("═══════════════════════════════════════════════════════");
    engine->info("🔧 AP_SETUP MODE - WiFi Configuration + Captive Portal");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   StepperNetwork: " + apName);
    engine->info("   IP: " + _cachedIP);
    engine->info("   📱 Captive Portal active - auto-opens on connect!");
    engine->info("   ⚠️  Hardware NOT initialized (config mode only)");
    engine->info("═══════════════════════════════════════════════════════");
}

// ============================================================================
// AP_DIRECT MODE (Full stepper control via AP, no router)
// ============================================================================

void StepperNetworkManager::startAPDirectMode() {
    _mode = NetworkMode::NET_AP_DIRECT;

    WiFi.mode(WIFI_AP);

    // Start Access Point with optional password
    String apName = String(otaHostname) + "-AP";
    if (strlen(AP_DIRECT_PASSWORD) > 0) {
        WiFi.softAP(apName.c_str(), AP_DIRECT_PASSWORD, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    } else {
        WiFi.softAP(apName.c_str(), nullptr, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    }

    WiFi.setSleep(WIFI_PS_NONE);
    _cachedIP = WiFi.softAPIP().toString();

    // Start DNS server so clients' connectivity checks resolve
    // (prevents OS from marking this WiFi as "no internet" and blocking WebSocket)
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _captivePortalActive = true;

    engine->info("═══════════════════════════════════════════════════════");
    engine->info("📡 AP_DIRECT MODE - Full Stepper Control via WiFi AP");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   StepperNetwork: " + apName);
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

void StepperNetworkManager::startParallelAP() {
    String apName = String(otaHostname) + "-AP";
    if (strlen(AP_DIRECT_PASSWORD) > 0) {
        WiFi.softAP(apName.c_str(), AP_DIRECT_PASSWORD, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    } else {
        WiFi.softAP(apName.c_str(), nullptr, AP_DIRECT_CHANNEL, 0, AP_DIRECT_MAX_CLIENTS);
    }

    // Start DNS server so AP clients' connectivity checks resolve
    // (prevents OS from marking this WiFi as "no internet" and blocking WebSocket)
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _captivePortalActive = true;

    engine->info("📡 Parallel AP started: " + apName + " (IP: " + WiFi.softAPIP().toString() + ", DNS active)");
}

// ============================================================================
// CAPTIVE PORTAL HANDLER
// ============================================================================

void StepperNetworkManager::handleCaptivePortal() {
    if (_captivePortalActive) {
        _dnsServer.processNextRequest();
    }
}

// ============================================================================
// STA MODE (Normal Operation + Parallel AP)
// ============================================================================

bool StepperNetworkManager::startSTAMode() {
    _mode = NetworkMode::NET_STA_AP;

    // Set WiFi mode: STA-only for best performance, AP_STA if parallel AP enabled
    WiFi.mode(ENABLE_PARALLEL_AP ? WIFI_AP_STA : WIFI_STA);

    // Get credentials - check NVS first, then Config.h defaults
    String targetSSID;
    String targetPassword;
    String credentialSource;

    if (WiFiConfig.isConfigured() && WiFiConfig.loadConfig(targetSSID, targetPassword)) {
        credentialSource = "NVS";
        engine->info("🔑 WiFi credentials from: NVS (saved config)");
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

        // NVS writes are atomic, no busy-wait needed

        // Failed to connect - switch to AP_DIRECT (full app, not setup!)
        WiFi.disconnect();
        startAPDirectMode();
        return false;
    }

    // Connected successfully - cache IP
    _cachedIP = WiFi.localIP().toString();

    // Start parallel AP if enabled (device also accessible via 192.168.4.1)
    if (ENABLE_PARALLEL_AP) {
        startParallelAP();
    }

    engine->info("═══════════════════════════════════════════════════════");
    engine->info(ENABLE_PARALLEL_AP ? "✅ STA+AP MODE - Stepper Controller Active" : "✅ STA MODE - Stepper Controller Active");
    engine->info("═══════════════════════════════════════════════════════");
    engine->info("   WiFi: " + targetSSID + " [" + credentialSource + "]");
    engine->info("   STA IP: " + _cachedIP);
    if (ENABLE_PARALLEL_AP) {
        engine->info("   AP IP:  " + WiFi.softAPIP().toString());
    }
    engine->info("   Hostname: http://" + String(otaHostname) + ".local");
    engine->info(ENABLE_PARALLEL_AP ? "   🎮 App accessible on BOTH interfaces!" : "   🎮 STA-only (no parallel AP → lower latency)");
    engine->info("═══════════════════════════════════════════════════════");

    // Disable WiFi power saving
    WiFi.setSleep(WIFI_PS_NONE);
    engine->info("⚡ WiFi power save: DISABLED (always active)");

    // Setup additional services (only available with router connection)
    setupMDNS(false);  // http/80 + ws/81 only (ArduinoOTA will register arduino/3232)
    _lastMdnsRefresh = millis();
    setupNTP();
    setupOTA();        // Calls MDNS.begin() internally (no-op) + enableArduino()

    return true;
}

// ============================================================================
// GET CONFIGURED SSID
// ============================================================================

String StepperNetworkManager::getConfiguredSSID() const {
    String savedSSID;
    if (String savedPassword; WiFiConfig.isConfigured() && WiFiConfig.loadConfig(savedSSID, savedPassword)) {
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

bool StepperNetworkManager::setupMDNS(bool includeOtaService) {
    if (MDNS.begin(otaHostname)) {
        engine->debug("✅ mDNS: http://" + String(otaHostname) + ".local");

        // Add services for discovery
        MDNS.addService("http", "tcp", 80);        // Web server
        MDNS.addService("ws", "tcp", 81);          // WebSocket
        if (includeOtaService) {
            MDNS.addService("arduino", "tcp", 3232);  // OTA discovery (for health check refresh)
        }

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

void StepperNetworkManager::setupNTP() {
    // TODO: Make timezone configurable (currently hardcoded to GMT+1/CET)
    configTime(3600, 0, "pool.ntp.org", "time.nist.gov");  // GMT+1
    engine->info("⏰ NTP configured (GMT+1)");

    delay(1000);
    if (TimeUtils::isSynchronized()) {
        engine->info("\u2713 Time: " + TimeUtils::format("%Y-%m-%d %H:%M:%S"));
    }
}

// ============================================================================
// OTA CONFIGURATION (STA mode only)
// ============================================================================

void StepperNetworkManager::setupOTA() {
    ArduinoOTA.setHostname(otaHostname);

    if (strlen(otaPassword) > 0) {
        ArduinoOTA.setPassword(otaPassword);
    }

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        engine->info("🔄 OTA Update: " + type);

        // Reuse shared safe shutdown (stop movement, disable motor, flush logs)
        StepperNetwork.safeShutdown();
    });

    ArduinoOTA.onEnd([]() {
        engine->info("✅ OTA Complete - Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastPercent = 100;  // Force log at 0% on each new OTA
        unsigned int percent = (progress * 100) / total;
        if (percent < lastPercent) lastPercent = 0;  // Reset on new transfer
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

bool StepperNetworkManager::begin() {
    engine->info("🌐 StepperNetwork initialization...");

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
// SAFE SHUTDOWN - Stop movement, disable motor, flush logs
// Called before ESP.restart() (watchdog, OTA, API reboot)
// ============================================================================

void StepperNetworkManager::safeShutdown() {
    engine->warn("🛑 Safe shutdown initiated...");

    // Stop all movement and motor activity
    stopMovement();
    if (seqState.isRunning) SeqExecutor.stop();
    Motor.disable();

    // Flush logs to filesystem (blocks until done)
    if (engine) {
        engine->flushLogBuffer(true);
        engine->info("✅ Safe shutdown complete");
    }
}

// ============================================================================
// CONNECTION WATCHDOG (STA+AP mode only)
// Three-tier escalation with gateway ping + mDNS checks:
//   Tier 1: WiFi.reconnect() (soft)
//   Tier 2: Full WiFi.disconnect() + WiFi.begin() (hard re-association)
//   Tier 3: ESP.restart() (emergency reboot)
// ============================================================================

// ============================================================================
// ASYNC GATEWAY PING (non-blocking)
// Launches an esp_ping session that runs in ESP-IDF's internal task.
// The callback sets _pingGotReply atomically.
// checkConnectionHealth() polls the result on subsequent calls.
// ============================================================================

bool StepperNetworkManager::startAsyncPing() {
    if (_pingInProgress) return false;  // Already running

    IPAddress gw = WiFi.gatewayIP();
    if (gw == IPAddress(0, 0, 0, 0)) return false;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr.u_addr.ip4.addr = gw;
    cfg.target_addr.type = ESP_IPADDR_TYPE_V4;
    cfg.count = 2;           // Send 2 pings — 1 success = pass
    cfg.timeout_ms = 1000;
    cfg.interval_ms = 200;   // 200ms between attempts

    _pingGotReply.store(false);
    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = &_pingGotReply;
    cbs.on_ping_success = [](esp_ping_handle_t h, void* arg) { // NOSONAR(cpp:S5008) ESP-IDF ping API requires void*
        static_cast<std::atomic<bool>*>(arg)->store(true);
    };

    if (esp_ping_new_session(&cfg, &cbs, &_pingHandle) != ESP_OK) return false;
    esp_ping_start(_pingHandle);

    _pingInProgress = true;
    _pingStartTime = millis();
    return true;
}

void StepperNetworkManager::cleanupPing() {
    if (_pingHandle) {
        esp_ping_stop(_pingHandle);
        esp_ping_delete_session(_pingHandle);
        _pingHandle = nullptr;
    }
    _pingInProgress = false;
}

// ============================================================================
// HEALTH CHECK SUB-ROUTINES
// ============================================================================

void StepperNetworkManager::handleHealthyConnection() {
    if (_wdState != WatchdogState::WD_HEALTHY) {
        engine->info("✅ Watchdog: Connection fully restored (WiFi + gateway OK) after " +
                     String(_wdSoftRetries) + " soft + " + String(_wdHardRetries) + " hard retries");
        setupNTP();
    }
    _wdState  = WatchdogState::WD_HEALTHY;
    _wdSoftRetries = 0;
    _wdHardRetries = 0;
    _pingFailCount = 0;
    _wasConnected  = true;
    _cachedIP = WiFi.localIP().toString();
}

void StepperNetworkManager::handleRecoveryTier1() {
    _wdState = WatchdogState::WD_RECOVERING_SOFT;
    _wdSoftRetries++;
    engine->info("🔄 Watchdog [Tier 1]: Soft reconnect " + String(_wdSoftRetries) + "/" + String(WATCHDOG_SOFT_MAX_RETRIES));
    WiFi.reconnect();
}

bool StepperNetworkManager::handleRecoveryTier2() {
    _wdState = WatchdogState::WD_RECOVERING_HARD;
    _wdHardRetries++;
    engine->warn("🔧 Watchdog [Tier 2]: Hard reconnect " + String(_wdHardRetries) + "/" + String(WATCHDOG_HARD_MAX_RETRIES));

    WiFi.disconnect(true);
    delay(1000);

    String targetSSID;
    String targetPassword;
    if (WiFiConfig.isConfigured() && WiFiConfig.loadConfig(targetSSID, targetPassword)) {
        engine->info("🔑 Hard reconnect using NVS credentials");
    } else {
        targetSSID = ssid;
        targetPassword = password;
        engine->info("🔑 Hard reconnect using Config.h credentials");
    }

    WiFi.mode(ENABLE_PARALLEL_AP ? WIFI_AP_STA : WIFI_STA);
    WiFi.setHostname(otaHostname);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.begin(targetSSID.c_str(), targetPassword.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WATCHDOG_HARD_RECONNECT_TIMEOUT_MS) {
        delay(500);
    }

    if (WiFi.status() != WL_CONNECTED) {
        engine->error("❌ Watchdog [Tier 2]: Hard reconnect " + String(_wdHardRetries) + "/" + String(WATCHDOG_HARD_MAX_RETRIES) + " failed");
        return false;
    }

    _cachedIP = WiFi.localIP().toString();
    engine->info("✅ Watchdog [Tier 2]: Hard reconnect succeeded! IP: " + _cachedIP);

    MDNS.end();
    delay(50);
    setupMDNS();
    _lastMdnsRefresh = millis();
    setupNTP();

    if (ENABLE_PARALLEL_AP) {
        startParallelAP();
    }

    _wdState  = WatchdogState::WD_HEALTHY;
    _wdSoftRetries = 0;
    _wdHardRetries = 0;
    _pingFailCount = 0;
    _wasConnected  = true;
    return true;
}

void StepperNetworkManager::handleRecoveryTier3() {
    if (!WATCHDOG_AUTO_REBOOT_ENABLED) {
        engine->error("❌ Watchdog: All recovery exhausted. Auto-reboot DISABLED → cycling back to Tier 1");
        _wdSoftRetries = 0;
        _wdHardRetries = 0;
        return;
    }

    _wdState = WatchdogState::WD_REBOOTING;
    engine->error("🚨 Watchdog [Tier 3]: All recovery exhausted (" +
        String(WATCHDOG_SOFT_MAX_RETRIES) + " soft + " + String(WATCHDOG_HARD_MAX_RETRIES) +
        " hard). REBOOTING in " + String(WATCHDOG_REBOOT_DELAY_MS / 1000) + "s...");

    safeShutdown();
    delay(WATCHDOG_REBOOT_DELAY_MS);
    ESP.restart();
}

// ============================================================================
// CONNECTION HEALTH CHECK (orchestrator — non-blocking)
// Uses async ping: launches ping on one call, collects result on next calls.
// Never blocks more than a few ms, so server.handleClient() stays responsive.
// ============================================================================

void StepperNetworkManager::checkConnectionHealth() {
    if (_mode != NetworkMode::NET_STA_AP) return;

    unsigned long now = millis();

    // One-shot delayed mDNS re-announce after boot
    if (!_mdnsBootReannounced && now >= MDNS_BOOT_REANNOUNCE_DELAY_MS) {
        _mdnsBootReannounced = true;
        MDNS.end();
        delay(100);
        setupMDNS(true);
        _lastMdnsRefresh = now;
        engine->info("\xF0\x9F\x94\x84 mDNS: Delayed re-announce (boot +" + String(now / 1000) + "s)");
    }

    // ── ASYNC PING IN PROGRESS: check result without blocking ──
    if (_pingInProgress) {
        bool gotReply = _pingGotReply.load();
        bool timedOut = (now - _pingStartTime) >= PING_TIMEOUT_MS;

        if (!gotReply && !timedOut) {
            return;  // Ping still running — yield back to networkTask loop immediately
        }

        // Ping completed (success or timeout) — clean up session
        bool networkOk = gotReply;
        cleanupPing();

        if (!networkOk) {
            _pingFailCount++;
            if (_pingFailCount < WATCHDOG_PING_FAIL_THRESHOLD) {
                engine->debug("⚠️ Watchdog: Gateway ping failed (" + String(_pingFailCount) + "/" + String(WATCHDOG_PING_FAIL_THRESHOLD) + ") — retrying next cycle");
                _lastHealthCheck = now;
                return;
            }
        } else {
            _pingFailCount = 0;
        }

        // ── Proactive mDNS refresh ──
        if (now - _lastMdnsRefresh >= WATCHDOG_MDNS_REFRESH_MS) {
            MDNS.end();
            delay(50);
            setupMDNS(true);
            _lastMdnsRefresh = now;
            engine->debug("🔄 Watchdog: Proactive mDNS refresh");
        }

        if (networkOk) {
            handleHealthyConnection();
            _lastHealthCheck = now;
            return;
        }

        // WiFi says connected but gateway unreachable → half-dead
        engine->warn("⚠️ Watchdog: WiFi connected but gateway ping FAILED " + String(WATCHDOG_PING_FAIL_THRESHOLD) + "x → treating as disconnected");
        _pingFailCount = 0;
        // Fall through to recovery path below
        goto recovery;  // NOSONAR — cleaner than duplicating the recovery block
    }

    {
        // ── RATE LIMIT: check if it's time for a new health check ──
        uint32_t checkInterval;
        if (_wdState != WatchdogState::WD_HEALTHY || _pingFailCount > 0) {
            checkInterval = WATCHDOG_RECOVERY_INTERVAL_MS;
        } else {
            checkInterval = WATCHDOG_CHECK_INTERVAL_MS;
        }
        if (now - _lastHealthCheck < checkInterval) return;
        _lastHealthCheck = now;

        // ── STEP 1: WiFi L2 link ──
        bool wifiConnected = (WiFi.status() == WL_CONNECTED);

        if (wifiConnected) {
            // ── STEP 2: Launch async gateway ping (L3) — returns immediately ──
            if (startAsyncPing()) {
                return;  // Ping launched, will check result on next call (~10-50ms later)
            }
            // Failed to start ping (no gateway?) → treat as ping fail
            _pingFailCount++;
            if (_pingFailCount < WATCHDOG_PING_FAIL_THRESHOLD) {
                engine->debug("⚠️ Watchdog: Cannot ping gateway (" + String(_pingFailCount) + "/" + String(WATCHDOG_PING_FAIL_THRESHOLD) + ")");
                return;
            }
            // Fall through to recovery
        }
    }

recovery:
    // ── RECOVERY PATH ──
    if (_wasConnected && _wdState == WatchdogState::WD_HEALTHY) {
        engine->warn("⚠️ Watchdog: Connection lost! Starting 3-tier recovery...");
        if (ENABLE_PARALLEL_AP) {
            engine->info("📡 AP still active at " + WiFi.softAPIP().toString());
        }
    }
    _wasConnected = false;

    if (_wdSoftRetries < WATCHDOG_SOFT_MAX_RETRIES) {
        handleRecoveryTier1();
    } else if (_wdHardRetries < WATCHDOG_HARD_MAX_RETRIES) {
        handleRecoveryTier2();
    } else {
        handleRecoveryTier3();
    }
}

// ============================================================================
// CLIENT TIME SYNC (for AP_DIRECT mode without NTP)
// ============================================================================

void StepperNetworkManager::syncTimeFromClient(uint64_t epochMs) {
    if (_timeSynced && _mode == NetworkMode::NET_STA_AP) {
        return;  // NTP already synced in STA mode, ignore client time
    }

    struct timeval tv;
    tv.tv_sec = epochMs / 1000;
    tv.tv_usec = (epochMs % 1000) * 1000;
    settimeofday(&tv, nullptr);
    _timeSynced = true;

    engine->info("⏰ Time synced from client: " + TimeUtils::format("%Y-%m-%d %H:%M:%S", static_cast<time_t>(epochMs / 1000)));
}