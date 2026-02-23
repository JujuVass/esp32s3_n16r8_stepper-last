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

using enum SystemState;

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

CalibrationManager& CalibrationManager::getInstance() {
    static CalibrationManager instance; // NOSONAR(cpp:S6018)
    return instance;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void CalibrationManager::init() {
    initialized_ = true;
    engine->debug("CalibrationManager initialized");
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void CalibrationManager::serviceDelay(unsigned long durationMs) {
    unsigned long start = millis();
    while (millis() - start < durationMs) {
        yield();
        delay(1);
    }
}

void CalibrationManager::yieldIfDue(unsigned long stepCount) {
    if (stepCount % WEBSOCKET_SERVICE_INTERVAL_STEPS != 0) return;
    yield();
}

void CalibrationManager::positionAtOffset(long targetSteps) {
    Motor.setDirection(true);  // Forward
    const unsigned long startMs = millis();
    constexpr unsigned long POSITION_TIMEOUT_MS = 30000;  // 30s safety timeout
    while (currentStep < targetSteps) {
        if (millis() - startMs > POSITION_TIMEOUT_MS) [[unlikely]] {
            engine->error("❌ positionAtOffset timeout after " + String(POSITION_TIMEOUT_MS / 1000) + "s");
            break;
        }
        Motor.step();
        currentStep = currentStep + 1;
        delayMicroseconds(CALIB_DELAY);
        yieldIfDue(static_cast<unsigned long>(currentStep));
    }
}

bool CalibrationManager::emergencyDecontactEnd() {
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
        }
    }

    if (emergencySteps >= MAX_EMERGENCY) {
        if (errorCallback_) {
            errorCallback_("❌ Cannot release from END contact");
        }
        Motor.disable();
        config.currentState = STATE_ERROR;
        return false;
    }

    engine->info("✓ Release successful (" + String(emergencySteps) + " steps)");
    delay(200);
    return true;
}

