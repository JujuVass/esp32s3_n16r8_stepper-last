/**
 * sequencer.js - Sequencer Module for ESP32 Stepper Controller
 * 
 * Contains pure functions for sequence validation and management.
 * These functions have NO side effects (no DOM, no global state mutation).
 * 
 * Pattern: Pure functions are suffixed with "Pure" and can be unit tested.
 */

// ============================================================================
// CONSTANTS
// ============================================================================
const SEQUENCER_LIMITS = {
  // VA-ET-VIENT
  SPEED_MIN: 0,
  SPEED_MAX: 20,
  DECEL_ZONE_MIN: 10,
  DECEL_ZONE_MAX: 200,
  
  // OSCILLATION
  FREQUENCY_MIN: 0.01,
  FREQUENCY_MAX: 10,
  RAMP_DURATION_MIN: 100,
  RAMP_DURATION_MAX: 10000,
  
  // CHAOS
  CHAOS_SPEED_MIN: 1,
  CHAOS_SPEED_MAX: 20,
  CHAOS_CRAZINESS_MIN: 0,
  CHAOS_CRAZINESS_MAX: 100,
  CHAOS_DURATION_MIN: 5,
  CHAOS_DURATION_MAX: 600,
  CHAOS_SEED_MIN: 0,
  CHAOS_SEED_MAX: 9999999,
  
  // COMMON
  CYCLE_COUNT_MIN: 1,
  CYCLE_COUNT_MAX: 1000,
  PAUSE_MIN: 0,
  PAUSE_MAX: 60000
};

// Movement type constants
const MOVEMENT_TYPE = {
  VAET: 0,
  OSCILLATION: 1,
  CHAOS: 2,
  CALIBRATION: 4
};

// ============================================================================
// PURE VALIDATION FUNCTIONS
// ============================================================================

/**
 * Validate a sequencer line configuration (PURE FUNCTION)
 * @param {Object} line - The line configuration object
 * @param {number} movementType - 0=VAET, 1=OSC, 2=CHAOS, 4=CALIBRATION
 * @param {number} effectiveMax - Maximum travel distance in mm
 * @returns {string[]} Array of error messages (empty if valid)
 */
