// ============================================================================
// OSCILLATION CONTROLLER MODULE
// ============================================================================

// ============================================================================
// OSCILLATION MODE - STATE
// ============================================================================

/**
 * Oscillation state is centralized in AppState.oscillation:
 * - validationTimer: debounce timer for form validation
 */

// ============================================================================
// OSCILLATION MODE - HELPER FUNCTIONS
// ============================================================================

/**
 * Toggle oscillation help section visibility
 */
function toggleOscHelp() {
  const helpSection = document.getElementById('oscHelpSection');
  helpSection.style.display = helpSection.style.display === 'none' ? 'block' : 'none';
}

/**
 * Validate oscillation limits (center ± amplitude must fit within calibrated range)
 * @returns {boolean} true if limits are valid
 */
function validateOscillationLimits() {
  const center = Number.parseFloat(DOM.oscCenter.value) || 0;
  const amplitude = Number.parseFloat(DOM.oscAmplitude.value) || 0;
  const totalDistMM = AppState.pursuit.totalDistanceMM || 0;
  
  const warning = document.getElementById('oscLimitWarning');
  const statusSpan = document.getElementById('oscLimitStatus');
  const btnStart = DOM.btnStartOscillation;
  
  // If not calibrated yet, show waiting message
  if (!AppState.system.canStart || !totalDistMM || totalDistMM === 0) {
    warning.style.display = 'none';
    statusSpan.textContent = t('oscillation.awaitingCalibration');
    statusSpan.style.color = '#ff9800';
    btnStart.disabled = true;
    btnStart.style.opacity = '0.5';
    btnStart.style.cursor = 'not-allowed';
    return false;
  }
  
  // Validate limits
  let isValid = true;
  const minPos = center - amplitude;
  const maxPos = center + amplitude;
  isValid = minPos >= 0 && maxPos <= totalDistMM;
  
  if (isValid) {
    warning.style.display = 'none';
    statusSpan.textContent = t('oscillation.valid');
    statusSpan.style.color = '#27ae60';
    btnStart.disabled = false;
    btnStart.style.opacity = '1';
    btnStart.style.cursor = 'pointer';
    updateOscillationPresets();  // Update preset buttons state
    return true;
  } else {
    warning.style.display = 'block';
    statusSpan.textContent = t('oscillation.invalid');
    statusSpan.style.color = '#e74c3c';
    btnStart.disabled = true;
    btnStart.style.opacity = '0.5';
    btnStart.style.cursor = 'not-allowed';
    return false;
  }
}

/**
 * Send oscillation configuration to backend
 */
function sendOscillationConfig() {
  const amplitude = Number.parseFloat(document.getElementById('oscAmplitude').value) || 0;
  const frequency = Number.parseFloat(document.getElementById('oscFrequency').value) || 0.5;
  
  // 🚀 SAFETY: Check if frequency would exceed speed limit
  const MAX_SPEED_MM_S = maxSpeedLevel * OSC_SPEED_MULTIPLIER;
  const theoreticalSpeed = 2 * Math.PI * frequency * amplitude;
  
  if (amplitude > 0 && theoreticalSpeed > MAX_SPEED_MM_S) {
    const maxAllowedFreq = MAX_SPEED_MM_S / (2 * Math.PI * amplitude);
    showNotification(
      '⚠️ ' + t('oscillation.freqLimited', {freq: frequency.toFixed(2), max: maxAllowedFreq.toFixed(2), speed: MAX_SPEED_MM_S.toFixed(0)}),
      'error',
      4000
    );
  }
  
  // Collect form values and delegate to pure function
  const formValues = {
    centerPos: document.getElementById('oscCenter').value,
    amplitude: document.getElementById('oscAmplitude').value,
    waveform: document.getElementById('oscWaveform').value,
    frequency: document.getElementById('oscFrequency').value,
    cycleCount: document.getElementById('oscCycleCount').value,
    enableRampIn: document.getElementById('oscRampInEnable').checked,
    rampInDuration: document.getElementById('oscRampInDuration').value,
    enableRampOut: document.getElementById('oscRampOutEnable').checked,
    rampOutDuration: document.getElementById('oscRampOutDuration').value,
    returnToCenter: document.getElementById('oscReturnCenter').checked
  };
  
  // Build config from form values
  const rampIn = Number.parseFloat(formValues.rampInDuration);
  const rampOut = Number.parseFloat(formValues.rampOutDuration);
  const config = {
    centerPositionMM: Number.parseFloat(formValues.centerPos) || 0,
    amplitudeMM: Number.parseFloat(formValues.amplitude) || 0,
    waveform: Number.parseInt(formValues.waveform) || 0,
    frequencyHz: Number.parseFloat(formValues.frequency) || 0.5,
    cycleCount: Number.parseInt(formValues.cycleCount) || 0,
    enableRampIn: formValues.enableRampIn,
    rampInDurationMs: Number.isNaN(rampIn) ? 2000 : rampIn,
    enableRampOut: formValues.enableRampOut,
    rampOutDurationMs: Number.isNaN(rampOut) ? 2000 : rampOut,
    returnToCenter: formValues.returnToCenter
  };
  
  sendCommand(WS_CMD.SET_OSCILLATION, config);
}

