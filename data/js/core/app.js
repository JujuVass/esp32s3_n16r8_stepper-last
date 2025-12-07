/**
 * ============================================================================
 * app.js - Global Application State & Constants
 * ============================================================================
 * Central state management and command definitions for the Stepper Controller
 * 
 * Contents:
 * - AppState: Global application state
 * - PlaylistState: Preset management state
 * - WS_CMD: WebSocket command constants
 * - SystemState: State machine enum
 * - MILESTONES: Distance achievement data
 * 
 * Created: December 2024 (extracted from index.html)
 * ============================================================================
 */

// ============================================================================
// GLOBAL APPLICATION STATE
// ============================================================================
// Centralized state management to avoid namespace pollution
const AppState = {
  // WebSocket connection
  ws: null,
  
  // Editing state
  editing: {
    input: null,           // Currently edited input field
    oscField: null,        // Specific oscillation field being edited
    lineId: null           // Sequence line being edited
  },
  
  // Pursuit mode state
  pursuit: {
    totalDistanceMM: 0,    // Total calibrated distance
    currentPositionMM: 0,  // Current stepper position
    targetMM: 0,           // Target position from gauge
    isDragging: false,     // Is user dragging gauge cursor
    lastCommandTime: 0,    // Last command sent timestamp
    active: false,         // Is pursuit mode active
    maxSpeedLevel: 10,     // Max speed level for pursuit
    isEditingMaxDistLimit: false  // Prevent WS updates while editing
  },
  
  // Oscillation mode state
  oscillation: {
    validationTimer: null  // Debounce timer for form validation
  },
  
  // Sequence mode state
  sequence: {
    lines: [],             // Array of sequence line objects
    editingLineId: null,   // Currently editing line ID
    isLoadingEditForm: false, // Loading edit form flag
    selectedIds: null,     // Set of selected line IDs (initialized as Set)
    lastSelectedIndex: null, // Last selected index for shift-click
    drag: {
      lineId: null,        // Currently dragged line ID
      lineIndex: null,     // Currently dragged line index
      lastEnterTime: 0     // Last drag enter timestamp
    }
  },
  
  // Stats state
  stats: {
    chart: null            // Chart.js instance reference
  },
  
  // System state
  system: {
    currentState: 0,       // Current system state (use SystemState enum)
    canStart: false,       // Can start operations (calibration done)
    currentMode: 'simple', // Current active mode/tab
    pendingModeSwitch: null // Pending mode change during operation
  },
  
  // UI initialization flags
  flags: {
    patternsInitialized: false,  // Chaos pattern checkboxes initialized
    domCacheReady: false          // DOM cache initialized
  },
  
  // Milestone tracking
  milestone: {
    current: null,  // Current milestone object
    lastThreshold: 0  // Last threshold passed (for notifications)
  },
  
  // Stats panel tracking (for on-demand system stats)
  statsPanel: {
    isVisible: false,  // Is stats panel currently visible
    lastToggle: 0      // Last toggle timestamp
  },
  
  // Logging preferences (synchronized with backend)
  logging: {
    enabled: false,     // Global logging enabled/disabled
    debugEnabled: false // Debug level logs enabled
  }
};

// ============================================================================
// PLAYLIST STATE - Preset Management
// ============================================================================
const PlaylistState = {
  simple: [],
  oscillation: [],
  chaos: [],
  loaded: false
};

// ============================================================================
// SYSTEM STATE ENUM
// ============================================================================
const SystemState = Object.freeze({
  INIT: 0,
  CALIBRATING: 1,
  READY: 2,
  RUNNING: 3,
  PAUSED: 4,
  ERROR: 5
});

