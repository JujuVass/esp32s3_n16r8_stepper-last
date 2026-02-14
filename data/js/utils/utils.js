/**
 * ============================================================================
 * utils.js - Utility Functions
 * ============================================================================
 * Common utility functions used throughout the application
 * 
 * Contents:
 * - NotificationManager: Advanced notification system with stacking,
 *   animations, anti-duplicate, and milestone support
 * - showNotification: Toast notification with types (success/error/warning/info/milestone)
 * - validateNumericInput: Number validation with clamping
 * - validateMinMaxPair: Min/max pair validation
 * - getISOWeek: ISO 8601 week number calculation
 * - canStartOperation: System readiness check
 * - setButtonState: Button state management
 * - setupEditableInput: Input edit tracking (Simple mode)
 * - setupEditableOscInput: Input edit tracking (Oscillation mode, with debounced input)
 * - setupPresetButtons: Preset button setup
 * - sendCommand: WebSocket command sender
 * 
 * Dependencies: app.js (AppState, SystemState)
 * Created: December 2024 (extracted from index.html)
 * Updated: December 2024 (enhanced notification system)
 * ============================================================================
 */

// ============================================================================
// NOTIFICATION SYSTEM (Enhanced with stacking, animations, anti-duplicate)
// ============================================================================

// Notification state management
const NotificationManager = {
  container: null,
  activeNotifications: [],
  recentMessages: new Map(), // Anti-duplicate: message -> timestamp
  MAX_NOTIFICATIONS: 5,
  DEDUPE_WINDOW_MS: 1000, // Ignore duplicate messages within 1 second

  init() {
    if (this.container) return this.container;

    // Create container for stacking notifications
    this.container = document.createElement('div');
    this.container.id = 'notification-container';
    document.body.appendChild(this.container);

    return this.container;
  },

  isDuplicate(message) {
    const now = Date.now();
    const lastShown = this.recentMessages.get(message);
    if (lastShown && (now - lastShown) < this.DEDUPE_WINDOW_MS) {
      return true;
    }
    this.recentMessages.set(message, now);
    // Cleanup old entries
    if (this.recentMessages.size > 50) {
      for (const [msg, time] of this.recentMessages) {
        if (now - time > 5000) this.recentMessages.delete(msg);
      }
    }
    return false;
  },

  removeOldest() {
    if (this.activeNotifications.length >= this.MAX_NOTIFICATIONS) {
      const oldest = this.activeNotifications.shift();
      if (oldest && oldest.parentNode) {
        oldest.remove();
      }
    }
  }
};

/**
 * Show a toast notification with stacking, animations and anti-duplicate
 * @param {string} message - The message to display
 * @param {string} type - Notification type: 'error', 'warning', 'success', 'info', 'milestone'
 * @param {number} duration - How long to show (ms), null for type default
 */
function showNotification(message, type = 'info', duration = null) {
  // Skip duplicate messages within dedupe window
  if (NotificationManager.isDuplicate(message)) {
    return null;
  }

  // Default durations per type (longer for important messages)
  const defaultDurations = {
    success: 3000,
    error: 4000,
    warning: 3500,
    info: 3500,
    milestone: 4000
  };

  // Use provided duration or default based on type
  const actualDuration = duration !== null ? duration : (defaultDurations[type] || 3500);

  // Initialize container if needed
  const container = NotificationManager.init();

  // Remove oldest if at limit
  NotificationManager.removeOldest();

  const colors = {
    success: '✅',
    error: '❌',
    warning: '⚠️',
    info: 'ℹ️',
    milestone: '🏆'
  };
  const icon = colors[type] || colors.info;

  const notification = document.createElement('div');
  notification.className = 'notification notification-' + type;
  // Safe: use textContent to prevent XSS from WebSocket messages
  const iconSpan = document.createElement('span');
  iconSpan.className = 'notif-icon';
  iconSpan.textContent = icon;
  const msgSpan = document.createElement('span');
  msgSpan.textContent = message;
  notification.append(iconSpan, msgSpan);

  // Store timeout ID to allow cancellation
  let timeoutId = null;

  // Function to remove notification with animation
  const removeNotification = () => {
    if (timeoutId) clearTimeout(timeoutId);
    notification.classList.add('slide-out');
    setTimeout(() => {
      if (notification.parentNode) notification.remove();
      const idx = NotificationManager.activeNotifications.indexOf(notification);
      if (idx > -1) NotificationManager.activeNotifications.splice(idx, 1);
    }, 300);
  };

  // Allow click to dismiss early
  notification.addEventListener('click', removeNotification);

  container.appendChild(notification);
  NotificationManager.activeNotifications.push(notification);

  if (actualDuration > 0) {
    timeoutId = setTimeout(removeNotification, actualDuration);
  }

  return notification;
}

