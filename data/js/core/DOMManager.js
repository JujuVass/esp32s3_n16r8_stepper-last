/**
 * ============================================================================
 * DOMManager.js - DOM Element Cache & Management
 * ============================================================================
 * Centralized DOM element caching for performance optimization.
 * Caches frequently accessed elements to avoid repeated getElementById() calls.
 * 
 * Usage:
 *   - Call initDOMCache() once on page load (before any DOM operations)
 *   - Access elements via DOM.elementName (e.g., DOM.btnStart, DOM.state)
 * 
 * Benefits:
 *   - Faster UI updates (~50 WebSocket messages/second)
 *   - Cleaner code (no repeated getElementById calls)
 *   - Single point of maintenance for element references
 * 
 * Dependencies: None (must load before main.js)
 * Created: December 2024 (extracted from main.js)
 * ============================================================================
 */

// ============================================================================
// DOM ELEMENT CACHE
// ============================================================================

/**
 * Global DOM cache object - stores references to frequently used elements
 * Elements are grouped by functional area for better organization
 */
const DOM = {};

/**
 * Initialize the DOM element cache
 * Must be called once on page load, before any other DOM operations
 * @returns {number} Number of cached elements
 */
function initDOMCache() {
  // ========================================================================
  // STATUS DISPLAY ELEMENTS (updated ~50 times/second via WebSocket)
  // ========================================================================
  DOM.state = document.getElementById('state');
  DOM.position = document.getElementById('position');
  DOM.currentStep = document.getElementById('currentStep');
  DOM.totalDist = document.getElementById('totalDist');
  DOM.totalTraveled = document.getElementById('totalTraveled');
  DOM.milestoneIcon = document.getElementById('milestoneIcon');
  DOM.progress = document.getElementById('progress');
  DOM.debugMovement = document.getElementById('debugMovement');

  // ========================================================================
  // INPUT FIELDS (Simple mode)
  // ========================================================================
  DOM.startPosition = document.getElementById('startPosition');
  DOM.distance = document.getElementById('distance');
  DOM.speedUnified = document.getElementById('speedUnified');
  DOM.speedForward = document.getElementById('speedForward');
  DOM.speedBackward = document.getElementById('speedBackward');

  // Info displays (some removed in compact mode)
  DOM.speedUnifiedInfo = document.getElementById('speedUnifiedInfo');
  DOM.speedForwardInfo = document.getElementById('speedForwardInfo');
  DOM.speedBackwardInfo = document.getElementById('speedBackwardInfo');
  DOM.maxStart = document.getElementById('maxStart');
  DOM.maxDist = document.getElementById('maxDist');

  // ========================================================================
  // CONTROL BUTTONS
  // ========================================================================
  DOM.btnStart = document.getElementById('btnStart');
  DOM.btnPause = document.getElementById('btnPause');
  DOM.btnStop = document.getElementById('btnStop');
  DOM.btnCalibrateCommon = document.getElementById('btnCalibrateCommon');
  DOM.btnResetDistanceCommon = document.getElementById('btnResetDistanceCommon');

  // ========================================================================
  // PURSUIT MODE ELEMENTS
  // ========================================================================
  DOM.gaugePosition = document.getElementById('gaugePosition');
  DOM.gaugeCursor = document.getElementById('gaugeCursor');
  DOM.gaugeContainer = document.getElementById('gaugeContainer');
  DOM.gaugeLimitLine = document.getElementById('gaugeLimitLine');
  DOM.currentPositionMM = document.getElementById('currentPositionMM');
  DOM.targetPositionMM = document.getElementById('targetPositionMM');
  DOM.positionError = document.getElementById('positionError');
  DOM.pursuitActiveCheckbox = document.getElementById('pursuitActiveCheckbox');
  DOM.btnActivatePursuit = document.getElementById('btnActivatePursuit');
  DOM.btnStopPursuit = document.getElementById('btnStopPursuit');
  DOM.pursuitMaxSpeed = document.getElementById('pursuitMaxSpeed');

  // ========================================================================
  // MAX DISTANCE LIMIT CONFIGURATION
  // ========================================================================
  DOM.btnConfigMaxDist = document.getElementById('btnConfigMaxDist');
  DOM.maxDistConfigPanel = document.getElementById('maxDistConfigPanel');
  DOM.maxDistLimitSlider = document.getElementById('maxDistLimitSlider');
  DOM.maxDistLimitValue = document.getElementById('maxDistLimitValue');
  DOM.maxDistLimitMM = document.getElementById('maxDistLimitMM');
  DOM.btnApplyMaxDistLimit = document.getElementById('btnApplyMaxDistLimit');
  DOM.btnCancelMaxDistLimit = document.getElementById('btnCancelMaxDistLimit');
  DOM.maxDistLimitWarning = document.getElementById('maxDistLimitWarning');

  // ========================================================================
  // OSCILLATION MODE ELEMENTS
  // ========================================================================
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
  DOM.oscLimitWarning = document.getElementById('oscLimitWarning');
  DOM.oscLimitStatus = document.getElementById('oscLimitStatus');
  DOM.btnStartOscillation = document.getElementById('btnStartOscillation');
  DOM.btnStopOscillation = document.getElementById('btnStopOscillation');
  DOM.btnPauseOscillation = document.getElementById('btnPauseOscillation');

  // Oscillation Cycle Pause
  DOM.oscCyclePauseEnabled = document.getElementById('oscCyclePauseEnabled');
  DOM.oscCyclePauseRandom = document.getElementById('oscCyclePauseRandom');
  DOM.oscCyclePauseDuration = document.getElementById('oscCyclePauseDuration');
  DOM.oscCyclePauseMin = document.getElementById('oscCyclePauseMin');
  DOM.oscCyclePauseMax = document.getElementById('oscCyclePauseMax');

  // ========================================================================
  // CHAOS MODE ELEMENTS
  // ========================================================================
  DOM.chaosStats = document.getElementById('chaosStats');
  DOM.btnStartChaos = document.getElementById('btnStartChaos');
  DOM.btnStopChaos = document.getElementById('btnStopChaos');
  DOM.btnPauseChaos = document.getElementById('btnPauseChaos');

  // Chaos configuration inputs
  DOM.chaosMinAmplitude = document.getElementById('chaosMinAmplitude');
  DOM.chaosMaxAmplitude = document.getElementById('chaosMaxAmplitude');
  DOM.chaosMinSpeed = document.getElementById('chaosMinSpeed');
  DOM.chaosMaxSpeed = document.getElementById('chaosMaxSpeed');
  DOM.chaosDuration = document.getElementById('chaosDuration');
  DOM.chaosPatternSelect = document.getElementById('chaosPatternSelect');

  // ========================================================================
  // SEQUENCER MODE ELEMENTS
  // ========================================================================
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
  DOM.btnAddLine = document.getElementById('btnAddLine');
  DOM.btnClearAll = document.getElementById('btnClearAll');
  DOM.btnImportSeq = document.getElementById('btnImportSeq');
  DOM.btnExportSeq = document.getElementById('btnExportSeq');
  DOM.sequenceTableBody = document.getElementById('sequenceTableBody');

  // ========================================================================
  // SIMPLE MODE CYCLE PAUSE
  // ========================================================================
  DOM.cyclePauseEnabled = document.getElementById('cyclePauseEnabled');
  DOM.cyclePauseRandom = document.getElementById('cyclePauseRandom');
  DOM.cyclePauseDuration = document.getElementById('cyclePauseDuration');
  DOM.cyclePauseMin = document.getElementById('cyclePauseMin');
  DOM.cyclePauseMax = document.getElementById('cyclePauseMax');

  // ========================================================================
  // LOGS PANEL ELEMENTS
  // ========================================================================
  DOM.logConsolePanel = document.getElementById('logConsolePanel');
  DOM.logsPanel = document.getElementById('logsPanel');
  DOM.btnShowLogs = document.getElementById('btnShowLogs');
  DOM.btnCloseLogs = document.getElementById('btnCloseLogs');
  DOM.logFilesList = document.getElementById('logFilesList');
  DOM.debugLevelCheckbox = document.getElementById('debugLevelCheckbox');
  DOM.btnClearLogsPanel = document.getElementById('btnClearLogsPanel');
  DOM.btnClearAllLogFiles = document.getElementById('btnClearAllLogFiles');

  // ========================================================================
  // STATS PANEL ELEMENTS
  // ========================================================================
  DOM.statsPanel = document.getElementById('statsPanel');
  DOM.btnShowStats = document.getElementById('btnShowStats');
  DOM.btnCloseStats = document.getElementById('btnCloseStats');
  DOM.btnClearStats = document.getElementById('btnClearStats');
  DOM.btnExportStats = document.getElementById('btnExportStats');
  DOM.btnImportStats = document.getElementById('btnImportStats');
  DOM.statsFileInput = document.getElementById('statsFileInput');
  DOM.statsTableBody = document.getElementById('statsTableBody');
  DOM.statsChartCanvas = document.getElementById('statsChart');

  // ========================================================================
  // SYSTEM PANEL ELEMENTS
  // ========================================================================
  DOM.systemPanel = document.getElementById('systemPanel');
  DOM.btnShowSystem = document.getElementById('btnShowSystem');
  DOM.btnCloseSystem = document.getElementById('btnCloseSystem');
  DOM.btnRefreshWifi = document.getElementById('btnRefreshWifi');
  DOM.btnReboot = document.getElementById('btnReboot');
  DOM.chkLoggingEnabled = document.getElementById('chkLoggingEnabled');
  DOM.chkDebugLevel = document.getElementById('chkDebugLevel');

  // System stats display
  DOM.sysUptime = document.getElementById('sysUptime');
  DOM.sysHeapFree = document.getElementById('sysHeapFree');
  DOM.sysHeapMin = document.getElementById('sysHeapMin');
  DOM.sysPsramFree = document.getElementById('sysPsramFree');
  DOM.sysWifiRssi = document.getElementById('sysWifiRssi');
  DOM.sysWifiIp = document.getElementById('sysWifiIp');
  DOM.sysCpuFreq = document.getElementById('sysCpuFreq');
  DOM.sysFlashSize = document.getElementById('sysFlashSize');

  // ========================================================================
  // MODALS & OVERLAYS
  // ========================================================================
  DOM.calibrationOverlay = document.getElementById('calibrationOverlay');
  DOM.rebootOverlay = document.getElementById('rebootOverlay');
  DOM.stopModal = document.getElementById('stopModal');
  DOM.modeChangeModal = document.getElementById('modeChangeModal');
  DOM.sequencerLimitModal = document.getElementById('sequencerLimitModal');
  DOM.playlistModal = document.getElementById('playlistModal');
  DOM.pendingChanges = document.getElementById('pendingChanges');

  // Modal controls
  DOM.returnToStartCheckbox = document.getElementById('returnToStartCheckbox');
  DOM.bypassCalibrationCheckbox = document.getElementById('bypassCalibrationCheckbox');

  // ========================================================================
  // PLAYLIST ELEMENTS
  // ========================================================================
  DOM.btnManagePlaylistSimple = document.getElementById('btnManagePlaylistSimple');
  DOM.btnManagePlaylistOscillation = document.getElementById('btnManagePlaylistOscillation');
  DOM.btnManagePlaylistChaos = document.getElementById('btnManagePlaylistChaos');
  DOM.btnAddCurrentToPlaylist = document.getElementById('btnAddCurrentToPlaylist');
  DOM.playlistPresetsContainer = document.getElementById('playlistPresetsContainer');
  DOM.playlistSearchInput = document.getElementById('playlistSearchInput');

  // ========================================================================
  // TABS
  // ========================================================================
  DOM.tabs = document.querySelectorAll('.tab');
  DOM.tabContents = document.querySelectorAll('.tab-content');

  // ========================================================================
  // PRESET BUTTONS (querySelectorAll cache for performance)
  // ========================================================================
  DOM.presetStartButtons = document.querySelectorAll('[data-start]');
  DOM.presetDistanceButtons = document.querySelectorAll('[data-distance]');
  DOM.presetSpeedUnifiedButtons = document.querySelectorAll('[data-speed-unified]');
  DOM.presetSpeedForwardButtons = document.querySelectorAll('[data-speed-forward]');
  DOM.presetSpeedBackwardButtons = document.querySelectorAll('[data-speed-backward]');
  DOM.presetPursuitSpeedButtons = document.querySelectorAll('[data-pursuit-speed]');

  // ========================================================================
  // COUNT & LOG
  // ========================================================================
  const elementCount = Object.keys(DOM).length;
  console.log('✅ DOM cache initialized (' + elementCount + ' elements)');
  
  return elementCount;
}

