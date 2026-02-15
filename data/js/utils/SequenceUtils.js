/**
 * SequenceUtils.js - Sequence Display Utilities Module
 * 
 * Pure utility functions for sequence table display and validation:
 * - Type display helpers (icons, info text)
 * - Deceleration summary
 * - Speed display formatting
 * - Cycles/Pause display
 * - Template generation
 * - Sequence line defaults builder
 * - Sequence line validation (mirrors backend Validators.h)
 * - Error field mapping for form highlight
 * 
 * These functions are "pure" - they don't depend on DOM state,
 * only on the data passed to them.
 * 
 * Dependencies: None (pure functions)
 */

// ============================================================================
// MOVEMENT TYPE CONSTANTS
// ============================================================================

const MOVEMENT_TYPE = {
  VA_ET_VIENT: 0,
  OSCILLATION: 1,
  CHAOS: 2,
  CALIBRATION: 4
};

// Short names for sequence table display (SIN/TRI/SQR)
const WAVEFORM_SHORT = ['SIN', 'TRI', 'SQR'];
const SPEED_CURVE_LABELS = ['Lin', 'Sin', 'Tri⁻¹', 'Sin⁻¹'];
function getSpeedEffectLabels() { return ['', t('seqUtils.decel'), t('seqUtils.accel')]; }

// ============================================================================
// LEGACY FORMAT CONVERSION
// ============================================================================

/**
 * Get zone effect config from an object, converting legacy decel* fields if needed.
 * Single source of truth for legacy → vaetZoneEffect conversion.
 * @param {Object} obj - Object with vaetZoneEffect or legacy decel* fields
 * @returns {Object} Normalized vaetZoneEffect object
 */
function getZoneEffectConfig(obj) {
  if (obj.vaetZoneEffect) return obj.vaetZoneEffect;
  
  // Legacy format - convert with unified defaults
  return {
    enabled: (obj.decelStartEnabled || obj.decelEndEnabled) || false,
    enableStart: obj.decelStartEnabled ?? false,
    enableEnd: obj.decelEndEnabled ?? false,
    zoneMM: obj.decelZoneMM || 50,
    speedEffect: 1,  // DECEL
    speedCurve: obj.decelMode || 1,
    speedIntensity: obj.decelEffectPercent || 75,
    mirrorOnReturn: false,
    randomTurnbackEnabled: false,
    turnbackChance: 30,
    endPauseEnabled: false,
    endPauseIsRandom: false,
    endPauseDurationSec: 1.0,
    endPauseMinSec: 0.5,
    endPauseMaxSec: 2.0
  };
}

// ============================================================================
// TYPE DISPLAY HELPERS
// ============================================================================

/**
 * Get display information for a movement type
 * @param {number} movementType - Movement type (0=Va-et-vient, 1=Oscillation, 2=Chaos, 4=Calibration)
 * @param {Object} line - Sequence line object
 * @returns {Object} { typeIcon, typeInfo, typeName }
 */