// ============================================================================
// VALIDATION HELPERS
// ============================================================================

/**
 * Validate and clamp a numeric input value
 * @param {*} value - The value to validate
 * @param {Object} options - Validation options
 * @param {number} options.min - Minimum allowed value  
 * @param {number} options.max - Maximum allowed value  
 * @param {number} options.defaultValue - Default value if invalid
 * @param {number} [options.step] - Rounding step (e.g., 0.1 for one decimal)
 * @param {boolean} [options.showNotification] - Show warning notification if clamped
 * @param {string} [options.fieldName] - Field name for notification message
 * @returns {{value: number, wasAdjusted: boolean}} Validated value and adjustment flag
 */
function validateNumericInput(value, options = {}) {
  const { min = 0, max = Infinity, defaultValue = 0, step = null, showNotification: notify = false, fieldName = 'Valeur' } = options;
  
  let numValue = parseFloat(value);
  let wasAdjusted = false;
  
  // Handle NaN
  if (isNaN(numValue)) {
    numValue = defaultValue;
    wasAdjusted = true;
  }
  
  // Clamp to min/max
  if (numValue < min) {
    if (notify) showNotification(`⚠️ ${t('utils.adjusted', {field: fieldName, from: numValue.toFixed(1), to: min.toFixed(1), type: t('utils.adjustedMin')})}`, 'warning');
    numValue = min;
    wasAdjusted = true;
  } else if (numValue > max) {
    if (notify) showNotification(`⚠️ ${t('utils.adjusted', {field: fieldName, from: numValue.toFixed(1), to: max.toFixed(1), type: t('utils.adjustedMax')})}`, 'warning');
    numValue = max;
    wasAdjusted = true;
  }
  
  // Round to step if specified
  if (step !== null && step > 0) {
    numValue = Math.round(numValue / step) * step;
  }
  
  return { value: numValue, wasAdjusted };
}

/**
 * Validate min/max pair ensuring min <= max
 * @param {number} minValue - Minimum value
 * @param {number} maxValue - Maximum value
 * @param {Object} options - Options
 * @param {string} [options.minFieldName] - Name for min field in notifications
 * @param {string} [options.maxFieldName] - Name for max field in notifications
 * @returns {{min: number, max: number, wasAdjusted: boolean}}
 */
function validateMinMaxPair(minValue, maxValue, options = {}) {
  const { minFieldName = 'Min', maxFieldName = 'Max' } = options;
  
  let min = parseFloat(minValue) || 0;
  let max = parseFloat(maxValue) || 0;
  let wasAdjusted = false;
  
  if (min > max) {
    showNotification(`⚠️ ${t('utils.minGtMax', {min: minFieldName, minVal: min.toFixed(1), max: maxFieldName, maxVal: max.toFixed(1)})}`, 'warning');
    max = min;
    wasAdjusted = true;
  }
  
  return { min, max, wasAdjusted };
}

// ============================================================================
// DATE/TIME UTILITIES
// ============================================================================

/**
 * Calculate ISO 8601 week number (Monday = start of week)
 * @param {Date} date - The date to calculate week number for
 * @returns {number} Week number (1-53)
 */
