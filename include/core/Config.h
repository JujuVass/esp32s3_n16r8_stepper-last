// ============================================================================
// CONFIG.H - System Configuration (WiFi, OTA, GPIO, Hardware)
// ============================================================================
// Central configuration file for all hardware and network settings
// Modify this file to adapt to different hardware setups
// ============================================================================

#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>  // For uint8_t, uint16_t, uint32_t

// ============================================================================
// CONFIGURATION - WiFi (extern declarations - defined in Config.cpp)
// ============================================================================
extern const char* ssid;
extern const char* password;

// ============================================================================
// CONFIGURATION - OTA (Over-The-Air Updates) (extern declarations)
// ============================================================================
extern const char* otaHostname;  // Also used for mDNS (http://esp32-stepper.local)
extern const char* otaPassword;  // OTA password protection

// ============================================================================
// CONFIGURATION - GPIO Pins
// ============================================================================
// Motor driver pins (HSS86 stepper driver)
// Cable colors: Yellow=PULSE, Orange=DIR, Red=ENABLE
constexpr int PIN_START_CONTACT = 4;   // MIN - opto sensor (yellow)
constexpr int PIN_END_CONTACT = 5;     // MAX - opto sensor (green)
constexpr int PIN_PULSE = 6;           // PU- via level shifter
constexpr int PIN_DIR = 7;             // DR- via level shifter
constexpr int PIN_ENABLE = 15;         // EN- via level shifter

// HSS86 feedback signals (directly connected, active states documented below)
// ALM (Alarm): LOW = alarm active (position error, over-current, overheat)
// PEND (Position End): HIGH = motor reached commanded position
constexpr int PIN_ALM = 17;            // ALM- via level shifter (input)
constexpr int PIN_PEND = 16;           // PED- via level shifter (input)

// AP Mode detection pin (GND = normal operation, floating/HIGH = AP_SETUP mode)
constexpr int PIN_AP_MODE = 19;        // GND = normal, floating = AP_SETUP

// Onboard RGB LED (WS2812 on Freenove ESP32-S3)
constexpr int PIN_NEOPIXEL = 48;

// ============================================================================
// CONFIGURATION - AP Direct Mode (Full app via WiFi AP)
// ============================================================================
// These settings apply when running in AP_DIRECT or STA+AP parallel mode
constexpr const char* AP_DIRECT_PASSWORD = "";      // Empty = open network, set for WPA2
constexpr int AP_DIRECT_CHANNEL = 1;                // WiFi channel (1-13, 1 recommended)
constexpr int AP_DIRECT_MAX_CLIENTS = 4;            // Max simultaneous AP clients (1-10)

// Parallel AP in STA mode: set to true to also run an AP alongside the router connection
// false = STA-only (better WiFi performance, no channel hopping)
// true  = STA+AP (device also reachable via 192.168.4.1, but may cause latency)
constexpr bool ENABLE_PARALLEL_AP = false;

// ============================================================================
// CONFIGURATION - Motor Parameters
// ============================================================================
constexpr int STEPS_PER_REV = 800;
constexpr float MM_PER_REV = 100.0f;  // HTD 5M belt, 20T pulley (5mm pitch × 20 teeth = 100mm)
constexpr float STEPS_PER_MM = STEPS_PER_REV / MM_PER_REV;  // 8.0 steps/mm

// ============================================================================
// CONFIGURATION - Drift Correction (Safety Offset)
// ============================================================================
// Safety offset from physical contacts (matches calibration offset)
// This creates a buffer zone for drift tolerance
constexpr int SAFETY_OFFSET_STEPS = 30;

// Hard drift detection zone (only test physical contacts when close to limits)
// Why 20mm? Balance between performance (88% less tests) and safety (~133 steps buffer)
// Reduces false positives and CPU overhead while maintaining excellent protection
constexpr float HARD_DRIFT_TEST_ZONE_MM = 20.0f;  // ~160 steps @ 8.0 steps/mm

