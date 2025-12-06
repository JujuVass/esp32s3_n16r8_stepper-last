/**
 * ============================================================================
 * core/utils.js - Utility Functions & Formatting
 * ============================================================================
 * Merged from: utils.js + formatting.js
 * 
 * Contents:
 * - Notification system
 * - Button state management
 * - Input validation helpers
 * - Command sending utilities
 * - Pure formatting functions (display)
 * ============================================================================
 */

// ============================================================================
// NOTIFICATION SYSTEM
// ============================================================================
function showNotification(message, type = 'info', duration = 3000) {
  const colors = {
    success: { bg: '#22c55e', icon: '✅' }, error: { bg: '#ef4444', icon: '❌' },
    warning: { bg: '#f59e0b', icon: '⚠️' }, info: { bg: '#3b82f6', icon: 'ℹ️' }
  };
  const style = colors[type] || colors.info;
  const notification = document.createElement('div');
  notification.className = 'notification notification-' + type;
  notification.style.cssText = `
    position: fixed; top: 20px; right: 20px; padding: 12px 20px;
    background: ${style.bg}; color: white; border-radius: 8px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3); z-index: 10000;
    animation: slideIn 0.3s ease; font-weight: 500; max-width: 350px;
    display: flex; align-items: center; gap: 10px;`;
  notification.innerHTML = `<span style="font-size: 18px;">${style.icon}</span><span>${message}</span>`;
  const style_anim = document.createElement('style');
  style_anim.textContent = `
    @keyframes slideIn { from { transform: translateX(100%); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
    @keyframes slideOut { from { transform: translateX(0); opacity: 1; } to { transform: translateX(100%); opacity: 0; } }`;
  if (!document.getElementById('notification-styles')) { style_anim.id = 'notification-styles'; document.head.appendChild(style_anim); }
  document.body.appendChild(notification);
  if (duration > 0) {
    setTimeout(() => {
      notification.style.animation = 'slideOut 0.3s ease forwards';
      setTimeout(() => notification.remove(), 300);
    }, duration);
  }
  return notification;
}

// ============================================================================
// BUTTON STATE MANAGEMENT
// ============================================================================
function setButtonState(button, state, labels) {
  if (!button) return;
  const states = {
    loading: { disabled: true, text: labels?.loading || 'Loading...' },
    success: { disabled: false, text: labels?.success || labels?.default || button.textContent },
    error: { disabled: false, text: labels?.error || labels?.default || button.textContent },
    default: { disabled: false, text: labels?.default || button.textContent }
  };
  const cfg = states[state] || states.default;
  button.disabled = cfg.disabled;
  button.textContent = cfg.text;
  if (state === 'loading') button.style.opacity = '0.7';
  else button.style.opacity = '1';
}

function setButtonLoading(button, isLoading, originalText) {
  if (!button) return;
  button.disabled = isLoading;
  button.style.opacity = isLoading ? '0.7' : '1';
  if (!isLoading && originalText) button.textContent = originalText;
}