function getISOWeek(date) {
  const target = new Date(date.valueOf());
  const dayNr = (date.getDay() + 6) % 7;  // Monday = 0
  target.setDate(target.getDate() - dayNr + 3);
  const firstThursday = target.valueOf();
  target.setMonth(0, 1);
  if (target.getDay() !== 4) {
    target.setMonth(0, 1 + ((4 - target.getDay()) + 7) % 7);
  }
  return 1 + Math.ceil((firstThursday - target) / 604800000);
}

// ============================================================================
// CENTER/AMPLITUDE PRESET VALIDATION (shared by Oscillation & Chaos)
// ============================================================================

/**
 * Update visual state of center/amplitude preset buttons.
 * Shared logic for both Oscillation and Chaos modes.
 * 
 * @param {Object} cfg - Configuration
 * @param {string} cfg.centerInputId - ID of center position input (e.g., 'oscCenter')
 * @param {string} cfg.amplitudeInputId - ID of amplitude input (e.g., 'oscAmplitude')
 * @param {string} cfg.linkedCheckboxId - ID of "=Centre" linked checkbox (e.g., 'oscAmplitudeLinked')
 * @param {string} cfg.dataPrefix - Data attribute prefix (e.g., 'osc' or 'chaos')
 * @param {string} cfg.i18nPrefix - i18n key prefix (e.g., 'oscillation' or 'chaos')
 */
