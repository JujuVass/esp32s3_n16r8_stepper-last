/**
 * ============================================================================
 * PursuitController.js - Pursuit Mode Interactive Gauge Control
 * ============================================================================
 * Handles the pursuit mode with interactive gauge for real-time position control.
 * User can click/drag on the gauge to set target position, motor follows in real-time.
 * 
 * Features:
 *   - Interactive vertical gauge with position indicator
 *   - Real-time target tracking via WebSocket commands
 *   - Speed presets and continuous command loop
 *   - Error display (target vs actual position)
 *   - Max distance limit configuration
 * 
 * Dependencies: 
 *   - app.js (AppState, SystemState, WS_CMD)
 *   - DOMManager.js (DOM cache)
 *   - utils.js (sendCommand, showNotification)
 * ============================================================================
 */

// ============================================================================
// PURSUIT MODE STATE & CONSTANTS
// ============================================================================

/**
 * Pursuit mode state is centralized in AppState.pursuit:
 * - active: boolean
 * - maxSpeedLevel: number
 * - isEditingMaxDistLimit: boolean
 * - totalDistanceMM, currentPositionMM, targetMM, isDragging, lastCommandTime
 */
const PURSUIT_COMMAND_INTERVAL = 20;  // Send command max every 20ms (50Hz)
let pursuitLoopGeneration = 0;         // Generation counter to prevent duplicate loops

// ============================================================================
// GAUGE DISPLAY FUNCTIONS
// ============================================================================

/**
 * Update the gauge position indicator (green line showing current motor position)
 * @param {number} positionMM - Current motor position in mm
 */
function updateGaugePosition(positionMM) {
  if (AppState.pursuit.totalDistanceMM <= 0) return;
  
  const containerHeight = DOM.gaugeContainer.offsetHeight;
  
  // Calculate position (0mm = bottom, totalDistanceMM = top)
  const percent = positionMM / AppState.pursuit.totalDistanceMM;
  const pixelPosition = containerHeight - (percent * containerHeight);
  
  DOM.gaugePosition.style.top = pixelPosition + 'px';
  DOM.currentPositionMM.textContent = positionMM.toFixed(1);
  
  // Update error display
  const error = Math.abs(AppState.pursuit.targetMM - positionMM);
  DOM.positionError.textContent = error.toFixed(1);
}

/**
 * Set the target position on the gauge (red cursor showing target)
 * @param {number} positionMM - Target position in mm
 */
function setGaugeTarget(positionMM) {
  if (AppState.pursuit.totalDistanceMM <= 0) return;
  
  // Clamp to valid range
  if (positionMM < 0) positionMM = 0;
  if (positionMM > AppState.pursuit.totalDistanceMM) positionMM = AppState.pursuit.totalDistanceMM;
  
  AppState.pursuit.targetMM = positionMM;
  
  const containerHeight = DOM.gaugeContainer.offsetHeight;
  
  // Calculate position (0mm = bottom, totalDistanceMM = top)
  const percent = positionMM / AppState.pursuit.totalDistanceMM;
  const pixelPosition = containerHeight - (percent * containerHeight);
  
  DOM.gaugeCursor.style.top = pixelPosition + 'px';
  DOM.targetPositionMM.textContent = positionMM.toFixed(1);
  
  // Update error display
  const error = Math.abs(AppState.pursuit.targetMM - AppState.pursuit.currentPositionMM);
  DOM.positionError.textContent = error.toFixed(1);
  
  // Send pursuit command if active and enough time has passed (throttling)
  if (AppState.pursuit.active) {
    const now = Date.now();
    if (now - AppState.pursuit.lastCommandTime > PURSUIT_COMMAND_INTERVAL) {
      sendPursuitCommand();
      AppState.pursuit.lastCommandTime = now;
    }
  }
}

// ============================================================================
// COMMAND FUNCTIONS
// ============================================================================

/**
 * Send pursuit move command to ESP32
 */