function getTypeDisplay(movementType, line) {
  let typeIcon = '', typeInfo = '', typeName = '';
  
  if (movementType === MOVEMENT_TYPE.VA_ET_VIENT) {
    typeIcon = '↔️';
    typeName = t('utils.backAndForth');
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>${line.startPositionMM.toFixed(1)}mm</div>
      <div>±${line.distanceMM.toFixed(1)}mm</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    typeIcon = '〰️';
    typeName = t('utils.oscillation');
    const waveformName = WAVEFORM_SHORT[line.oscWaveform] || '?';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>C:${line.oscCenterPositionMM ? line.oscCenterPositionMM.toFixed(0) : '100'}mm</div>
      <div>A:±${line.oscAmplitudeMM ? line.oscAmplitudeMM.toFixed(0) : '50'}mm</div>
      <div>${waveformName} ${line.oscFrequencyHz ? line.oscFrequencyHz.toFixed(2) : '0.5'}Hz</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.CHAOS) {
    typeIcon = '🌀';
    typeName = t('utils.chaos');
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>⏱️${line.chaosDurationSeconds || 30}s</div>
      <div>🎲${line.chaosCrazinessPercent ? line.chaosCrazinessPercent.toFixed(0) : '50'}%</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.CALIBRATION) {
    typeIcon = '📏';
    typeName = t('utils.calibration');
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>${t('utils.calibration')}</div>
      <div>${t('seqUtils.complete')}</div>
    </div>`;
  }
  
  return { typeIcon, typeInfo, typeName };
}

// ============================================================================
// ZONE EFFECTS SUMMARY
// ============================================================================

/**
 * Get zone effects summary HTML for a sequence line
 * Supports both new format (vaetZoneEffect) and legacy format (decel* fields)
 * @param {Object} line - Sequence line object
 * @param {number} movementType - Movement type
 * @returns {string} HTML string for zone effects summary
 */
function getDecelSummary(line, movementType) {
  if (movementType !== MOVEMENT_TYPE.VA_ET_VIENT) {
    return '<span style="color: #999; font-size: 10px;">--</span>';
  }
  
  // Get zone effect config (new format or convert from legacy)
  const ze = getZoneEffectConfig(line);
  
  if (!ze.enabled) {
    return '<span style="color: #999; font-size: 10px;">--</span>';
  }
  
  // Build summary parts
  const parts = [];
  
  // Position indicators (Start/End/Physical position)
  const posIndicator = [];
  if (ze.enableStart) posIndicator.push('D');
  if (ze.enableEnd) posIndicator.push('F');
  if (ze.mirrorOnReturn) posIndicator.push('📌');
  
  // Speed effect
  let effectLine = '';
  if (ze.speedEffect > 0) {
    const effectName = getSpeedEffectLabels()[ze.speedEffect] || 'Eff';
    const curveName = SPEED_CURVE_LABELS[ze.speedCurve] || '';
    effectLine = `${effectName} ${curveName} ${ze.speedIntensity}%`;
  }
  
  // Random turnback indicator
  const turnbackIndicator = ze.randomTurnbackEnabled ? `🔄${ze.turnbackChance || 30}%` : '';
  
  // End pause indicator
  const pauseIndicator = ze.endPauseEnabled ? '⏸' : '';
  
  // Build HTML
  let html = `<div style="font-size: 10px; line-height: 1.3;">`;
  html += `<div style="color: #4CAF50; font-weight: bold;">${posIndicator.join('/')} ${ze.zoneMM}mm</div>`;
  if (effectLine) {
    html += `<div>${effectLine}</div>`;
  }
  if (turnbackIndicator || pauseIndicator) {
    html += `<div>${turnbackIndicator} ${pauseIndicator}</div>`;
  }
  html += `</div>`;
  
  return html;
}

// ============================================================================
// SPEED DISPLAY
// ============================================================================

/**
 * Get speed display HTML for a sequence line
 * @param {Object} line - Sequence line object
 * @param {number} movementType - Movement type
 * @returns {string} HTML string for speed display
 */
function getSpeedsDisplay(line, movementType) {
  if (movementType === MOVEMENT_TYPE.VA_ET_VIENT) {
    return `<div style="font-size: 11px; line-height: 1.3;">
      <div style="color: #2196F3; font-weight: bold;">↗${line.speedForward.toFixed(1)}</div>
      <div style="color: #FF9800; font-weight: bold;">↙${line.speedBackward.toFixed(1)}</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    const peakSpeedMMPerSec = 2 * Math.PI * line.oscFrequencyHz * line.oscAmplitudeMM;
    return `<div style="font-size: 11px; font-weight: bold; color: #9C27B0;">${peakSpeedMMPerSec.toFixed(0)} mm/s</div>`;
  } else if (movementType === MOVEMENT_TYPE.CHAOS) {
    return `<div style="font-size: 11px; font-weight: bold; color: #E91E63;">${line.chaosMaxSpeedLevel ? line.chaosMaxSpeedLevel.toFixed(1) : '10.0'}</div>`;
  }
  return '<span style="color: #999;">--</span>';
}

// ============================================================================
// CYCLES & PAUSE DISPLAY
// ============================================================================