function updateCenterAmplitudePresets(cfg) {
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  if (effectiveMax === 0) return;
  
  const currentCenter = parseFloat(document.getElementById(cfg.centerInputId).value) || 0;
  const currentAmplitude = parseFloat(document.getElementById(cfg.amplitudeInputId).value) || 0;
  const isLinked = document.getElementById(cfg.linkedCheckboxId)?.checked || false;
  const dp = cfg.dataPrefix; // shorthand
  
  // Validate center presets (must allow current amplitude)
  document.querySelectorAll('[data-' + dp + '-center]').forEach(btn => {
    const centerValue = parseFloat(btn.getAttribute('data-' + dp + '-center'));
    const minPos = centerValue - currentAmplitude;
    const maxPos = centerValue + currentAmplitude;
    const isValid = minPos >= 0 && maxPos <= effectiveMax;
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.3';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
  
  // Validate amplitude presets (must respect current center)
  document.querySelectorAll('[data-' + dp + '-amplitude]').forEach(btn => {
    const amplitudeValue = parseFloat(btn.getAttribute('data-' + dp + '-amplitude'));
    const minPos = currentCenter - amplitudeValue;
    const maxPos = currentCenter + amplitudeValue;
    const isValid = minPos >= 0 && maxPos <= effectiveMax;
    btn.disabled = !isValid || isLinked;
    btn.style.opacity = (isValid && !isLinked) ? '1' : '0.3';
    btn.style.cursor = (isValid && !isLinked) ? 'pointer' : 'not-allowed';
  });
  
  // Validate relative center presets
  document.querySelectorAll('[data-' + dp + '-center-rel]').forEach(btn => {
    const relValue = parseInt(btn.getAttribute('data-' + dp + '-center-rel'));
    const newCenter = currentCenter + relValue;
    const minPos = newCenter - currentAmplitude;
    const maxPos = newCenter + currentAmplitude;
    const isValid = newCenter >= 0 && minPos >= 0 && maxPos <= effectiveMax;
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.5';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
  
  // Validate relative amplitude presets
  document.querySelectorAll('[data-' + dp + '-amplitude-rel]').forEach(btn => {
    const relValue = parseInt(btn.getAttribute('data-' + dp + '-amplitude-rel'));
    const newAmplitude = currentAmplitude + relValue;
    const minPos = currentCenter - newAmplitude;
    const maxPos = currentCenter + newAmplitude;
    const isValid = newAmplitude >= 1 && minPos >= 0 && maxPos <= effectiveMax;
    btn.disabled = !isValid || isLinked;
    btn.style.opacity = (isValid && !isLinked) ? '1' : '0.5';
    btn.style.cursor = (isValid && !isLinked) ? 'pointer' : 'not-allowed';
  });
  
  // Handle amplitude input disabled state when linked
  const ampInput = document.getElementById(cfg.amplitudeInputId);
  if (ampInput) {
    ampInput.disabled = isLinked;
    ampInput.style.opacity = isLinked ? '0.5' : '1';
  }
  
  // SAFETY: Validate and disable "=Centre" checkbox if center*2 > effectiveMax
  const linkedCheckbox = document.getElementById(cfg.linkedCheckboxId);
  if (linkedCheckbox) {
    const wouldBeValid = (currentCenter * 2) <= effectiveMax && currentCenter >= 1;
    linkedCheckbox.disabled = !wouldBeValid;
    linkedCheckbox.parentElement.style.opacity = wouldBeValid ? '1' : '0.5';
    linkedCheckbox.parentElement.style.cursor = wouldBeValid ? 'pointer' : 'not-allowed';
    linkedCheckbox.parentElement.title = wouldBeValid 
      ? t(cfg.i18nPrefix + '.amplitudeLinked')
      : '⚠️ ' + t(cfg.i18nPrefix + '.cannotLink', {val: currentCenter * 2, max: effectiveMax});
    
    // If currently linked but now invalid, uncheck it
    if (isLinked && !wouldBeValid) {
      linkedCheckbox.checked = false;
      if (ampInput) {
        ampInput.disabled = false;
        ampInput.style.opacity = '1';
      }
    }
  }
}

// ============================================================================
// CYCLE PAUSE UI SYNC (shared by Simple & Oscillation modes)
// ============================================================================

/**
 * Sync cycle pause UI elements with backend state data.
 * Shared by updateSimpleUI() and updateOscillationUI().
 * 
 * @param {Object} cyclePauseData - Cycle pause data from WebSocket status
 * @param {string} suffix - Element ID suffix: '' for Simple, 'Osc' for Oscillation
 * @param {Function} getSectionFn - Function returning the collapsible section element
 * @param {string} i18nPrefix - i18n key prefix: 'simple' or 'oscillation'
 */
function syncCyclePauseUI(cyclePauseData, suffix, getSectionFn, i18nPrefix) {
  const pauseStatusEl = document.getElementById('cyclePauseStatus' + suffix);
  const pauseRemainingEl = document.getElementById('cyclePauseRemaining' + suffix);
  
  if (cyclePauseData.isPausing && pauseStatusEl && pauseRemainingEl) {
    const remainingSec = (cyclePauseData.remainingMs / 1000).toFixed(1);
    pauseStatusEl.style.display = 'block';
    pauseRemainingEl.textContent = remainingSec + 's';
  } else if (pauseStatusEl) {
    pauseStatusEl.style.display = 'none';
  }
  
  // Sync UI to backend state (only if section is expanded)
  const section = getSectionFn();
  const headerText = document.getElementById('cyclePause' + (suffix || '') + 'HeaderText');
  if (section && headerText) {
    const isEnabled = cyclePauseData.enabled;
    const isCollapsed = section.classList.contains('collapsed');
    
    if (isEnabled && isCollapsed) {
      section.classList.remove('collapsed');
      headerText.textContent = t(i18nPrefix + '.cyclePauseEnabled');
    } else if (!isEnabled && !isCollapsed) {
      section.classList.add('collapsed');
      headerText.textContent = t(i18nPrefix + '.cyclePauseDisabled');
    }
    
    // Sync radio buttons
    if (cyclePauseData.isRandom) {
      document.getElementById('pauseModeRandom' + suffix).checked = true;
      document.getElementById('pauseFixedControls' + suffix).style.display = 'none';
      document.getElementById('pauseRandomControls' + suffix).style.display = 'block';
    } else {
      document.getElementById('pauseModeFixed' + suffix).checked = true;
      document.getElementById('pauseFixedControls' + suffix).style.display = 'flex';
      document.getElementById('pauseRandomControls' + suffix).style.display = 'none';
    }
    
    // Sync input values (avoid overwriting if user is editing)
    if (document.activeElement !== document.getElementById('cyclePauseDuration' + suffix)) {
      document.getElementById('cyclePauseDuration' + suffix).value = cyclePauseData.pauseDurationSec.toFixed(1);
    }
    if (document.activeElement !== document.getElementById('cyclePauseMin' + suffix)) {
      document.getElementById('cyclePauseMin' + suffix).value = cyclePauseData.minPauseSec.toFixed(1);
    }
    if (document.activeElement !== document.getElementById('cyclePauseMax' + suffix)) {
      document.getElementById('cyclePauseMax' + suffix).value = cyclePauseData.maxPauseSec.toFixed(1);
    }
  }
}

// ============================================================================
// SYSTEM STATE HELPERS
// ============================================================================

/**
 * Check if system can start a new operation
 * Centralized validation logic to avoid code duplication
 * @returns {boolean} True if system is ready to start (calibrated and not calibrating)
 */
function canStartOperation() {
  return AppState.system.canStart && 
         AppState.system.currentState !== SystemState.CALIBRATING;
}

/**
 * Uniformly set button enabled/disabled state with visual feedback
 * @param {HTMLElement} button - The button element to update
 * @param {boolean} enabled - Whether the button should be enabled
 */
function setButtonState(button, enabled) {
  if (!button) return;
  button.disabled = !enabled;
  button.style.opacity = enabled ? '1' : '0.5';
  button.style.cursor = enabled ? 'pointer' : 'not-allowed';
}

// ============================================================================
// INPUT HELPERS
// ============================================================================

/**
 * Setup standard edit-tracking listeners for an input element
 * Manages AppState.editing.input to prevent server overwrite during user editing
 * @param {string} elementId - The ID of the input element
 * @param {string} editingKey - The key to use in AppState.editing.input
 * @param {Function} onChangeCallback - Callback function when value changes
 */
function setupEditableInput(elementId, editingKey, onChangeCallback) {
  const element = document.getElementById(elementId);
  if (!element) return;
  
  element.addEventListener('mousedown', function() {
    AppState.editing.input = editingKey;
    this.focus();
  });
  element.addEventListener('focus', function() {
    AppState.editing.input = editingKey;
  });
  element.addEventListener('blur', function() {
    AppState.editing.input = null;
  });
  element.addEventListener('change', function() {
    if (onChangeCallback) {
      onChangeCallback(this.value, this);
    }
    AppState.editing.input = null;
  });
}

/**
 * Setup standard edit-tracking listeners for an oscillation input element
 * Uses AppState.editing.oscField and supports optional debounced input validation
 * @param {string} elementId - The ID of the input element
 * @param {Object} options - Configuration options
 * @param {Function} [options.onBlur] - Callback on blur (e.g., validate + send config)
 * @param {Function} [options.onChange] - Callback on change (for select elements)
 * @param {Function} [options.onInput] - Debounced callback on input (e.g., live validation)
 * @param {number} [options.debounceMs=300] - Debounce delay for onInput
 */
function setupEditableOscInput(elementId, options = {}) {
  const element = document.getElementById(elementId);
  if (!element) return;
  
  element.addEventListener('mousedown', function() {
    AppState.editing.oscField = elementId;
    this.focus();
  });
  element.addEventListener('focus', function() {
    AppState.editing.oscField = elementId;
  });
  
  if (options.onBlur) {
    element.addEventListener('blur', function() {
      AppState.editing.oscField = null;
      options.onBlur(this.value, this);
    });
  } else {
    element.addEventListener('blur', function() {
      AppState.editing.oscField = null;
    });
  }
  
  if (options.onChange) {
    element.addEventListener('change', function() {
      AppState.editing.oscField = null;
      options.onChange(this.value, this);
    });
  }
  
  if (options.onInput) {
    const delay = options.debounceMs || 300;
    element.addEventListener('input', function() {
      clearTimeout(AppState.oscillation.validationTimer);
      AppState.oscillation.validationTimer = setTimeout(() => {
        options.onInput(this.value, this);
      }, delay);
    });
  }
}

/**
 * Setup preset buttons for a given data attribute
 * @param {string} dataAttribute - The data attribute name (e.g., 'data-speed-forward')
 * @param {Function} onClickCallback - Callback with the preset value
 */
function setupPresetButtons(dataAttribute, onClickCallback) {
  document.querySelectorAll(`[${dataAttribute}]`).forEach(btn => {
    btn.addEventListener('click', function() {
      const value = parseFloat(this.getAttribute(dataAttribute));
      if (onClickCallback && !isNaN(value)) {
        onClickCallback(value, this);
      }
    });
  });
}

// ============================================================================
// WEBSOCKET COMMUNICATION
// ============================================================================

/**
 * Send a command to the ESP32 via WebSocket
 * @param {string} cmd - Command identifier (use WS_CMD constants)
 * @param {Object} params - Additional parameters to send with the command
 */
function sendCommand(cmd, params = {}) {
  if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
    const message = JSON.stringify({cmd: cmd, ...params});
    AppState.ws.send(message);
  } else {
    console.warn('Cannot send command:', cmd, '- WebSocket not connected (retrying...)');
  }
}

// ============================================================================
// NUMERIC INPUT CONSTRAINTS
// ============================================================================

/**
 * Enforce numeric constraints on an input element
 * - Filters invalid characters (only digits, decimal, minus allowed)
 * - Ensures single decimal point and minus only at start
 * - Enforces min/max on blur
 * - Triggers validateEditForm() if available
 * @param {HTMLInputElement} input - The input element to constrain
 */
function enforceNumericConstraints(input) {
  // Filter invalid characters after input AND trigger validation
  input.addEventListener('input', function(e) {
    const oldValue = this.value;
    // Allow: digits, decimal point, minus sign
    // Remove all non-numeric characters except . and -
    let newValue = this.value.replace(/[^\d.-]/g, '');
    
    // Ensure only one decimal point
    const parts = newValue.split('.');
    if (parts.length > 2) {
      newValue = parts[0] + '.' + parts.slice(1).join('');
    }
    
    // Ensure minus only at start
    if (newValue.indexOf('-') > 0) {
      newValue = newValue.replace(/-/g, '');
    }
    
    // Update value if changed
    if (newValue !== oldValue) {
      this.value = newValue;
    }
    
    // Trigger real-time validation (red border + error messages)
    // Use setTimeout to ensure value is updated before validation
    setTimeout(function() {
      if (typeof validateEditForm === 'function') {
        validateEditForm();
      }
    }, 10);
  });
  
  // Enforce min/max on blur
  input.addEventListener('blur', function() {
    const min = parseFloat(this.getAttribute('min'));
    const max = parseFloat(this.getAttribute('max'));
    const val = parseFloat(this.value);
    
    if (!isNaN(min) && val < min) {
      this.value = min;
      if (typeof validateEditForm === 'function') validateEditForm();
    }
    if (!isNaN(max) && val > max) {
      this.value = max;
      if (typeof validateEditForm === 'function') validateEditForm();
    }
  });
}

/**
 * Apply numeric constraints to a list of input IDs
 * @param {string[]} inputIds - Array of input element IDs
 */
function applyNumericConstraints(inputIds) {
  inputIds.forEach(id => {
    const input = document.getElementById(id);
    if (input) enforceNumericConstraints(input);
  });
}

/**
 * Initialize numeric constraints on all main control inputs (classic modes)
 * Called from main.js on page load
 */
function initMainNumericConstraints() {
  const mainNumericInputs = [
    // VA-ET-VIENT (Simple mode)
    'startPosition', 'distance', 'speedUnified', 'speedForward', 'speedBackward',
    // OSCILLATION
    'oscCenter', 'oscAmplitude', 'oscFrequency',
    'oscRampInDuration', 'oscRampOutDuration',
    // CHAOS
    'chaosCenterPos', 'chaosAmplitude', 'chaosMaxSpeed', 'chaosCraziness',
    'chaosDuration', 'chaosSeed'
  ];
  applyNumericConstraints(mainNumericInputs);
}

// ============================================================================
// FETCH WITH RETRY (Network resilience for ESP32 communication)
// ============================================================================

/**
 * Fetch with automatic retry on network errors
 * Handles transient ESP32 network issues gracefully
 * 
 * @param {string} url - The URL to fetch
 * @param {Object} options - Fetch options (method, headers, body, etc.)
 * @param {Object} retryConfig - Retry configuration
 * @param {number} retryConfig.maxRetries - Maximum retry attempts (default: 3)
 * @param {number} retryConfig.baseDelay - Base delay in ms (default: 500)
 * @param {boolean} retryConfig.exponentialBackoff - Use exponential backoff (default: true)
 * @param {boolean} retryConfig.silent - Don't show notifications on retry (default: false)
 * @returns {Promise<Response>} - Fetch response
 */
async function fetchWithRetry(url, options = {}, retryConfig = {}) {
  const {
    maxRetries = 3,
    baseDelay = 1000,
    exponentialBackoff = true,
    silent = false,
    timeout = 8000
  } = retryConfig;

  let lastError;
  
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      // Create AbortController for timeout only if no external signal provided
      const hasExternalSignal = !!options.signal;
      const controller = hasExternalSignal ? null : new AbortController();
      const timeoutId = hasExternalSignal ? null : setTimeout(() => controller.abort(), timeout);
      
      const response = await fetch(url, {
        ...options,
        signal: options.signal || controller.signal
      });
      
      if (timeoutId) clearTimeout(timeoutId);
      
      // Success - return response
      if (attempt > 0 && !silent) {
        console.log(`✅ Request succeeded after ${attempt} retry(ies): ${url}`);
      }
      return response;
      
    } catch (error) {
      lastError = error;
      
      // Check if it's a retryable error (network issues, timeouts)
      const isRetryable = 
        error.name === 'TypeError' ||        // NetworkError
        error.name === 'AbortError' ||       // Abort/Timeout via AbortController
        error.name === 'DOMException' ||     // Timeout via AbortSignal.timeout()
        error.name === 'TimeoutError' ||     // Explicit timeout
        error.message?.includes('NetworkError') ||
        error.message?.includes('Failed to fetch') ||
        error.message?.includes('network') ||
        error.message?.includes('timed out') ||
        error.message?.includes('timeout');
      
      if (!isRetryable || attempt >= maxRetries) {
        // Not retryable or max retries reached
        throw error;
      }
      
      // Calculate delay with optional exponential backoff
      const delay = exponentialBackoff 
        ? baseDelay * Math.pow(2, attempt) 
        : baseDelay;
      
      if (!silent) {
        console.warn(`⚠️ Request failed (${error.name}), retry ${attempt + 1}/${maxRetries} in ${delay}ms: ${url}`);
      }
      
      // Wait before retry
      await new Promise(resolve => setTimeout(resolve, delay));
    }
  }
  
  // Should not reach here, but just in case
  throw lastError;
}

/**
 * Wrapper for POST requests with retry
 * @param {string} url - API endpoint
 * @param {Object} data - Data to send as JSON
 * @param {Object} retryConfig - Optional retry configuration
 * @returns {Promise<Object>} - Parsed JSON response
 */
async function postWithRetry(url, data, retryConfig = {}) {
  const response = await fetchWithRetry(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data)
  }, retryConfig);
  
  return response.json();
}

/**
 * Wrapper for GET requests with retry
 * @param {string} url - API endpoint
 * @param {Object} retryConfig - Optional retry configuration
 * @returns {Promise<Object>} - Parsed JSON response
 */
async function getWithRetry(url, retryConfig = {}) {
  const response = await fetchWithRetry(url, {
    method: 'GET'
  }, retryConfig);
  
  return response.json();
}
