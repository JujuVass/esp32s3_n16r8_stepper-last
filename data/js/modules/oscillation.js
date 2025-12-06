/**
 * oscillation.js - Oscillation Mode Module for ESP32 Stepper Controller
 * 
 * Contains pure functions for oscillation mode validation and configuration.
 * These functions have NO side effects (no DOM, no global state mutation).
 */

// ============================================================================
// CONSTANTS
// ============================================================================
const OSCILLATION_LIMITS = {
  CENTER_MIN: 0,
  AMPLITUDE_MIN: 1,
  FREQUENCY_MIN: 0.01,
  FREQUENCY_MAX: 10,
  RAMP_DURATION_MIN: 100,
  RAMP_DURATION_MAX: 10000,
  CYCLE_COUNT_MIN: 0,  // 0 = infinite
  CYCLE_COUNT_MAX: 10000
};

const WAVEFORM_TYPE = {
  SINE: 0,
  TRIANGLE: 1,
  SQUARE: 2
};

const WAVEFORM_NAMES = ['🌊 Sine', '📐 Triangle', '⬜ Carré'];
const WAVEFORM_SHORT = ['SIN', 'TRI', 'SQR'];

// ============================================================================
// PURE VALIDATION FUNCTIONS
// ============================================================================

/**
 * Validate oscillation mode limits (PURE FUNCTION)
 * @param {number} centerPos - Center position in mm
 * @param {number} amplitude - Amplitude in mm
 * @param {number} totalDistMM - Total available distance in mm
 * @returns {Object} { valid: boolean, error?: string, min: number, max: number }
 */
function validateOscillationLimitsPure(centerPos, amplitude, totalDistMM) {
  const min = centerPos - amplitude;
  const max = centerPos + amplitude;
  
  if (isNaN(centerPos) || isNaN(amplitude)) {
    return { valid: false, error: "Invalid center position or amplitude", min: min, max: max };
  }
  if (min < 0) {
    return { valid: false, error: "Movement would go below 0mm (min: " + min.toFixed(1) + "mm)", min: min, max: max };
  }
  if (max > totalDistMM) {
    return { valid: false, error: "Movement would exceed track (max: " + max.toFixed(1) + "mm > " + totalDistMM + "mm)", min: min, max: max };
  }
  return { valid: true, min: min, max: max };
}

/**
 * Build oscillation configuration object (PURE FUNCTION)
 * @param {Object} formValues - Values from the form
 * @returns {Object} Oscillation configuration object
 */
function buildOscillationConfigPure(formValues) {
  return {
    centerPositionMM: parseFloat(formValues.centerPos) || 100,
    amplitudeMM: parseFloat(formValues.amplitude) || 20,
    waveform: parseInt(formValues.waveform) || WAVEFORM_TYPE.SINE,
    frequencyHz: parseFloat(formValues.frequency) || 0.5,
    cycleCount: parseInt(formValues.cycleCount) || 10,
    enableRampIn: formValues.enableRampIn || false,
    rampInDurationMs: parseInt(formValues.rampInDuration) || 1000,
    enableRampOut: formValues.enableRampOut || false,
    rampOutDurationMs: parseInt(formValues.rampOutDuration) || 1000,
    returnToCenter: formValues.returnToCenter || false
  };
}

/**
 * Calculate peak speed for oscillation (PURE FUNCTION)
 * Peak speed = 2π × frequency × amplitude (for sine wave)
 * @param {number} frequencyHz - Frequency in Hz
 * @param {number} amplitudeMM - Amplitude in mm
 * @returns {number} Peak speed in mm/s
 */
function calculateOscillationPeakSpeedPure(frequencyHz, amplitudeMM) {
  return 2 * Math.PI * frequencyHz * amplitudeMM;
}

/**
 * Get waveform display name (PURE FUNCTION)
 * @param {number} waveformId - Waveform type (0=SINE, 1=TRIANGLE, 2=SQUARE)
 * @returns {string} Waveform display name with icon
 */
function getWaveformNamePure(waveformId) {
  return WAVEFORM_NAMES[waveformId] || WAVEFORM_NAMES[0];
}

/**
 * Get waveform short name (PURE FUNCTION)
 * @param {number} waveformId - Waveform type
 * @returns {string} Short waveform name (SIN, TRI, SQR)
 */
function getWaveformShortPure(waveformId) {
  return WAVEFORM_SHORT[waveformId] || WAVEFORM_SHORT[0];
}

