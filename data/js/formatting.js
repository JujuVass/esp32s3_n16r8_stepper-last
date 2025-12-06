/**
 * formatting.js - Pure formatting functions
 * NO DOM dependencies - can be unit tested
 */

// ============================================================================
// WIFI QUALITY FORMATTING
// ============================================================================

/**
 * Get WiFi quality info from RSSI value (PURE FUNCTION)
 * @param {number} rssi - RSSI value in dBm
 * @returns {Object} { quality: string, color: string }
 */
function getWifiQualityPure(rssi) {
  if (rssi >= -50) {
    return { quality: 'Excellent', color: '#4CAF50' };
  } else if (rssi >= -60) {
    return { quality: 'Très bon', color: '#8BC34A' };
  } else if (rssi >= -70) {
    return { quality: 'Bon', color: '#FFC107' };
  } else if (rssi >= -80) {
    return { quality: 'Faible', color: '#FF9800' };
  } else {
    return { quality: 'Très faible', color: '#f44336' };
  }
}

// ============================================================================
// UPTIME FORMATTING
// ============================================================================

/**
 * Format uptime seconds to human readable string (PURE FUNCTION)
 * @param {number} uptimeSeconds - Uptime in seconds
 * @returns {string} Formatted uptime string
 */
function formatUptimePure(uptimeSeconds) {
  const hours = Math.floor(uptimeSeconds / 3600);
  const minutes = Math.floor((uptimeSeconds % 3600) / 60);
  const seconds = uptimeSeconds % 60;
  
  if (hours > 0) {
    return `${hours}h ${minutes}m ${seconds}s`;
  } else if (minutes > 0) {
    return `${minutes}m ${seconds}s`;
  } else {
    return `${seconds}s`;
  }
}

// ============================================================================
// DURATION FORMATTING
// ============================================================================

/**
 * Format duration in seconds to human readable string (PURE FUNCTION)
 * @param {number} seconds - Duration in seconds
 * @param {boolean} includeMs - Include milliseconds precision
 * @returns {string} Formatted duration string
 */
function formatDurationPure(seconds, includeMs = false) {
  if (seconds === 0) return '∞';
  
  const mins = Math.floor(seconds / 60);
  const secs = seconds % 60;
  
  if (mins > 0) {
    if (includeMs && secs % 1 !== 0) {
      return `${mins}m ${secs.toFixed(1)}s`;
    }
    return `${mins}m ${Math.floor(secs)}s`;
  } else {
    if (includeMs && secs % 1 !== 0) {
      return `${secs.toFixed(1)}s`;
    }
    return `${Math.floor(secs)}s`;
  }
}

/**
 * Format duration with automatic unit selection (PURE FUNCTION)
 * @param {number} ms - Duration in milliseconds
 * @returns {string} Formatted duration with appropriate unit
 */
function formatDurationAutoUnitPure(ms) {
  if (ms < 1000) {
    return `${ms}ms`;
  } else if (ms < 60000) {
    return `${(ms / 1000).toFixed(1)}s`;
  } else {
    const mins = Math.floor(ms / 60000);
    const secs = Math.floor((ms % 60000) / 1000);
    return `${mins}m ${secs}s`;
  }
}

// ============================================================================
// SPEED FORMATTING
// ============================================================================

/**
 * Format speed level with visual indicator (PURE FUNCTION)
 * @param {number} speedLevel - Speed level (1-20)
 * @param {number} maxLevel - Maximum speed level (default 20)
 * @returns {Object} { display: string, percent: number }
 */
function formatSpeedLevelPure(speedLevel, maxLevel = 20) {
  const percent = (speedLevel / maxLevel) * 100;
  return {
    display: `${speedLevel}/${maxLevel}`,
    percent: percent
  };
}

/**
 * Get speed level color based on value (PURE FUNCTION)
 * @param {number} speedLevel - Speed level (1-20)
 * @returns {string} CSS color
 */
function getSpeedColorPure(speedLevel) {
  if (speedLevel <= 5) {
    return '#4CAF50';  // Green - slow
  } else if (speedLevel <= 10) {
    return '#FFC107';  // Yellow - medium
  } else if (speedLevel <= 15) {
    return '#FF9800';  // Orange - fast
  } else {
    return '#f44336';  // Red - very fast
  }
}

// ============================================================================
// POSITION FORMATTING
// ============================================================================

