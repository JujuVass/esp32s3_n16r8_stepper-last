// ============================================================================
// CALIBRATION_MANAGER.CPP - Stepper Motor Calibration Controller
// ============================================================================
// Implementation of calibration logic
// ============================================================================

#include "movement/CalibrationManager.h"
#include "core/GlobalState.h"
#include "core/MovementMath.h"
#include "core/UtilityEngine.h"

extern UtilityEngine* engine;

// External WebSocket/Server (for servicing during long operations)
#include <WebSocketsServer.h>
#include <WebServer.h>

using enum SystemState;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

CalibrationManager& CalibrationManager::getInstance() {
    static CalibrationManager instance;
    return instance;
}

// Global accessor
CalibrationManager& Calibration = CalibrationManager::getInstance();

// ============================================================================
// INITIALIZATION
// ============================================================================

void CalibrationManager::init(WebSocketsServer* ws, WebServer* server) {
    m_webSocket = ws;
    m_server = server;
    m_initialized = true;
    engine->debug("CalibrationManager initialized");
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void CalibrationManager::serviceWebSocket(unsigned long durationMs) {
    if (!m_webSocket || !m_server) return;
    
    unsigned long start = millis();
    while (millis() - start < durationMs) {
        if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            m_webSocket->loop();
            m_server->handleClient();
            xSemaphoreGive(wsMutex);
        }
        yield();
        delay(1);
    }
}

bool CalibrationManager::handleFailure() {
    Motor.disable();
    config.currentState = STATE_ERROR;
    
    m_attemptCount++;
    if (m_attemptCount >= MAX_CALIBRATION_RETRIES) {
        if (m_errorCallback) {
            m_errorCallback("❌ ERROR: Calibration failed after " + String(MAX_CALIBRATION_RETRIES) + " attempts");
        }
        m_attemptCount = 0;
    }
    
    return false;
}

// ============================================================================
// CONTACT DETECTION
// ============================================================================
// OPTO LOGIC: HIGH = blocked/active, LOW = clear/inactive

bool CalibrationManager::findContact(bool moveForward, uint8_t contactPin, const char* contactName) {
    Motor.setDirection(moveForward);
    unsigned long stepCount = 0;
    
    // Check if already on the contact (opto HIGH = blocked = already triggered)
    if (Contacts.isActive(contactPin)) {
        engine->info(String("⚠️ Already on ") + contactName + " opto - backing off first...");
        
        // Back off in opposite direction until opto clears (goes LOW)
        Motor.setDirection(!moveForward);
        int backoffSteps = 0;
        while (Contacts.isActive(contactPin) && backoffSteps < SAFETY_OFFSET_STEPS * 2) {
            Motor.step();
            backoffSteps++;
            delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
        }
        
        if (backoffSteps >= SAFETY_OFFSET_STEPS * 2) {
            engine->error("❌ Cannot clear " + String(contactName) + " opto!");
            return false;
        }
        
        engine->info("✓ Cleared opto after " + String(backoffSteps) + " steps, continuing calibration...");
        Motor.setDirection(moveForward);
        serviceWebSocket(100);
    }
    
    // Search for contact: move while opto is LOW (clear), stop when HIGH (blocked)
    while (Contacts.isClear(contactPin)) {
        Motor.step();
        currentStep += moveForward ? 1 : -1;
        delayMicroseconds(CALIB_DELAY);
        stepCount++;
        
        // Service WebSocket periodically
        if (stepCount % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
            yield();
            if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (m_webSocket) m_webSocket->loop();
                if (m_server) m_server->handleClient();
                xSemaphoreGive(wsMutex);
            }
        }
        
        // Timeout protection
        if (stepCount > CALIBRATION_MAX_STEPS) {
            String errorMsg = "❌ ERROR: Contact ";
            errorMsg += contactName;
            errorMsg += " not found";
            if (m_errorCallback) m_errorCallback(errorMsg);
            Motor.disable();
            config.currentState = STATE_ERROR;
            return false;
        }
    }
    
    // Validate END contact distance (sanity check - not a retry loop)
    if (contactPin == PIN_END_CONTACT) {
        long detectedSteps = abs(currentStep);
        long minExpectedSteps = MovementMath::mmToSteps(HARD_MIN_DISTANCE_MM);
        
        if (detectedSteps < minExpectedSteps) {
            engine->error("❌ Opto END detected too early (" + 
                         String(detectedSteps) + " < " + String(minExpectedSteps) + " steps)");
            engine->error("→ Check wiring or opto sensor positions");
            return false;  // Fail without infinite retry
        }
    }
    
    return true;
}