function sendPursuitCommand() {
  sendCommand(WS_CMD.PURSUIT_MOVE, {
    targetPosition: AppState.pursuit.targetMM,
    maxSpeed: AppState.pursuit.maxSpeedLevel
  });
}

/**
 * Pursuit command loop - sends commands periodically while pursuit is active
 */
function startPursuitLoop() {
  const myGeneration = ++pursuitLoopGeneration;
  
  function loop() {
    if (!AppState.pursuit.active || myGeneration !== pursuitLoopGeneration) return;
    
    sendPursuitCommand();
    
    // Continue loop
    setTimeout(loop, PURSUIT_COMMAND_INTERVAL);
  }
  loop();
}

// ============================================================================
// MOUSE INTERACTION
// ============================================================================

/**
 * Convert pointer position (mouse or touch) to gauge target position
 * @param {number} clientY - The clientY coordinate from mouse or touch event
 */
function updateGaugeFromPointer(clientY) {
  const container = DOM.gaugeContainer;
  const rect = container.getBoundingClientRect();
  const y = clientY - rect.top;
  const containerHeight = rect.height;
  
  // Convert y position to percentage (top = 100%, bottom = 0%)
  let percent = 1 - (y / containerHeight);
  if (percent < 0) percent = 0;
  if (percent > 1) percent = 1;
  
  // Convert to position in mm
  const positionMM = percent * AppState.pursuit.totalDistanceMM;
  setGaugeTarget(positionMM);
}

// ============================================================================
// PURSUIT MODE ACTIVATION
// ============================================================================

/**
 * Enable pursuit mode
 */
function enablePursuitMode() {
  // Check if system is calibrating
  if (AppState.system.currentState === SystemState.CALIBRATING) {
    DOM.pursuitActiveCheckbox.checked = false;
    AppState.pursuit.active = false;
    showAlert(t('overlay.calibrationTitle'), { type: 'warning', title: t('overlay.calibrationTitle') });
    return;
  }
  
  AppState.pursuit.active = true;
  
  // Update button state
  DOM.btnActivatePursuit.textContent = '⏸ ' + t('pursuit.pausePursuit');
  DOM.btnActivatePursuit.classList.remove('btn-success');
  DOM.btnActivatePursuit.classList.add('btn-warning');
  
  // Enable gauge interaction
  DOM.gaugeContainer.style.opacity = '1';
  DOM.gaugeContainer.style.cursor = 'crosshair';
  DOM.gaugeContainer.style.pointerEvents = 'auto';
  
  // Use already set target position (from gauge clicks or current position)
  // Don't reset to current position - keep user's target choice
  if (AppState.pursuit.targetMM === undefined || Number.isNaN(AppState.pursuit.targetMM)) {
    // Only initialize if never set before
    AppState.pursuit.targetMM = AppState.pursuit.currentPositionMM;
    setGaugeTarget(AppState.pursuit.currentPositionMM);
  }
  
  // Enable pursuit mode on ESP32
  sendCommand(WS_CMD.ENABLE_PURSUIT_MODE, {});
  
  // Send initial position command after ESP32 mode switch completes
  setTimeout(function() {
    sendPursuitCommand();
    setTimeout(startPursuitLoop, PURSUIT_COMMAND_INTERVAL);
  }, 200);
}

/**
 * Disable pursuit mode
 */
function disablePursuitMode() {
  AppState.pursuit.active = false;
  pursuitLoopGeneration++;  // Invalidate any running pursuit loop
  
  // Update button state
  DOM.btnActivatePursuit.textContent = '▶ ' + t('common.start');
  DOM.btnActivatePursuit.classList.remove('btn-warning');
  DOM.btnActivatePursuit.classList.add('btn-success');
  DOM.gaugeContainer.style.opacity = '0.5';
  DOM.gaugeContainer.style.cursor = 'not-allowed';
  DOM.gaugeContainer.style.pointerEvents = 'none';
  
  // Disable pursuit mode on ESP32
  sendCommand(WS_CMD.DISABLE_PURSUIT_MODE, {});
  
  // NOTE: AppState.pursuit.targetMM is preserved for when user re-enables pursuit mode
}

