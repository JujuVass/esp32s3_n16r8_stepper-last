/**
 * PlaylistUtils.js - Pure Utility Functions for Playlist
 * 
 * Contains pure/helper functions extracted from PlaylistController.js:
 * - Name generation (preset names)
 * - Tooltip generation and display
 * - Preset button state updates
 * - Constants (MODE_ICONS, WAVEFORM_NAMES, TYPE_NAMES)
 * 
 * All functions are pure (no DOM mutations except tooltip display)
 * or simple DOM state updates (preset buttons).
 * 
 * Dependencies: DOM cache (optional)
 */

// ============================================================================
// CONSTANTS
// ============================================================================

const MODE_ICONS = {
  simple: '↔️',
  oscillation: '🌊',
  chaos: '🎲'
};

const WAVEFORM_NAMES = ['Sine', 'Triangle', 'Square'];

const TYPE_NAMES = ['Va-et-vient', 'Oscillation', 'Chaos', 'Calibration'];

// ============================================================================
// NAME GENERATION (Pure Functions)
// ============================================================================

/**
 * Generate default preset name based on mode and config
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @param {Object} config - Configuration object
 * @returns {string} Generated name
 */
function generatePresetNamePure(mode, config) {
  if (mode === 'simple') {
    return `${config.startPositionMM}→${config.startPositionMM + config.distanceMM}mm v:${config.speedLevelForward}/${config.speedLevelBackward}`;
  } else if (mode === 'oscillation') {
    const waveName = WAVEFORM_NAMES[config.waveform] || 'Sine';
    return `${waveName} ${config.frequencyHz}Hz ±${config.amplitudeMM}mm`;
  } else if (mode === 'chaos') {
    return `Chaos ${config.durationSeconds}s (${config.crazinessPercent}%)`;
  }
  return 'Preset';
}

// ============================================================================
// TOOLTIP GENERATION (Pure Functions)
// ============================================================================

/**
 * Generate tooltip content for a preset (pure)
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @param {Object} config - Configuration object
 * @returns {string} Tooltip HTML string
 */
function generatePresetTooltipPure(mode, config) {
  if (mode === 'simple') {
    let tooltip = `📍 Départ: ${config.startPositionMM || 0}mm\n`;
    tooltip += `📏 Distance: ${config.distanceMM || 50}mm\n`;
    tooltip += `⚡ Vitesse: ${config.speedLevelForward || 5}/${config.speedLevelBackward || 5}`;
    if (config.cycleCount !== undefined) {
      tooltip += `\n🔄 Cycles: ${config.cycleCount === 0 ? '∞' : config.cycleCount}`;
    }
    // Zone Effects info (new format or legacy)
    const ze = config.vaetZoneEffect;
    if (ze && ze.enabled) {
      const pos = [];
      if (ze.enableStart) pos.push('D');
      if (ze.enableEnd) pos.push('F');
      if (ze.mirrorOnReturn) pos.push('🔀');
      tooltip += `\n🎯 Zone: ${pos.join('/')} ${ze.zoneMM}mm`;
      if (ze.randomTurnbackEnabled) tooltip += ` 🔄${ze.turnbackChance}%`;
      if (ze.endPauseEnabled) tooltip += ' ⏸';
    } else if (config.decelStartEnabled || config.decelEndEnabled) {
      // Legacy format
      const pos = [];
      if (config.decelStartEnabled) pos.push('D');
      if (config.decelEndEnabled) pos.push('F');
      tooltip += `\n🎯 Décel: ${pos.join('/')} ${config.decelZoneMM || 20}mm`;
    }
    return tooltip;
  } else if (mode === 'oscillation') {
    let tooltip = `📍 Centre: ${config.centerPositionMM || 100}mm\n`;
    tooltip += `↔️ Amplitude: ±${config.amplitudeMM || 20}mm\n`;
    tooltip += `🌊 Forme: ${WAVEFORM_NAMES[config.waveform] || 'Sine'}\n`;
    tooltip += `⚡ Fréquence: ${config.frequencyHz || 1}Hz`;
    if (config.cycleCount !== undefined) {
      tooltip += `\n🔄 Cycles: ${config.cycleCount === 0 ? '∞' : config.cycleCount}`;
    }
    return tooltip;
  } else if (mode === 'chaos') {
    let tooltip = `📍 Centre: ${config.centerPositionMM || 100}mm\n`;
    tooltip += `↔️ Amplitude: ±${config.amplitudeMM || 40}mm\n`;
    tooltip += `🎲 Folie: ${config.crazinessPercent || 50}%\n`;
    tooltip += `⏱️ Durée: ${config.durationSeconds === 0 ? '∞' : config.durationSeconds + 's'}`;
    return tooltip;
  }
  return 'Preset';
}

/**
 * Generate tooltip content for sequence line (pure)
 * @param {Object} line - Sequence line object
 * @returns {string} HTML tooltip content
 */