// ============================================================================
// CONFIGURATION - Step Timing
// ============================================================================
constexpr int STEP_PULSE_MICROS = 3;         // HSS86 requires min 2.5µs → rounded up to 3µs
constexpr int STEP_EXECUTION_TIME_MICROS = STEP_PULSE_MICROS * 2;  // Total: 6µs per step (HIGH + LOW)
constexpr int DIR_CHANGE_DELAY_MICROS = 15;  // HSS86 driver requires time to process direction changes

// ============================================================================
// CONFIGURATION - Calibration Constants
// ============================================================================
// WebSocket servicing during calibration
// Why 20? At 10us/step (5us HIGH + 5us LOW), 20 steps = 200us
// This ensures WebSocket keeps connection alive without interfering with timing
constexpr int WEBSOCKET_SERVICE_INTERVAL_STEPS = 20;

constexpr int CALIB_DELAY = 2000;  // 2ms per step = 500 steps/sec (safer for heavy loads)

// Safety limit for contact search
// Why 3000? At 6.67 steps/mm, 3000 steps = 450mm (well above physical max ~200mm)
// Prevents infinite loop if contact sensor fails
constexpr int CALIBRATION_MAX_STEPS = 3000;

// Speed reduction for precise positioning
// Why 3? Slows down by 3x (e.g., 5ms -> 15ms per step)
// Reduces mechanical shock when contacting limit switches
constexpr int CALIBRATION_SLOW_FACTOR = 3;

// Acceptable position error after full calibration cycle
// Why 5%? Allows ~10 steps drift on 200-step movement (realistic for belt drive)
// Higher tolerance needed due to HSS86 closed-loop microstepping variance
constexpr float MAX_CALIBRATION_ERROR_PERCENT = 5.0f;

// Maximum retry attempts before failure
// Why 3? Balances reliability (retry transient issues) vs speed (don't retry forever)
constexpr int MAX_CALIBRATION_RETRIES = 3;

// Safety margin for return verification
// Why 1000? 1000 steps = 150mm @ 6.67 steps/mm
// If we move 150mm backward without hitting START contact, something is wrong
constexpr int CALIBRATION_ERROR_MARGIN_STEPS = 1000;

// ============================================================================
// CONFIGURATION - Oscillation Mode Constants
// ============================================================================
// Phase tracking and position tolerances
constexpr float OSC_INITIAL_POSITIONING_TOLERANCE_MM = 2.0f;  // ±2mm considered "at center"
constexpr float OSC_RAMP_START_DELAY_MS = 500.0f;  // Wait 500ms before starting ramp-in

// Step timing for oscillation phases
constexpr unsigned long OSC_POSITIONING_STEP_DELAY_MICROS = 2000;  // Slow initial positioning (25mm/s)
constexpr unsigned long OSC_MIN_STEP_DELAY_MICROS = 50;  // Minimum delay for oscillation (ultra-high resolution: 33kHz)
constexpr int OSC_MAX_STEPS_PER_CATCH_UP = 2;  // Max steps per loop iteration (anti-jerk)

// Sine wave lookup table (optional performance optimization)
#define USE_SINE_LOOKUP_TABLE  // Enable pre-calculated sine table (saves ~13us per call)
constexpr int SINE_TABLE_SIZE = 1024;  // 1024 points = 0.1% precision, 4KB RAM

// Smooth transitions
constexpr unsigned long OSC_FREQ_TRANSITION_DURATION_MS = 1000;  // Smooth 1000ms frequency interpolation
constexpr unsigned long OSC_CENTER_TRANSITION_DURATION_MS = 1000;  // Smooth 1000ms center position interpolation
constexpr unsigned long OSC_AMPLITUDE_TRANSITION_DURATION_MS = 1000;  // Smooth 1000ms amplitude interpolation
constexpr float OSC_CATCH_UP_THRESHOLD_MM = 3.0f;  // Only trigger catch-up if position error > 3mm (prevents continuous jerks)

// Debug logging intervals (reduce noise)
constexpr unsigned long OSC_DEBUG_LOG_INTERVAL_MS = 5000;  // General debug logs every 5s (was 2s)
constexpr unsigned long OSC_TRANSITION_LOG_INTERVAL_MS = 200;  // Transition logs every 200ms (was 100ms)

