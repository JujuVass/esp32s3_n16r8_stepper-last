/**
 * ============================================================================
 * core/app.js - Global Application State, Constants & Context
 * ============================================================================
 * Merged from: app.js + context.js
 * 
 * Contents:
 * - AppState: Global application state
 * - PlaylistState: Preset management state  
 * - WS_CMD: WebSocket command constants
 * - SystemState: State machine enum
 * - Context: Dependency injection container
 * - Unit conversion utilities
 * ============================================================================
 */

// ============================================================================
// GLOBAL APPLICATION STATE
// ============================================================================
const AppState = {
  ws: null,
  editing: { input: null, oscField: null, lineId: null },
  pursuit: {
    totalDistanceMM: 0, effectiveMaxDistMM: 0, currentPositionMM: 0,
    targetMM: 0, isDragging: false, lastCommandTime: 0, active: false
  },
  system: {
    currentState: 0, canStart: false, currentMode: 'simple', pendingModeSwitch: null
  },
  flags: { patternsInitialized: false, domCacheReady: false },
  milestone: { current: null, lastThreshold: 0 },
  statsPanel: { isVisible: false, lastToggle: 0 },
  logging: { enabled: false, debugEnabled: false }
};

// ============================================================================
// PLAYLIST STATE
// ============================================================================
const PlaylistState = {
  simple: [], oscillation: [], chaos: [], loaded: false
};

// ============================================================================
// SYSTEM STATE ENUM
// ============================================================================
const SystemState = Object.freeze({
  INIT: 0, CALIBRATING: 1, READY: 2, RUNNING: 3, PAUSED: 4, ERROR: 5
});

// ============================================================================
// WEBSOCKET COMMANDS
// ============================================================================
const WS_CMD = Object.freeze({
  // Movement Control
  START: 'start', STOP: 'stop', PAUSE: 'pause', CALIBRATE: 'calibrate',
  RETURN_TO_START: 'returnToStart', RESET_TOTAL_DISTANCE: 'resetTotalDistance',
  // Simple Mode
  SET_START_POSITION: 'setStartPosition', SET_DISTANCE: 'setDistance',
  SET_SPEED_FORWARD: 'setSpeedForward', SET_SPEED_BACKWARD: 'setSpeedBackward',
  SET_CYCLE_PAUSE: 'setCyclePause', SET_DECEL_ZONE: 'setDecelZone',
  SET_MAX_DISTANCE_LIMIT: 'setMaxDistanceLimit', UPDATE_CYCLE_PAUSE: 'updateCyclePause',
  // Oscillation Mode
  SET_OSCILLATION: 'setOscillation', SET_OSCILLATION_CONFIG: 'setOscillationConfig',
  START_OSCILLATION: 'startOscillation', STOP_OSCILLATION: 'stopOscillation',
  UPDATE_CYCLE_PAUSE_OSC: 'updateCyclePauseOsc',
  // Chaos Mode
  SET_CHAOS_CONFIG: 'setChaosConfig', START_CHAOS: 'startChaos', STOP_CHAOS: 'stopChaos',
  // Sequence Mode
  ADD_SEQUENCE_LINE: 'addSequenceLine', DELETE_SEQUENCE_LINE: 'deleteSequenceLine',
  UPDATE_SEQUENCE_LINE: 'updateSequenceLine', MOVE_SEQUENCE_LINE: 'moveSequenceLine',
  DUPLICATE_SEQUENCE_LINE: 'duplicateSequenceLine', TOGGLE_SEQUENCE_LINE: 'toggleSequenceLine',
  START_SEQUENCE: 'startSequence', STOP_SEQUENCE: 'stopSequence', LOOP_SEQUENCE: 'loopSequence',
  TOGGLE_SEQUENCE_PAUSE: 'toggleSequencePause', SKIP_SEQUENCE_LINE: 'skipSequenceLine',
  CLEAR_SEQUENCE: 'clearSequence', EXPORT_SEQUENCE: 'exportSequence', GET_SEQUENCE_TABLE: 'getSequenceTable',
  // Pursuit Mode
  PURSUIT_MOVE: 'pursuitMove', ENABLE_PURSUIT_MODE: 'enablePursuitMode', DISABLE_PURSUIT_MODE: 'disablePursuitMode',
  // Status & Stats
  GET_STATUS: 'getStatus', SAVE_STATS: 'saveStats'
});