/**
 * Get cycles and pause display info for a sequence line
 * @param {Object} line - Sequence line object
 * @param {number} movementType - Movement type
 * @returns {Object} { cyclesDisplay, pauseDisplay, pauseColor, pauseWeight }
 */
function getCyclesPause(line, movementType) {
  let cyclesDisplay = line.cycleCount;
  if (movementType === MOVEMENT_TYPE.CHAOS) cyclesDisplay = '--';
  
  const pauseMs = line.pauseAfterMs || 0;
  const pauseDisplay = pauseMs > 0 ? (pauseMs / 1000).toFixed(1) + 's' : '--';
  const pauseColor = pauseMs > 0 ? '#9C27B0' : '#999';
  const pauseWeight = pauseMs > 0 ? 'bold' : 'normal';
  
  return { cyclesDisplay, pauseDisplay, pauseColor, pauseWeight };
}

// ============================================================================
// TEMPLATE GENERATION
// ============================================================================

/**
 * Get the default sequence template document with documentation
 * @returns {Object} Template document with TEMPLATE and DOCUMENTATION sections
 */
function getSequenceTemplateDoc() {
  return {
    TEMPLATE: {
      version: "2.0",
      lineCount: 1,
      lines: [{
        lineId: 1,
        enabled: true,
        movementType: 4,
        cycleCount: 1,
        pauseAfterMs: 1000,
        startPositionMM: 0,
        distanceMM: 100,
        speedForward: 5.0,
        speedBackward: 5.0,
        vaetZoneEffect: {
          enabled: false,
          enableStart: true,
          enableEnd: true,
          zoneMM: 50,
          speedEffect: 1,
          speedCurve: 1,
          speedIntensity: 75,
          randomTurnbackEnabled: false,
          turnbackChance: 30,
          endPauseEnabled: false,
          endPauseIsRandom: false,
          endPauseDurationSec: 1.0,
          endPauseMinSec: 0.5,
          endPauseMaxSec: 2.0
        },
        vaetCyclePauseEnabled: false,
        vaetCyclePauseIsRandom: false,
        vaetCyclePauseDurationSec: 0.0,
        vaetCyclePauseMinSec: 0.5,
        vaetCyclePauseMaxSec: 3.0,
        oscCenterPositionMM: 100.0,
        oscAmplitudeMM: 50.0,
        oscWaveform: 0,
        oscFrequencyHz: 0.1,
        oscEnableRampIn: false,
        oscEnableRampOut: false,
        oscRampInDurationMs: 1000.0,
        oscRampOutDurationMs: 1000.0,
        oscCyclePauseEnabled: false,
        oscCyclePauseIsRandom: false,
        oscCyclePauseDurationSec: 0.0,
        oscCyclePauseMinSec: 0.5,
        oscCyclePauseMaxSec: 3.0,
        chaosCenterPositionMM: 110.0,
        chaosAmplitudeMM: 50.0,
        chaosMaxSpeedLevel: 10.0,
        chaosCrazinessPercent: 50.0,
        chaosDurationSeconds: 30,
        chaosSeed: 0,
        chaosPatternsEnabled: [true,true,true,true,true,true,true,true,true,true,true]
      }]
    },
    DOCUMENTATION: {
      "Note": "Minimal template - See full documentation for more options"
    }
  };
}

// ============================================================================
// SEQUENCE LINE DEFAULTS BUILDER
// ============================================================================

// Validation constants (mirror backend Config.h / Validators.h)
const SEQ_LIMITS = Object.freeze({
  MAX_SPEED_LEVEL: 35.0,
  MIN_SPEED_LEVEL: 0.1,
  MAX_FREQUENCY_HZ: 10.0,
  MIN_FREQUENCY_HZ: 0.001,
  MAX_CRAZINESS: 100,
  MIN_CRAZINESS: 0,
  MAX_RAMP_MS: 60000,
  MAX_CYCLE_PAUSE_SEC: 300,
  MAX_CHAOS_DURATION_SEC: 3600,
  MAX_CYCLE_COUNT: 9999,
  CHAOS_PATTERN_COUNT: 11
});