/**
 * Update visual state of oscillation preset buttons
 */
function updateOscillationPresets() {
  // Shared center/amplitude/linked preset validation
  updateCenterAmplitudePresets({
    centerInputId: 'oscCenter',
    amplitudeInputId: 'oscAmplitude',
    linkedCheckboxId: 'oscAmplitudeLinked',
    dataPrefix: 'osc',
    i18nPrefix: 'oscillation'
  });
  
  // Oscillation-specific: Validate frequency presets (must not exceed speed limit)
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  if (effectiveMax === 0) return;
  
  const currentAmplitude = Number.parseFloat(document.getElementById('oscAmplitude').value) || 0;
  const MAX_SPEED_MM_S = maxSpeedLevel * OSC_SPEED_MULTIPLIER;
  
  document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
    const frequencyValue = Number.parseFloat(btn.dataset.oscFrequency);
    
    // Calculate theoretical speed for this frequency
    if (currentAmplitude > 0) {
      const theoreticalSpeed = 2 * Math.PI * frequencyValue * currentAmplitude;
      const isValid = theoreticalSpeed <= MAX_SPEED_MM_S;
      
      btn.disabled = !isValid;
      btn.style.opacity = isValid ? '1' : '0.3';
      btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      btn.title = isValid 
        ? `${frequencyValue} Hz (${theoreticalSpeed.toFixed(0)} mm/s)` 
        : '⚠️ ' + t('oscillation.freqExceed', {freq: frequencyValue, max: MAX_SPEED_MM_S, calc: theoreticalSpeed.toFixed(0)});
    }
  });
}

/**
 * Start oscillation mode
 */
function startOscillation() {
  if (!validateOscillationLimits()) {
    showNotification(t('oscillation.limitWarning'), 'error');
    return;
  }
  
  // Send final config + start (config already sent in real-time, but ensure it's current)
  sendOscillationConfig();
  
  // Wait a bit then start
  setTimeout(function() {
    sendCommand(WS_CMD.START_OSCILLATION, {});
  }, 50);
}

/**
 * Stop oscillation mode (with optional return to start modal)
 */
function stopOscillation() {
  // Only show modal if motor has moved (currentStep > 0)
  if (currentPositionMM > 0.5) {
    showStopModal();
  } else {
    // Direct stop if at position 0
    sendCommand(WS_CMD.STOP_OSCILLATION, {});
  }
}

/**
 * Pause/resume oscillation mode
 */
function pauseOscillation() {
  sendCommand(WS_CMD.PAUSE);
}

// ============================================================================
// OSCILLATION MODE - UI UPDATE (called from main.js updateUI)
// ============================================================================

/**
 * Update oscillation mode UI from WebSocket status data
 * @param {Object} data - Status data from WebSocket
 */