function validateSequencerLinePure(line, movementType, effectiveMax) {
  const errors = [];
  
  if (movementType === MOVEMENT_TYPE.VAET) {
    // VA-ET-VIENT validation
    const endPosition = line.startPositionMM + line.distanceMM;
    
    // Position départ
    if (line.startPositionMM < 0) {
      errors.push('⚠️ Position de départ ne peut pas être négative');
    }
    if (line.startPositionMM > effectiveMax) {
      errors.push(`⚠️ Position de départ (${line.startPositionMM.toFixed(1)}mm) dépasse la course disponible (${effectiveMax.toFixed(1)}mm)`);
    }
    
    // Distance
    if (line.distanceMM <= 0) {
      errors.push('⚠️ Distance doit être positive');
    }
    if (endPosition > effectiveMax) {
      errors.push(`⚠️ Position finale (${endPosition.toFixed(1)}mm) dépasse la course disponible (${effectiveMax.toFixed(1)}mm)`);
    }
    
    // Vitesses
    if (line.speedForward < SEQUENCER_LIMITS.SPEED_MIN || line.speedForward > SEQUENCER_LIMITS.SPEED_MAX) {
      errors.push(`⚠️ Vitesse aller doit être entre ${SEQUENCER_LIMITS.SPEED_MIN} et ${SEQUENCER_LIMITS.SPEED_MAX}`);
    }
    if (line.speedBackward < SEQUENCER_LIMITS.SPEED_MIN || line.speedBackward > SEQUENCER_LIMITS.SPEED_MAX) {
      errors.push(`⚠️ Vitesse retour doit être entre ${SEQUENCER_LIMITS.SPEED_MIN} et ${SEQUENCER_LIMITS.SPEED_MAX}`);
    }
    
    // Zone décélération
    if (line.decelZoneMM < SEQUENCER_LIMITS.DECEL_ZONE_MIN || line.decelZoneMM > SEQUENCER_LIMITS.DECEL_ZONE_MAX) {
      errors.push(`⚠️ Zone de décélération doit être entre ${SEQUENCER_LIMITS.DECEL_ZONE_MIN} et ${SEQUENCER_LIMITS.DECEL_ZONE_MAX} mm`);
    }
    
  } else if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    // OSCILLATION validation
    const minPos = line.oscCenterPositionMM - line.oscAmplitudeMM;
    const maxPos = line.oscCenterPositionMM + line.oscAmplitudeMM;
    
    // Centre & Amplitude
    if (minPos < 0) {
      errors.push(`⚠️ Position min (${minPos.toFixed(1)}mm) est négative`);
    }
    if (maxPos > effectiveMax) {
      errors.push(`⚠️ Position max (${maxPos.toFixed(1)}mm) dépasse la course disponible (${effectiveMax.toFixed(1)}mm)`);
    }
    if (line.oscAmplitudeMM <= 0) {
      errors.push('⚠️ Amplitude doit être positive');
    }
    
    // Fréquence
    if (line.oscFrequencyHz < SEQUENCER_LIMITS.FREQUENCY_MIN || line.oscFrequencyHz > SEQUENCER_LIMITS.FREQUENCY_MAX) {
      errors.push(`⚠️ Fréquence doit être entre ${SEQUENCER_LIMITS.FREQUENCY_MIN} et ${SEQUENCER_LIMITS.FREQUENCY_MAX} Hz`);
    }
    
    // Durée rampes
    if (line.oscRampInDurationMs < SEQUENCER_LIMITS.RAMP_DURATION_MIN || line.oscRampInDurationMs > SEQUENCER_LIMITS.RAMP_DURATION_MAX) {
      errors.push(`⚠️ Durée rampe IN doit être entre ${SEQUENCER_LIMITS.RAMP_DURATION_MIN} et ${SEQUENCER_LIMITS.RAMP_DURATION_MAX} ms`);
    }
    if (line.oscRampOutDurationMs < SEQUENCER_LIMITS.RAMP_DURATION_MIN || line.oscRampOutDurationMs > SEQUENCER_LIMITS.RAMP_DURATION_MAX) {
      errors.push(`⚠️ Durée rampe OUT doit être entre ${SEQUENCER_LIMITS.RAMP_DURATION_MIN} et ${SEQUENCER_LIMITS.RAMP_DURATION_MAX} ms`);
    }
    
  } else if (movementType === MOVEMENT_TYPE.CHAOS) {
    // CHAOS validation
    const minPos = line.chaosCenterPositionMM - line.chaosAmplitudeMM;
    const maxPos = line.chaosCenterPositionMM + line.chaosAmplitudeMM;
    
    // Centre & Amplitude
    if (minPos < 0) {
      errors.push(`⚠️ Position min (${minPos.toFixed(1)}mm) est négative`);
    }
    if (maxPos > effectiveMax) {
      errors.push(`⚠️ Position max (${maxPos.toFixed(1)}mm) dépasse la course disponible (${effectiveMax.toFixed(1)}mm)`);
    }
    if (line.chaosAmplitudeMM <= 0) {
      errors.push('⚠️ Amplitude doit être positive');
    }
    
    // Vitesse max
    if (line.chaosMaxSpeedLevel < SEQUENCER_LIMITS.CHAOS_SPEED_MIN || line.chaosMaxSpeedLevel > SEQUENCER_LIMITS.CHAOS_SPEED_MAX) {
      errors.push(`⚠️ Vitesse max doit être entre ${SEQUENCER_LIMITS.CHAOS_SPEED_MIN} et ${SEQUENCER_LIMITS.CHAOS_SPEED_MAX}`);
    }
    
    // Degré de folie
    if (line.chaosCrazinessPercent < SEQUENCER_LIMITS.CHAOS_CRAZINESS_MIN || line.chaosCrazinessPercent > SEQUENCER_LIMITS.CHAOS_CRAZINESS_MAX) {
      errors.push(`⚠️ Degré de folie doit être entre ${SEQUENCER_LIMITS.CHAOS_CRAZINESS_MIN} et ${SEQUENCER_LIMITS.CHAOS_CRAZINESS_MAX}%`);
    }
    
    // Durée
    if (line.chaosDurationSeconds < SEQUENCER_LIMITS.CHAOS_DURATION_MIN || line.chaosDurationSeconds > SEQUENCER_LIMITS.CHAOS_DURATION_MAX) {
      errors.push(`⚠️ Durée doit être entre ${SEQUENCER_LIMITS.CHAOS_DURATION_MIN} et ${SEQUENCER_LIMITS.CHAOS_DURATION_MAX} secondes`);
    }
    
    // Seed
    if (line.chaosSeed < SEQUENCER_LIMITS.CHAOS_SEED_MIN || line.chaosSeed > SEQUENCER_LIMITS.CHAOS_SEED_MAX) {
      errors.push(`⚠️ Seed doit être entre ${SEQUENCER_LIMITS.CHAOS_SEED_MIN} et ${SEQUENCER_LIMITS.CHAOS_SEED_MAX}`);
    }
    
  } else if (movementType === MOVEMENT_TYPE.CALIBRATION) {
    // CALIBRATION - no validation needed (no parameters)
  }
  
  // Validation commune (Cycles & Pause)
  if (movementType !== MOVEMENT_TYPE.CHAOS && movementType !== MOVEMENT_TYPE.CALIBRATION) {
    // CHAOS uses duration, CALIBRATION forces 1 cycle
    if (line.cycleCount < SEQUENCER_LIMITS.CYCLE_COUNT_MIN || line.cycleCount > SEQUENCER_LIMITS.CYCLE_COUNT_MAX) {
      errors.push(`⚠️ Nombre de cycles doit être entre ${SEQUENCER_LIMITS.CYCLE_COUNT_MIN} et ${SEQUENCER_LIMITS.CYCLE_COUNT_MAX}`);
    }
  }
  
  if (line.pauseAfterMs < SEQUENCER_LIMITS.PAUSE_MIN || line.pauseAfterMs > SEQUENCER_LIMITS.PAUSE_MAX) {
    errors.push(`⚠️ Pause doit être entre ${SEQUENCER_LIMITS.PAUSE_MIN / 1000} et ${SEQUENCER_LIMITS.PAUSE_MAX / 1000} secondes`);
  }
  
  return errors;
}