// ============================================================================
// INPUT VALIDATION HELPERS
// ============================================================================
function validateNumericInput(value, min, max, defaultValue) {
  const num = parseFloat(value);
  if (isNaN(num)) return defaultValue;
  return Math.max(min, Math.min(max, num));
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function isValidNumber(value) {
  return typeof value === 'number' && !isNaN(value) && isFinite(value);
}

function parseNumericValue(value, defaultValue = 0) {
  const parsed = parseFloat(value);
  return isValidNumber(parsed) ? parsed : defaultValue;
}

/**
 * Validate min/max pair ensuring min <= max
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
 */
function setButtonState(button, enabled) {
  if (!button) return;
  button.disabled = !enabled;
  button.style.opacity = enabled ? '1' : '0.5';
  button.style.cursor = enabled ? 'pointer' : 'not-allowed';
}

// ============================================================================
// INPUT SETUP HELPERS
// ============================================================================

/**
 * Setup standard edit-tracking listeners for an input element
 * Manages AppState.editing.input to prevent server overwrite during user editing
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
    if (onChangeCallback) onChangeCallback(this.value, this);
    AppState.editing.input = null;
  });
}

/**
 * Setup preset buttons for a given data attribute
 */
function setupPresetButtons(dataAttribute, onClickCallback) {
  document.querySelectorAll(`[${dataAttribute}]`).forEach(btn => {
    btn.addEventListener('click', function() {
      const value = parseFloat(this.getAttribute(dataAttribute));
      if (onClickCallback && !isNaN(value)) onClickCallback(value, this);
    });
  });
}

// ============================================================================
// COMMAND SENDING UTILITIES
// ============================================================================
function sendCommand(command, value = null) {
  if (!AppState.ws || AppState.ws.readyState !== WebSocket.OPEN) {
    console.warn('⚠️ WebSocket not connected');
    return false;
  }
  const msg = value !== null ? { cmd: command, value: value } : { cmd: command };
  AppState.ws.send(JSON.stringify(msg));
  console.log('📤 Sent:', msg);
  return true;
}

function sendCommandWithCallback(command, value, onSuccess, onError) {
  try {
    const result = sendCommand(command, value);
    if (result && onSuccess) setTimeout(onSuccess, 100);
    else if (!result && onError) onError(new Error('WebSocket not connected'));
    return result;
  } catch (e) {
    if (onError) onError(e);
    return false;
  }
}

// ============================================================================
// DEBOUNCE & THROTTLE
// ============================================================================
function debounce(func, wait) {
  let timeout;
  return function(...args) {
    clearTimeout(timeout);
    timeout = setTimeout(() => func.apply(this, args), wait);
  };
}

function throttle(func, limit) {
  let inThrottle;
  return function(...args) {
    if (!inThrottle) {
      func.apply(this, args);
      inThrottle = true;
      setTimeout(() => inThrottle = false, limit);
    }
  };
}

// ============================================================================
// DOM HELPERS
// ============================================================================
function getElement(id) {
  return document.getElementById(id);
}

function getValue(id, defaultValue = '') {
  const el = document.getElementById(id);
  return el ? el.value : defaultValue;
}

function setValue(id, value) {
  const el = document.getElementById(id);
  if (el) el.value = value;
}

function setDisplay(id, display) {
  const el = document.getElementById(id);
  if (el) el.style.display = display;
}

function addClass(element, className) {
  if (element && className) element.classList.add(className);
}

function removeClass(element, className) {
  if (element && className) element.classList.remove(className);
}

function toggleClass(element, className, force) {
  if (element && className) return element.classList.toggle(className, force);
  return false;
}

// ============================================================================
// PURE FORMATTING FUNCTIONS (No side effects, no DOM access)
// ============================================================================

/**
 * Get WiFi signal quality description
 */
function getWifiQualityPure(rssi) {
  if (rssi > -50) return { quality: 'Excellent', color: '#22c55e', percent: 100 };
  if (rssi > -60) return { quality: 'Good', color: '#84cc16', percent: 80 };
  if (rssi > -70) return { quality: 'Fair', color: '#eab308', percent: 60 };
  if (rssi > -80) return { quality: 'Weak', color: '#f97316', percent: 40 };
  return { quality: 'Poor', color: '#ef4444', percent: 20 };
}

/**
 * Format uptime from seconds to human-readable string
 */
function formatUptimePure(seconds) {
  if (typeof seconds !== 'number' || isNaN(seconds)) return '0s';
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  const secs = Math.floor(seconds % 60);
  if (days > 0) return `${days}d ${hours}h ${mins}m`;
  if (hours > 0) return `${hours}h ${mins}m ${secs}s`;
  if (mins > 0) return `${mins}m ${secs}s`;
  return `${secs}s`;
}

/**
 * Format duration in milliseconds to display string
 */
function formatDurationPure(ms) {
  if (typeof ms !== 'number' || isNaN(ms)) return '0ms';
  if (ms < 1000) return `${Math.round(ms)}ms`;
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`;
  const mins = Math.floor(ms / 60000);
  const secs = Math.floor((ms % 60000) / 1000);
  return `${mins}m ${secs}s`;
}

/**
 * Get speed indicator color
 */
function getSpeedColorPure(speed, maxSpeed = 6000) {
  const ratio = speed / maxSpeed;
  if (ratio < 0.3) return '#22c55e';      // Green - Slow
  if (ratio < 0.6) return '#eab308';      // Yellow - Medium
  if (ratio < 0.8) return '#f97316';      // Orange - Fast
  return '#ef4444';                        // Red - Very Fast
}

/**
 * Format distance with appropriate units
 */
function formatDistancePure(mm, precision = 1) {
  if (typeof mm !== 'number' || isNaN(mm)) return '0 mm';
  if (Math.abs(mm) >= 1000) return `${(mm / 1000).toFixed(precision)} m`;
  if (Math.abs(mm) >= 10) return `${Math.round(mm)} mm`;
  return `${mm.toFixed(precision)} mm`;
}

/**
 * Format large numbers with K/M suffixes
 */
function formatLargeNumberPure(num) {
  if (typeof num !== 'number' || isNaN(num)) return '0';
  if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`;
  if (num >= 1000) return `${(num / 1000).toFixed(1)}K`;
  return num.toString();
}

/**
 * Format percentage value
 */
function formatPercentPure(value, total, decimals = 1) {
  if (!total || total === 0) return '0%';
  return `${((value / total) * 100).toFixed(decimals)}%`;
}

/**
 * Get state name from state code
 */
function getStateNamePure(stateCode) {
  const states = { 0: 'Init', 1: 'Calibrating', 2: 'Ready', 3: 'Running', 4: 'Paused', 5: 'Error' };
  return states[stateCode] || 'Unknown';
}

/**
 * Get state color for display
 */
function getStateColorPure(stateCode) {
  const colors = { 0: '#6b7280', 1: '#3b82f6', 2: '#22c55e', 3: '#eab308', 4: '#f97316', 5: '#ef4444' };
  return colors[stateCode] || '#6b7280';
}

/**
 * Format frequency value (Hz)
 */
function formatFrequencyPure(hz, precision = 2) {
  if (typeof hz !== 'number' || isNaN(hz)) return '0 Hz';
  if (hz >= 1000) return `${(hz / 1000).toFixed(precision)} kHz`;
  return `${hz.toFixed(precision)} Hz`;
}

/**
 * Format cycle count for display
 */
function formatCycleCountPure(count) {
  if (typeof count !== 'number' || isNaN(count)) return '0';
  if (count >= 1000000) return `${(count / 1000000).toFixed(2)}M`;
  if (count >= 1000) return `${(count / 1000).toFixed(1)}K`;
  return count.toString();
}

/**
 * Calculate and format rate per hour
 */
function formatRatePerHourPure(count, elapsedMs) {
  if (!elapsedMs || elapsedMs === 0) return '0/h';
  const perHour = (count / elapsedMs) * 3600000;
  return `${formatLargeNumberPure(Math.round(perHour))}/h`;
}

/**
 * Get chaos intensity label
 */
function getChaosIntensityLabelPure(speedRatio) {
  if (speedRatio < 0.25) return { label: 'Gentle', color: '#22c55e' };
  if (speedRatio < 0.50) return { label: 'Moderate', color: '#84cc16' };
  if (speedRatio < 0.75) return { label: 'Intense', color: '#f97316' };
  return { label: 'Extreme', color: '#ef4444' };
}

/**
 * Format oscillation parameters for display
 */
function formatOscillationParamsPure(centerMM, amplitudeMM, frequencyHz) {
  return {
    center: formatDistancePure(centerMM),
    amplitude: formatDistancePure(amplitudeMM),
    frequency: formatFrequencyPure(frequencyHz),
    range: `${formatDistancePure(centerMM - amplitudeMM)} - ${formatDistancePure(centerMM + amplitudeMM)}`
  };
}

/**
 * Calculate ETA based on current progress
 */
function calculateETAPure(completed, total, elapsedMs) {
  if (!completed || !total || completed >= total) return null;
  const remaining = total - completed;
  const msPerUnit = elapsedMs / completed;
  const remainingMs = remaining * msPerUnit;
  return { remainingMs, formatted: formatDurationPure(remainingMs) };
}

/**
 * Format memory usage
 */
function formatMemoryPure(bytes) {
  if (typeof bytes !== 'number' || isNaN(bytes)) return '0 B';
  if (bytes >= 1048576) return `${(bytes / 1048576).toFixed(2)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${bytes} B`;
}

/**
 * Get progress bar color based on percentage
 */
function getProgressColorPure(percent) {
  if (percent < 25) return '#ef4444';       // Red
  if (percent < 50) return '#f97316';       // Orange
  if (percent < 75) return '#eab308';       // Yellow
  return '#22c55e';                          // Green
}

// Ensure _console is available (defined in app.js)
if (typeof _console !== 'undefined') {
  _console.log('✅ core/utils.js loaded - Utilities & Formatting functions ready');
} else {
  console.log('✅ core/utils.js loaded - Utilities & Formatting functions ready');
}
