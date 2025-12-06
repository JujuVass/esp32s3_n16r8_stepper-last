/**
 * validation.js - Validation logic and error field mapping
 * Contains pure validation functions and field mapping data
 */

// ============================================================================
// ERROR FIELD MAPPING (DATA)
// ============================================================================

/**
 * Mapping between error message keywords and DOM field IDs
 * Used by highlightErrorFields in main.js
 */
const ERROR_FIELD_MAPPING = {
  // VA-ET-VIENT fields
  'Position de départ': 'editStartPos',
  'Distance': 'editDistance',
  'Vitesse aller': 'editSpeedFwd',
  'Vitesse retour': 'editSpeedBack',
  'Zone décélération': 'editDecelZone',
  
  // OSCILLATION fields
  'Centre oscillation': 'editOscCenter',
  'Amplitude oscillation': 'editOscAmplitude',
  'Fréquence': 'editOscFrequency',
  'Durée rampe IN': 'editOscRampInDur',
  'Durée rampe OUT': 'editOscRampOutDur',
  
  // CHAOS fields
  'Centre chaos': 'editChaosCenter',
  'Amplitude chaos': 'editChaosAmplitude',
  'Vitesse max chaos': 'editChaosSpeed',
  'Degré de folie': 'editChaosCraziness',
  'Durée chaos': 'editChaosDuration',
  'Seed': 'editChaosSeed',
  
  // Common fields
  'Nombre de cycles': 'editCycles',
  'Pause': 'editPause'
};

/**
 * All editable field IDs for clearing highlights
 */
const ALL_EDIT_FIELDS = [
  // VA-ET-VIENT
  'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack', 'editDecelZone',
  // OSCILLATION
  'editOscCenter', 'editOscAmplitude', 'editOscFrequency',
  'editOscRampInDur', 'editOscRampOutDur',
  // CHAOS
  'editChaosCenter', 'editChaosAmplitude', 'editChaosSpeed', 'editChaosCraziness',
  'editChaosDuration', 'editChaosSeed',
  // COMMON
  'editCycles', 'editPause'
];

// ============================================================================
// VALIDATION LIMITS (DATA)
// ============================================================================

/**
 * Validation limits for all fields by movement type
 */
const VALIDATION_LIMITS = {
  // Common limits
  common: {
    cycleCount: { min: 1, max: 1000 },
    pauseAfterMs: { min: 0, max: 60000 }
  },
  
  // VA-ET-VIENT limits
  vaet: {
    startPositionMM: { min: 0 },  // max depends on effectiveMax
    distanceMM: { min: 1 },       // max depends on effectiveMax
    speedForward: { min: 1, max: 20 },
    speedBackward: { min: 1, max: 20 },
    decelZoneMM: { min: 10, max: 200 },
    decelEffectPercent: { min: 0, max: 100 }
  },
  
  // OSCILLATION limits
  oscillation: {
    centerPositionMM: { min: 0 },  // max depends on effectiveMax
    amplitudeMM: { min: 1 },       // max depends on effectiveMax
    frequencyHz: { min: 0.01, max: 10 },
    rampDurationMs: { min: 100, max: 10000 }
  },
  
  // CHAOS limits
  chaos: {
    centerPositionMM: { min: 0 },  // max depends on effectiveMax
    amplitudeMM: { min: 1 },       // max depends on effectiveMax
    maxSpeedLevel: { min: 1, max: 20 },
    crazinessPercent: { min: 0, max: 100 },
    durationSeconds: { min: 5, max: 600 },
    seed: { min: 0, max: 9999999 }
  }
};

// ============================================================================
// PURE VALIDATION FUNCTIONS
// ============================================================================

/**
 * Get field IDs to highlight from error messages (PURE FUNCTION)
 * @param {string[]} errorMessages - Array of error message strings
 * @returns {string[]} Array of field IDs to highlight
 */
function getErrorFieldIdsPure(errorMessages) {
  const fieldIds = [];
  
  if (!errorMessages || !Array.isArray(errorMessages)) {
    return fieldIds;
  }
  
  errorMessages.forEach(error => {
    for (const [keyword, fieldId] of Object.entries(ERROR_FIELD_MAPPING)) {
      if (error.includes(keyword)) {
        if (!fieldIds.includes(fieldId)) {
          fieldIds.push(fieldId);
        }
      }
    }
  });
  
  return fieldIds;
}

/**
 * Validate VA-ET-VIENT line fields (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} effectiveMax - Maximum position in mm
 * @returns {Object} { valid: boolean, invalidFields: string[] }
 */