function generateSequenceLineTooltipPure(line) {
  const typeName = TYPE_NAMES[line.movementType] || 'Inconnu';
  
  let tooltip = `<b>${typeName}</b><br>`;
  
  if (line.movementType === 0) {
    // Simple/Va-et-vient
    tooltip += `📍 Départ: ${line.startPositionMM?.toFixed(1) || 0}mm<br>`;
    tooltip += `📏 Distance: ${line.distanceMM?.toFixed(1) || 50}mm<br>`;
    tooltip += `⚡ Vitesse: ${line.speedForward?.toFixed(1) || 5}/${line.speedBackward?.toFixed(1) || 5}`;
    if (line.cycles !== undefined) {
      tooltip += `<br>🔄 Cycles: ${line.cycles === 0 ? '∞' : line.cycles}`;
    }
  } else if (line.movementType === 1) {
    // Oscillation
    tooltip += `📍 Centre: ${line.oscCenterPositionMM?.toFixed(1) || 100}mm<br>`;
    tooltip += `↔️ Amplitude: ±${line.oscAmplitudeMM?.toFixed(1) || 20}mm<br>`;
    tooltip += `🌊 Fréquence: ${line.oscFrequencyHz?.toFixed(2) || 1}Hz`;
    if (line.oscWaveform !== undefined) {
      tooltip += `<br>📈 Forme: ${WAVEFORM_NAMES[line.oscWaveform] || 'Sine'}`;
    }
    if (line.oscCycles !== undefined) {
      tooltip += `<br>🔄 Cycles: ${line.oscCycles === 0 ? '∞' : line.oscCycles}`;
    }
  } else if (line.movementType === 2) {
    // Chaos
    tooltip += `📍 Centre: ${line.chaosCenterPositionMM?.toFixed(1) || 100}mm<br>`;
    tooltip += `↔️ Amplitude: ±${line.chaosAmplitudeMM?.toFixed(1) || 40}mm<br>`;
    tooltip += `🎲 Folie: ${line.chaosCrazinessPercent?.toFixed(0) || 50}%<br>`;
    tooltip += `⏱️ Durée: ${line.chaosDurationSeconds || 30}s`;
  }
  
  return tooltip;
}

// ============================================================================
// TOOLTIP DISPLAY FUNCTIONS
// ============================================================================

/**
 * Show playlist tooltip overlay
 * @param {HTMLElement} element - Element with data-tooltip attribute
 */
function showPlaylistTooltip(element) {
  const tooltipContent = element.getAttribute('data-tooltip');
  const overlay = document.getElementById('playlistTooltipOverlay');
  if (overlay && tooltipContent) {
    overlay.innerHTML = tooltipContent;
    overlay.classList.add('visible');
  }
}

/**
 * Hide playlist tooltip overlay
 */
function hidePlaylistTooltip() {
  const overlay = document.getElementById('playlistTooltipOverlay');
  if (overlay) {
    overlay.classList.remove('visible');
  }
}

/**
 * Show sequence line tooltip with header
 * @param {HTMLElement} element - Element with tooltip data
 */
function showSequenceTooltip(element) {
  const tooltipContent = element.getAttribute('data-tooltip');
  const lineNumber = element.getAttribute('data-line-number');
  const lineType = element.getAttribute('data-line-type');
  
  const overlay = document.getElementById('playlistTooltipOverlay');
  if (overlay && tooltipContent) {
    const header = `<div style="font-weight: 600; margin-bottom: 8px; font-size: 14px; border-bottom: 2px solid rgba(255,255,255,0.3); padding-bottom: 6px;">#${lineNumber} - ${lineType}</div>`;
    overlay.innerHTML = header + tooltipContent;
    overlay.classList.add('visible');
  }
}

// ============================================================================
// PRESET BUTTON STATE UPDATES
// ============================================================================

/**
 * Update start position preset buttons based on max distance
 * @param {number} maxDist - Maximum allowed distance in mm
 */
function updateStartPresets(maxDist) {
  // Use cached NodeList for performance (called ~50 times/second via updateUI)
  if (typeof DOM !== 'undefined' && DOM.presetStartButtons) {
    DOM.presetStartButtons.forEach(btn => {
      const startPos = parseFloat(btn.getAttribute('data-start'));
      const isValid = startPos <= maxDist;
      btn.disabled = !isValid;
      btn.style.opacity = isValid ? '1' : '0.3';
      btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
    });
  }
}

/**
 * Update distance preset buttons based on max available distance
 * @param {number} maxAvailable - Maximum available distance in mm
 */
function updateDistancePresets(maxAvailable) {
  // Use cached NodeList for performance (called ~50 times/second via updateUI)
  if (typeof DOM !== 'undefined' && DOM.presetDistanceButtons) {
    DOM.presetDistanceButtons.forEach(btn => {
      const distance = parseFloat(btn.getAttribute('data-distance'));
      const isValid = distance <= maxAvailable;
      btn.disabled = !isValid;
      btn.style.opacity = isValid ? '1' : '0.3';
      btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
    });
  }
}

// ============================================================================
// MODAL TITLE GENERATION
// ============================================================================

/**
 * Get playlist modal title based on mode
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @returns {string} Modal title
 */
function getPlaylistModalTitlePure(mode) {
  const icon = MODE_ICONS[mode] || '📋';
  const modeNames = {
    simple: 'Simple',
    oscillation: 'Oscillation',
    chaos: 'Chaos'
  };
  return `${icon} Playlist ${modeNames[mode] || mode}`;
}

console.log('✅ PlaylistUtils.js loaded - Pure utility functions for Playlist');
