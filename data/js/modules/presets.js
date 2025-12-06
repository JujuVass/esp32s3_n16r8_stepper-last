/**
 * presets.js - Pure functions for preset name and tooltip generation
 * NO DOM dependencies - can be unit tested
 */

// ============================================================================
// PRESET NAME GENERATION (PURE)
// ============================================================================

/**
 * Generate a compact preset name based on mode and config
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @param {Object} config - Mode-specific configuration
 * @returns {string} Compact name for the preset
 */
function generatePresetNamePure(mode, config) {
  if (mode === 'simple') {
    return `${config.startPositionMM}→${config.startPositionMM + config.distanceMM}mm v:${config.speedLevelForward}/${config.speedLevelBackward}`;
  } else if (mode === 'oscillation') {
    const waveNames = ['Sine', 'Triangle', 'Square'];
    return `${waveNames[config.waveform] || 'Sine'} ${config.frequencyHz}Hz ±${config.amplitudeMM}mm`;
  } else if (mode === 'chaos') {
    return `Chaos ${config.durationSeconds}s (${config.crazinessPercent}%)`;
  }
  return 'Preset';
}

// ============================================================================
// SIMPLE MODE TOOLTIP
// ============================================================================

/**
 * Generate deceleration info string
 * @param {Object} config - Configuration with decel properties
 * @returns {string} Formatted decel info
 */
function formatDecelInfoPure(config) {
  if (!config || (!config.decelStartEnabled && !config.decelEndEnabled)) {
    return 'Aucune';
  }
  
  const parts = [];
  if (config.decelStartEnabled) parts.push('Départ');
  if (config.decelEndEnabled) parts.push('Fin');
  
  const modeNames = ['Lin', 'Sin', 'Tri⁻¹', 'Sin⁻¹'];
  return parts.join('+') + ` (${config.decelZoneMM || 20}mm, ${config.decelEffectPercent || 50}%, ${modeNames[config.decelMode || 1]})`;
}

/**
 * Generate cycle pause info string for simple/oscillation modes
 * @param {Object} config - Configuration with cyclePause properties
 * @returns {string} Formatted pause info
 */
function formatCyclePauseInfoPresetPure(config) {
  if (!config || !config.cyclePauseEnabled) {
    return 'Aucune';
  }
  
  if (config.cyclePauseIsRandom) {
    return `${config.cyclePauseMinSec || 0.5}s-${config.cyclePauseMaxSec || 3.0}s (aléatoire)`;
  } else {
    return `${config.cyclePauseDurationSec || 0}s (fixe)`;
  }
}

/**
 * Generate tooltip for Simple mode preset
 * @param {Object} config - Simple mode configuration
 * @returns {string} Multi-line tooltip
 */
function generateSimplePresetTooltipPure(config) {
  const decelInfo = formatDecelInfoPure(config);
  const cyclePauseInfo = formatCyclePauseInfoPresetPure(config);
  
  // Estimate duration (very rough)
  const avgSpeed = ((config.speedLevelForward || 5) + (config.speedLevelBackward || 5)) / 2;
  const estimatedDuration = ((config.distanceMM || 50) / (avgSpeed * 10)) * 60;
  
  return `📍 Départ: ${config.startPositionMM || 0}mm
📏 Distance: ${config.distanceMM || 50}mm
➡️ Vitesse aller: ${config.speedLevelForward || 5}/20
⬅️ Vitesse retour: ${config.speedLevelBackward || 5}/20
🛑 Décel: ${decelInfo}
⏸️ Pause/cycle: ${cyclePauseInfo}
⏱️ Durée estimée: ${estimatedDuration.toFixed(1)}s`;
}

// ============================================================================
// OSCILLATION MODE TOOLTIP
// ============================================================================

/**
 * Generate tooltip for Oscillation mode preset
 * @param {Object} config - Oscillation mode configuration
 * @returns {string} Multi-line tooltip
 */
function generateOscillationPresetTooltipPure(config) {
  const waveNames = ['🌊 Sine', '📐 Triangle', '⬜ Carré'];
  const ramps = [];
  if (config.enableRampIn) ramps.push('IN');
  if (config.enableRampOut) ramps.push('OUT');
  
  const cyclePauseInfo = formatCyclePauseInfoPresetPure(config);
  
  let tooltip = `${waveNames[config.waveform] || '🌊 Sine'}
📍 Centre: ${config.centerPositionMM || 100}mm
↔️ Amplitude: ±${config.amplitudeMM || 20}mm
⚡ Fréquence: ${config.frequencyHz || 1}Hz
🔄 Cycles: ${config.cycleCount === 0 ? '∞' : config.cycleCount}`;
  
  if (ramps.length > 0) {
    tooltip += `\n📈 Rampes: ${ramps.join(', ')}`;
  }
  
  tooltip += `\n⏸️ Pause/cycle: ${cyclePauseInfo}`;
  
  return tooltip;
}

// ============================================================================
// CHAOS MODE TOOLTIP
// ============================================================================

/**
 * Generate tooltip for Chaos mode preset
 * @param {Object} config - Chaos mode configuration
 * @returns {string} Multi-line tooltip
 */
function generateChaosPresetTooltipPure(config) {
  const enabledCount = config.patternsEnabled 
    ? config.patternsEnabled.filter(p => p).length 
    : 11;
  
  return `📍 Centre: ${config.centerPositionMM}mm
↔️ Amplitude: ±${config.amplitudeMM}mm
⚡ Vitesse max: ${config.maxSpeedLevel}/20
🎲 Folie: ${config.crazinessPercent}%
⏱️ Durée: ${config.durationSeconds === 0 ? '∞' : config.durationSeconds + 's'}
🎭 Patterns: ${enabledCount}/11 actifs`;
}