function updateOscillationUI(data) {
  if (!data) return;
  
  const isRunning = data.state === SystemState.RUNNING;
  const isPaused = data.state === SystemState.PAUSED;
  const isRunningOrPaused = isRunning || isPaused;
  const isError = data.state === SystemState.ERROR;
  
  // ===== OSCILLATION STATE DISPLAY =====
  if (data.oscillation && data.oscillationState) {
    DOM.oscCurrentAmplitude.textContent = 
      (data.oscillationState.currentAmplitude ?? data.oscillation.amplitudeMM).toFixed(2);
    DOM.oscCompletedCycles.textContent = 
      data.oscillationState.completedCycles;
    
    let rampStatus = t('common.none');
    if (data.oscillationState.isTransitioning) {
      rampStatus = t('oscillation.rampTransition');
    } else if (data.oscillationState.isRampingIn) {
      rampStatus = t('oscillation.rampInLabel');
    } else if (data.oscillationState.isRampingOut) {
      rampStatus = '📉 ' + t('oscillation.rampOut');
    } else if (data.movementType === 3 && data.state === SystemState.RUNNING) {
      rampStatus = t('oscillation.rampStable');
    }
    DOM.oscRampStatus.textContent = rampStatus;
    
    // 🔒 DISABLE frequency controls during transition (500ms smooth change)
    const isTransitioning = data.oscillationState.isTransitioning || false;
    DOM.oscFrequency.disabled = isTransitioning;
    
    // Apply visual feedback during transition
    if (isTransitioning) {
      DOM.oscFrequency.style.backgroundColor = '#fff3cd';
      DOM.oscFrequency.style.cursor = 'not-allowed';
    } else {
      DOM.oscFrequency.style.backgroundColor = '';
      DOM.oscFrequency.style.cursor = '';
    }
    
    // Also disable preset buttons during transition
    document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
      btn.disabled = isTransitioning;
      btn.style.opacity = isTransitioning ? '0.5' : '1';
      btn.style.cursor = isTransitioning ? 'not-allowed' : 'pointer';
    });
    
    // Sync oscillation config to UI (skip fields being edited OR having focus)
    if (AppState.editing.oscField !== 'oscCenter' && document.activeElement !== DOM.oscCenter) {
      DOM.oscCenter.value = data.oscillation.centerPositionMM.toFixed(1);
    }
    
    if (AppState.editing.oscField !== 'oscAmplitude' && document.activeElement !== DOM.oscAmplitude) {
      DOM.oscAmplitude.value = data.oscillation.amplitudeMM.toFixed(1);
    }
    
    if (AppState.editing.oscField !== 'oscWaveform' && document.activeElement !== DOM.oscWaveform) {
      DOM.oscWaveform.value = data.oscillation.waveform;
    }
    
    if (AppState.editing.oscField !== 'oscFrequency' && document.activeElement !== DOM.oscFrequency && !isTransitioning) {
      // Use effective frequency if available (accounts for speed limiting)
      const displayFreq = data.oscillation.effectiveFrequencyHz || data.oscillation.frequencyHz;
      const isFreqLimited = data.oscillation.effectiveFrequencyHz && 
                            Math.abs(data.oscillation.effectiveFrequencyHz - data.oscillation.frequencyHz) > 0.001;
      
      DOM.oscFrequency.value = displayFreq.toFixed(3);
      
      // Visual feedback if frequency is limited
      if (isFreqLimited) {
        DOM.oscFrequency.style.backgroundColor = '#ffe8e8';
        DOM.oscFrequency.style.fontWeight = 'bold';
        DOM.oscFrequency.style.color = '#d32f2f';
        DOM.oscFrequency.title = '⚠️ ' + t('oscillation.freqLimitTitle', {from: data.oscillation.frequencyHz.toFixed(2), to: displayFreq.toFixed(2)});
      } else {
        DOM.oscFrequency.style.backgroundColor = '';
        DOM.oscFrequency.style.fontWeight = '';
        DOM.oscFrequency.style.color = '';
        DOM.oscFrequency.title = '';
      }
    }
    
    if (AppState.editing.oscField !== 'oscRampInDuration' && document.activeElement !== DOM.oscRampInDuration) {
      DOM.oscRampInDuration.value = data.oscillation.rampInDurationMs;
    }
    
    if (AppState.editing.oscField !== 'oscRampOutDuration' && document.activeElement !== DOM.oscRampOutDuration) {
      DOM.oscRampOutDuration.value = data.oscillation.rampOutDurationMs;
    }
    
    if (AppState.editing.oscField !== 'oscCycleCount' && document.activeElement !== DOM.oscCycleCount) {
      DOM.oscCycleCount.value = data.oscillation.cycleCount;
    }
    
    // Checkboxes (not edited via focus)
    DOM.oscRampInEnable.checked = data.oscillation.enableRampIn;
    DOM.oscRampOutEnable.checked = data.oscillation.enableRampOut;
    DOM.oscReturnCenter.checked = data.oscillation.returnToCenter;
    
    // Validate limits (only if not editing center or amplitude)
    if (AppState.editing.oscField !== 'oscCenter' && AppState.editing.oscField !== 'oscAmplitude') {
      validateOscillationLimits();
    }
    
    // Update preset buttons visual state
    updateOscillationPresets();
  }
  
  // ===== CYCLE PAUSE DISPLAY (MODE OSCILLATION) =====
  if (data.oscillation?.cyclePause) {
    syncCyclePauseUI(data.oscillation.cyclePause, 'Osc', getCyclePauseOscSection, 'oscillation');
  }
  
  // ===== PAUSE/STOP BUTTONS =====
  const btnPauseOsc = document.getElementById('btnPauseOscillation');
  if (btnPauseOsc) {
    btnPauseOsc.disabled = !isRunningOrPaused;
    if (isPaused) {
      btnPauseOsc.innerHTML = '▶ ' + t('common.resume');
    } else {
      btnPauseOsc.innerHTML = '⏸ ' + t('common.pause');
    }
  }
  
  const btnStopOsc = document.getElementById('btnStopOscillation');
  if (btnStopOsc) {
    btnStopOsc.disabled = !(isRunningOrPaused || isError);
  }
}