// ============================================================================
// CONSOLE WRAPPER - Respect logging preferences
// ============================================================================
const _console = {
  log: console.log.bind(console), error: console.error.bind(console),
  warn: console.warn.bind(console), debug: console.debug.bind(console), info: console.info.bind(console)
};
console.log = (...args) => { if (AppState.logging.enabled) _console.log(...args); };
console.error = (...args) => { _console.error(...args); }; // Errors always shown
console.warn = (...args) => { if (AppState.logging.enabled) _console.warn(...args); };
console.debug = (...args) => { if (AppState.logging.enabled && AppState.logging.debugEnabled) _console.debug(...args); };
console.info = (...args) => { if (AppState.logging.enabled) _console.info(...args); };

// ============================================================================
// MILESTONES - External data reference
// ============================================================================
if (typeof MILESTONES === 'undefined') window.MILESTONES = [];

function getMilestoneInfo(distanceMeters) {
  let current = null, next = MILESTONES[0];
  for (let i = 0; i < MILESTONES.length; i++) {
    if (distanceMeters >= MILESTONES[i].threshold) {
      current = MILESTONES[i];
      next = (i + 1 < MILESTONES.length) ? MILESTONES[i + 1] : null;
    } else { if (!current) next = MILESTONES[i]; break; }
  }
  let progress = 0;
  if (next && current) progress = Math.min(100, ((distanceMeters - current.threshold) / (next.threshold - current.threshold)) * 100);
  else if (next && !current) progress = Math.min(100, (distanceMeters / next.threshold) * 100);
  return { current, next, progress, distanceToNext: next ? (next.threshold - distanceMeters) : 0 };
}

// ============================================================================
// DEPENDENCY INJECTION CONTEXT
// ============================================================================
const Context = {
  dom: { chaosCenterPos: null, chaosAmplitude: null, chaosMaxSpeed: null,
         oscCenterPos: null, oscAmplitude: null, oscFrequency: null },
  state: null, playlist: null, ws: null,
  ui: { showNotification: null, updateStatusDot: null, log: null },
  SystemState: null, milestones: null, initialized: false
};

function initContext() {
  _console.log("🔧 Initializing Context...");
  Context.dom.chaosCenterPos = document.getElementById("chaosCenterPos");
  Context.dom.chaosAmplitude = document.getElementById("chaosAmplitude");
  Context.dom.chaosMaxSpeed = document.getElementById("chaosMaxSpeed");
  Context.dom.oscCenterPos = document.getElementById("oscCenter");
  Context.dom.oscAmplitude = document.getElementById("oscAmplitude");
  Context.dom.oscFrequency = document.getElementById("oscFrequency");
  Context.state = AppState;
  Context.playlist = PlaylistState;
  Context.SystemState = SystemState;
  Context.milestones = MILESTONES;
  if (typeof showNotification === "function") Context.ui.showNotification = showNotification;
  Context.initialized = true;
  _console.log("✅ Context initialized");
  return Context;
}

// ============================================================================
// UNIT CONVERSION UTILITIES (PURE)
// ============================================================================
function mmToSteps(mm, stepsPerMM = 80) { return Math.round(mm * stepsPerMM); }
function stepsToMm(steps, stepsPerMM = 80) { return steps / stepsPerMM; }
function getEffectiveMaxDistMM(ctx) {
  ctx = ctx || Context;
  return ctx.state?.pursuit?.effectiveMaxDistMM || ctx.state?.pursuit?.totalDistanceMM || 0;
}
function getTotalDistMM(ctx) {
  ctx = ctx || Context;
  return ctx.state?.pursuit?.totalDistanceMM || 0;
}

// ============================================================================
// CONTEXT-AWARE VALIDATION WRAPPERS
// ============================================================================
function validateChaosLimitsCtx(ctx) {
  ctx = ctx || Context;
  const centerPos = parseFloat(ctx.dom.chaosCenterPos?.value || 0);
  const amplitude = parseFloat(ctx.dom.chaosAmplitude?.value || 0);
  if (typeof validateChaosLimitsPure === 'function') 
    return validateChaosLimitsPure(centerPos, amplitude, getTotalDistMM(ctx));
  return { valid: true, min: 0, max: getTotalDistMM(ctx) };
}

function validateOscillationLimitsCtx(ctx) {
  ctx = ctx || Context;
  const centerPos = parseFloat(ctx.dom.oscCenterPos?.value || 0);
  const amplitude = parseFloat(ctx.dom.oscAmplitude?.value || 0);
  if (typeof validateOscillationLimitsPure === 'function')
    return validateOscillationLimitsPure(centerPos, amplitude, getTotalDistMM(ctx));
  return { valid: true, min: 0, max: getTotalDistMM(ctx) };
}

_console.log('✅ core/app.js loaded - AppState, WS_CMD, SystemState, Context initialized');