// ============================================================================
// UNIFIED PRESET TOOLTIP
// ============================================================================

/**
 * Generate tooltip for any preset mode
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @param {Object} config - Mode-specific configuration
 * @returns {string} Multi-line tooltip
 */
function generatePresetTooltipPure(mode, config) {
  if (mode === 'simple') {
    return generateSimplePresetTooltipPure(config);
  } else if (mode === 'oscillation') {
    return generateOscillationPresetTooltipPure(config);
  } else if (mode === 'chaos') {
    return generateChaosPresetTooltipPure(config);
  }
  return 'Preset';
}

// ============================================================================
// DECELERATION CURVE (PURE)
// ============================================================================

/**
 * Calculate slowdown factor for deceleration zones
 * Matches the exact curve formulas from ESP32 firmware
 * @param {number} zoneProgress - Progress through zone (0.0 to 1.0)
 * @param {number} maxSlowdown - Maximum slowdown factor
 * @param {number} mode - Deceleration mode (0=Linear, 1=Sine, 2=TriangleInv, 3=SineInv)
 * @returns {number} Slowdown factor (1.0 = no slowdown)
 */
function calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode) {
  let factor = 1.0;
  
  switch(mode) {
    case 0: // DECEL_LINEAR
      factor = 1.0 + (1.0 - zoneProgress) * (maxSlowdown - 1.0);
      break;
      
    case 1: // DECEL_SINE
      const smoothProgress = (1.0 - Math.cos(zoneProgress * Math.PI)) / 2.0;
      factor = 1.0 + (1.0 - smoothProgress) * (maxSlowdown - 1.0);
      break;
      
    case 2: // DECEL_TRIANGLE_INV
      const invProgressTri = 1.0 - zoneProgress;
      const curvedTri = invProgressTri * invProgressTri;
      factor = 1.0 + curvedTri * (maxSlowdown - 1.0);
      break;
      
    case 3: // DECEL_SINE_INV
      const invProgressSin = 1.0 - zoneProgress;
      const curvedSin = Math.sin(invProgressSin * Math.PI / 2.0);
      factor = 1.0 + curvedSin * (maxSlowdown - 1.0);
      break;
      
    default:
      factor = 1.0 + (1.0 - zoneProgress) * (maxSlowdown - 1.0);
  }
  
  return factor;
}

// Export constants for decel modes
const DECEL_CURVE_MODES = {
  LINEAR: 0,
  SINE: 1,
  TRIANGLE_INV: 2,
  SINE_INV: 3
};

// ============================================================================
// CONFIG PREVIEW HTML (PURE)
// ============================================================================

/**
 * Generate HTML for current config preview in playlist modal
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @param {Object} config - Mode-specific configuration
 * @returns {string} HTML string for config preview
 */
function generateConfigPreviewHTMLPure(mode, config) {
  if (mode === 'simple') {
    return `
      • Départ: ${config.startPositionMM || 0} mm<br>
      • Distance: ${config.distanceMM || 50} mm<br>
      • Vitesse aller: ${config.speedLevelForward || 5}<br>
      • Vitesse retour: ${config.speedLevelBackward || 5}
    `;
  } else if (mode === 'oscillation') {
    const waveNames = ['Sine', 'Triangle', 'Square'];
    return `
      • Centre: ${config.centerPositionMM || 100} mm<br>
      • Amplitude: ±${config.amplitudeMM || 20} mm<br>
      • Forme: ${waveNames[config.waveform] || 'Sine'}<br>
      • Fréquence: ${config.frequencyHz || 1} Hz<br>
      • Cycles: ${config.cycleCount === 0 ? '∞ (infini)' : config.cycleCount}
    `;
  } else if (mode === 'chaos') {
    const enabledCount = config.patternsEnabled 
      ? config.patternsEnabled.filter(p => p).length 
      : 11;
    return `
      • Centre: ${config.centerPositionMM || 100} mm<br>
      • Amplitude: ±${config.amplitudeMM || 40} mm<br>
      • Vitesse max: ${config.maxSpeedLevel || 15}<br>
      • Craziness: ${config.crazinessPercent || 50}%<br>
      • Durée: ${config.durationSeconds === 0 ? '∞ (infini)' : (config.durationSeconds || 30) + 's'}<br>
      • Patterns actifs: ${enabledCount}/11
    `;
  }
  return '';
}

/**
 * Get modal title for mode
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @returns {string} Title string
 */
function getPlaylistModalTitlePure(mode) {
  const titles = {
    'simple': 'Mode Simple',
    'oscillation': 'Mode Oscillation',
    'chaos': 'Mode Chaos'
  };
  return titles[mode] || mode;
}

// ============================================================================
// EXPORTS (Browser globals)
// ============================================================================
window.generatePresetNamePure = generatePresetNamePure;
window.generatePresetTooltipPure = generatePresetTooltipPure;
window.generateSimplePresetTooltipPure = generateSimplePresetTooltipPure;
window.generateOscillationPresetTooltipPure = generateOscillationPresetTooltipPure;
window.generateChaosPresetTooltipPure = generateChaosPresetTooltipPure;
window.calculateSlowdownFactorPure = calculateSlowdownFactorPure;
window.formatDecelInfoPure = formatDecelInfoPure;
window.formatCyclePauseInfoPresetPure = formatCyclePauseInfoPresetPure;
window.DECEL_CURVE_MODES = DECEL_CURVE_MODES;
window.generateConfigPreviewHTMLPure = generateConfigPreviewHTMLPure;
window.getPlaylistModalTitlePure = getPlaylistModalTitlePure;

console.log('✅ presets.js loaded - preset name/tooltip pure functions available');