// ============================================================================
// OSCILLATION MODE - INITIALIZATION
// ============================================================================

/**
 * Initialize all oscillation mode event listeners
 * Called from main.js on window load
 */
function initOscillationListeners() {
  // ===== EDITABLE INPUTS (using setupEditableOscInput from utils.js) =====
  
  // Center position: validate + send on blur, live validation on input
  setupEditableOscInput('oscCenter', {
    onBlur: () => { validateOscillationLimits(); sendOscillationConfig(); },
    onInput: () => validateOscillationLimits()
  });
  
  // Amplitude: validate + send on blur, live validation on input
  setupEditableOscInput('oscAmplitude', {
    onBlur: () => { validateOscillationLimits(); sendOscillationConfig(); },
    onInput: () => validateOscillationLimits()
  });
  
  // Waveform (select): send on change
  setupEditableOscInput('oscWaveform', {
    onChange: () => sendOscillationConfig()
  });
  
  // Frequency: validate + send on blur, live validation + preset update on input
  setupEditableOscInput('oscFrequency', {
    onBlur: () => { validateOscillationLimits(); sendOscillationConfig(); },
    onInput: () => { validateOscillationLimits(); updateOscillationPresets(); }
  });
  
  // Ramp durations: send on blur only
  setupEditableOscInput('oscRampInDuration', {
    onBlur: () => sendOscillationConfig()
  });
  setupEditableOscInput('oscRampOutDuration', {
    onBlur: () => sendOscillationConfig()
  });
  
  // Cycle count: send on blur only
  setupEditableOscInput('oscCycleCount', {
    onBlur: () => sendOscillationConfig()
  });
  
  // Ramp toggles
  document.getElementById('oscRampInEnable').addEventListener('change', function() {
    sendOscillationConfig();
  });
  document.getElementById('oscRampOutEnable').addEventListener('change', function() {
    sendOscillationConfig();
  });
  document.getElementById('oscReturnCenter').addEventListener('change', function() {
    sendOscillationConfig();
  });
  
  // Note: Cycle Pause listeners are handled by createCyclePauseHandlers() in initCyclePauseHandlers()
  
  // Start/Stop/Pause buttons
  document.getElementById('btnStartOscillation').addEventListener('click', startOscillation);
  document.getElementById('btnStopOscillation').addEventListener('click', stopOscillation);
  document.getElementById('btnPauseOscillation').addEventListener('click', pauseOscillation);
  
  // Preset buttons - Center
  document.querySelectorAll('[data-osc-center]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        document.getElementById('oscCenter').value = this.dataset.oscCenter;
        sendOscillationConfig();
        validateOscillationLimits();
        updateOscillationPresets();
      }
    });
  });
  
  // Preset buttons - Amplitude
  document.querySelectorAll('[data-osc-amplitude]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const newAmplitude = this.dataset.oscAmplitude;
        console.debug('🎯 Preset amplitude clicked: ' + newAmplitude + 'mm');
        document.getElementById('oscAmplitude').value = newAmplitude;
        console.debug('📤 Sending oscillation config with amplitude=' + newAmplitude);
        sendOscillationConfig();
        validateOscillationLimits();
        updateOscillationPresets();
      }
    });
  });
  
  // Preset buttons - Frequency
  document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        document.getElementById('oscFrequency').value = this.dataset.oscFrequency;
        sendOscillationConfig();
      }
    });
  });
  
  // 🆕 Relative preset buttons - Center
  document.querySelectorAll('[data-osc-center-rel]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const relValue = Number.parseInt(this.dataset.oscCenterRel);
        const currentCenter = Number.parseFloat(document.getElementById('oscCenter').value) || 0;
        const newCenter = Math.max(0, currentCenter + relValue);
        document.getElementById('oscCenter').value = newCenter;
        
        // If linked, also update amplitude
        if (document.getElementById('oscAmplitudeLinked')?.checked) {
          document.getElementById('oscAmplitude').value = newCenter;
        }
        
        sendOscillationConfig();
        validateOscillationLimits();
        updateOscillationPresets();
      }
    });
  });
  
  // 🆕 Relative preset buttons - Amplitude
  document.querySelectorAll('[data-osc-amplitude-rel]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const relValue = Number.parseInt(this.dataset.oscAmplitudeRel);
        const currentAmplitude = Number.parseFloat(document.getElementById('oscAmplitude').value) || 0;
        const newAmplitude = Math.max(1, currentAmplitude + relValue);
        document.getElementById('oscAmplitude').value = newAmplitude;
        sendOscillationConfig();
        validateOscillationLimits();
        updateOscillationPresets();
      }
    });
  });
  
  // 🆕 Amplitude linked checkbox (amplitude = center)
  const oscAmplitudeLinked = document.getElementById('oscAmplitudeLinked');
  if (oscAmplitudeLinked) {
    oscAmplitudeLinked.addEventListener('change', function() {
      if (this.checked) {
        // Link: set amplitude = center
        const center = Number.parseFloat(document.getElementById('oscCenter').value) || 0;
        document.getElementById('oscAmplitude').value = center;
        sendOscillationConfig();
        validateOscillationLimits();
      }
      updateOscillationPresets();
    });
  }
  
  // 🆕 When center changes and linked, update amplitude too
  const oscCenterInput = document.getElementById('oscCenter');
  if (oscCenterInput) {
    oscCenterInput.addEventListener('input', function() {
      if (document.getElementById('oscAmplitudeLinked')?.checked) {
        document.getElementById('oscAmplitude').value = this.value;
      }
    });
  }
  
  console.debug('🌊 OscillationController initialized');
}