/**
 * Build default values for a new sequence line (PURE FUNCTION)
 * @param {number} effectiveMax - Maximum travel distance in mm
 * @returns {Object} Default line configuration
 */
function buildSequenceLineDefaultsPure(effectiveMax) {
  const center = effectiveMax / 2;
  
  return {
    enabled: true,
    movementType: MOVEMENT_TYPE.VAET,
    
    // VA-ET-VIENT fields
    startPositionMM: 0,
    distanceMM: Math.min(100, effectiveMax),
    speedForward: 5.0,
    speedBackward: 5.0,
    decelStartEnabled: false,
    decelEndEnabled: true,
    decelZoneMM: 20,
    decelEffectPercent: 50,
    decelMode: 1,
    
    // VA-ET-VIENT cycle pause
    vaetCyclePauseEnabled: false,
    vaetCyclePauseIsRandom: false,
    vaetCyclePauseDurationSec: 0.0,
    vaetCyclePauseMinSec: 0.5,
    vaetCyclePauseMaxSec: 3.0,
    
    // OSCILLATION fields
    oscCenterPositionMM: center,
    oscAmplitudeMM: Math.min(50.0, center),
    oscWaveform: 0,  // SINE
    oscFrequencyHz: 0.5,
    oscEnableRampIn: false,
    oscEnableRampOut: false,
    oscRampInDurationMs: 1000.0,
    oscRampOutDurationMs: 1000.0,
    
    // OSCILLATION cycle pause
    oscCyclePauseEnabled: false,
    oscCyclePauseIsRandom: false,
    oscCyclePauseDurationSec: 0.0,
    oscCyclePauseMinSec: 0.5,
    oscCyclePauseMaxSec: 3.0,
    
    // CHAOS fields
    chaosCenterPositionMM: center,
    chaosAmplitudeMM: Math.min(50.0, center),
    chaosMaxSpeedLevel: 10.0,
    chaosCrazinessPercent: 50.0,
    chaosDurationSeconds: 30,
    chaosSeed: 0,
    chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true],
    
    // COMMON fields
    cycleCount: 1,
    pauseAfterMs: 0
  };
}

/**
 * Build a sequencer line from a playlist preset (PURE FUNCTION)
 * Merges preset config with default values for missing fields.
 * 
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @param {Object} config - Preset configuration from playlist
 * @param {number} effectiveMax - Maximum travel distance in mm
 * @returns {Object} Complete sequencer line ready to be validated and sent
 */
