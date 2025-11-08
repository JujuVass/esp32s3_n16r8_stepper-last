// ============================================================================
// CONFIG.H - System Configuration (WiFi, OTA, GPIO, Hardware)
// ============================================================================
// Central configuration file for all hardware and network settings
// Modify this file to adapt to different hardware setups
// ============================================================================

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// CONFIGURATION - WiFi
// ============================================================================
const char* ssid = "your_ssid";
const char* password = "REDACTED_WIFI_PASSWORD";

// ============================================================================
// CONFIGURATION - OTA (Over-The-Air Updates)
// ============================================================================
const char* otaHostname = "esp32s3";
const char* otaPassword = "REDACTED_OTA_PASSWORD";  // OTA password protection

// ============================================================================
// CONFIGURATION - GPIO Pins
// ============================================================================
// Motor driver pins (HSS86 stepper driver)
// Cable colors: Yellow=PULSE, Orange=DIR, Red=ENABLE
const int PIN_START_CONTACT = 47;  // Pull-up
const int PIN_END_CONTACT = 21;    // Pull-up
const int PIN_PULSE = 12;          // Cable jaune
const int PIN_DIR = 13;            // Cable orange
const int PIN_ENABLE = 14;         // Cable rouge

// ============================================================================
// CONFIGURATION - Motor Parameters
// ============================================================================
const int STEPS_PER_REV = 600;
const float MM_PER_REV = 100.0;  // HTD 5M belt, 20T pulley
const float STEPS_PER_MM = STEPS_PER_REV / MM_PER_REV;  // 6 steps/mm

// ============================================================================
// CONFIGURATION - Drift Correction (Safety Offset)
// ============================================================================
// Safety offset from physical contacts (matches calibration offset)
// Why 10? Position 0 is set 10 steps AFTER START contact release
// maxStep is set 10 steps BEFORE END contact
// This creates a buffer zone for drift tolerance
const int SAFETY_OFFSET_STEPS = 10;  // 10 steps = 1.67mm @ 6 steps/mm

// ============================================================================
// CONFIGURATION - Step Timing
// ============================================================================
const int STEP_PULSE_MICROS = 3;   // HSS86 requires min 2.5µs (used for HIGH + LOW)
const float STEP_EXECUTION_TIME_MICROS = STEP_PULSE_MICROS * 2.0;  // Total: 10µs per step
const int DIR_CHANGE_DELAY_MICROS = 30;  // HSS86 driver requires time to process direction changes

// ============================================================================
// CONFIGURATION - Calibration Constants
// ============================================================================
// WebSocket servicing during calibration
// Why 20? At 10µs/step (5µs HIGH + 5µs LOW), 20 steps = 200µs
// This ensures WebSocket keeps connection alive without interfering with timing
const int WEBSOCKET_SERVICE_INTERVAL_STEPS = 20;

const int CALIB_DELAY = 2000;  // 2ms per step = 500 steps/sec (safer for heavy loads)

// Safety limit for contact search
// Why 5000? At 6 steps/mm, 5000 steps = 833mm (well above physical max 221mm)
// Prevents infinite loop if contact sensor fails
const int CALIBRATION_MAX_STEPS = 3000;

// Speed reduction for precise positioning
// Why 3? Slows down by 3× (e.g., 5ms → 15ms per step)
// Reduces mechanical shock when contacting limit switches
const int CALIBRATION_SLOW_FACTOR = 3;

// Acceptable position error after full calibration cycle
// Why 5%? Allows ~10 steps drift on 200-step movement (realistic for belt drive)
// Higher tolerance needed due to HSS86 closed-loop microstepping variance
const float MAX_CALIBRATION_ERROR_PERCENT = 5.0;

// Maximum retry attempts before failure
// Why 3? Balances reliability (retry transient issues) vs speed (don't retry forever)
const int MAX_CALIBRATION_RETRIES = 3;

// Safety margin for return verification
// Why 1000? 1000 steps = 166mm @ 6 steps/mm
// If we move 166mm backward without hitting START contact, something is wrong
const int CALIBRATION_ERROR_MARGIN_STEPS = 1000;

// ============================================================================
// CONFIGURATION - Oscillation Mode Constants
// ============================================================================
// Phase tracking and position tolerances
const float OSC_INITIAL_POSITIONING_TOLERANCE_MM = 2.0;  // ±2mm considered "at center"
const float OSC_RAMP_START_DELAY_MS = 500.0;  // Wait 500ms before starting ramp-in