// ============================================================================
// CONFIGURATION - Chaos Mode
// ============================================================================
// Why 50000us? Chaos mode: max step delay = 50ms = 20 steps/sec (minimum sane speed)
constexpr unsigned long CHAOS_MAX_STEP_DELAY_MICROS = 50000;

// Positioning speed for blocking moves (sequence repositioning, chaos center move)
// Why 990µs? Corresponds to speed level 5.0 (~126 mm/s) — safe for all belt loads
constexpr unsigned long POSITIONING_STEP_DELAY_MICROS = 990;

// Why 500ms? Sequence status: update frequency during wait (balance responsiveness vs traffic)
constexpr unsigned long SEQUENCE_STATUS_UPDATE_MS = 500;

// Blocking move helper timings (used by SequenceExecutor::blockingMoveToStep)
constexpr unsigned long BLOCKING_MOVE_WS_SERVICE_MS = 10;      // WebSocket service interval during blocking moves
constexpr unsigned long BLOCKING_MOVE_STATUS_INTERVAL_MS = 250; // Status broadcast interval during blocking moves

// Minimum pattern duration before allowing early pattern change in Chaos mode
constexpr unsigned long CHAOS_MIN_PATTERN_DURATION_MS = 150;

// LED blink interval in AP_SETUP mode
constexpr unsigned long AP_LED_BLINK_INTERVAL_MS = 500;

// ============================================================================
// CONFIGURATION - doStep() Safety Thresholds
// ============================================================================

// Why 10? Consider "at start" if within 10 steps of minStep (for wasAtStart flag)
constexpr long WASATSTART_THRESHOLD_STEPS = 10;

// Hard mechanical limits for safety
constexpr float HARD_MAX_DISTANCE_MM = 365.0f;
constexpr float HARD_MIN_DISTANCE_MM = 250.0f;

// ============================================================================
// CONFIGURATION - Speed Limits
// ============================================================================
constexpr float MAX_SPEED_LEVEL = 35.0f;  // Maximum speed level for all modes (1-35)

// Adaptive speed limiting for oscillation (uses MAX_SPEED_LEVEL for consistency)
// MAX_SPEED_LEVEL * 20.0 = max speed in mm/s (e.g., 35 * 20 = 700 mm/s)
constexpr float OSC_MAX_SPEED_MM_S = MAX_SPEED_LEVEL * 20.0f;  // Maximum oscillation speed (adaptive delay kicks in above this)

// ============================================================================
// CONFIGURATION - Loop Timing
// ============================================================================
constexpr unsigned long WEBSERVICE_INTERVAL_US = 3000;    // Service WebSocket every 3ms
constexpr unsigned long STATUS_UPDATE_INTERVAL_MS = 100;  // Default status interval (active movement)
constexpr unsigned long STATUS_PURSUIT_INTERVAL_MS = 50;  // Fast rate during pursuit mode (20 Hz)
constexpr unsigned long STATUS_IDLE_INTERVAL_MS = 1000;   // Reduced rate when idle (READY/INIT/ERROR)
constexpr unsigned long STATUS_CALIB_INTERVAL_MS = 200;   // Moderate rate during calibration
constexpr unsigned long STATUS_UPLOAD_INTERVAL_MS = 2000; // Very slow rate during file upload (0.5 Hz)
constexpr unsigned long UPLOAD_ACTIVITY_TIMEOUT_MS = 5000; // Upload considered done 5s after last activity
constexpr unsigned long UPLOAD_POST_CLOSE_DELAY_MS = 50;   // Delay after file.close() to let LittleFS settle
constexpr unsigned long SUMMARY_LOG_INTERVAL_MS = 30000;  // Print summary every 30s

// ============================================================================
// CONFIGURATION - Speed Compensation
// ============================================================================
// System overhead compensation factor (WebSocket, direction changes, debouncing)
// Applied to step delay calculation to compensate for system delays
// Set to 1.0 = no compensation (disabled). Set to 1.20 for +20% compensation if needed.
constexpr float SPEED_COMPENSATION_FACTOR = 1.0f;  // 1.0 = disabled (no compensation applied)