/**
 * Toggle pursuit mode on/off
 */
function togglePursuitMode() {
  if (AppState.pursuit.active) {
    disablePursuitMode();
    DOM.pursuitActiveCheckbox.checked = false;
  } else {
    enablePursuitMode();
    DOM.pursuitActiveCheckbox.checked = true;
  }
}

/**
 * Stop pursuit and return to start position
 */
function stopPursuitAndReturn() {
  // Disable pursuit mode
  disablePursuitMode();
  DOM.pursuitActiveCheckbox.checked = false;
  
  // Return to start position to verify contact
  setTimeout(function() {
    console.debug('Stopping pursuit - returning to start for contact verification');
    sendCommand(WS_CMD.RETURN_TO_START, {});
  }, 200);  // Small delay to let pursuit mode disable first
}

// ============================================================================
// MAX DISTANCE LIMIT CONFIGURATION
// ============================================================================

/**
 * Update max distance limit UI based on current state
 */
function updateMaxDistLimitUI() {
  const isReady = AppState.system.currentState === SystemState.READY;
  const totalMM = AppState.pursuit.totalDistanceMM || 0;
  
  // Get current limit percent from AppState (or default to 100)
  const currentPercent = AppState.pursuit.maxDistLimitPercent || 100;
  
  // Only update slider value if user is NOT currently editing it
  if (!AppState.pursuit.isEditingMaxDistLimit) {
    DOM.maxDistLimitSlider.value = currentPercent;
  }
  
  // Enable/disable controls based on state
  DOM.maxDistLimitSlider.disabled = !isReady;
  DOM.btnApplyMaxDistLimit.disabled = !isReady;
  
  // Show/hide warning
  DOM.maxDistLimitWarning.style.display = isReady ? 'none' : 'block';
  
  // Update slider value and display (only if not editing)
  if (!AppState.pursuit.isEditingMaxDistLimit) {
    const effectiveMM = (totalMM * currentPercent / 100).toFixed(1);
    DOM.maxDistLimitValue.textContent = currentPercent + '%';
    DOM.maxDistLimitMM.textContent = '(' + effectiveMM + ' mm)';
  }
}

/**
 * Initialize max distance limit event listeners
 * Call this after initDOMCache()
 */