/**
 * Build default values for a new sequence line
 * Pure function: no DOM, no AppState dependency
 * @param {number} effectiveMax - Effective max distance in mm
 * @returns {Object} Complete sequence line object with default values
 */
function buildSequenceLineDefaultsPure(effectiveMax) {
  const center = effectiveMax / 2;
  return {
    enabled: true,
    movementType: 0,
    startPositionMM: 0,
    distanceMM: Math.min(100, effectiveMax),
    speedForward: 5.0,
    speedBackward: 5.0,
    vaetZoneEffect: {
      enabled: false,
      enableStart: true,
      enableEnd: true,
      mirrorOnReturn: false,
      zoneMM: 50,
      speedEffect: 1,
      speedCurve: 1,
      speedIntensity: 75,
      randomTurnbackEnabled: false,
      turnbackChance: 30,
      endPauseEnabled: false,
      endPauseIsRandom: false,
      endPauseDurationSec: 1.0,
      endPauseMinSec: 0.5,
      endPauseMaxSec: 2.0
    },
    vaetCyclePauseEnabled: false,
    vaetCyclePauseIsRandom: false,
    vaetCyclePauseDurationSec: 0.0,
    vaetCyclePauseMinSec: 0.5,
    vaetCyclePauseMaxSec: 3.0,
    oscCenterPositionMM: center,
    oscAmplitudeMM: Math.min(50.0, center),
    oscWaveform: 0,
    oscFrequencyHz: 0.5,
    oscEnableRampIn: false,
    oscEnableRampOut: false,
    oscRampInDurationMs: 1000.0,
    oscRampOutDurationMs: 1000.0,
    oscCyclePauseEnabled: false,
    oscCyclePauseIsRandom: false,
    oscCyclePauseDurationSec: 0.0,
    oscCyclePauseMinSec: 0.5,
    oscCyclePauseMaxSec: 3.0,
    chaosCenterPositionMM: center,
    chaosAmplitudeMM: Math.min(50.0, center),
    chaosMaxSpeedLevel: 10.0,
    chaosCrazinessPercent: 50.0,
    chaosDurationSeconds: 30,
    chaosSeed: 0,
    chaosPatternsEnabled: Array(SEQ_LIMITS.CHAOS_PATTERN_COUNT).fill(true),
    cycleCount: 1,
    pauseAfterMs: 0
  };
}

// ============================================================================
// SEQUENCE LINE VALIDATION (mirrors backend Validators.h)
// ============================================================================

/**
 * Validate a sequence line's parameters
 * Pure function: mirrors backend Validators.h logic
 * @param {Object} line - Sequence line object
 * @param {number} movementType - Movement type (0=VAET, 1=OSC, 2=CHAOS, 4=CALIB)
 * @param {number} effectiveMax - Effective max distance in mm
 * @returns {Array<string>} Array of error messages (empty = valid)
 */