void CalibrationManager::releaseContact(uint8_t contactPin, bool moveForward) {
    Motor.setDirection(moveForward);
    
    // Move slowly until opto clears (HIGH->LOW transition)
    // Timeout: if we can't release within SAFETY_OFFSET_STEPS*4 steps, sensor is stuck
    int releaseSteps = 0;
    const int MAX_RELEASE_STEPS = SAFETY_OFFSET_STEPS * 4;
    while (Contacts.isActive(contactPin) && releaseSteps < MAX_RELEASE_STEPS) {
        Motor.step();
        if (!moveForward) currentStep = currentStep - 1;
        else currentStep = currentStep + 1;
        releaseSteps++;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);
    }
    
    if (releaseSteps >= MAX_RELEASE_STEPS) {
        engine->error("❌ Cannot release contact - sensor stuck after " + String(releaseSteps) + " steps");
    }
    
    // Add safety margin
    for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
        Motor.step();
        if (!moveForward) currentStep = currentStep - 1;
        else currentStep = currentStep + 1;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
    }
    
    // Settling time for opto sensor stabilization
    delay(10);
}

float CalibrationManager::validateAccuracy() {
    long stepDifference = abs(currentStep);
    float differencePercent = (m_maxStep > 0) ? 
        ((float)stepDifference / (float)m_maxStep) * 100.0f : 0.0f;
    
    if (stepDifference == 0) {
        engine->debug("✓ Return accuracy: PERFECT!");
    } else {
        engine->warn("⚠️ Return accuracy: " + String(stepDifference) + " steps (" +
                    String(differencePercent, 1) + "%)");
    }
    
    m_lastErrorPercent = differencePercent;
    return differencePercent;
}

// ============================================================================
// PUBLIC METHODS
// ============================================================================

