// ============================================================================
// MOTOR_DRIVER.CPP - HSS86 Stepper Motor Driver Implementation
// ============================================================================

#include "hardware/MotorDriver.h"
#include "core/UtilityEngine.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

MotorDriver& MotorDriver::getInstance() {
    static MotorDriver instance;
    return instance;
}

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
    
    // Set initial state: disabled, forward direction
    digitalWrite(PIN_ENABLE, HIGH);  // Disable (active LOW)
    digitalWrite(PIN_DIR, HIGH);     // Forward direction
    digitalWrite(PIN_PULSE, LOW);    // Pulse idle LOW
    
    m_enabled = false;
    m_direction = true;  // Forward
    m_lastPendHighMs = millis();
    m_lastPendState = true;
    m_initialized = true;
    
    engine->info("✅ MotorDriver initialized (ALM=GPIO" + String(PIN_ALM) + ", PEND=GPIO" + String(PIN_PEND) + ")");
}

// ============================================================================
// STEP EXECUTION
// ============================================================================

void MotorDriver::step() {
    // HSS86 requires minimum 2.5µs pulse width
    // We use STEP_PULSE_MICROS (3µs) for safety margin
    // Total step time: ~6µs (HIGH phase + LOW phase)
    
    digitalWrite(PIN_PULSE, HIGH);
    delayMicroseconds(STEP_PULSE_MICROS);
    digitalWrite(PIN_PULSE, LOW);
    delayMicroseconds(STEP_PULSE_MICROS);
}

// ============================================================================
// DIRECTION CONTROL
// ============================================================================

void MotorDriver::setDirection(bool forward) {
    // Optimization: only change if direction is different
    // This avoids unnecessary delay when direction hasn't changed
    if (forward == m_direction) return;
    
    // Update GPIO
    digitalWrite(PIN_DIR, forward ? HIGH : LOW);
    
    // HSS86 needs time to process direction change before next step
    delayMicroseconds(DIR_CHANGE_DELAY_MICROS);
    
    m_direction = forward;
}

bool MotorDriver::getDirection() const {
    return m_direction;
}

// ============================================================================
// ENABLE/DISABLE CONTROL
// ============================================================================

void MotorDriver::enable() {
    if (m_enabled) return;  // Already enabled
    
    // HSS86 ENABLE is active LOW
    digitalWrite(PIN_ENABLE, LOW);
    m_enabled = true;
}

void MotorDriver::disable() {
    if (!m_enabled) return;  // Already disabled
    
    // HSS86 ENABLE is active LOW, set HIGH to disable
    digitalWrite(PIN_ENABLE, HIGH);
    m_enabled = false;
}

bool MotorDriver::isEnabled() const {
    return m_enabled;
}

// ============================================================================
// HSS86 FEEDBACK SIGNALS (ALM & PEND)
// ============================================================================

bool MotorDriver::isAlarmActive() const {
    // ALM is active LOW (LOW = alarm, HIGH = OK)
    return digitalRead(PIN_ALM) == LOW;
}

bool MotorDriver::isPositionReached() const {
    // PEND is active HIGH (HIGH = position reached)
    return digitalRead(PIN_PEND) == HIGH;
}

unsigned long MotorDriver::getPositionLagMs() const {
    if (isPositionReached()) {
        return 0;  // Currently at position, no lag
    }
    return millis() - m_lastPendHighMs;
}

void MotorDriver::updatePendTracking() {
    bool currentPend = isPositionReached();
    
    if (currentPend && !m_lastPendState) {
        // PEND just went HIGH - motor reached position
        m_lastPendHighMs = millis();
    }
    
    m_lastPendState = currentPend;
}