// ============================================================================
// WEBSOCKET COMMANDS - Centralized command strings
// ============================================================================
// Why: Prevents typos in command strings, enables autocomplete, single source of truth
const WS_CMD = Object.freeze({
  // === Movement Control ===
  START: 'start',
  STOP: 'stop',
  PAUSE: 'pause',
  CALIBRATE: 'calibrate',
  RETURN_TO_START: 'returnToStart',
  RESET_TOTAL_DISTANCE: 'resetTotalDistance',
  
  // === Simple Mode ===
  SET_START_POSITION: 'setStartPosition',
  SET_DISTANCE: 'setDistance',
  SET_SPEED_FORWARD: 'setSpeedForward',
  SET_SPEED_BACKWARD: 'setSpeedBackward',
  SET_CYCLE_PAUSE: 'setCyclePause',
  SET_DECEL_ZONE: 'setDecelZone',
  SET_MAX_DISTANCE_LIMIT: 'setMaxDistanceLimit',
  UPDATE_CYCLE_PAUSE: 'updateCyclePause',
  
  // === Oscillation Mode ===
  SET_OSCILLATION: 'setOscillation',
  SET_OSCILLATION_CONFIG: 'setOscillationConfig',
  START_OSCILLATION: 'startOscillation',
  STOP_OSCILLATION: 'stopOscillation',
  UPDATE_CYCLE_PAUSE_OSC: 'updateCyclePauseOsc',
  
  // === Chaos Mode ===
  SET_CHAOS_CONFIG: 'setChaosConfig',
  START_CHAOS: 'startChaos',
  STOP_CHAOS: 'stopChaos',
  
  // === Sequence Mode ===
  ADD_SEQUENCE_LINE: 'addSequenceLine',
  DELETE_SEQUENCE_LINE: 'deleteSequenceLine',
  UPDATE_SEQUENCE_LINE: 'updateSequenceLine',
  MOVE_SEQUENCE_LINE: 'moveSequenceLine',
  DUPLICATE_SEQUENCE_LINE: 'duplicateSequenceLine',
  TOGGLE_SEQUENCE_LINE: 'toggleSequenceLine',
  START_SEQUENCE: 'startSequence',
  STOP_SEQUENCE: 'stopSequence',
  LOOP_SEQUENCE: 'loopSequence',
  TOGGLE_SEQUENCE_PAUSE: 'toggleSequencePause',
  SKIP_SEQUENCE_LINE: 'skipSequenceLine',
  CLEAR_SEQUENCE: 'clearSequence',
  EXPORT_SEQUENCE: 'exportSequence',
  GET_SEQUENCE_TABLE: 'getSequenceTable',
  
  // === Pursuit Mode ===
  PURSUIT_MOVE: 'pursuitMove',
  ENABLE_PURSUIT_MODE: 'enablePursuitMode',
  DISABLE_PURSUIT_MODE: 'disablePursuitMode',
  
  // === Status & Stats ===
  GET_STATUS: 'getStatus',
  SAVE_STATS: 'saveStats'
});

// ============================================================================
// CONSOLE WRAPPER - Respect AppState.logging preferences
// ============================================================================

// Store original console methods BEFORE any override
const _console = {
  log: console.log.bind(console),
  error: console.error.bind(console),
  warn: console.warn.bind(console),
  debug: console.debug.bind(console),
  info: console.info.bind(console)
};

// Override console methods to respect AppState.logging
console.log = function(...args) {
  if (AppState.logging.enabled) {
    _console.log(...args);
  }
};

console.error = function(...args) {
  // Errors are ALWAYS shown (critical information)
  _console.error(...args);
};

console.warn = function(...args) {
  if (AppState.logging.enabled) {
    _console.warn(...args);
  }
};

console.debug = function(...args) {
  // Debug logs require BOTH logging enabled AND debug flag enabled
  if (AppState.logging.enabled && AppState.logging.debugEnabled) {
    _console.debug(...args);
  }
};

console.info = function(...args) {
  if (AppState.logging.enabled) {
    _console.info(...args);
  }
};

// ============================================================================
// MILESTONES DATA - Distance achievements
// ============================================================================
// Note: This is loaded from external file MILESTONES_SORTED.js if available
// Fallback: Define here if not loaded externally
if (typeof MILESTONES === 'undefined') {
  window.MILESTONES = [];
}

// ============================================================================
// UTILITY: Get milestone info for given distance
// ============================================================================
/**
 * Get milestone info for given distance
 * @param {number} distanceMeters - Distance traveled in meters
 * @returns {object} Milestone object with emoji, name, threshold, next milestone, and progress
 */
function getMilestoneInfo(distanceMeters) {
  let current = null;
  let next = MILESTONES[0]; // Default: first milestone
  
  // Find current and next milestones
  for (let i = 0; i < MILESTONES.length; i++) {
    if (distanceMeters >= MILESTONES[i].threshold) {
      current = MILESTONES[i];
      next = (i + 1 < MILESTONES.length) ? MILESTONES[i + 1] : null;
    } else {
      if (!current) {
        next = MILESTONES[i];
      }
      break;
    }
  }
  
  // Calculate progress to next milestone
  let progress = 0;
  if (next && current) {
    const range = next.threshold - current.threshold;
    const traveled = distanceMeters - current.threshold;
    progress = Math.min(100, (traveled / range) * 100);
  } else if (next && !current) {
    progress = Math.min(100, (distanceMeters / next.threshold) * 100);
  }
  
  return {
    current: current,
    next: next,
    progress: progress,
    distanceToNext: next ? (next.threshold - distanceMeters) : 0
  };
}

// ============================================================================
// INITIALIZATION
// ============================================================================
// Initialize Set for sequence selection (cannot be done inline in const)
AppState.sequence.selectedIds = new Set();

// Log initialization
_console.log('✅ app.js loaded - AppState, WS_CMD, SystemState initialized');