function buildSequenceLineFromPresetPure(mode, config, effectiveMax) {
  const center = effectiveMax / 2;
  
  // Base line with common fields
  const baseLine = {
    enabled: true,
    cycleCount: 1,
    pauseAfterMs: 0
  };
  
  // Default VA-ET-VIENT fields
  const defaultVaet = {
    startPositionMM: 0,
    distanceMM: Math.min(100, effectiveMax),
    speedForward: 5,
    speedBackward: 5,
    decelStartEnabled: false,
    decelEndEnabled: true,
    decelZoneMM: 20,
    decelEffectPercent: 50,
    decelMode: 1,
    vaetCyclePauseEnabled: false,
    vaetCyclePauseIsRandom: false,
    vaetCyclePauseDurationSec: 0.0,
    vaetCyclePauseMinSec: 0.5,
    vaetCyclePauseMaxSec: 3.0
  };
  
  // Default OSCILLATION fields
  const defaultOsc = {
    oscCenterPositionMM: center,
    oscAmplitudeMM: Math.min(50, center),
    oscWaveform: 0,
    oscFrequencyHz: 0.5,
    oscEnableRampIn: false,
    oscEnableRampOut: false,
    oscRampInDurationMs: 1000,
    oscRampOutDurationMs: 1000,
    oscCyclePauseEnabled: false,
    oscCyclePauseIsRandom: false,
    oscCyclePauseDurationSec: 0.0,
    oscCyclePauseMinSec: 0.5,
    oscCyclePauseMaxSec: 3.0
  };
  
  // Default CHAOS fields
  const defaultChaos = {
    chaosCenterPositionMM: center,
    chaosAmplitudeMM: Math.min(50, center),
    chaosMaxSpeedLevel: 10,
    chaosCrazinessPercent: 50,
    chaosDurationSeconds: 30,
    chaosSeed: 0,
    chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true]
  };
  
  let newLine = { ...baseLine, ...defaultVaet, ...defaultOsc, ...defaultChaos };
  
  if (mode === 'simple') {
    newLine.movementType = MOVEMENT_TYPE.VAET;
    // Override with preset values
    newLine.startPositionMM = config.startPositionMM || 0;
    newLine.distanceMM = config.distanceMM || 50;
    newLine.speedForward = config.speedLevelForward || 5;
    newLine.speedBackward = config.speedLevelBackward || 5;
    newLine.decelStartEnabled = config.decelStartEnabled || false;
    newLine.decelEndEnabled = config.decelEndEnabled !== undefined ? config.decelEndEnabled : true;
    newLine.decelZoneMM = config.decelZoneMM || 20;
    newLine.decelEffectPercent = config.decelEffectPercent || 50;
    newLine.decelMode = config.decelMode || 1;
    // VA-ET-VIENT Cycle Pause from preset
    newLine.vaetCyclePauseEnabled = config.cyclePauseEnabled || false;
    newLine.vaetCyclePauseIsRandom = config.cyclePauseIsRandom || false;
    newLine.vaetCyclePauseDurationSec = config.cyclePauseDurationSec || 0.0;
    newLine.vaetCyclePauseMinSec = config.cyclePauseMinSec || 0.5;
    newLine.vaetCyclePauseMaxSec = config.cyclePauseMaxSec || 3.0;
    
  } else if (mode === 'oscillation') {
    newLine.movementType = MOVEMENT_TYPE.OSCILLATION;
    // Override with preset values
    newLine.oscCenterPositionMM = config.centerPositionMM || center;
    newLine.oscAmplitudeMM = config.amplitudeMM || 50;
    newLine.oscWaveform = config.waveform || 0;
    newLine.oscFrequencyHz = config.frequencyHz || 0.5;
    newLine.oscEnableRampIn = config.enableRampIn || false;
    newLine.oscEnableRampOut = config.enableRampOut || false;
    newLine.oscRampInDurationMs = config.rampInDurationMs || 1000;
    newLine.oscRampOutDurationMs = config.rampOutDurationMs || 1000;
    newLine.cycleCount = config.cycleCount || 1;
    // OSCILLATION Cycle Pause from preset
    newLine.oscCyclePauseEnabled = config.cyclePauseEnabled || false;
    newLine.oscCyclePauseIsRandom = config.cyclePauseIsRandom || false;
    newLine.oscCyclePauseDurationSec = config.cyclePauseDurationSec || 0.0;
    newLine.oscCyclePauseMinSec = config.cyclePauseMinSec || 0.5;
    newLine.oscCyclePauseMaxSec = config.cyclePauseMaxSec || 3.0;
    
  } else if (mode === 'chaos') {
    newLine.movementType = MOVEMENT_TYPE.CHAOS;
    // Override with preset values
    newLine.chaosCenterPositionMM = config.centerPositionMM || center;
    newLine.chaosAmplitudeMM = config.amplitudeMM || 50;
    newLine.chaosMaxSpeedLevel = config.maxSpeedLevel || 10;
    newLine.chaosCrazinessPercent = config.crazinessPercent || 50;
    newLine.chaosDurationSeconds = config.durationSeconds || 30;
    newLine.chaosSeed = config.seed || 0;
    newLine.chaosPatternsEnabled = config.patternsEnabled || [true, true, true, true, true, true, true, true, true, true, true];
  }
  
  return newLine;
}

// ============================================================================
// VA-ET-VIENT TOOLTIP HELPER (PURE FUNCTION)
// ============================================================================

const DECEL_MODES = ['Linéaire', 'Sinusoïdal', 'Triangle⁻¹', 'Sine⁻¹'];

/**
 * Generate VA-ET-VIENT tooltip content (PURE FUNCTION)
 * @param {Object} line - Sequence line configuration
 * @returns {string} Formatted tooltip string
 */
