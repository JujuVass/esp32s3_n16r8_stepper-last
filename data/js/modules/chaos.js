/**
 * chaos.js - Chaos Mode Module for ESP32 Stepper Controller
 * 
 * Contains pure functions for chaos mode validation and configuration.
 * These functions have NO side effects (no DOM, no global state mutation).
 */

// ============================================================================
// CONSTANTS
// ============================================================================
const CHAOS_LIMITS = {
  CENTER_MIN: 0,
  AMPLITUDE_MIN: 1,
  SPEED_MIN: 1,
  SPEED_MAX: 20,
  CRAZINESS_MIN: 0,
  CRAZINESS_MAX: 100,
  DURATION_MIN: 5,
  DURATION_MAX: 600,
  SEED_MIN: 0,
  SEED_MAX: 9999999,
  PATTERN_COUNT: 11
};

const CHAOS_PATTERNS = [
  { id: 0, name: 'Zigzag', icon: '⚡' },
  { id: 1, name: 'Sweep', icon: '🌊' },
  { id: 2, name: 'Pulse', icon: '💓' },
  { id: 3, name: 'Drift', icon: '🌀' },
  { id: 4, name: 'Burst', icon: '💥' },
  { id: 5, name: 'Wave', icon: '〰️' },
  { id: 6, name: 'Pendulum', icon: '🕰️' },
  { id: 7, name: 'Spiral', icon: '🌪️' },
  { id: 8, name: 'Calm', icon: '😌' },
  { id: 9, name: 'BruteForce', icon: '💪' },
  { id: 10, name: 'Liberator', icon: '🦅' }
];

// ============================================================================
// PURE VALIDATION FUNCTIONS
// ============================================================================

/**
 * Validate chaos mode limits (PURE FUNCTION)
 * @param {number} centerPos - Center position in mm
 * @param {number} amplitude - Amplitude in mm
 * @param {number} totalDistMM - Total available distance in mm
 * @returns {Object} { valid: boolean, error?: string, min: number, max: number }
 */
function validateChaosLimitsPure(centerPos, amplitude, totalDistMM) {
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
 * Build chaos configuration object (PURE FUNCTION)
 * @param {Object} formValues - Values from the form
 * @returns {Object} Chaos configuration object
 */
function buildChaosConfigPure(formValues) {
  return {
    centerPositionMM: parseFloat(formValues.centerPos) || 0,
    amplitudeMM: parseFloat(formValues.amplitude) || 0,
    maxSpeedLevel: parseFloat(formValues.maxSpeed) || 10,
    crazinessPercent: parseInt(formValues.craziness) || 50,
    durationSeconds: parseInt(formValues.duration) || 30,
    seed: parseInt(formValues.seed) || 0,
    patternsEnabled: formValues.patternsEnabled || Array(11).fill(true)
  };
}

/**
 * Count enabled patterns (PURE FUNCTION)
 * @param {boolean[]} patternsEnabled - Array of pattern enabled states
 * @returns {number} Count of enabled patterns
 */
function countEnabledPatternsPure(patternsEnabled) {
  if (!patternsEnabled || !Array.isArray(patternsEnabled)) {
    return CHAOS_LIMITS.PATTERN_COUNT;
  }
  return patternsEnabled.filter(p => p).length;
}

/**
 * Generate chaos tooltip content (PURE FUNCTION)
 * @param {Object} config - Chaos configuration
 * @returns {string} Formatted tooltip string
 */
function generateChaosTooltipPure(config) {
  const enabledCount = countEnabledPatternsPure(config.patternsEnabled);
  
  return `🌀 CHAOS
━━━━━━━━━━━━━━━━━━━━━━
📍 Centre: ${config.centerPositionMM || config.chaosCenterPositionMM}mm
↔️ Amplitude: ±${config.amplitudeMM || config.chaosAmplitudeMM}mm
⚡ Vitesse max: ${(config.maxSpeedLevel || config.chaosMaxSpeedLevel || 10).toFixed(1)}/20
🎲 Folie: ${config.crazinessPercent || config.chaosCrazinessPercent}%
⏱️ Durée: ${config.durationSeconds || config.chaosDurationSeconds}s
🌱 Seed: ${config.seed || config.chaosSeed || 0}
🎭 Patterns: ${enabledCount}/11 actifs
⏱️ Pause après: ${config.pauseAfterMs > 0 ? (config.pauseAfterMs / 1000).toFixed(1) + 's' : 'Aucune'}`;
}

// ============================================================================
// EXPORTS (Browser globals)
// ============================================================================
window.CHAOS_LIMITS = CHAOS_LIMITS;
window.CHAOS_PATTERNS = CHAOS_PATTERNS;
window.validateChaosLimitsPure = validateChaosLimitsPure;
window.buildChaosConfigPure = buildChaosConfigPure;
window.countEnabledPatternsPure = countEnabledPatternsPure;
window.generateChaosTooltipPure = generateChaosTooltipPure;

console.log('✅ chaos.js loaded');