// ============================================================================
// CONFIGURATION - HSS86 Feedback Monitoring
// ============================================================================
// PEND (Position End) lag threshold - warn if motor hasn't reached position
// Why 100ms? At 6.67 steps/mm and typical speeds, >100ms lag indicates load/resistance
constexpr unsigned long PEND_LAG_WARN_THRESHOLD_MS = 100;

// PEND warning cooldown - don't spam logs
constexpr unsigned long PEND_WARN_COOLDOWN_MS = 5000;  // Max 1 warning per 5 seconds

// ============================================================================
// CONFIGURATION - Logging & Performance Monitoring
// ============================================================================
constexpr int LOG_BUFFER_SIZE = 100;  // Circular buffer size for async log writes

// Slow broadcast threshold for performance monitoring (microseconds)
constexpr unsigned long BROADCAST_SLOW_THRESHOLD_US = 200000;  // 200ms

// Stack high-water mark monitoring interval
// Why 60s? Provides early warning of stack pressure without log spam.
// Reports the minimum free stack bytes ever seen for each FreeRTOS task.
// If HWM drops below ~500 bytes, increase the task's stack allocation.
constexpr unsigned long STACK_HWM_LOG_INTERVAL_MS = 240000;  // 240s between stack checks

// ============================================================================
// CONFIGURATION - System Timing Intervals
// ============================================================================
// Status broadcasting and debouncing intervals
constexpr uint32_t CONTACT_DEBOUNCE_MS = 50;            // Physical contact debounce time
constexpr uint32_t OTA_CHECK_INTERVAL_MS = 1000;        // OTA update check frequency
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;   // WiFi reconnection delay
constexpr uint32_t STATS_SAVE_INTERVAL_MS = 60000;      // Auto-save stats to filesystem
constexpr uint32_t WEBSOCKET_RECONNECT_MS = 2000;       // Client-side WS reconnect delay

// NTP timezone configuration
constexpr long     NTP_GMT_OFFSET_SEC      = 0;      // GMT+0 (UTC). Change to your timezone offset in seconds
constexpr int      NTP_DAYLIGHT_OFFSET_SEC = 0;         // DST offset (3600 for CEST summer time, 0 to disable)

// ============================================================================
// CONFIGURATION - Connection Watchdog (STA mode auto-recovery)
// ============================================================================
// Three-tier escalation: Soft reconnect → Hard re-association → Emergency reboot
constexpr uint32_t WATCHDOG_CHECK_INTERVAL_MS          = 60000;  // Deep health check when healthy (60s)
constexpr uint32_t WATCHDOG_RECOVERY_INTERVAL_MS       = 20000;  // Faster checks during active recovery (20s)
constexpr uint8_t  WATCHDOG_PING_FAIL_THRESHOLD        = 3;      // Consecutive ping failures before escalating
constexpr uint8_t  WATCHDOG_SOFT_MAX_RETRIES           = 5;      // Tier 1: WiFi.reconnect() attempts
constexpr uint8_t  WATCHDOG_HARD_MAX_RETRIES           = 3;      // Tier 2: Full disconnect + re-associate attempts
constexpr uint32_t WATCHDOG_HARD_RECONNECT_TIMEOUT_MS  = 15000;  // Timeout for WiFi.begin() during hard reconnect
constexpr uint32_t WATCHDOG_REBOOT_DELAY_MS            = 2000;   // Safety delay before ESP.restart()
constexpr uint32_t WATCHDOG_MDNS_REFRESH_MS            = 60000;  // Proactive mDNS re-announce interval (60s)
constexpr uint32_t MDNS_BOOT_REANNOUNCE_DELAY_MS       = 5000;   // Delayed re-announce after boot (IGMP settle)
constexpr bool     WATCHDOG_AUTO_REBOOT_ENABLED        = true;   // Master switch for Tier 3 auto-reboot