// Step timing for oscillation phases
const unsigned long OSC_POSITIONING_STEP_DELAY_MICROS = 2000;  // Slow initial positioning (25mm/s)
const unsigned long OSC_MIN_STEP_DELAY_MICROS = 50;  // Minimum delay for oscillation (ultra-high resolution: 33kHz)
const int OSC_MAX_STEPS_PER_CATCH_UP = 2;  // Max steps per loop iteration (anti-jerk)

// Sine wave lookup table (optional performance optimization)
#define USE_SINE_LOOKUP_TABLE true  // Enable pre-calculated sine table (saves ~13µs per call)
const int SINE_TABLE_SIZE = 1024;  // 1024 points = 0.1% precision, 4KB RAM

// Smooth transitions
const unsigned long OSC_FREQ_TRANSITION_DURATION_MS = 1000;  // Smooth 1000ms frequency interpolation
const unsigned long OSC_CENTER_TRANSITION_DURATION_MS = 1000;  // Smooth 1000ms center position interpolation
const unsigned long OSC_AMPLITUDE_TRANSITION_DURATION_MS = 1000;  // Smooth 1000ms amplitude interpolation
const float OSC_CATCH_UP_THRESHOLD_MM = 3.0;  // Only trigger catch-up if position error > 5mm (prevents continuous jerks)

// Debug logging intervals (reduce noise)
const unsigned long OSC_DEBUG_LOG_INTERVAL_MS = 5000;  // General debug logs every 5s (was 2s)
const unsigned long OSC_TRANSITION_LOG_INTERVAL_MS = 200;  // Transition logs every 200ms (was 100ms)

// ============================================================================
// CONFIGURATION - Chaos Mode
// ============================================================================
// Why 50000µs? Chaos mode: max step delay = 50ms = 20 steps/sec (minimum sane speed)
const unsigned long CHAOS_MAX_STEP_DELAY_MICROS = 50000;

// Why 500ms? Sequence status: update frequency during wait (balance responsiveness vs traffic)
const unsigned long SEQUENCE_STATUS_UPDATE_MS = 500;

// ============================================================================
// CONFIGURATION - doStep() Safety Thresholds
// ============================================================================

// Why 10? Consider "at start" if within 10 steps of minStep (for wasAtStart flag)
const long WASATSTART_THRESHOLD_STEPS = 10;

// Hard mechanical limits for safety
// Why 221mm? Physical constraint of HTD 5M belt system (20T pulley, limited travel)
// Prevents over-travel damage to mechanics
const float HARD_MAX_DISTANCE_MM = 220.0;

// Why 200mm? Minimum expected travel distance (detects mechanical issues early)
// If calibration finds less than 200mm, something is wrong (contact not working, obstruction, etc.)
const float HARD_MIN_DISTANCE_MM = 200.0;

// ============================================================================
// CONFIGURATION - Speed Limits MAXGLOSPE
// ============================================================================
const float MAX_SPEED_LEVEL = 35.0;  // Maximum speed level for all modes (1-30)

// Adaptive speed limiting for oscillation (uses MAX_SPEED_LEVEL for consistency)
// MAX_SPEED_LEVEL * 10.0 = max speed in mm/s (e.g., 30 * 10 = 300 mm/s)
const float OSC_MAX_SPEED_MM_S = MAX_SPEED_LEVEL * 20.0;  // Maximum oscillation speed (adaptive delay kicks in above this)

// ============================================================================
// CONFIGURATION - Loop Timing
// ============================================================================
const unsigned long WEBSERVICE_INTERVAL_US = 5000;    // Service WebSocket every 3ms
const unsigned long STATUS_UPDATE_INTERVAL_MS = 250;  // Send status every 80ms
const unsigned long SUMMARY_LOG_INTERVAL_MS = 60000;  // Print summary every 60s

// ============================================================================
// CONFIGURATION - Speed Compensation
// ============================================================================
// System overhead compensation factor (WebSocket, direction changes, debouncing)
// Why 1.20? Measured overhead is ~20% (1450ms actual vs 1200ms theoretical)
// Applied to step delay calculation to compensate for system delays
const float SPEED_COMPENSATION_FACTOR = 1.20;  // +20% faster to compensate overhead

// ============================================================================
// CONFIGURATION - Logging
// ============================================================================
#define LOG_BUFFER_SIZE 100  // Circular buffer size for async log writes

#endif // CONFIG_H