// ============================================================================
// DOM HELPER FUNCTIONS
// ============================================================================

/**
 * Safely get a DOM element value (handles null elements)
 * @param {string} key - The DOM cache key
 * @param {string} property - The property to get (default: 'value')
 * @param {*} defaultValue - Default value if element doesn't exist
 * @returns {*} The element's property value or default
 */
function getDOMValue(key, property = 'value', defaultValue = null) {
  const element = DOM[key];
  if (!element) return defaultValue;
  return element[property] !== undefined ? element[property] : defaultValue;
}

/**
 * Safely set a DOM element value (handles null elements)
 * @param {string} key - The DOM cache key
 * @param {*} value - The value to set
 * @param {string} property - The property to set (default: 'value')
 * @returns {boolean} True if successful, false if element not found
 */
function setDOMValue(key, value, property = 'value') {
  const element = DOM[key];
  if (!element) return false;
  element[property] = value;
  return true;
}

/**
 * Safely set textContent of a DOM element
 * @param {string} key - The DOM cache key
 * @param {string} text - The text to set
 * @returns {boolean} True if successful
 */
function setDOMText(key, text) {
  return setDOMValue(key, text, 'textContent');
}

/**
 * Safely show/hide a DOM element
 * @param {string} key - The DOM cache key
 * @param {boolean} visible - Whether to show or hide
 * @param {string} displayType - Display type when visible (default: 'block')
 */
function setDOMVisible(key, visible, displayType = 'block') {
  const element = DOM[key];
  if (!element) return;
  element.style.display = visible ? displayType : 'none';
}

/**
 * Safely enable/disable a DOM element (button, input, etc.)
 * @param {string} key - The DOM cache key
 * @param {boolean} enabled - Whether to enable or disable
 */
function setDOMEnabled(key, enabled) {
  const element = DOM[key];
  if (!element) return;
  element.disabled = !enabled;
  element.style.opacity = enabled ? '1' : '0.5';
  element.style.cursor = enabled ? 'pointer' : 'not-allowed';
}