function validateVaetFieldsPure(line, effectiveMax) {
  const invalidFields = [];
  const limits = VALIDATION_LIMITS.vaet;
  
  // Position départ
  if (line.startPositionMM < limits.startPositionMM.min || line.startPositionMM > effectiveMax) {
    invalidFields.push('editStartPos');
  }
  
  // Distance (end position must not exceed effectiveMax)
  const endPosition = line.startPositionMM + line.distanceMM;
  if (endPosition > effectiveMax || line.distanceMM < limits.distanceMM.min) {
    invalidFields.push('editDistance');
  }
  
  // Vitesses
  if (line.speedForward < limits.speedForward.min || line.speedForward > limits.speedForward.max) {
    invalidFields.push('editSpeedFwd');
  }
  if (line.speedBackward < limits.speedBackward.min || line.speedBackward > limits.speedBackward.max) {
    invalidFields.push('editSpeedBack');
  }
  
  // Zone décélération
  if (line.decelZoneMM < limits.decelZoneMM.min || line.decelZoneMM > limits.decelZoneMM.max) {
    invalidFields.push('editDecelZone');
  }
  
  return {
    valid: invalidFields.length === 0,
    invalidFields: invalidFields
  };
}

/**
 * Validate OSCILLATION line fields (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} effectiveMax - Maximum position in mm
 * @returns {Object} { valid: boolean, invalidFields: string[] }
 */
function validateOscillationFieldsPure(line, effectiveMax) {
  const invalidFields = [];
  const limits = VALIDATION_LIMITS.oscillation;
  
  const minPos = line.oscCenterPositionMM - line.oscAmplitudeMM;
  const maxPos = line.oscCenterPositionMM + line.oscAmplitudeMM;
  
  // Centre & Amplitude
  if (minPos < 0 || maxPos > effectiveMax) {
    invalidFields.push('editOscCenter');
  }
  if (line.oscAmplitudeMM < limits.amplitudeMM.min || maxPos > effectiveMax) {
    invalidFields.push('editOscAmplitude');
  }
  
  // Fréquence
  if (line.oscFrequencyHz < limits.frequencyHz.min || line.oscFrequencyHz > limits.frequencyHz.max) {
    invalidFields.push('editOscFrequency');
  }
  
  // Durée rampes
  if (line.oscRampInDurationMs < limits.rampDurationMs.min || line.oscRampInDurationMs > limits.rampDurationMs.max) {
    invalidFields.push('editOscRampInDur');
  }
  if (line.oscRampOutDurationMs < limits.rampDurationMs.min || line.oscRampOutDurationMs > limits.rampDurationMs.max) {
    invalidFields.push('editOscRampOutDur');
  }
  
  return {
    valid: invalidFields.length === 0,
    invalidFields: invalidFields
  };
}

/**
 * Validate CHAOS line fields (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} effectiveMax - Maximum position in mm
 * @returns {Object} { valid: boolean, invalidFields: string[] }
 */
function validateChaosFieldsPure(line, effectiveMax) {
  const invalidFields = [];
  const limits = VALIDATION_LIMITS.chaos;
  
  const minPos = line.chaosCenterPositionMM - line.chaosAmplitudeMM;
  const maxPos = line.chaosCenterPositionMM + line.chaosAmplitudeMM;
  
  // Centre & Amplitude
  if (minPos < 0 || maxPos > effectiveMax) {
    invalidFields.push('editChaosCenter');
  }
  if (line.chaosAmplitudeMM < limits.amplitudeMM.min || maxPos > effectiveMax) {
    invalidFields.push('editChaosAmplitude');
  }
  
  // Vitesse max
  if (line.chaosMaxSpeedLevel < limits.maxSpeedLevel.min || line.chaosMaxSpeedLevel > limits.maxSpeedLevel.max) {
    invalidFields.push('editChaosSpeed');
  }
  
  // Degré de folie
  if (line.chaosCrazinessPercent < limits.crazinessPercent.min || line.chaosCrazinessPercent > limits.crazinessPercent.max) {
    invalidFields.push('editChaosCraziness');
  }
  
  // Durée
  if (line.chaosDurationSeconds < limits.durationSeconds.min || line.chaosDurationSeconds > limits.durationSeconds.max) {
    invalidFields.push('editChaosDuration');
  }
  
  // Seed
  if (line.chaosSeed < limits.seed.min || line.chaosSeed > limits.seed.max) {
    invalidFields.push('editChaosSeed');
  }
  
  return {
    valid: invalidFields.length === 0,
    invalidFields: invalidFields
  };
}

/**
 * Validate common line fields (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} movementType - Movement type code
 * @returns {Object} { valid: boolean, invalidFields: string[] }
 */
function validateCommonFieldsPure(line, movementType) {
  const invalidFields = [];
  const limits = VALIDATION_LIMITS.common;
  
  // Cycles (not for CHAOS=2 or CALIBRATION=4)
  if (movementType !== 2 && movementType !== 4) {
    if (line.cycleCount < limits.cycleCount.min || line.cycleCount > limits.cycleCount.max) {
      invalidFields.push('editCycles');
    }
  }
  
  // Pause
  if (line.pauseAfterMs < limits.pauseAfterMs.min || line.pauseAfterMs > limits.pauseAfterMs.max) {
    invalidFields.push('editPause');
  }
  
  return {
    valid: invalidFields.length === 0,
    invalidFields: invalidFields
  };
}

/**
 * Get all invalid fields for a line (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} movementType - Movement type code
 * @param {number} effectiveMax - Maximum position in mm
 * @param {string[]} emptyFieldErrors - Error messages for empty fields
 * @returns {string[]} Array of invalid field IDs
 */