function generateVaetTooltipPure(line) {
  const decelInfo = [];
  if (line.decelStartEnabled) decelInfo.push('Départ');
  if (line.decelEndEnabled) decelInfo.push('Fin');
  
  // Cycle pause info
  let cyclePauseInfo = 'Aucune';
  if (line.vaetCyclePauseEnabled) {
    if (line.vaetCyclePauseIsRandom) {
      cyclePauseInfo = `${line.vaetCyclePauseMinSec}s-${line.vaetCyclePauseMaxSec}s (aléatoire)`;
    } else {
      cyclePauseInfo = `${line.vaetCyclePauseDurationSec}s (fixe)`;
    }
  }
  
  return `🔄 VA-ET-VIENT
━━━━━━━━━━━━━━━━━━━━━━
📍 Départ: ${line.startPositionMM}mm
📏 Distance: ${line.distanceMM}mm
➡️ Vitesse aller: ${line.speedForward.toFixed(1)}/20
⬅️ Vitesse retour: ${line.speedBackward.toFixed(1)}/20
${decelInfo.length > 0 ? '🛑 Décel: ' + decelInfo.join(' + ') + ' (' + line.decelZoneMM + 'mm, ' + line.decelEffectPercent + '%, ' + DECEL_MODES[line.decelMode] + ')' : '🛑 Décel: Aucune'}
🔄 Cycles: ${line.cycleCount}
⏸️ Pause/cycle: ${cyclePauseInfo}
⏱️ Pause après: ${line.pauseAfterMs > 0 ? (line.pauseAfterMs / 1000).toFixed(1) + 's' : 'Aucune'}`;
}

/**
 * Generate CALIBRATION tooltip content (PURE FUNCTION)
 * @returns {string} Formatted tooltip string
 */
function generateCalibrationTooltipPure() {
  return `📏 CALIBRATION
━━━━━━━━━━━━━━━━━━━━━━
Recalibration complète du système
Réinitialise la position à 0mm
Détecte la limite physique`;
}

/**
 * Generate tooltip content for any sequence line (PURE FUNCTION)
 * Delegates to mode-specific tooltip generators
 * @param {Object} line - Sequence line configuration
 * @returns {string} Formatted tooltip string
 */
function generateSequenceLineTooltipPure(line) {
  const movementType = line.movementType !== undefined ? line.movementType : MOVEMENT_TYPE.VAET;
  
  if (movementType === MOVEMENT_TYPE.VAET) {
    return generateVaetTooltipPure(line);
  } else if (movementType === MOVEMENT_TYPE.OSCILLATION) {
    // Delegate to oscillation.js if available
    if (typeof generateOscillationTooltipPure === 'function') {
      return generateOscillationTooltipPure(line);
    }
    return '〰️ OSCILLATION (module not loaded)';
  } else if (movementType === MOVEMENT_TYPE.CHAOS) {
    // Delegate to chaos.js if available
    if (typeof generateChaosTooltipPure === 'function') {
      return generateChaosTooltipPure(line);
    }
    return '🌀 CHAOS (module not loaded)';
  } else if (movementType === MOVEMENT_TYPE.CALIBRATION) {
    return generateCalibrationTooltipPure();
  }
  
  return 'Ligne de séquence';
}

// ============================================================================
// LINE DISPLAY HELPERS (PURE FUNCTIONS)
// ============================================================================

/**
 * Get movement type display info (PURE FUNCTION)
 * @param {number} movementType - Movement type code
 * @param {Object} line - Line data
 * @returns {Object} { icon, name, info }
 */
function getMovementTypeDisplayPure(movementType, line) {
  const WAVEFORM_NAMES_SHORT = ['SIN', 'TRI', 'SQR'];
  
  switch (movementType) {
    case MOVEMENT_TYPE.VAET:
      return {
        icon: '🔄',
        name: 'Va-et-vient',
        info: `${line.startPositionMM?.toFixed(1) || 0}mm ±${line.distanceMM?.toFixed(1) || 50}mm`
      };
      
    case MOVEMENT_TYPE.OSCILLATION:
      const waveformName = WAVEFORM_NAMES_SHORT[line.oscWaveform] || 'SIN';
      return {
        icon: '〰️',
        name: 'Oscillation',
        info: `C:${line.oscCenterPositionMM?.toFixed(0) || 100}mm A:±${line.oscAmplitudeMM?.toFixed(0) || 50}mm ${waveformName} ${line.oscFrequencyHz?.toFixed(2) || 0.5}Hz`
      };
      
    case MOVEMENT_TYPE.CHAOS:
      return {
        icon: '🌀',
        name: 'Chaos',
        info: `⏱️${line.chaosDurationSeconds || 30}s 🎲${line.chaosCrazinessPercent?.toFixed(0) || 50}%`
      };
      
    case MOVEMENT_TYPE.CALIBRATION:
      return {
        icon: '📏',
        name: 'Calibration',
        info: 'Calibration complète'
      };
      
    default:
      return {
        icon: '❓',
        name: 'Inconnu',
        info: `Type ${movementType}`
      };
  }
}

/**
 * Get deceleration summary for a line (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} movementType - Movement type
 * @returns {Object} { enabled, parts, zoneMM, effectPercent, modeName }
 */
function getDecelSummaryPure(line, movementType) {
  const MODE_LABELS = ['Lin', 'Sin', 'Tri⁻¹', 'Sin⁻¹'];
  
  // Only for VAET
  if (movementType !== MOVEMENT_TYPE.VAET) {
    return { enabled: false };
  }
  
  if (!line.decelStartEnabled && !line.decelEndEnabled) {
    return { enabled: false };
  }
  
  const parts = [];
  if (line.decelStartEnabled) parts.push('D');
  if (line.decelEndEnabled) parts.push('F');
  
  return {
    enabled: true,
    parts: parts,
    partsText: parts.join('/'),
    zoneMM: line.decelZoneMM || 20,
    effectPercent: line.decelEffectPercent || 50,
    modeName: MODE_LABELS[line.decelMode] || 'Lin'
  };
}