function initMaxDistLimitListeners() {
  // Toggle configuration panel
  DOM.btnConfigMaxDist.addEventListener('click', function() {
    const isVisible = DOM.maxDistConfigPanel.style.display !== 'none';
    DOM.maxDistConfigPanel.style.display = isVisible ? 'none' : 'block';
    
    if (isVisible) {
      // Panel closed - stop blocking updates
      AppState.pursuit.isEditingMaxDistLimit = false;
    } else {
      // Panel just opened - load current value and START blocking updates
      AppState.pursuit.isEditingMaxDistLimit = true;
      updateMaxDistLimitUI();
    }
  });
  
  // Update slider display while dragging
  DOM.maxDistLimitSlider.addEventListener('input', function() {
    const percent = Number.parseFloat(this.value);
    const totalMM = AppState.pursuit.totalDistanceMM || 0;
    const effectiveMM = (totalMM * percent / 100).toFixed(1);
    
    DOM.maxDistLimitValue.textContent = percent + '%';
    DOM.maxDistLimitMM.textContent = '(' + effectiveMM + ' mm)';
  });
  
  // Apply limit
  DOM.btnApplyMaxDistLimit.addEventListener('click', function() {
    // Defensive guard: ignore if system is not in READY state
    if (AppState.system.currentState !== SystemState.READY) {
      showNotification('⚠️ ' + t('status.modifyReadyOnly'), 'error');
      return;
    }
    const percent = Number.parseFloat(DOM.maxDistLimitSlider.value);
    
    // Reset input fields to safe defaults when applying limit
    document.getElementById('startPosition').value = 0;
    document.getElementById('distance').value = 0;
    
    // Update oscillation and chaos centers with new effective max
    const totalMM = AppState.pursuit.totalDistanceMM || 0;
    const effectiveMax = totalMM * percent / 100;
    
    const oscCenterField = document.getElementById('oscCenter');
    if (oscCenterField && effectiveMax > 0) {
      oscCenterField.value = (effectiveMax / 2).toFixed(1);
      // Send to backend to update oscillation config
      sendCommand(WS_CMD.SET_OSCILLATION, {
        centerPositionMM: effectiveMax / 2,
        amplitudeMM: Number.parseFloat(document.getElementById('oscAmplitude').value) || 50,
        frequencyHz: Number.parseFloat(document.getElementById('oscFrequency').value) || 1,
        waveform: Number.parseInt(document.getElementById('oscWaveform').value) || 0
      });
    }
    
    const chaosCenterField = document.getElementById('chaosCenterPos');
    if (chaosCenterField && effectiveMax > 0) {
      chaosCenterField.value = (effectiveMax / 2).toFixed(1);
    }
    
    sendCommand(WS_CMD.SET_MAX_DISTANCE_LIMIT, {percent: percent});
    DOM.maxDistConfigPanel.style.display = 'none';
    AppState.pursuit.isEditingMaxDistLimit = false;
  });
  
  // Cancel
  DOM.btnCancelMaxDistLimit.addEventListener('click', function() {
    DOM.maxDistConfigPanel.style.display = 'none';
    AppState.pursuit.isEditingMaxDistLimit = false;
  });
  
  // Sensors inversion checkbox
  DOM.chkSensorsInverted.addEventListener('change', function() {
    sendCommand(WS_CMD.SET_SENSORS_INVERTED, { inverted: this.checked });
  });
}

// ============================================================================
// EVENT LISTENERS INITIALIZATION
// ============================================================================

// --- Drag helpers (used by gauge interaction) ---
function startDrag(clientY) {
  if (!AppState.pursuit.active) return;
  AppState.pursuit.isDragging = true;
  document.body.style.userSelect = 'none';
  updateGaugeFromPointer(clientY);
}

function moveDrag(clientY) {
  if (!AppState.pursuit.isDragging) return;
  updateGaugeFromPointer(clientY);
}

function endDrag() {
  if (!AppState.pursuit.isDragging) return;
  AppState.pursuit.isDragging = false;
  document.body.style.userSelect = '';
}

/**
 * Initialize all pursuit mode event listeners
 * Call this after initDOMCache() on page load
 */
