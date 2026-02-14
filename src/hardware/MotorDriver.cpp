// ============================================================================
// MOTOR_DRIVER.CPP - HSS86 Stepper Motor Driver Implementation
// ============================================================================

#include "hardware/MotorDriver.h"
#include "core/UtilityEngine.h"
#include "core/GlobalState.h"  // For sensorsInverted

// ============================================================================
// ISR FOR PEND SIGNAL (counts all transitions for debugging)
// ============================================================================
static volatile unsigned long pendInterruptCount = 0;

void IRAM_ATTR pendISR() {
    pendInterruptCount++;
}

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

MotorDriver& MotorDriver::getInstance() {
    static MotorDriver instance;
    return instance;
}

// Global accessor
MotorDriver& Motor = MotorDriver::getInstance();

// ============================================================================
// INITIALIZATION
// ============================================================================

void MotorDriver::init() {
    if (m_initialized) return;  // Prevent double initialization
    
    // Configure GPIO pins as outputs
    pinMode(PIN_PULSE, OUTPUT);
    pinMode(PIN_DIR, OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    
    // Configure HSS86 feedback pins as inputs with pull-up
    // ALM: normally HIGH, goes LOW on alarm
    // PEND: HIGH when position reached
    pinMode(PIN_ALM, INPUT_PULLUP);
    pinMode(PIN_PEND, INPUT_PULLUP);
    
    // Attach ISR on PEND for debugging (detect ANY change)
    attachInterrupt(digitalPinToInterrupt(PIN_PEND), pendISR, CHANGE);
    
    // Set initial state for PULSE pin (no wrapper method needed)
    digitalWrite(PIN_PULSE, LOW);    // Pulse idle LOW
    
    // Initialize state tracking BEFORE calling methods
    m_enabled = true;    // Set to true so disable() will execute
    m_direction = false; // Set to false so setDirection(true) will execute
    m_lastPendHighMs = millis();
    m_lastPendState = true;
    m_initialized = true;  // Mark initialized so methods work
    
    // Use proper methods for ENABLE and DIR (respects sensorsInverted)
    disable();            // Safely disable motor
    setDirection(true);   // Forward - will apply inversion if needed
    
    engine->info("✅ MotorDriver initialized (ALM=GPIO" + String(PIN_ALM) + ", PEND=GPIO" + String(PIN_PEND) + " with ISR)");
}

// ============================================================================
// STEP EXECUTION
// ============================================================================

void MotorDriver::step() {
    // HSS86 requires minimum 2.5µs pulse width → using 3µs (STEP_PULSE_MICROS)
    digitalWrite(PIN_PULSE, HIGH);
    delayMicroseconds(STEP_PULSE_MICROS);
    digitalWrite(PIN_PULSE, LOW);
    delayMicroseconds(STEP_PULSE_MICROS);
}

// ============================================================================
// DIRECTION CONTROL
// ============================================================================

void MotorDriver::setDirection(bool forward) {
    // Optimization: skip if logical direction unchanged
    if (forward == m_direction) return;
    
    // Apply sensors inversion: if inverted, flip the physical direction
    bool physicalForward = sensorsInverted ? !forward : forward;
    
    // Update GPIO with physical direction
    digitalWrite(PIN_DIR, physicalForward ? HIGH : LOW);
    
    // HSS86 needs time to process direction change before next step
    delayMicroseconds(DIR_CHANGE_DELAY_MICROS);
    
    m_direction = forward;  // Store logical direction
}

bool MotorDriver::getDirection() const {
    return m_direction;
}

// ============================================================================
// ENABLE/DISABLE CONTROL
// ============================================================================

void MotorDriver::enable() {
    if (m_enabled) return;  // Already enabled
    
    // HSS86 ENABLE is active LOW, BSS138 level shifter inverts:
    // MCU HIGH → Driver LOW (enabled)
    digitalWrite(PIN_ENABLE, HIGH);
    m_enabled = true;
}

void MotorDriver::disable() {
    if (!m_enabled) return;  // Already disabled
    
    // HSS86 ENABLE is active LOW, BSS138 level shifter inverts:
    // MCU LOW → Driver HIGH (disabled)
    digitalWrite(PIN_ENABLE, LOW);
    m_enabled = false;
}

bool MotorDriver::isEnabled() const {
    return m_enabled;
}

// ============================================================================
// HSS86 FEEDBACK SIGNALS (ALM & PEND)
// ============================================================================

bool MotorDriver::isAlarmActive() {
    // Read current state
    bool currentAlarm = (digitalRead(PIN_ALM) == LOW);
    
    // Debounce: ALM must be stable for ALM_DEBOUNCE_MS to trigger
    if (currentAlarm != m_lastAlarmState) {
        m_alarmChangeMs = millis();
        m_lastAlarmState = currentAlarm;
    }
    
    // Only report alarm if stable for debounce period
    if (currentAlarm && (millis() - m_alarmChangeMs >= ALM_DEBOUNCE_MS)) {
        return true;
    }
    
    return false;
}

bool MotorDriver::isPositionReached() const {
    return digitalRead(PIN_PEND) == HIGH;  // Enable when wiring confirmed
}

void MotorDriver::updatePendTracking() {
    bool currentPend = isPositionReached();
    
    if (currentPend && !m_lastPendState) {
        // PEND just went HIGH - motor reached position
        m_lastPendHighMs = millis();
    }
    
    m_lastPendState = currentPend;
}

void MotorDriver::resetPendTracking() {
    m_lastPendHighMs = millis();
    m_lastPendState = isPositionReached();
}

unsigned long MotorDriver::getPendInterruptCount() const {
    return pendInterruptCount;
}