/**
 * Get speed display for a line (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} movementType - Movement type
 * @returns {Object} { type, forward, backward, peakSpeed }
 */
function getLineSpeedsDisplayPure(line, movementType) {
  switch (movementType) {
    case MOVEMENT_TYPE.VAET:
      return {
        type: 'bidirectional',
        forward: line.speedForward?.toFixed(1) || '5.0',
        backward: line.speedBackward?.toFixed(1) || '5.0'
      };
      
    case MOVEMENT_TYPE.OSCILLATION:
      // Peak speed = 2π × f × A
      const freq = line.oscFrequencyHz || 0.5;
      const amp = line.oscAmplitudeMM || 50;
      const peakSpeed = 2 * Math.PI * freq * amp;
      return {
        type: 'peak',
        peakSpeedMMPerSec: peakSpeed,
        peakSpeedDisplay: `${peakSpeed.toFixed(0)} mm/s`
      };
      
    case MOVEMENT_TYPE.CHAOS:
      return {
        type: 'max',
        maxSpeed: line.chaosMaxSpeedLevel || 15,
        maxSpeedDisplay: `Max: ${line.chaosMaxSpeedLevel || 15}/20`
      };
      
    case MOVEMENT_TYPE.CALIBRATION:
      return {
        type: 'fixed',
        display: 'Auto'
      };
      
    default:
      return { type: 'unknown' };
  }
}

/**
 * Get cycles and pause display for a line (PURE FUNCTION)
 * @param {Object} line - Line data
 * @param {number} movementType - Movement type
 * @returns {Object} { cycles, pauseMs, pauseSec }
 */
function getLineCyclesPausePure(line, movementType) {
  // Calibration doesn't have cycles
  if (movementType === MOVEMENT_TYPE.CALIBRATION) {
    return {
      cycles: 1,
      cyclesDisplay: '1',
      pauseMs: 0,
      pauseSec: 0,
      pauseDisplay: '--'
    };
  }
  
  // Chaos uses duration, not cycles
  if (movementType === MOVEMENT_TYPE.CHAOS) {
    return {
      cycles: 1,
      cyclesDisplay: `${line.chaosDurationSeconds || 30}s`,
      pauseMs: line.pauseAfterMs || 0,
      pauseSec: (line.pauseAfterMs || 0) / 1000,
      pauseDisplay: line.pauseAfterMs ? `${(line.pauseAfterMs / 1000).toFixed(1)}s` : '--'
    };
  }
  
  const cycles = line.cycleCount || 1;
  const pauseMs = line.pauseAfterMs || 0;
  
  return {
    cycles: cycles,
    cyclesDisplay: cycles === 0 ? '∞' : String(cycles),
    pauseMs: pauseMs,
    pauseSec: pauseMs / 1000,
    pauseDisplay: pauseMs > 0 ? `${(pauseMs / 1000).toFixed(1)}s` : '--'
  };
}

// ============================================================================
// SEQUENCE TEMPLATE DATA (for downloadTemplate)
// ============================================================================

/**
 * Example sequence template with all movement types
 * Used by downloadTemplate() in main.js
 */
