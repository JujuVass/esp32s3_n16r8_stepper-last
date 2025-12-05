/**
 * ============================================================================
 * utils.js - Utility Functions
 * ============================================================================
 * Common utility functions used throughout the application
 * 
 * Contents:
 * - showNotification: Toast notification system
 * - validateNumericInput: Number validation with clamping
 * - validateMinMaxPair: Min/max pair validation
 * - getISOWeek: ISO 8601 week number calculation
 * - canStartOperation: System readiness check
 * - setButtonState: Button state management
 * - setupEditableInput: Input edit tracking
 * - setupPresetButtons: Preset button setup
 * 
 * Dependencies: app.js (AppState, SystemState)
 * Created: December 2024 (extracted from index.html)
 * ============================================================================
 */

// ============================================================================
// NOTIFICATION SYSTEM
// ============================================================================

/**
 * Show a toast notification
 * @param {string} message - The message to display
 * @param {string} type - Notification type: 'error', 'warning', 'success', 'info'
 * @param {number} duration - How long to show (ms)
 */
function showNotification(message, type = 'error', duration = 5000) {
  // Remove existing notification if any
  const existingNotif = document.querySelector('.notification');
  if (existingNotif) {
    existingNotif.remove();
  }
  
  // Create notification element
  const notif = document.createElement('div');
  notif.className = 'notification notification-' + type;
  notif.textContent = message;
  document.body.appendChild(notif);
  
  // Show notification
  setTimeout(() => {
    notif.classList.add('show');
  }, 10);
  
  // Auto-hide after duration
  setTimeout(() => {
    notif.classList.remove('show');
    setTimeout(() => notif.remove(), 300);
  }, duration);
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