/**
 * Format position in mm with optional step info (PURE FUNCTION)
 * @param {number} positionMM - Position in millimeters
 * @param {number} decimals - Decimal places (default 1)
 * @returns {string} Formatted position string
 */
function formatPositionPure(positionMM, decimals = 1) {
  return `${positionMM.toFixed(decimals)}mm`;
}

/**
 * Format position range (PURE FUNCTION)
 * @param {number} startMM - Start position in mm
 * @param {number} endMM - End position in mm
 * @returns {string} Formatted range string
 */
function formatPositionRangePure(startMM, endMM) {
  return `${startMM.toFixed(0)}→${endMM.toFixed(0)}mm`;
}

// ============================================================================
// PERCENTAGE FORMATTING
// ============================================================================

/**
 * Format percentage with optional decimal places (PURE FUNCTION)
 * @param {number} value - Value (0-100)
 * @param {number} decimals - Decimal places (default 0)
 * @returns {string} Formatted percentage string
 */
function formatPercentPure(value, decimals = 0) {
  return `${value.toFixed(decimals)}%`;
}

// ============================================================================
// BYTE SIZE FORMATTING
// ============================================================================

/**
 * Format byte size to human readable string (PURE FUNCTION)
 * @param {number} bytes - Size in bytes
 * @returns {string} Formatted size string
 */
function formatBytesPure(bytes) {
  if (bytes < 1024) {
    return `${bytes} B`;
  } else if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  } else {
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  }
}

// ============================================================================
// SYSTEM STATE FORMATTING
// ============================================================================

/**
 * Get state display info (PURE FUNCTION)
 * Uses SystemState enum if available, falls back to codes
 * @param {number} stateCode - System state code
 * @param {string} errorMessage - Optional error message
 * @returns {Object} { text: string, color: string, icon: string }
 */
function getStateDisplayPure(stateCode, errorMessage = null) {
  // Map state codes to display info
  const stateMap = {
    0: { text: 'Non calibré', color: '#FF9800', icon: '⚠️' },     // NOT_CALIBRATED
    1: { text: 'Au repos', color: '#4CAF50', icon: '✅' },          // IDLE
    2: { text: 'Calibration...', color: '#2196F3', icon: '🔄' },   // CALIBRATING
    3: { text: 'En mouvement', color: '#9C27B0', icon: '▶️' },     // MOVING
    4: { text: 'Poursuite active', color: '#00BCD4', icon: '🎯' }, // PURSUIT
    5: { text: 'Oscillation', color: '#E91E63', icon: '🌊' },      // OSCILLATING
    6: { text: 'Mode Chaos', color: '#f44336', icon: '🎲' },       // CHAOS
    7: { text: 'Séquence', color: '#673AB7', icon: '📋' },         // SEQUENCE
    8: { text: 'Arrêt d\'urgence', color: '#f44336', icon: '🛑' }, // EMERGENCY_STOP
    9: { text: 'Erreur', color: '#f44336', icon: '❌' },           // ERROR
    10: { text: 'Pause', color: '#FFC107', icon: '⏸️' }            // PAUSED
  };
  
  const display = stateMap[stateCode] || { text: `État ${stateCode}`, color: '#666', icon: '❓' };
  
  // If in error state, append error message
  if (stateCode === 9 && errorMessage) {
    display.text = `Erreur: ${errorMessage}`;
  }
  
  return display;
}

// ============================================================================
// PATTERN NAME FORMATTING
// ============================================================================

/**
 * Get chaos pattern name (PURE FUNCTION)
 * @param {number} patternId - Pattern ID (0-10)
 * @returns {string} Pattern display name
 */
function getChaosPatternNamePure(patternId) {
  const patterns = [
    'Zigzag',
    'Sweep',
    'Pulse',
    'Drift',
    'Burst',
    'Wave',
    'Pendulum',
    'Spiral',
    'Calm',
    'BruteForce',
    'Liberator'
  ];
  return patterns[patternId] || `Pattern ${patternId}`;
}

/**
 * Get waveform name with icon (PURE FUNCTION)
 * @param {number} waveformId - Waveform ID (0=Sine, 1=Triangle, 2=Square)
 * @returns {string} Waveform display name with icon
 */
function getWaveformDisplayPure(waveformId) {
  const waveforms = ['🌊 Sine', '📐 Triangle', '⬜ Square'];
  return waveforms[waveformId] || waveforms[0];
}

console.log('✅ formatting.js loaded - pure formatting functions available');