bool CalibrationManager::handleFailure() {
    Motor.disable();
    config.currentState = STATE_ERROR;

    attemptCount_++;
    if (attemptCount_ >= MAX_CALIBRATION_RETRIES) {
        if (errorCallback_) {
            errorCallback_("❌ ERROR: Calibration failed after " + String(MAX_CALIBRATION_RETRIES) + " attempts");
        }
        attemptCount_ = 0;
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
            currentStep = currentStep + (!moveForward ? -1 : 1);  // 🔧 FIX: Track position during backoff
            backoffSteps++;
            delayMicroseconds(CALIB_DELAY * CALIBRATION_SLOW_FACTOR);
        }

        if (backoffSteps >= SAFETY_OFFSET_STEPS * 2) {
            engine->error("❌ Cannot clear " + String(contactName) + " opto!");
            return false;
        }

        engine->info("✓ Cleared opto after " + String(backoffSteps) + " steps, continuing calibration...");
        Motor.setDirection(moveForward);
        serviceDelay(100);
    }

    // Search for contact: move while opto is LOW (clear), stop when HIGH (blocked)
    while (Contacts.isClear(contactPin)) {
        Motor.step();
        currentStep = currentStep + (moveForward ? 1 : -1);
        delayMicroseconds(CALIB_DELAY);
        stepCount++;

        yieldIfDue(stepCount);

        // Timeout protection
        if (stepCount > CALIBRATION_MAX_STEPS) {
            String errorMsg = "❌ ERROR: Contact ";
            errorMsg += contactName;
            errorMsg += " not found";
            if (errorCallback_) errorCallback_(errorMsg);
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
        return;  // 🔧 FIX: Abort if sensor stuck — don't proceed with incorrect position
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
    float differencePercent = (maxStep_ > 0) ?
        ((float)stepDifference / (float)maxStep_) * 100.0f : 0.0f;

    if (stepDifference == 0) {
        engine->debug("✓ Return accuracy: PERFECT!");
    } else {
        engine->warn("⚠️ Return accuracy: " + String(stepDifference) + " steps (" +
                    String(differencePercent, 1) + "%)");
    }

    lastErrorPercent_ = differencePercent;
    return differencePercent;
}

// ============================================================================
// PUBLIC METHODS
// ============================================================================

bool CalibrationManager::startCalibration() {
    if (!initialized_) {
        engine->error("CalibrationManager not initialized!");
        return false;
    }

    // Loop for retries (replaces recursive calls to avoid stack overflow on Core 1)
    while (true) {

    engine->info(attemptCount_ == 0 ? "Starting calibration..." : "Retry calibration...");
    config.currentState = STATE_CALIBRATING;

    // Send status updates
    if (statusCallback_) {
        for (int i = 0; i < 5; i++) {
            statusCallback_();
            serviceDelay(20);
        }
    }

    Motor.enable();
    serviceDelay(200);  // Settling time

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
    maxStep_ = currentStep;
    totalDistanceMM_ = MovementMath::stepsToMM(maxStep_);
    config.maxStep = maxStep_;
    config.totalDistanceMM = totalDistanceMM_;

    // Check distance is within acceptable range (returns tri-state)
    if (int distResult = validateDistance(); distResult != 0) {
        if (distResult < 0) return false;
        continue;
    }

    // ========================================
    // Step 3: Return to START
    // ========================================
    if (!returnToStart()) {
        return handleFailure();
    }

    // Check return-to-start accuracy is within tolerance (returns tri-state)
    if (int accResult = validateCalibrationAccuracy(); accResult != 0) {
        if (accResult < 0) return false;
        continue;
    }

    return finalizeCalibration();

    } // end while(true) retry loop
}

/**
 * Validate calibrated distance range.
 * @return 0=OK, 1=retry needed, -1=fatal failure
 */
int CalibrationManager::validateDistance() {
    if (totalDistanceMM_ < HARD_MIN_DISTANCE_MM) {
        attemptCount_++;
        if (attemptCount_ >= MAX_CALIBRATION_RETRIES) {
            if (errorCallback_) {
                errorCallback_("❌ Calibrated distance too short (" +
                              String(totalDistanceMM_, 1) + " mm)");
            }
            Motor.disable();
            config.currentState = STATE_ERROR;
            attemptCount_ = 0;
            return -1;
        }
        engine->warn("⚠️ Distance too short - Retry " + String(attemptCount_));
        serviceDelay(500);
        return 1;
    }

    if (totalDistanceMM_ > HARD_MAX_DISTANCE_MM) {
        engine->warn("⚠️ Distance limited to " + String(HARD_MAX_DISTANCE_MM, 1) + " mm");
        totalDistanceMM_ = HARD_MAX_DISTANCE_MM;
        maxStep_ = MovementMath::mmToSteps(totalDistanceMM_);
        config.maxStep = maxStep_;
        config.totalDistanceMM = totalDistanceMM_;
    }

    engine->debug("✓ Total distance: " + String(totalDistanceMM_, 1) + " mm");
    return 0;
}

/**
 * Validate calibration accuracy against tolerance.
 * @return 0=OK, 1=retry needed, -1=fatal failure
 */
int CalibrationManager::validateCalibrationAccuracy() {
    if (float errorPercent = validateAccuracy(); errorPercent <= MAX_CALIBRATION_ERROR_PERCENT) return 0;

    attemptCount_++;
    if (attemptCount_ >= MAX_CALIBRATION_RETRIES) {
        if (errorCallback_) {
            errorCallback_("❌ Calibration failed - error too large");
        }
        Motor.disable();
        config.currentState = STATE_ERROR;
        attemptCount_ = 0;
        return -1;
    }

    engine->warn("⚠️ Error too large - Retry");
    serviceDelay(500);
    return 1;
}

/** Position at 10% and finalize calibrated state. */
bool CalibrationManager::finalizeCalibration() {
    currentStep = 0;
    config.minStep = 0;

    // Position at 10% of total distance (rounded up to nearest mm)
    float tenPercentMM = ceil(totalDistanceMM_ * 0.1f);
    long targetSteps = MovementMath::mmToSteps(tenPercentMM);

    engine->info("📍 Positioning at 10% (" + String(tenPercentMM, 0) + " mm)...");
    positionAtOffset(targetSteps);

    motion.startPositionMM = tenPercentMM;
    engine->info("✓ Start position set to " + String(tenPercentMM, 0) + " mm");

    config.currentState = STATE_READY;
    calibrated_ = true;
    attemptCount_ = 0;

    engine->info("✓ Calibration complete");

    if (completionCallback_) {
        completionCallback_();
    }

    return true;
}

bool CalibrationManager::returnToStart() {
    engine->debug("Returning to START contact...");

    // Check if stuck at END contact (HIGH = opto blocked = still on contact)
    if (Contacts.isEndActive() && !emergencyDecontactEnd()) {
        return false;
    }

    // Search for START contact: move backward while opto is LOW (clear), stop when HIGH (blocked)
    Motor.setDirection(false);  // Backward

    while (Contacts.isStartClear()) {
        Motor.step();
        currentStep = currentStep - 1;
        delayMicroseconds(CALIB_DELAY);

        yieldIfDue(static_cast<unsigned long>(abs(currentStep)));

        if (currentStep < -CALIBRATION_ERROR_MARGIN_STEPS) {
            if (errorCallback_) {
                errorCallback_("❌ Cannot return to START contact");
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
// CALLBACK SETTERS
// ============================================================================

void CalibrationManager::setStatusCallback(void (*callback)()) {  // NOSONAR(cpp:S5205)
    statusCallback_ = callback;
}

void CalibrationManager::setErrorCallback(void (*callback)(const String& msg)) {  // NOSONAR(cpp:S5205)
    errorCallback_ = callback;
}

void CalibrationManager::setCompletionCallback(void (*callback)()) {  // NOSONAR(cpp:S5205)
    completionCallback_ = callback;
}