function getAllInvalidFieldsPure(line, movementType, effectiveMax, emptyFieldErrors) {
  let allInvalidFields = [];
  
  // Get fields from error messages
  const errorFields = getErrorFieldIdsPure(emptyFieldErrors);
  allInvalidFields = allInvalidFields.concat(errorFields);
  
  // Type-specific validation
  if (movementType === 0) {
    const vaetResult = validateVaetFieldsPure(line, effectiveMax);
    allInvalidFields = allInvalidFields.concat(vaetResult.invalidFields);
  } else if (movementType === 1) {
    const oscResult = validateOscillationFieldsPure(line, effectiveMax);
    allInvalidFields = allInvalidFields.concat(oscResult.invalidFields);
  } else if (movementType === 2) {
    const chaosResult = validateChaosFieldsPure(line, effectiveMax);
    allInvalidFields = allInvalidFields.concat(chaosResult.invalidFields);
  }
  
  // Common validation
  const commonResult = validateCommonFieldsPure(line, movementType);
  allInvalidFields = allInvalidFields.concat(commonResult.invalidFields);
  
  // Remove duplicates
  return [...new Set(allInvalidFields)];
}

// ============================================================================
// EMPTY FIELD DETECTION (PURE)
// ============================================================================

/**
 * Check for empty/invalid form field values (PURE FUNCTION)
 * @param {Object} formValues - Object with field names as keys and values
 * @param {number} movementType - Movement type code
 * @returns {string[]} Array of error messages for empty fields
 */
function checkEmptyFieldsPure(formValues, movementType) {
  const errors = [];
  
  // Define required fields by movement type
  const requiredByType = {
    0: ['startPositionMM', 'distanceMM', 'speedForward', 'speedBackward', 'decelZoneMM'],
    1: ['oscCenterPositionMM', 'oscAmplitudeMM', 'oscFrequencyHz', 'oscRampInDurationMs', 'oscRampOutDurationMs'],
    2: ['chaosCenterPositionMM', 'chaosAmplitudeMM', 'chaosMaxSpeedLevel', 'chaosCrazinessPercent', 'chaosDurationSeconds', 'chaosSeed']
  };
  
  // Field name to error message mapping
  const fieldMessages = {
    startPositionMM: '⚠️ Position de départ est incorrect',
    distanceMM: '⚠️ Distance est incorrect',
    speedForward: '⚠️ Vitesse aller est incorrect',
    speedBackward: '⚠️ Vitesse retour est incorrect',
    decelZoneMM: '⚠️ Zone décélération est incorrect',
    oscCenterPositionMM: '⚠️ Centre oscillation est incorrect',
    oscAmplitudeMM: '⚠️ Amplitude oscillation est incorrect',
    oscFrequencyHz: '⚠️ Fréquence est incorrect',
    oscRampInDurationMs: '⚠️ Durée rampe IN est incorrect',
    oscRampOutDurationMs: '⚠️ Durée rampe OUT est incorrect',
    chaosCenterPositionMM: '⚠️ Centre chaos est incorrect',
    chaosAmplitudeMM: '⚠️ Amplitude chaos est incorrect',
    chaosMaxSpeedLevel: '⚠️ Vitesse max chaos est incorrect',
    chaosCrazinessPercent: '⚠️ Degré de folie est incorrect',
    chaosDurationSeconds: '⚠️ Durée chaos est incorrect',
    chaosSeed: '⚠️ Seed est incorrect',
    cycleCount: '⚠️ Nombre de cycles est incorrect',
    pauseAfterSec: '⚠️ Pause est incorrect'
  };
  
  // Check type-specific fields
  const required = requiredByType[movementType] || [];
  required.forEach(field => {
    if (formValues[field] === '' || formValues[field] === undefined || formValues[field] === null) {
      errors.push(fieldMessages[field] || `⚠️ ${field} est incorrect`);
    }
  });
  
  // Check common fields (not for CALIBRATION)
  if (movementType !== 4) {
    if (movementType !== 2 && (formValues.cycleCount === '' || formValues.cycleCount === undefined)) {
      errors.push(fieldMessages.cycleCount);
    }
    if (formValues.pauseAfterSec === '' || formValues.pauseAfterSec === undefined) {
      errors.push(fieldMessages.pauseAfterSec);
    }
  }
  
  return errors;
}

// ============================================================================
// EXPORTS (Browser globals)
// ============================================================================
window.ERROR_FIELD_MAPPING = ERROR_FIELD_MAPPING;
window.ALL_EDIT_FIELDS = ALL_EDIT_FIELDS;
window.VALIDATION_LIMITS = VALIDATION_LIMITS;
window.getErrorFieldIdsPure = getErrorFieldIdsPure;
window.validateVaetFieldsPure = validateVaetFieldsPure;
window.validateOscillationFieldsPure = validateOscillationFieldsPure;
window.validateChaosFieldsPure = validateChaosFieldsPure;
window.validateCommonFieldsPure = validateCommonFieldsPure;
window.getAllInvalidFieldsPure = getAllInvalidFieldsPure;
window.checkEmptyFieldsPure = checkEmptyFieldsPure;

console.log('✅ validation.js loaded - validation logic and field mapping available');