/**
 * Format cycle pause info (PURE FUNCTION)
 * @param {boolean} enabled - Is cycle pause enabled
 * @param {boolean} isRandom - Is random mode
 * @param {number} durationSec - Fixed duration in seconds
 * @param {number} minSec - Random min in seconds
 * @param {number} maxSec - Random max in seconds
 * @returns {string} Formatted pause info
 */
function formatCyclePauseInfoPure(enabled, isRandom, durationSec, minSec, maxSec) {
  if (!enabled) return 'Aucune';
  
  if (isRandom) {
    return `${minSec}s-${maxSec}s (aléatoire)`;
  } else {
    return `${durationSec}s (fixe)`;
  }
}

/**
 * Generate oscillation tooltip content (PURE FUNCTION)
 * @param {Object} config - Oscillation configuration (can be line or form config)
 * @returns {string} Formatted tooltip string
 */
function generateOscillationTooltipPure(config) {
  // Handle both line format (oscCenterPositionMM) and config format (centerPositionMM)
  const centerMM = config.oscCenterPositionMM || config.centerPositionMM || 100;
  const amplitudeMM = config.oscAmplitudeMM || config.amplitudeMM || 20;
  const waveform = config.oscWaveform !== undefined ? config.oscWaveform : (config.waveform || 0);
  const frequencyHz = config.oscFrequencyHz || config.frequencyHz || 0.5;
  const cycleCount = config.cycleCount || 10;
  
  const enableRampIn = config.oscEnableRampIn || config.enableRampIn || false;
  const enableRampOut = config.oscEnableRampOut || config.enableRampOut || false;
  const rampInMs = config.oscRampInDurationMs || config.rampInDurationMs || 1000;
  const rampOutMs = config.oscRampOutDurationMs || config.rampOutDurationMs || 1000;
  
  const ramps = [];
  if (enableRampIn) ramps.push('IN (' + rampInMs + 'ms)');
  if (enableRampOut) ramps.push('OUT (' + rampOutMs + 'ms)');
  
  const peakSpeed = calculateOscillationPeakSpeedPure(frequencyHz, amplitudeMM).toFixed(1);
  
  // Cycle pause info
  const cyclePauseEnabled = config.oscCyclePauseEnabled || config.cyclePauseEnabled || false;
  const cyclePauseIsRandom = config.oscCyclePauseIsRandom || config.cyclePauseIsRandom || false;
  const cyclePauseDurationSec = config.oscCyclePauseDurationSec || config.cyclePauseDurationSec || 0;
  const cyclePauseMinSec = config.oscCyclePauseMinSec || config.cyclePauseMinSec || 0.5;
  const cyclePauseMaxSec = config.oscCyclePauseMaxSec || config.cyclePauseMaxSec || 3.0;
  
  const cyclePauseInfo = formatCyclePauseInfoPure(
    cyclePauseEnabled, cyclePauseIsRandom, 
    cyclePauseDurationSec, cyclePauseMinSec, cyclePauseMaxSec
  );
  
  const pauseAfterMs = config.pauseAfterMs || 0;
  
  return `〰️ OSCILLATION
━━━━━━━━━━━━━━━━━━━━━━
${getWaveformNamePure(waveform)}
📍 Centre: ${centerMM}mm
↔️ Amplitude: ±${amplitudeMM}mm
⚡ Fréquence: ${frequencyHz}Hz
⚡ Vitesse pic: ${peakSpeed}mm/s
🔄 Cycles: ${cycleCount}
${ramps.length > 0 ? '📈 Rampes: ' + ramps.join(' + ') : '📈 Rampes: Aucune'}
⏸️ Pause/cycle: ${cyclePauseInfo}
⏱️ Pause après: ${pauseAfterMs > 0 ? (pauseAfterMs / 1000).toFixed(1) + 's' : 'Aucune'}`;
}

// ============================================================================
// EXPORTS (Browser globals)
// ============================================================================
window.OSCILLATION_LIMITS = OSCILLATION_LIMITS;
window.WAVEFORM_TYPE = WAVEFORM_TYPE;
window.WAVEFORM_NAMES = WAVEFORM_NAMES;
window.WAVEFORM_SHORT = WAVEFORM_SHORT;
window.validateOscillationLimitsPure = validateOscillationLimitsPure;
window.buildOscillationConfigPure = buildOscillationConfigPure;
window.calculateOscillationPeakSpeedPure = calculateOscillationPeakSpeedPure;
window.getWaveformNamePure = getWaveformNamePure;
window.getWaveformShortPure = getWaveformShortPure;
window.formatCyclePauseInfoPure = formatCyclePauseInfoPure;
window.generateOscillationTooltipPure = generateOscillationTooltipPure;

console.log('✅ oscillation.js loaded');