function validateSequencerLinePure(line, movementType, effectiveMax) {
  const errors = [];
  const L = SEQ_LIMITS;

  // === VA-ET-VIENT (type 0) ===
  if (movementType === MOVEMENT_TYPE.VA_ET_VIENT) {
    // Position range (mirrors Validators::position + Validators::motionRange)
    if (line.startPositionMM < 0) {
      errors.push('⚠️ ' + t('sequencer.startPosNegative'));
    } else if (effectiveMax > 0 && line.startPositionMM > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.startPosExceedsMax', { max: effectiveMax.toFixed(1) }));
    }

    if (line.distanceMM <= 0) {
      errors.push('⚠️ ' + t('sequencer.distanceMustBePositive'));
    } else if (effectiveMax > 0 && line.distanceMM > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.distanceExceedsMax', { max: effectiveMax.toFixed(1) }));
    }

    // Combined range check
    if (effectiveMax > 0 && (line.startPositionMM + line.distanceMM) > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.endPosExceedsMax', { end: (line.startPositionMM + line.distanceMM).toFixed(1), max: effectiveMax.toFixed(1) }));
    }

    // Speed validation (mirrors Validators::speed)
    if (line.speedForward < L.MIN_SPEED_LEVEL || line.speedForward > L.MAX_SPEED_LEVEL) {
      errors.push('⚠️ ' + t('sequencer.speedFwdOutOfRange', { min: L.MIN_SPEED_LEVEL, max: L.MAX_SPEED_LEVEL }));
    }
    if (line.speedBackward < L.MIN_SPEED_LEVEL || line.speedBackward > L.MAX_SPEED_LEVEL) {
      errors.push('⚠️ ' + t('sequencer.speedBwdOutOfRange', { min: L.MIN_SPEED_LEVEL, max: L.MAX_SPEED_LEVEL }));
    }

    // Zone effect validation
    if (line.vaetZoneEffect && line.vaetZoneEffect.enabled) {
      const ze = line.vaetZoneEffect;
      if (ze.zoneMM <= 0) {
        errors.push('⚠️ ' + t('sequencer.zoneMustBePositive'));
      } else if (ze.zoneMM > line.distanceMM / 2) {
        errors.push('⚠️ ' + t('sequencer.zoneTooLarge', { max: (line.distanceMM / 2).toFixed(1) }));
      }
      if (ze.speedIntensity < 0 || ze.speedIntensity > 100) {
        errors.push('⚠️ ' + t('sequencer.intensityOutOfRange'));
      }
      if (ze.randomTurnbackEnabled && (ze.turnbackChance < 0 || ze.turnbackChance > 100)) {
        errors.push('⚠️ ' + t('sequencer.turnbackChanceOutOfRange'));
      }
    }
  }

  // === OSCILLATION (type 1) ===
  if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    // Center position (mirrors Validators::oscillationParams)
    if (line.oscCenterPositionMM < 0) {
      errors.push('⚠️ ' + t('sequencer.oscCenterNegative'));
    } else if (effectiveMax > 0 && line.oscCenterPositionMM > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.oscCenterExceedsMax', { max: effectiveMax.toFixed(1) }));
    }

    // Amplitude
    if (line.oscAmplitudeMM <= 0) {
      errors.push('⚠️ ' + t('sequencer.oscAmplitudeMustBePositive'));
    }

    // Bounds: center ± amplitude within limits
    if (line.oscCenterPositionMM - line.oscAmplitudeMM < 0) {
      errors.push('⚠️ ' + t('sequencer.oscBoundsNegative'));
    }
    if (effectiveMax > 0 && (line.oscCenterPositionMM + line.oscAmplitudeMM) > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.oscBoundsExceedsMax', { max: effectiveMax.toFixed(1) }));
    }

    // Frequency
    if (line.oscFrequencyHz <= 0) {
      errors.push('⚠️ ' + t('sequencer.freqMustBePositive'));
    } else if (line.oscFrequencyHz > L.MAX_FREQUENCY_HZ) {
      errors.push('⚠️ ' + t('sequencer.freqTooHigh', { max: L.MAX_FREQUENCY_HZ }));
    }

    // Ramp durations (sanity check)
    if (line.oscEnableRampIn && (line.oscRampInDurationMs < 0 || line.oscRampInDurationMs > L.MAX_RAMP_MS)) {
      errors.push('⚠️ ' + t('sequencer.rampInOutOfRange'));
    }
    if (line.oscEnableRampOut && (line.oscRampOutDurationMs < 0 || line.oscRampOutDurationMs > L.MAX_RAMP_MS)) {
      errors.push('⚠️ ' + t('sequencer.rampOutOutOfRange'));
    }
  }

  // === CHAOS (type 2) ===
  if (movementType === MOVEMENT_TYPE.CHAOS) {
    // Center position (mirrors Validators::chaosParams)
    if (line.chaosCenterPositionMM < 0) {
      errors.push('⚠️ ' + t('sequencer.chaosCenterNegative'));
    } else if (effectiveMax > 0 && line.chaosCenterPositionMM > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.chaosCenterExceedsMax', { max: effectiveMax.toFixed(1) }));
    }

    // Amplitude
    if (line.chaosAmplitudeMM <= 0) {
      errors.push('⚠️ ' + t('sequencer.chaosAmplitudeMustBePositive'));
    }

    // Bounds: center ± amplitude within limits
    if (line.chaosCenterPositionMM - line.chaosAmplitudeMM < 0) {
      errors.push('⚠️ ' + t('sequencer.chaosBoundsNegative'));
    }
    if (effectiveMax > 0 && (line.chaosCenterPositionMM + line.chaosAmplitudeMM) > effectiveMax) {
      errors.push('⚠️ ' + t('sequencer.chaosBoundsExceedsMax', { max: effectiveMax.toFixed(1) }));
    }

    // Speed
    if (line.chaosMaxSpeedLevel < L.MIN_SPEED_LEVEL || line.chaosMaxSpeedLevel > L.MAX_SPEED_LEVEL) {
      errors.push('⚠️ ' + t('sequencer.chaosSpeedOutOfRange', { min: L.MIN_SPEED_LEVEL, max: L.MAX_SPEED_LEVEL }));
    }

    // Craziness
    if (line.chaosCrazinessPercent < L.MIN_CRAZINESS || line.chaosCrazinessPercent > L.MAX_CRAZINESS) {
      errors.push('⚠️ ' + t('sequencer.crazinessOutOfRange'));
    }

    // Duration
    if (line.chaosDurationSeconds <= 0) {
      errors.push('⚠️ ' + t('sequencer.durationMustBePositive'));
    } else if (line.chaosDurationSeconds > L.MAX_CHAOS_DURATION_SEC) {
      errors.push('⚠️ ' + t('sequencer.durationTooLong', { max: L.MAX_CHAOS_DURATION_SEC }));
    }
  }

  // === COMMON FIELDS (all types except calibration) ===
  if (movementType !== MOVEMENT_TYPE.CALIBRATION) {
    // Cycle count (not for chaos)
    if (movementType !== MOVEMENT_TYPE.CHAOS) {
      if (!Number.isInteger(line.cycleCount) || line.cycleCount < 1) {
        errors.push('⚠️ ' + t('sequencer.cyclesMustBePositiveInt'));
      } else if (line.cycleCount > L.MAX_CYCLE_COUNT) {
        errors.push('⚠️ ' + t('sequencer.cyclesTooMany', { max: L.MAX_CYCLE_COUNT }));
      }
    }

    // Pause after (must be >= 0)
    if (line.pauseAfterMs < 0) {
      errors.push('⚠️ ' + t('sequencer.pauseMustBePositive'));
    }
  }

  return errors;
}

