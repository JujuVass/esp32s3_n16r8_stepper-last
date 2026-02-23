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
  DOM.currentSpeed = document.getElementById('currentSpeed');
  DOM.speedIcon = document.getElementById('speedIcon');
  DOM.realSpeed = document.getElementById('realSpeed');
  DOM.progressMini = document.getElementById('progressMini');
  DOM.progressPct = document.getElementById('progressPct');
  DOM.sensorsInvertedIcon = document.getElementById('sensorsInvertedIcon');

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
  DOM.chkSensorsInverted = document.getElementById('chkSensorsInverted');
  DOM.sensorsInvertedStatus = document.getElementById('sensorsInvertedStatus');

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
  DOM.statsTotalDistance = document.getElementById('statsTotalDistance');
  DOM.statsTotalMilestone = document.getElementById('statsTotalMilestone');
  DOM.statsRecordingEnabled = document.getElementById('statsRecordingEnabled');
  DOM.statsRecordingWarning = document.getElementById('statsRecordingWarning');

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
  DOM.sysCpuFreq = document.getElementById('sysCpuFreq');
  DOM.sysTemp = document.getElementById('sysTemp');
  DOM.sysRam = document.getElementById('sysRam');
  DOM.sysRamPercent = document.getElementById('sysRamPercent');
  DOM.sysPsram = document.getElementById('sysPsram');
  DOM.sysPsramPercent = document.getElementById('sysPsramPercent');
  DOM.sysWifi = document.getElementById('sysWifi');
  DOM.sysWifiQuality = document.getElementById('sysWifiQuality');
  DOM.sysIpSta = document.getElementById('sysIpSta');
  DOM.sysIpAp = document.getElementById('sysIpAp');
  DOM.sysHostname = document.getElementById('sysHostname');
  DOM.sysSsid = document.getElementById('sysSsid');
  DOM.sysApClients = document.getElementById('sysApClients');
  DOM.sysDegradedBadge = document.getElementById('sysDegradedBadge');
  DOM.networkInfoSection = document.getElementById('networkInfoSection');

  // ========================================================================
  // MODALS & OVERLAYS
  // ========================================================================
  DOM.calibrationOverlay = document.getElementById('calibrationOverlay');
  DOM.rebootOverlay = document.getElementById('rebootOverlay');
  DOM.updateOverlay = document.getElementById('updateOverlay');
  DOM.rebootMessage = document.getElementById('rebootMessage');
  DOM.rebootStatus = document.getElementById('rebootStatus');
  DOM.stopModal = document.getElementById('stopModal');
  DOM.modeChangeModal = document.getElementById('modeChangeModal');
  DOM.sequencerLimitModal = document.getElementById('sequencerLimitModal');
  DOM.playlistModal = document.getElementById('playlistModal');
  DOM.pendingChanges = document.getElementById('pendingChanges');

  // Unified Alert Modal
  DOM.unifiedAlertModal = document.getElementById('unifiedAlertModal');
  DOM.unifiedAlertIcon = document.getElementById('unifiedAlertIcon');
  DOM.unifiedAlertTitle = document.getElementById('unifiedAlertTitle');
  DOM.unifiedAlertMessage = document.getElementById('unifiedAlertMessage');
  DOM.unifiedAlertOkBtn = document.getElementById('unifiedAlertOkBtn');

  // Unified Confirm Modal
  DOM.unifiedConfirmModal = document.getElementById('unifiedConfirmModal');
  DOM.unifiedConfirmIcon = document.getElementById('unifiedConfirmIcon');
  DOM.unifiedConfirmTitle = document.getElementById('unifiedConfirmTitle');
  DOM.unifiedConfirmMessage = document.getElementById('unifiedConfirmMessage');
  DOM.unifiedConfirmOkBtn = document.getElementById('unifiedConfirmOkBtn');
  DOM.unifiedConfirmCancelBtn = document.getElementById('unifiedConfirmCancelBtn');

  // Unified Prompt Modal
  DOM.unifiedPromptModal = document.getElementById('unifiedPromptModal');
  DOM.unifiedPromptIcon = document.getElementById('unifiedPromptIcon');
  DOM.unifiedPromptTitle = document.getElementById('unifiedPromptTitle');
  DOM.unifiedPromptMessage = document.getElementById('unifiedPromptMessage');
  DOM.unifiedPromptInput = document.getElementById('unifiedPromptInput');
  DOM.unifiedPromptOkBtn = document.getElementById('unifiedPromptOkBtn');
  DOM.unifiedPromptCancelBtn = document.getElementById('unifiedPromptCancelBtn');

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
  // TABS & LAYOUT
  // ========================================================================
  DOM.tabsContainer = document.getElementById('tabsContainer');
  DOM.welcomeMessage = document.getElementById('welcomeMessage');
  DOM.speedModeSeparate = document.getElementById('speedModeSeparate');
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
  console.debug('✅ DOM cache initialized (' + elementCount + ' elements)');
  
  return elementCount;
}
