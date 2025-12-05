// ============================================================================
// VALIDATORS - Centralized Parameter Validation
// ============================================================================
// Purpose: All validation functions in one place for cleaner code
// - Motion parameters (distance, speed, position)
// - Chaos mode parameters
// - Oscillation mode parameters
// - Deceleration zone configuration
//
// Usage: Include this header and use Validators namespace
//   #include "core/Validators.h"
//   String err;
//   if (!Validators::distance(100.0, err)) { sendError(err); }
// ============================================================================

#ifndef VALIDATORS_H
#define VALIDATORS_H

#include <Arduino.h>
#include "Config.h"
#include "Types.h"

// Note: GlobalState.h provides all extern declarations
// But we need these before GlobalState might be included
extern float effectiveMaxDistanceMM;
extern float maxDistanceLimitPercent;
extern SystemConfig config;

namespace Validators {

// ============================================================================
// BASIC VALIDATORS
// ============================================================================

/**
 * Validate distance parameter
 * @param distMM Distance in millimeters
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool distance(float distMM, String& errorMsg) {
  if (distMM < 0) {
    errorMsg = "Distance négative invalide: " + String(distMM, 1) + " mm";
    return false;
  }
  
  // Use effective max distance (with limitation factor applied)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (maxAllowed > 0 && distMM > maxAllowed) {
    errorMsg = "Distance dépasse limite: " + String(distMM, 1) + " > " + String(maxAllowed, 1) + " mm";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (limite " + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  return true;
}

/**
 * Validate speed level parameter (0.1 - MAX_SPEED_LEVEL)
 * @param speedLevel Speed level to validate
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool speed(float speedLevel, String& errorMsg) {
  if (speedLevel < 0.1) {
    errorMsg = "Vitesse trop faible: " + String(speedLevel, 1) + " (min: 0.1)";
    return false;
  }
  
  if (speedLevel > MAX_SPEED_LEVEL) {
    errorMsg = "Vitesse trop élevée: " + String(speedLevel, 1) + " (max: " + String(MAX_SPEED_LEVEL, 1) + ")";
    return false;
  }
  
  return true;
}

/**
 * Validate position parameter
 * @param positionMM Position in millimeters
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool position(float positionMM, String& errorMsg) {
  if (positionMM < 0) {
    errorMsg = "Position négative invalide: " + String(positionMM, 1) + " mm";
    return false;
  }
  
  // Use effective max distance (with limitation factor applied)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (maxAllowed > 0 && positionMM > maxAllowed) {
    errorMsg = "Position dépasse limite: " + String(positionMM, 1) + " > " + String(maxAllowed, 1) + " mm";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (limite " + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  return true;
}

// ============================================================================
// COMPOSITE VALIDATORS
// ============================================================================

/**
 * Validate start position + distance (combined range check)
 * @param startMM Start position in mm
 * @param distMM Distance in mm
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool motionRange(float startMM, float distMM, String& errorMsg) {
  // Validate start position alone
  if (!position(startMM, errorMsg)) {
    return false;
  }
  
  // Validate distance alone
  if (!distance(distMM, errorMsg)) {
    return false;
  }
  
  // Validate combined range
  float endPositionMM = startMM + distMM;
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (maxAllowed > 0 && endPositionMM > maxAllowed) {
    errorMsg = "Position finale dépasse limite: " + String(endPositionMM, 1) + " mm (start " + 
               String(startMM, 1) + " + distance " + String(distMM, 1) + ") > " + 
               String(maxAllowed, 1) + " mm";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (limite " + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  return true;
}

// ============================================================================
// MODE-SPECIFIC VALIDATORS
// ============================================================================

/**
 * Validate Chaos mode parameters
 * @param centerMM Center position in mm
 * @param amplitudeMM Amplitude in mm (± from center)
 * @param maxSpeed Maximum speed level
 * @param craziness Craziness percentage (0-100)
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool chaosParams(float centerMM, float amplitudeMM, float maxSpeed, float craziness, String& errorMsg) {
  // Validate center position
  if (!position(centerMM, errorMsg)) {
    return false;
  }
  
  // Validate amplitude
  if (amplitudeMM <= 0) {
    errorMsg = "Amplitude doit être > 0 mm";
    return false;
  }
  
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  if (amplitudeMM > maxAllowed / 2.0) {
    errorMsg = "Amplitude trop grande: " + String(amplitudeMM, 1) + " > " + String(maxAllowed / 2.0, 1) + " mm (max)";
    return false;
  }
  
  // Validate that center ± amplitude stays within bounds
  if (centerMM - amplitudeMM < 0) {
    errorMsg = "Centre - amplitude < 0 mm (centre=" + String(centerMM, 1) + ", amplitude=" + String(amplitudeMM, 1) + ")";
    return false;
  }
  
  if (centerMM + amplitudeMM > maxAllowed) {
    errorMsg = "Centre + amplitude > " + String(maxAllowed, 1) + " mm limite";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (" + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  // Validate speed
  if (!speed(maxSpeed, errorMsg)) {
    return false;
  }
  
  // Validate craziness (0-100%)
  if (craziness < 0 || craziness > 100) {
    errorMsg = "Craziness doit être 0-100% (reçu: " + String(craziness, 1) + ")";
    return false;
  }
  
  return true;
}

/**
 * Validate Oscillation mode parameters
 * @param centerMM Center position in mm
 * @param amplitudeMM Amplitude in mm (± from center)
 * @param frequency Frequency in Hz
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool oscillationParams(float centerMM, float amplitudeMM, float frequency, String& errorMsg) {
  // Validate center position
  if (!position(centerMM, errorMsg)) {
    return false;
  }
  
  // Validate amplitude
  if (amplitudeMM <= 0) {
    errorMsg = "Amplitude doit être > 0 mm";
    return false;
  }
  
  // Use effective max distance (respects limitation percentage)
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  // Validate bounds (center ± amplitude must stay within limits)
  if (centerMM - amplitudeMM < 0) {
    errorMsg = "Centre - amplitude < 0 mm (centre=" + String(centerMM, 1) + ", amplitude=" + String(amplitudeMM, 1) + ")";
    return false;
  }
  
  if (centerMM + amplitudeMM > maxAllowed) {
    errorMsg = "Centre + amplitude > " + String(maxAllowed, 1) + " mm limite";
    if (maxDistanceLimitPercent < 100.0) {
      errorMsg += " (" + String(maxDistanceLimitPercent, 0) + "%)";
    }
    return false;
  }
  
  // Validate frequency
  if (frequency <= 0) {
    errorMsg = "Fréquence doit être > 0 Hz";
    return false;
  }
  
  if (frequency > 10.0) {  // Reasonable max frequency for mechanical system
    errorMsg = "Fréquence trop élevée: " + String(frequency, 3) + " Hz (max: 10 Hz)";
    return false;
  }
  
  return true;
}

/**
 * Validate oscillation amplitude given center position and limits
 * @param centerMM Center position in mm
 * @param amplitudeMM Desired amplitude in mm
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool oscillationAmplitude(float centerMM, float amplitudeMM, String& errorMsg) {
  float maxAllowed = (effectiveMaxDistanceMM > 0) ? effectiveMaxDistanceMM : config.totalDistanceMM;
  
  // Calculate maximum safe amplitude
  float maxAmplitude = min(centerMM, maxAllowed - centerMM);
  
  if (amplitudeMM > maxAmplitude) {
    errorMsg = "Amplitude " + String(amplitudeMM, 1) + "mm trop grande pour centre " + 
               String(centerMM, 1) + "mm. Maximum: " + String(maxAmplitude, 1) + "mm";
    return false;
  }
  
  return true;
}

/**
 * Validate percentage value (0-100)
 * @param percent Value to validate
 * @param name Name for error message
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool percentage(float percent, const char* name, String& errorMsg) {
  if (percent < 0 || percent > 100) {
    errorMsg = String(name) + " doit être 0-100% (reçu: " + String(percent, 1) + ")";
    return false;
  }
  return true;
}

/**
 * Validate positive value
 * @param value Value to validate
 * @param name Name for error message
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool positive(float value, const char* name, String& errorMsg) {
  if (value <= 0) {
    errorMsg = String(name) + " doit être > 0 (reçu: " + String(value, 1) + ")";
    return false;
  }
  return true;
}

/**
 * Validate value within range
 * @param value Value to validate
 * @param minVal Minimum allowed value
 * @param maxVal Maximum allowed value
 * @param name Name for error message
 * @param errorMsg Output error message if validation fails
 * @return true if valid, false otherwise
 */
inline bool range(float value, float minVal, float maxVal, const char* name, String& errorMsg) {
  if (value < minVal || value > maxVal) {
    errorMsg = String(name) + " doit être " + String(minVal, 1) + "-" + String(maxVal, 1) + 
               " (reçu: " + String(value, 1) + ")";
    return false;
  }
  return true;
}

}  // namespace Validators

#endif  // VALIDATORS_H
