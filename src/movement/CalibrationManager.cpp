// ============================================================================
// CALIBRATION_MANAGER.CPP - Stepper Motor Calibration Controller
// ============================================================================
// Implementation of calibration logic
// ============================================================================

#include "movement/CalibrationManager.h"
#include "core/GlobalState.h"
#include "core/UtilityEngine.h"

// NOTE: currentStep, config now via GlobalState.h (Phase 4D cleanup)
extern UtilityEngine* engine;

// External WebSocket/Server (for servicing during long operations)
#include <WebSocketsServer.h>
#include <WebServer.h>

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

CalibrationManager& CalibrationManager::getInstance() {
    static CalibrationManager instance;
    return instance;
}

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
        m_webSocket->loop();
        m_server->handleClient();
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
            m_errorCallback("❌ ERROR: Calibration échouée après " + String(MAX_CALIBRATION_RETRIES) + " tentatives");
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
            if (m_webSocket) m_webSocket->loop();
            if (m_server) m_server->handleClient();
        }
        
        // Timeout protection
        if (stepCount > CALIBRATION_MAX_STEPS) {
            String errorMsg = "❌ ERROR: Contact ";
            errorMsg += contactName;
            errorMsg += " introuvable";
            if (m_errorCallback) m_errorCallback(errorMsg);
            Motor.disable();
            config.currentState = STATE_ERROR;
            return false;
        }
    }
    
    // Validate END contact distance (sanity check - not a retry loop)
    if (contactPin == PIN_END_CONTACT) {
        long detectedSteps = abs(currentStep);
        long minExpectedSteps = (long)(HARD_MIN_DISTANCE_MM * STEPS_PER_MM);
        
        if (detectedSteps < minExpectedSteps) {
            engine->error("❌ Opto END détecté trop tôt (" + 
                         String(detectedSteps) + " < " + String(minExpectedSteps) + " steps)");
            engine->error("→ Vérifier le câblage ou la position des optos");
            return false;  // Fail without infinite retry
        }
    }
    
    return true;
}

void CalibrationManager::releaseContact(uint8_t contactPin, bool moveForward) {
    Motor.setDirection(moveForward);
    
    // Move slowly until opto clears (HIGH->LOW transition)
    while (Contacts.isActive(contactPin)) {
        Motor.step();
        if (!moveForward) currentStep--;
        else currentStep++;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);
    }
    
    // Add safety margin
    for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
        Motor.step();
        if (!moveForward) currentStep--;
        else currentStep++;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
    }
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
    m_totalDistanceMM = m_maxStep / STEPS_PER_MM;
    config.maxStep = m_maxStep;
    config.totalDistanceMM = m_totalDistanceMM;
    
    // Validate distance
    if (m_totalDistanceMM < HARD_MIN_DISTANCE_MM) {
        m_attemptCount++;
        if (m_attemptCount >= MAX_CALIBRATION_RETRIES) {
            if (m_errorCallback) {
                m_errorCallback("❌ Distance calibrée trop courte (" + 
                              String(m_totalDistanceMM, 1) + " mm)");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            m_attemptCount = 0;
            return false;
        }
        
        engine->warn("⚠️ Distance trop courte - Retry " + String(m_attemptCount));
        serviceWebSocket(500);
        return startCalibration();  // Recursive retry
    }
    
    if (m_totalDistanceMM > HARD_MAX_DISTANCE_MM) {
        engine->warn("⚠️ Distance limitée à " + String(HARD_MAX_DISTANCE_MM, 1) + " mm");
        m_totalDistanceMM = HARD_MAX_DISTANCE_MM;
        m_maxStep = (long)(m_totalDistanceMM * STEPS_PER_MM);
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
                m_errorCallback("❌ Calibration échouée - erreur trop grande");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            m_attemptCount = 0;
            return false;
        }
        
        engine->warn("⚠️ Error too large - Retry");
        serviceWebSocket(500);
        return startCalibration();
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
    long targetSteps = (long)(tenPercentMM * STEPS_PER_MM);
    
    engine->info("📍 Positioning at 10% (" + String(tenPercentMM, 0) + " mm)...");
    
    Motor.setDirection(true);  // Forward
    while (currentStep < targetSteps) {
        Motor.step();
        currentStep++;
        delayMicroseconds(CALIB_DELAY);
        
        // Service WebSocket periodically
        if (currentStep % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
            yield();
            if (m_webSocket) m_webSocket->loop();
            if (m_server) m_server->handleClient();
        }
    }
    
    // Update start position in BOTH motion configs (global + system config)
    motion.startPositionMM = tenPercentMM;         // Global - used by StatusBroadcaster
    config.motion.startPositionMM = tenPercentMM;  // SystemConfig - used for persistence
    engine->info("✓ Start position set to " + String(tenPercentMM, 0) + " mm");
    
    config.currentState = STATE_READY;
    m_calibrated = true;
    m_attemptCount = 0;
    
    engine->info("✓ Calibration complete");
    
    if (m_completionCallback) {
        m_completionCallback();
    }
    
    return true;
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
            currentStep--;
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
                m_errorCallback("❌ Impossible de décoller du contact END");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            return false;
        }
        
        engine->info("✓ Décollement réussi (" + String(emergencySteps) + " steps)");
        delay(200);
    }
    
    // Search for START contact: move backward while opto is LOW (clear), stop when HIGH (blocked)
    Motor.setDirection(false);  // Backward
    
    while (Contacts.isStartClear()) {
        Motor.step();
        currentStep--;
        delayMicroseconds(CALIB_DELAY);
        
        if (currentStep % WEBSOCKET_SERVICE_INTERVAL_STEPS == 0) {
            yield();
            if (m_webSocket) m_webSocket->loop();
            if (m_server) m_server->handleClient();
        }
        
        if (currentStep < -CALIBRATION_ERROR_MARGIN_STEPS) {
            if (m_errorCallback) {
                m_errorCallback("❌ Impossible de retourner au contact START");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            return false;
        }
    }
    
    // Release contact: move forward while opto is HIGH (blocked), until it goes LOW (clear)
    engine->debug("START contact detected - releasing...");
    Motor.setDirection(true);  // Forward
    
    while (Contacts.isStartActive()) {
        Motor.step();
        currentStep++;
        delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR * 2);
    }
    
    // Add safety margin
    for (int i = 0; i < SAFETY_OFFSET_STEPS; i++) {
        Motor.step();
        currentStep++;
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

float CalibrationManager::getLastErrorPercent() const {
    return m_lastErrorPercent;
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
