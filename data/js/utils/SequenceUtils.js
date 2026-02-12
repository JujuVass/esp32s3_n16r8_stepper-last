/**
 * SequenceUtils.js - Sequence Display Utilities Module
 * 
 * Pure utility functions for sequence table display:
 * - Type display helpers (icons, info text)
 * - Deceleration summary
 * - Speed display formatting
 * - Cycles/Pause display
 * - Template generation
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
const SPEED_EFFECT_LABELS = ['', 'Décel', 'Accel'];

// Legacy alias for backward compatibility
const DECEL_MODE_LABELS = SPEED_CURVE_LABELS;

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
    typeName = 'Va-et-vient';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>${line.startPositionMM.toFixed(1)}mm</div>
      <div>±${line.distanceMM.toFixed(1)}mm</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    typeIcon = '〰️';
    typeName = 'Oscillation';
    const waveformName = WAVEFORM_SHORT[line.oscWaveform] || '?';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>C:${line.oscCenterPositionMM ? line.oscCenterPositionMM.toFixed(0) : '100'}mm</div>
      <div>A:±${line.oscAmplitudeMM ? line.oscAmplitudeMM.toFixed(0) : '50'}mm</div>
      <div>${waveformName} ${line.oscFrequencyHz ? line.oscFrequencyHz.toFixed(2) : '0.5'}Hz</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.CHAOS) {
    typeIcon = '🌀';
    typeName = 'Chaos';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>⏱️${line.chaosDurationSeconds || 30}s</div>
      <div>🎲${line.chaosCrazinessPercent ? line.chaosCrazinessPercent.toFixed(0) : '50'}%</div>
    </div>`;
  } else if (movementType === MOVEMENT_TYPE.CALIBRATION) {
    typeIcon = '📏';
    typeName = 'Calibration';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>Calibration</div>
      <div>complète</div>
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
  let ze = line.vaetZoneEffect;
  if (!ze) {
    // Legacy format - convert
    ze = {
      enabled: line.decelStartEnabled || line.decelEndEnabled,
      enableStart: line.decelStartEnabled ?? false,
      enableEnd: line.decelEndEnabled ?? false,
      zoneMM: line.decelZoneMM || 50,
      speedEffect: 1,  // DECEL
      speedCurve: line.decelMode || 1,
      speedIntensity: line.decelEffectPercent || 75,
      randomTurnbackEnabled: false,
      endPauseEnabled: false
    };
  }
  
  if (!ze.enabled) {
    return '<span style="color: #999; font-size: 10px;">--</span>';
  }
  
  // Build summary parts
  const parts = [];
  
  // Position indicators (Début/Fin/Miroir)
  const posIndicator = [];
  if (ze.enableStart) posIndicator.push('D');
  if (ze.enableEnd) posIndicator.push('F');
  if (ze.mirrorOnReturn) posIndicator.push('🔀');
  
  // Speed effect
  let effectLine = '';
  if (ze.speedEffect > 0) {
    const effectName = SPEED_EFFECT_LABELS[ze.speedEffect] || 'Eff';
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
  // Check if pure function exists (from app.js)
  if (typeof getSequenceTemplateDocPure === 'function') {
    return getSequenceTemplateDocPure();
  }
  
  // Fallback minimal template
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
      "Note": "Template minimal - Voir documentation complète pour plus d'options"
    }
  };
}

// ============================================================================
// MOVEMENT TYPE HELPERS
// ============================================================================

/**
 * Get movement type name from type number
 * @param {number} movementType - Movement type number
 * @returns {string} Movement type name
 */
function getMovementTypeName(movementType) {
  const names = {
    [MOVEMENT_TYPE.VA_ET_VIENT]: 'Va-et-vient',
    [MOVEMENT_TYPE.OSCILLATION]: 'Oscillation',
    [MOVEMENT_TYPE.CHAOS]: 'Chaos',
    [MOVEMENT_TYPE.CALIBRATION]: 'Calibration'
  };
  return names[movementType] || 'Inconnu';
}

/**
 * Get movement type icon from type number
 * @param {number} movementType - Movement type number
 * @returns {string} Movement type icon emoji
 */
function getMovementTypeIcon(movementType) {
  const icons = {
    [MOVEMENT_TYPE.VA_ET_VIENT]: '↔️',
    [MOVEMENT_TYPE.OSCILLATION]: '〰️',
    [MOVEMENT_TYPE.CHAOS]: '🌀',
    [MOVEMENT_TYPE.CALIBRATION]: '📏'
  };
  return icons[movementType] || '❓';
}

/**
 * Check if a movement type supports deceleration zones
 * @param {number} movementType - Movement type number
 * @returns {boolean} True if decel zones are supported
 */
function supportsDecelZone(movementType) {
  return movementType === MOVEMENT_TYPE.VA_ET_VIENT;
}

/**
 * Check if a movement type has cycle count
 * @param {number} movementType - Movement type number
 * @returns {boolean} True if cycle count applies
 */
function hasCycleCount(movementType) {
  return movementType !== MOVEMENT_TYPE.CHAOS;
}