function initPursuitListeners() {
  // ========================================================================
  // Gauge interaction - Triple strategy for maximum compatibility:
  // 1. Pointer Events (modern browsers) with document-level move/up
  // 2. Touch Events fallback (older mobile browsers)  
  // 3. Mouse Events fallback (desktop)
  // ========================================================================
  
  // --- Pointer Events (primary) ---
  if (globalThis.PointerEvent) {
    DOM.gaugeContainer.addEventListener('pointerdown', function(e) {
      e.preventDefault();
      startDrag(e.clientY);
    });
    
    document.addEventListener('pointermove', function(e) {
      if (!AppState.pursuit.isDragging) return;
      e.preventDefault();
      moveDrag(e.clientY);
    });
    
    document.addEventListener('pointerup', function() {
      endDrag();
    });
    
    document.addEventListener('pointercancel', function() {
      endDrag();
    });
  }
  
  // --- Touch Events fallback ---
  DOM.gaugeContainer.addEventListener('touchstart', function(e) {
    if (globalThis.PointerEvent) return;
    e.preventDefault();
    startDrag(e.touches[0].clientY);
  }, { passive: false });
  
  document.addEventListener('touchmove', function(e) {
    if (globalThis.PointerEvent) return;
    if (!AppState.pursuit.isDragging) return;
    e.preventDefault();
    moveDrag(e.touches[0].clientY);
  }, { passive: false });
  
  document.addEventListener('touchend', function() {
    if (globalThis.PointerEvent) return;
    endDrag();
  });
  
  document.addEventListener('touchcancel', function() {
    if (globalThis.PointerEvent) return;
    endDrag();
  });
  
  // --- Mouse Events fallback (desktop without pointer events) ---
  DOM.gaugeContainer.addEventListener('mousedown', function(e) {
    if (globalThis.PointerEvent) return;
    e.preventDefault();
    startDrag(e.clientY);
  });
  
  document.addEventListener('mousemove', function(e) {
    if (globalThis.PointerEvent) return;
    if (!AppState.pursuit.isDragging) return;
    moveDrag(e.clientY);
  });
  
  document.addEventListener('mouseup', function() {
    if (globalThis.PointerEvent) return;
    endDrag();
  });
  
  // Pursuit mode checkbox
  DOM.pursuitActiveCheckbox.addEventListener('change', function() {
    if (this.checked) {
      enablePursuitMode();
    } else {
      disablePursuitMode();
    }
  });
  
  // Activate/Pause button
  DOM.btnActivatePursuit.addEventListener('click', function() {
    DOM.pursuitActiveCheckbox.checked = !DOM.pursuitActiveCheckbox.checked;
    DOM.pursuitActiveCheckbox.dispatchEvent(new Event('change'));
  });
  
  // Max speed input
  if (DOM.pursuitMaxSpeed) {
    DOM.pursuitMaxSpeed.addEventListener('change', function() {
      AppState.pursuit.maxSpeedLevel = Number.parseFloat(this.value);
    });
  }
  
  // Speed presets
  DOM.presetPursuitSpeedButtons.forEach(btn => {
    btn.addEventListener('click', function() {
      const speed = Number.parseFloat(this.dataset.pursuitSpeed);
      if (DOM.pursuitMaxSpeed) {
        DOM.pursuitMaxSpeed.value = speed;
      }
      AppState.pursuit.maxSpeedLevel = speed;
      
      DOM.presetPursuitSpeedButtons.forEach(b => b.classList.remove('active'));
      this.classList.add('active');
    });
  });
  
  // Stop button
  if (DOM.btnStopPursuit) {
    DOM.btnStopPursuit.addEventListener('click', stopPursuitAndReturn);
  }
  
  // Initialize max distance limit listeners
  initMaxDistLimitListeners();
  
  console.debug('✅ Pursuit mode listeners initialized');
}

/**
 * Initialize pursuit mode on page load
 * Ensures gauge is disabled until user activates pursuit
 */
function initPursuitModeOnLoad() {
  // Ensure pursuit checkbox is unchecked on fresh load
  DOM.pursuitActiveCheckbox.checked = false;
  AppState.pursuit.active = false;
  
  // Reset button text
  DOM.btnActivatePursuit.textContent = '▶ ' + t('common.start');
  DOM.btnActivatePursuit.classList.remove('btn-warning');
  DOM.btnActivatePursuit.classList.add('btn-success');
  
  // Disable gauge interaction on page load (pursuit mode inactive)
  if (DOM.gaugeContainer) {
    DOM.gaugeContainer.style.opacity = '0.5';
    DOM.gaugeContainer.style.cursor = 'not-allowed';
    DOM.gaugeContainer.style.pointerEvents = 'none';
  }
}

// ============================================================================
// HELPER FUNCTIONS (for use in other modules)
// ============================================================================

/**
 * Check if pursuit mode is currently active
 * @returns {boolean}
 */
function isPursuitActive() {
  return AppState.pursuit.active;
}

/**
 * Get current pursuit max speed level
 * @returns {number}
 */
function getPursuitMaxSpeed() {
  return AppState.pursuit.maxSpeedLevel;
}

/**
 * Check if max distance limit is being edited
 * @returns {boolean}
 */
function isEditingMaxDistanceLimit() {
  return AppState.pursuit.isEditingMaxDistLimit;
}
