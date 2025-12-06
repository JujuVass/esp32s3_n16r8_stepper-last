/**
 * DOMManager.js - Centralized DOM Cache Manager
 * 
 * Cache les éléments DOM fréquemment accédés pour éviter les appels répétés à getElementById().
 * Optimisation critique car updateUI() est appelé ~50 fois/seconde via WebSocket.
 * 
 * Usage:
 *   - DOM.state, DOM.position, etc. sont accessibles globalement après initDOMCache()
 *   - Appeler initDOMCache() une seule fois au démarrage (DOMContentLoaded)
 */

// ========================================================================
// DOM CACHE - Performance Optimization
// ========================================================================
// Cache frequently accessed DOM elements to avoid repeated getElementById() calls
const DOM = {};

function initDOMCache() {
  // Status elements (updated ~50 times/second via WebSocket)
  DOM.state = document.getElementById('state');
  DOM.position = document.getElementById('position');
  DOM.currentStep = document.getElementById('currentStep');
  DOM.totalDist = document.getElementById('totalDist');
  DOM.totalTraveled = document.getElementById('totalTraveled');
  DOM.milestoneIcon = document.getElementById('milestoneIcon');
  DOM.progress = document.getElementById('progress');
  DOM.debugMovement = document.getElementById('debugMovement');
  
  // Input fields
  DOM.startPosition = document.getElementById('startPosition');
  DOM.distance = document.getElementById('distance');
  DOM.speedUnified = document.getElementById('speedUnified');
  DOM.speedForward = document.getElementById('speedForward');
  DOM.speedBackward = document.getElementById('speedBackward');
  
  // Info displays (removed in compact mode)
  DOM.speedUnifiedInfo = document.getElementById('speedUnifiedInfo'); // Removed
  DOM.speedForwardInfo = document.getElementById('speedForwardInfo'); // Removed
  DOM.speedBackwardInfo = document.getElementById('speedBackwardInfo'); // Removed
  DOM.maxStart = document.getElementById('maxStart'); // Removed in compact mode
  DOM.maxDist = document.getElementById('maxDist'); // Removed in compact mode
  
  // Buttons
  DOM.btnStart = document.getElementById('btnStart');
  DOM.btnCalibrateCommon = document.getElementById('btnCalibrateCommon');
  
  // Pursuit elements
  DOM.gaugePosition = document.getElementById('gaugePosition');
  DOM.gaugeCursor = document.getElementById('gaugeCursor');
  DOM.gaugeContainer = document.getElementById('gaugeContainer');
  DOM.gaugeLimitLine = document.getElementById('gaugeLimitLine');
  DOM.currentPositionMM = document.getElementById('currentPositionMM');
  DOM.targetPositionMM = document.getElementById('targetPositionMM');
  DOM.positionError = document.getElementById('positionError');
  DOM.pursuitActiveCheckbox = document.getElementById('pursuitActiveCheckbox');
  DOM.btnActivatePursuit = document.getElementById('btnActivatePursuit');
  
  // Max distance limit elements
  DOM.btnConfigMaxDist = document.getElementById('btnConfigMaxDist');
  DOM.maxDistConfigPanel = document.getElementById('maxDistConfigPanel');
  DOM.maxDistLimitSlider = document.getElementById('maxDistLimitSlider');
  DOM.maxDistLimitValue = document.getElementById('maxDistLimitValue');
  DOM.maxDistLimitMM = document.getElementById('maxDistLimitMM');
  DOM.btnApplyMaxDistLimit = document.getElementById('btnApplyMaxDistLimit');
  DOM.btnCancelMaxDistLimit = document.getElementById('btnCancelMaxDistLimit');
  DOM.maxDistLimitWarning = document.getElementById('maxDistLimitWarning');
  
  // Oscillation elements
  DOM.oscCurrentAmplitude = document.getElementById('oscCurrentAmplitude');
  DOM.oscCompletedCycles = document.getElementById('oscCompletedCycles');
  DOM.oscRampStatus = document.getElementById('oscRampStatus');
  DOM.oscCenter = document.getElementById('oscCenter');
  DOM.oscAmplitude = document.getElementById('oscAmplitude');
  DOM.oscWaveform = document.getElementById('oscWaveform');
  DOM.oscFrequency = document.getElementById('oscFrequency');
  DOM.oscRampInDuration = document.getElementById('oscRampInDuration');
  DOM.oscRampOutDuration = document.getElementById('oscRampOutDuration');
  DOM.oscCycleCount = document.getElementById('oscCycleCount');
  DOM.oscRampInEnable = document.getElementById('oscRampInEnable');
  DOM.oscRampOutEnable = document.getElementById('oscRampOutEnable');
  DOM.oscReturnCenter = document.getElementById('oscReturnCenter');
  // DOM.oscRampInConfig = document.getElementById('oscRampInConfig'); // Removed in compact mode
  // DOM.oscRampOutConfig = document.getElementById('oscRampOutConfig'); // Removed in compact mode
  DOM.oscLimitWarning = document.getElementById('oscLimitWarning');
  DOM.oscLimitStatus = document.getElementById('oscLimitStatus');
  DOM.btnStartOscillation = document.getElementById('btnStartOscillation');
  
  // Logs panel
  DOM.logConsolePanel = document.getElementById('logConsolePanel');
  DOM.logsPanel = document.getElementById('logsPanel');
  DOM.btnShowLogs = document.getElementById('btnShowLogs');
  DOM.logFilesList = document.getElementById('logFilesList');
  
  // Sequencer
  DOM.seqMode = document.getElementById('seqMode');
  DOM.seqCurrentLine = document.getElementById('seqCurrentLine');
  DOM.seqLineCycle = document.getElementById('seqLineCycle');
  DOM.seqLoopCount = document.getElementById('seqLoopCount');
  DOM.seqPauseRemaining = document.getElementById('seqPauseRemaining');
  DOM.btnStartSequence = document.getElementById('btnStartSequence');
  DOM.btnLoopSequence = document.getElementById('btnLoopSequence');
  DOM.btnPauseSequence = document.getElementById('btnPauseSequence');
  DOM.btnStopSequence = document.getElementById('btnStopSequence');
  DOM.btnSkipLine = document.getElementById('btnSkipLine');
  
  // Pending changes
  DOM.pendingChanges = document.getElementById('pendingChanges');
  
  // Calibration overlay
  DOM.calibrationOverlay = document.getElementById('calibrationOverlay');
  
  // Reboot overlay
  DOM.rebootOverlay = document.getElementById('rebootOverlay');
  
  // Chaos stats
  DOM.chaosStats = document.getElementById('chaosStats');
  DOM.btnStartChaos = document.getElementById('btnStartChaos');
  DOM.btnStopChaos = document.getElementById('btnStopChaos');
  
  // Preset buttons (querySelectorAll cache for performance)
  DOM.presetStartButtons = document.querySelectorAll('[data-start]');
  DOM.presetDistanceButtons = document.querySelectorAll('[data-distance]');
  
  console.log('✅ DOM cache initialized (' + Object.keys(DOM).length + ' elements)');
}

// ========================================================================
// HELPER FUNCTIONS
// ========================================================================

/**
 * Get a cached DOM element or fetch it if not cached
 * @param {string} id - Element ID
 * @returns {HTMLElement|null}
 */
function getDOM(id) {
  if (!DOM[id]) {
    DOM[id] = document.getElementById(id);
  }
  return DOM[id];
}

/**
 * Refresh a specific DOM cache entry (useful after dynamic content changes)
 * @param {string} key - DOM cache key
 * @param {string} id - Element ID (optional, defaults to key)
 */
function refreshDOMCache(key, id) {
  DOM[key] = document.getElementById(id || key);
  return DOM[key];
}