// ============================================================================
// ERROR FIELD MAPPING (for form field highlighting)
// ============================================================================

// All editable field IDs in the edit modal
const ALL_EDIT_FIELDS = [
  'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack', 'editZoneMM',
  'editOscCenter', 'editOscAmplitude', 'editOscFrequency', 'editOscRampInDur', 'editOscRampOutDur',
  'editChaosCenter', 'editChaosAmplitude', 'editChaosSpeed', 'editChaosCraziness',
  'editChaosDuration', 'editChaosSeed', 'editCycles', 'editPause'
];

/**
 * Map validation errors + empty field errors to form field IDs for highlighting
 * @param {Object} line - Sequence line values
 * @param {number} movementType - Movement type
 * @param {number} effectiveMax - Effective max distance
 * @param {Array<string>} emptyFieldErrors - Errors from empty field checks
 * @returns {Array<string>} Array of field IDs that have errors
 */
function getAllInvalidFieldsPure(line, movementType, effectiveMax, emptyFieldErrors) {
  const fields = new Set();

  // Map empty field errors (from validateEditForm's text matching)
  const emptyText = emptyFieldErrors.join(' ');
  const emptyMap = {
    'startPos': 'editStartPos', 'distance': 'editDistance',
    'speedFwd': 'editSpeedFwd', 'speedBwd': 'editSpeedBack',
    'zone': 'editZoneMM',
    'oscCenter': 'editOscCenter', 'oscAmplitude': 'editOscAmplitude',
    'freq': 'editOscFrequency', 'rampIn': 'editOscRampInDur', 'rampOut': 'editOscRampOutDur',
    'chaosCenter': 'editChaosCenter', 'chaosAmplitude': 'editChaosAmplitude',
    'chaosSpeed': 'editChaosSpeed', 'craziness': 'editChaosCraziness',
    'duration': 'editChaosDuration', 'Seed': 'editChaosSeed',
    'cycles': 'editCycles', 'pause': 'editPause'
  };
  for (const [keyword, fieldId] of Object.entries(emptyMap)) {
    if (emptyText.toLowerCase().includes(keyword.toLowerCase())) {
      fields.add(fieldId);
    }
  }

  // Map validation errors from validateSequencerLinePure
  if (movementType === MOVEMENT_TYPE.VA_ET_VIENT) {
    if (line.startPositionMM < 0 || (effectiveMax > 0 && line.startPositionMM > effectiveMax)) fields.add('editStartPos');
    if (line.distanceMM <= 0 || (effectiveMax > 0 && line.distanceMM > effectiveMax)) fields.add('editDistance');
    if (effectiveMax > 0 && (line.startPositionMM + line.distanceMM) > effectiveMax) { fields.add('editStartPos'); fields.add('editDistance'); }
    if (line.speedForward < SEQ_LIMITS.MIN_SPEED_LEVEL || line.speedForward > SEQ_LIMITS.MAX_SPEED_LEVEL) fields.add('editSpeedFwd');
    if (line.speedBackward < SEQ_LIMITS.MIN_SPEED_LEVEL || line.speedBackward > SEQ_LIMITS.MAX_SPEED_LEVEL) fields.add('editSpeedBack');
    if (line.zoneMM !== undefined && line.zoneMM <= 0) fields.add('editZoneMM');
  }

  if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    if (line.oscCenterPositionMM < 0 || (effectiveMax > 0 && line.oscCenterPositionMM > effectiveMax)) fields.add('editOscCenter');
    if (line.oscAmplitudeMM <= 0) fields.add('editOscAmplitude');
    if (line.oscCenterPositionMM - line.oscAmplitudeMM < 0 || (effectiveMax > 0 && (line.oscCenterPositionMM + line.oscAmplitudeMM) > effectiveMax)) {
      fields.add('editOscCenter'); fields.add('editOscAmplitude');
    }
    if (line.oscFrequencyHz <= 0 || line.oscFrequencyHz > SEQ_LIMITS.MAX_FREQUENCY_HZ) fields.add('editOscFrequency');
  }

  if (movementType === MOVEMENT_TYPE.CHAOS) {
    if (line.chaosCenterPositionMM < 0 || (effectiveMax > 0 && line.chaosCenterPositionMM > effectiveMax)) fields.add('editChaosCenter');
    if (line.chaosAmplitudeMM <= 0) fields.add('editChaosAmplitude');
    if (line.chaosCenterPositionMM - line.chaosAmplitudeMM < 0 || (effectiveMax > 0 && (line.chaosCenterPositionMM + line.chaosAmplitudeMM) > effectiveMax)) {
      fields.add('editChaosCenter'); fields.add('editChaosAmplitude');
    }
    if (line.chaosMaxSpeedLevel < SEQ_LIMITS.MIN_SPEED_LEVEL || line.chaosMaxSpeedLevel > SEQ_LIMITS.MAX_SPEED_LEVEL) fields.add('editChaosSpeed');
    if (line.chaosCrazinessPercent < 0 || line.chaosCrazinessPercent > 100) fields.add('editChaosCraziness');
    if (line.chaosDurationSeconds <= 0) fields.add('editChaosDuration');
  }

  if (movementType !== MOVEMENT_TYPE.CALIBRATION && movementType !== MOVEMENT_TYPE.CHAOS) {
    if (!Number.isInteger(line.cycleCount) || line.cycleCount < 1) fields.add('editCycles');
  }
  if (line.pauseAfterMs < 0) fields.add('editPause');

  return Array.from(fields);
}

console.debug('✅ SequenceUtils.js loaded - Sequence display, validation & defaults ready');
