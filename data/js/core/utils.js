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
 * - setupEditableInput: Input edit tracking
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
  notification.innerHTML = `<span class="notif-icon">${icon}</span><span>${message}</span>`;

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
    if (notify) showNotification(`⚠️ ${fieldName} ajustée: ${numValue.toFixed(1)} → ${min.toFixed(1)} (minimum)`, 'warning');
    numValue = min;
    wasAdjusted = true;
  } else if (numValue > max) {
    if (notify) showNotification(`⚠️ ${fieldName} ajustée: ${numValue.toFixed(1)} → ${max.toFixed(1)} (maximum)`, 'warning');
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
    showNotification(`⚠️ ${minFieldName} (${min.toFixed(1)}) > ${maxFieldName} (${max.toFixed(1)}) - ajusté`, 'warning');
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
    'decelZone', 'decelEffect',
    // OSCILLATION
    'oscCenterPosition', 'oscAmplitude', 'oscFrequency', 'oscSpeed',
    'oscRampInDuration', 'oscRampOutDuration',
    // CHAOS
    'chaosCenter', 'chaosAmplitude', 'chaosSpeed', 'chaosCraziness',
    'chaosDuration', 'chaosSeed'
  ];
  applyNumericConstraints(mainNumericInputs);
}