bool CalibrationManager::startCalibration() {
    if (!m_initialized) {
        engine->error("CalibrationManager not initialized!");
        return false;
    }
    
    // Loop for retries (replaces recursive calls to avoid stack overflow on Core 1)
    while (true) {
    
    engine->info(m_attemptCount == 0 ? "Starting calibration..." : "Retry calibration...");
    config.currentState = STATE_CALIBRATING;
    
    // Send status updates
    if (m_statusCallback) {
        for (int i = 0; i < 5; i++) {
            m_statusCallback();
            serviceWebSocket(20);
        }
    }
    
    Motor.enable();
    serviceWebSocket(200);  // Settling time
    
    // ========================================
    // Step 1: Find START contact
    // ========================================
    if (!findContact(false, PIN_START_CONTACT, "START")) {
        return handleFailure();
    }
    
    engine->debug("✓ Start contact found - releasing slowly...");
    releaseContact(PIN_START_CONTACT, true);  // Move forward to release
    
    // THIS position = new logical zero
    config.minStep = 0;
    currentStep = 0;
    engine->debug("✓ Position 0 set");
    
    // ========================================
    // Step 2: Find END contact
    // ========================================
    if (!findContact(true, PIN_END_CONTACT, "END")) {
        return handleFailure();
    }
    
    engine->debug("✓ End contact found - releasing slowly...");
    releaseContact(PIN_END_CONTACT, false);  // Move backward to release
    
    // This position = maxStep
    m_maxStep = currentStep;
    m_totalDistanceMM = MovementMath::stepsToMM(m_maxStep);
    config.maxStep = m_maxStep;
    config.totalDistanceMM = m_totalDistanceMM;
    
    // Validate distance
    if (m_totalDistanceMM < HARD_MIN_DISTANCE_MM) {
        m_attemptCount++;
        if (m_attemptCount >= MAX_CALIBRATION_RETRIES) {
            if (m_errorCallback) {
                m_errorCallback("❌ Calibrated distance too short (" + 
                              String(m_totalDistanceMM, 1) + " mm)");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            m_attemptCount = 0;
            return false;
        }
        
        engine->warn("⚠️ Distance too short - Retry " + String(m_attemptCount));
        serviceWebSocket(500);
        continue;  // Retry via loop (was recursive call)
    }
    
    if (m_totalDistanceMM > HARD_MAX_DISTANCE_MM) {
        engine->warn("⚠️ Distance limited to " + String(HARD_MAX_DISTANCE_MM, 1) + " mm");
        m_totalDistanceMM = HARD_MAX_DISTANCE_MM;
        m_maxStep = MovementMath::mmToSteps(m_totalDistanceMM);
        config.maxStep = m_maxStep;
        config.totalDistanceMM = m_totalDistanceMM;
    }
    
    engine->debug("✓ Total distance: " + String(m_totalDistanceMM, 1) + " mm");
    
    // ========================================
    // Step 3: Return to START
    // ========================================
    if (!returnToStart()) {
        return handleFailure();
    }
    
    // ========================================
    // Step 4: Validate accuracy
    // ========================================
    float errorPercent = validateAccuracy();
    
    if (errorPercent > MAX_CALIBRATION_ERROR_PERCENT) {
        m_attemptCount++;
        if (m_attemptCount >= MAX_CALIBRATION_RETRIES) {
            if (m_errorCallback) {
                m_errorCallback("❌ Calibration failed - error too large");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            m_attemptCount = 0;
            return false;
        }
        
        engine->warn("⚠️ Error too large - Retry");
        serviceWebSocket(500);
        continue;  // Retry via loop (was recursive call)
    }
    
    // ========================================
    // SUCCESS
    // ========================================
    currentStep = 0;
    config.minStep = 0;
    
    // ========================================
    // Step 5: Position at 10% of total distance
    // (rounded up to nearest mm)
    // ========================================
    float tenPercentMM = ceil(m_totalDistanceMM * 0.1f);  // 10% rounded up
    long targetSteps = MovementMath::mmToSteps(tenPercentMM);
    
    engine->info("📍 Positioning at 10% (" + String(tenPercentMM, 0) + " mm)...");
    
    Motor.setDirection(true);  // Forward
    while (currentStep < targetSteps) {
        Motor.step();
        currentStep = currentStep + 1;
        delayMicroseconds(CALIB_DELAY);
        
        // Service WebSocket periodically
        if (currentStep % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
            yield();
            if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (m_webSocket) m_webSocket->loop();
                if (m_server) m_server->handleClient();
                xSemaphoreGive(wsMutex);
            }
        }
    }
    
    // Update start position (single source of truth)
    motion.startPositionMM = tenPercentMM;
    engine->info("✓ Start position set to " + String(tenPercentMM, 0) + " mm");
    
    config.currentState = STATE_READY;
    m_calibrated = true;
    m_attemptCount = 0;
    
    engine->info("✓ Calibration complete");
    
    if (m_completionCallback) {
        m_completionCallback();
    }
    
    return true;
    
    } // end while(true) retry loop
}

bool CalibrationManager::returnToStart() {
    engine->debug("Returning to START contact...");
    
    // Check if stuck at END contact (HIGH = opto blocked = still on contact)
    if (Contacts.isEndActive()) {
        engine->warn("⚠️ Stuck at END - emergency decontact...");
        Motor.setDirection(false);  // Backward
        
        int emergencySteps = 0;
        const int MAX_EMERGENCY = 300;
        
        // Move until opto clears (goes LOW)
        while (Contacts.isEndActive() && emergencySteps < MAX_EMERGENCY) {
            Motor.step();
            currentStep = currentStep - 1;
            emergencySteps++;
            delayMicroseconds(CALIB_DELAY);
            
            if (emergencySteps % 50 == 0) {
                yield();
                if (m_webSocket) m_webSocket->loop();
                if (m_server) m_server->handleClient();
            }
        }
        
        if (emergencySteps >= MAX_EMERGENCY) {
            if (m_errorCallback) {
                m_errorCallback("❌ Cannot release from END contact");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            return false;
        }
        
        engine->info("✓ Release successful (" + String(emergencySteps) + " steps)");
        delay(200);
    }
    
    // Search for START contact: move backward while opto is LOW (clear), stop when HIGH (blocked)
    Motor.setDirection(false);  // Backward
    
    while (Contacts.isStartClear()) {
        Motor.step();
        currentStep = currentStep - 1;
        delayMicroseconds(CALIB_DELAY);
        
        if (currentStep % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
            yield();
            if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (m_webSocket) m_webSocket->loop();
                if (m_server) m_server->handleClient();
                xSemaphoreGive(wsMutex);
            }
        }
        
        if (currentStep < -CALIBRATION_ERROR_MARGIN_STEPS) {
            if (m_errorCallback) {
                m_errorCallback("❌ Cannot return to START contact");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            return false;
        }
    }
    
    // Release contact: move forward while opto is HIGH (blocked), until it goes LOW (clear)
    engine->debug("START contact detected - releasing...");
    Motor.setDirection(true);  // Forward
    
    int releaseCount = 0;
    const int MAX_RELEASE = SAFETY_OFFSET_STEPS * 4;
    while (Contacts.isStartActive() && releaseCount < MAX_RELEASE) {
        Motor.step();
        currentStep = currentStep + 1;
        releaseCount++;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);
    }
    
    if (releaseCount >= MAX_RELEASE) {
        engine->error("❌ Cannot release START contact in returnToStart()");
        Motor.disable();
        config.currentState = STATE_ERROR;
        return false;
    }
    
    // Add safety margin
    for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
        Motor.step();
        currentStep = currentStep + 1;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
    }
    
    currentStep = 0;
    return true;
}

// ============================================================================
// GETTERS
// ============================================================================

bool CalibrationManager::isAtStart() const {
    return currentStep == 0;
}

bool CalibrationManager::isCalibrated() const {
    return m_calibrated;
}

float CalibrationManager::getTotalDistanceMM() const {
    return m_totalDistanceMM;
}

long CalibrationManager::getMaxStep() const {
    return m_maxStep;
}

// ============================================================================
// CALLBACK SETTERS
// ============================================================================

void CalibrationManager::setStatusCallback(void (*callback)()) {
    m_statusCallback = callback;
}

void CalibrationManager::setErrorCallback(void (*callback)(const String& msg)) {
    m_errorCallback = callback;
}

void CalibrationManager::setCompletionCallback(void (*callback)()) {
    m_completionCallback = callback;
}