const SEQUENCE_TEMPLATE = {
  version: "2.0",
  lineCount: 5,
  lines: [
    {
      lineId: 1,
      enabled: true,
      movementType: 4,  // CALIBRATION (toujours en premier!)
      cycleCount: 1,
      pauseAfterMs: 1000,
      startPositionMM: 0,
      distanceMM: 100,
      speedForward: 5.0,
      speedBackward: 5.0,
      decelStartEnabled: false,
      decelEndEnabled: false,
      decelZoneMM: 50.0,
      decelEffectPercent: 50.0,
      decelMode: 0,
      oscCenterPositionMM: 100.0,
      oscAmplitudeMM: 50.0,
      oscWaveform: 0,
      oscFrequencyHz: 0.1,
      oscEnableRampIn: false,
      oscEnableRampOut: false,
      oscRampInDurationMs: 1000.0,
      oscRampOutDurationMs: 1000.0,
      chaosCenterPositionMM: 110.0,
      chaosAmplitudeMM: 50.0,
      chaosMaxSpeedLevel: 10.0,
      chaosCrazinessPercent: 50.0,
      chaosDurationSeconds: 30,
      chaosSeed: 0,
      chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true]
    },
    {
      lineId: 2,
      enabled: true,
      movementType: 0,  // VA-ET-VIENT (Simple)
      cycleCount: 10,
      pauseAfterMs: 500,
      startPositionMM: 0.0,
      distanceMM: 100.0,
      speedForward: 8.0,
      speedBackward: 8.0,
      decelStartEnabled: true,
      decelEndEnabled: true,
      decelZoneMM: 20.0,
      decelEffectPercent: 75.0,
      decelMode: 1,
      oscCenterPositionMM: 100.0,
      oscAmplitudeMM: 50.0,
      oscWaveform: 0,
      oscFrequencyHz: 0.25,
      oscEnableRampIn: false,
      oscEnableRampOut: false,
      oscRampInDurationMs: 1000.0,
      oscRampOutDurationMs: 1000.0,
      chaosCenterPositionMM: 110.0,
      chaosAmplitudeMM: 50.0,
      chaosMaxSpeedLevel: 10.0,
      chaosCrazinessPercent: 50.0,
      chaosDurationSeconds: 30,
      chaosSeed: 0,
      chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true]
    },
    {
      lineId: 3,
      enabled: true,
      movementType: 1,  // OSCILLATION
      cycleCount: 5,
      pauseAfterMs: 2000,
      startPositionMM: 0.0,
      distanceMM: 100.0,
      speedForward: 5.0,
      speedBackward: 5.0,
      decelStartEnabled: false,
      decelEndEnabled: false,
      decelZoneMM: 50.0,
      decelEffectPercent: 50.0,
      decelMode: 0,
      oscCenterPositionMM: 100.0,
      oscAmplitudeMM: 80.0,
      oscWaveform: 0,
      oscFrequencyHz: 0.5,
      oscEnableRampIn: true,
      oscEnableRampOut: true,
      oscRampInDurationMs: 2000.0,
      oscRampOutDurationMs: 2000.0,
      chaosCenterPositionMM: 110.0,
      chaosAmplitudeMM: 50.0,
      chaosMaxSpeedLevel: 10.0,
      chaosCrazinessPercent: 50.0,
      chaosDurationSeconds: 30,
      chaosSeed: 0,
      chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true]
    },
    {
      lineId: 4,
      enabled: true,
      movementType: 2,  // CHAOS
      cycleCount: 1,
      pauseAfterMs: 1000,
      startPositionMM: 0.0,
      distanceMM: 100.0,
      speedForward: 5.0,
      speedBackward: 5.0,
      decelStartEnabled: false,
      decelEndEnabled: false,
      decelZoneMM: 50.0,
      decelEffectPercent: 50.0,
      decelMode: 0,
      oscCenterPositionMM: 100.0,
      oscAmplitudeMM: 50.0,
      oscWaveform: 0,
      oscFrequencyHz: 1.0,
      oscEnableRampIn: false,
      oscEnableRampOut: false,
      oscRampInDurationMs: 1000.0,
      oscRampOutDurationMs: 1000.0,
      chaosCenterPositionMM: 100.0,
      chaosAmplitudeMM: 80.0,
      chaosMaxSpeedLevel: 15.0,
      chaosCrazinessPercent: 75.0,
      chaosDurationSeconds: 60,
      chaosSeed: 12345,
      chaosPatternsEnabled: [false, false, false, false, false, true, true, true, true, false, false]
    },
    {
      lineId: 5,
      enabled: true,
      movementType: 0,  // VA-ET-VIENT (retour à 0)
      cycleCount: 1,
      pauseAfterMs: 0,
      startPositionMM: 0.0,
      distanceMM: 50.0,
      speedForward: 5.0,
      speedBackward: 5.0,
      decelStartEnabled: false,
      decelEndEnabled: true,
      decelZoneMM: 10.0,
      decelEffectPercent: 50.0,
      decelMode: 1,
      oscCenterPositionMM: 100.0,
      oscAmplitudeMM: 50.0,
      oscWaveform: 0,
      oscFrequencyHz: 2.0,
      oscEnableRampIn: false,
      oscEnableRampOut: false,
      oscRampInDurationMs: 1000.0,
      oscRampOutDurationMs: 1000.0,
      chaosCenterPositionMM: 110.0,
      chaosAmplitudeMM: 50.0,
      chaosMaxSpeedLevel: 10.0,
      chaosCrazinessPercent: 50.0,
      chaosDurationSeconds: 30,
      chaosSeed: 0,
      chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true]
    }
  ]
};

/**
 * Documentation for sequence template
 */