// ============================================================================
// CONFIGURATION - Motion Limits
// ============================================================================
constexpr float MAX_SPEED_MM_PER_SEC = 300.0f;          // Absolute max motor speed
constexpr float MIN_STEP_INTERVAL_US = 20.0f;           // Minimum step pulse interval
constexpr uint8_t DEFAULT_SPEED_LEVEL = 5;              // Default speed on startup
constexpr float SPEED_LEVEL_TO_MM_S = 10.0f;            // speedLevel * 10 = mm/s

// ============================================================================
// CONFIGURATION - Chaos Mode Defaults
// ============================================================================
// CHAOS_PATTERN_COUNT is defined in Types.h (used by structs there)
constexpr float CHAOS_DEFAULT_CENTER_MM = 100.0f;       // Default center position
constexpr float CHAOS_DEFAULT_AMPLITUDE_MM = 40.0f;     // Default amplitude
constexpr float CHAOS_MIN_AMPLITUDE_MM = 5.0f;          // Minimum safe amplitude
constexpr float CHAOS_MAX_AMPLITUDE_MM = 200.0f;        // Maximum amplitude
constexpr uint8_t CHAOS_DEFAULT_SPEED_LEVEL = 15;       // Default chaos speed
constexpr uint8_t CHAOS_DEFAULT_CRAZINESS = 50;         // Default craziness %
constexpr uint16_t CHAOS_DEFAULT_DURATION_SEC = 30;     // Default duration

// ============================================================================
// CONFIGURATION - Sequencer Limits
// ============================================================================
constexpr uint8_t MAX_SEQUENCE_LINES = 20;              // Max lines in sequence
constexpr uint16_t MAX_CYCLES_PER_LINE = 9999;          // Max cycles per sequence line
constexpr uint32_t MAX_PAUSE_AFTER_MS = 60000;          // Max pause between lines (60s)
constexpr uint8_t MAX_PLAYLISTS_PER_MODE = 20;          // Max saved presets per mode

// ============================================================================
// CONFIGURATION - Deceleration Zone Defaults
// ============================================================================
constexpr float DECEL_DEFAULT_ZONE_MM = 20.0f;          // Default decel zone size
constexpr uint8_t DECEL_DEFAULT_EFFECT_PERCENT = 50;    // Default decel effect
// Decel mode constants removed — use SpeedCurve::CURVE_LINEAR/SpeedCurve::CURVE_SINE/etc. from Types.h

// ============================================================================
// CONFIGURATION - Cycle Pause Defaults
// ============================================================================
constexpr float CYCLE_PAUSE_DEFAULT_SEC = 0.0f;         // Default pause duration
constexpr float CYCLE_PAUSE_MIN_SEC = 0.1f;             // Minimum pause duration
constexpr float CYCLE_PAUSE_MAX_SEC = 30.0f;            // Maximum pause duration
constexpr float CYCLE_PAUSE_RANDOM_MIN_DEFAULT = 0.5f;  // Default random min
constexpr float CYCLE_PAUSE_RANDOM_MAX_DEFAULT = 3.0f;  // Default random max

// ============================================================================
// CONFIGURATION - Oscillation Defaults
// ============================================================================
constexpr float OSC_DEFAULT_CENTER_MM = 100.0f;         // Default center position
constexpr float OSC_DEFAULT_AMPLITUDE_MM = 20.0f;       // Default amplitude
constexpr float OSC_DEFAULT_FREQUENCY_HZ = 1.0f;        // Default frequency
constexpr uint8_t OSC_DEFAULT_WAVEFORM = 0;             // 0=Sine, 1=Triangle, 2=Square
constexpr uint16_t OSC_DEFAULT_CYCLE_COUNT = 10;        // Default cycle count
constexpr uint16_t OSC_DEFAULT_RAMP_DURATION_MS = 2000; // Default ramp duration

// ============================================================================
// CONFIGURATION - UI/UX Constants
// ============================================================================
constexpr uint16_t NOTIFICATION_DEFAULT_DURATION_MS = 5000;  // Default notification time
constexpr uint16_t NOTIFICATION_SUCCESS_DURATION_MS = 3000;  // Success notification time
constexpr uint16_t NOTIFICATION_ERROR_DURATION_MS = 6000;    // Error notification time

#endif // CONFIG_H