const SEQUENCE_TEMPLATE_HELP = {
  "🔧 GUIDE D'UTILISATION": {
    "Format": "JSON version 2.0",
    "Structure": "Objet avec 'version', 'lineCount' et 'lines' (array)",
    "Ordre": "⚠️ TOUJOURS commencer par CALIBRATION (movementType=4) !"
  },
  "📋 TYPES DE MOUVEMENT (movementType)": {
    "0": "VA-ET-VIENT (Simple) - Mouvement aller-retour classique",
    "1": "OSCILLATION - Mouvement sinusoïdal continu",
    "2": "CHAOS - Mouvements aléatoires chaotiques",
    "4": "CALIBRATION - Calibration automatique (toujours en premier)"
  },
  "⚙️ PARAMÈTRES COMMUNS": {
    "lineId": "ID unique de la ligne (entier)",
    "enabled": "Ligne active ? (true/false)",
    "cycleCount": "Nombre de cycles (1 forcé pour CALIBRATION)",
    "pauseAfterMs": "Pause après mouvement en millisecondes (0 = aucune)"
  },
  "🎯 VA-ET-VIENT (movementType=0)": {
    "startPositionMM": "Position de départ (mm)",
    "distanceMM": "Distance du mouvement (mm)",
    "speedForward": "Vitesse aller (1-20)",
    "speedBackward": "Vitesse retour (1-20)",
    "decelStartEnabled": "Décélération au départ ? (true/false)",
    "decelEndEnabled": "Décélération à la fin ? (true/false)",
    "decelZoneMM": "Taille zone décélération (mm)",
    "decelEffectPercent": "Effet décélération (0-100%)",
    "decelMode": "Type: 0=LINEAR, 1=SINE, 2=TRIANGLE_INV, 3=SINE_INV"
  },
  "🌊 OSCILLATION (movementType=1)": {
    "oscCenterPositionMM": "Position centrale (mm)",
    "oscAmplitudeMM": "Amplitude ±amplitude (mm)",
    "oscWaveform": "Forme d'onde: 0=SINE, 1=TRIANGLE, 2=SQUARE",
    "oscFrequencyHz": "Fréquence (0.1-10 Hz)",
    "oscEnableRampIn": "Rampe d'entrée ? (true/false)",
    "oscEnableRampOut": "Rampe de sortie ? (true/false)",
    "oscRampInDurationMs": "Durée rampe entrée (ms)",
    "oscRampOutDurationMs": "Durée rampe sortie (ms)"
  },
  "🌪️ CHAOS (movementType=2)": {
    "chaosCenterPositionMM": "Position centrale (mm)",
    "chaosAmplitudeMM": "Amplitude max ±amplitude (mm)",
    "chaosMaxSpeedLevel": "Vitesse max (1-20)",
    "chaosCrazinessPercent": "Degré folie (0-100%)",
    "chaosDurationSeconds": "Durée en secondes (0=infini)",
    "chaosSeed": "Graine aléatoire (0=auto)",
    "chaosPatternsEnabled": "Array[11] booléens (ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, BREATHING, BRUTE_FORCE, LIBERATOR)"
  },
  "📏 CALIBRATION (movementType=4)": {
    "Note": "Calibration automatique complète",
    "cycleCount": "Toujours forcé à 1",
    "Autres_params": "Ignorés mais doivent être présents dans le JSON"
  },
  "⚠️ IMPORTANT": {
    "Tous_les_champs": "TOUS les champs doivent être présents dans chaque ligne !",
    "Ordre_execution": "Les lignes sont exécutées dans l'ordre du tableau",
    "Validation": "Les valeurs sont validées côté backend (limites physiques)",
    "IDs": "Les lineId doivent être uniques"
  }
};

/**
 * Get full template document with documentation (PURE FUNCTION)
 * @returns {Object} { TEMPLATE, DOCUMENTATION }
 */
function getSequenceTemplateDocPure() {
  return {
    TEMPLATE: SEQUENCE_TEMPLATE,
    DOCUMENTATION: SEQUENCE_TEMPLATE_HELP
  };
}

// ============================================================================
// EXPORTS (Browser globals)
// ============================================================================
window.SEQUENCER_LIMITS = SEQUENCER_LIMITS;
window.MOVEMENT_TYPE = MOVEMENT_TYPE;
window.DECEL_MODES = DECEL_MODES;
window.validateSequencerLinePure = validateSequencerLinePure;
window.buildSequenceLineDefaultsPure = buildSequenceLineDefaultsPure;
window.buildSequenceLineFromPresetPure = buildSequenceLineFromPresetPure;
window.generateVaetTooltipPure = generateVaetTooltipPure;
window.generateCalibrationTooltipPure = generateCalibrationTooltipPure;
window.generateSequenceLineTooltipPure = generateSequenceLineTooltipPure;
window.getMovementTypeDisplayPure = getMovementTypeDisplayPure;
window.getDecelSummaryPure = getDecelSummaryPure;
window.getLineSpeedsDisplayPure = getLineSpeedsDisplayPure;
window.getLineCyclesPausePure = getLineCyclesPausePure;
window.SEQUENCE_TEMPLATE = SEQUENCE_TEMPLATE;
window.SEQUENCE_TEMPLATE_HELP = SEQUENCE_TEMPLATE_HELP;
window.getSequenceTemplateDocPure = getSequenceTemplateDocPure;

console.log('✅ sequencer.js loaded');
