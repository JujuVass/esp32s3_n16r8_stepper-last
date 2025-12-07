// ============================================================================
// OSCILLATION CONTROLLER MODULE
// Extracted from main.js - Oscillation mode control and UI management
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
  const center = parseFloat(DOM.oscCenter.value) || 0;
  const amplitude = parseFloat(DOM.oscAmplitude.value) || 0;
  const totalDistMM = AppState.pursuit.totalDistanceMM || 0;
  
  const warning = document.getElementById('oscLimitWarning');
  const statusSpan = document.getElementById('oscLimitStatus');
  const btnStart = DOM.btnStartOscillation;
  
  // If not calibrated yet, show waiting message
  if (!AppState.system.canStart || !totalDistMM || totalDistMM === 0) {
    warning.style.display = 'none';
    statusSpan.textContent = '⏳ En attente calibration';
    statusSpan.style.color = '#ff9800';
    btnStart.disabled = true;
    btnStart.style.opacity = '0.5';
    btnStart.style.cursor = 'not-allowed';
    return false;
  }
  
  // Use pure function if available (from oscillation.js)
  let isValid = true;
  if (typeof validateOscillationLimitsPure === 'function') {
    const result = validateOscillationLimitsPure(center, amplitude, totalDistMM);
    isValid = result.valid;
  } else {
    // Fallback to inline logic
    const minPos = center - amplitude;
    const maxPos = center + amplitude;
    isValid = minPos >= 0 && maxPos <= totalDistMM;
  }
  
  if (!isValid) {
    warning.style.display = 'block';
    statusSpan.textContent = '❌ Invalide';
    statusSpan.style.color = '#e74c3c';
    btnStart.disabled = true;
    btnStart.style.opacity = '0.5';
    btnStart.style.cursor = 'not-allowed';
    return false;
  } else {
    warning.style.display = 'none';
    statusSpan.textContent = '✅ Valide';
    statusSpan.style.color = '#27ae60';
    btnStart.disabled = false;
    btnStart.style.opacity = '1';
    btnStart.style.cursor = 'pointer';
    updateOscillationPresets();  // Update preset buttons state
    return true;
  }
}

/**
 * Send oscillation configuration to backend
 */
function sendOscillationConfig() {
  const amplitude = parseFloat(document.getElementById('oscAmplitude').value) || 0;
  const frequency = parseFloat(document.getElementById('oscFrequency').value) || 0.5;
  
  // 🚀 SAFETY: Check if frequency would exceed speed limit
  const MAX_SPEED_MM_S = maxSpeedLevel * 20.0; // 300 mm/s by default
  
  // Use pure function if available (from oscillation.js)
  if (typeof calculateOscillationPeakSpeedPure === 'function') {
    const theoreticalSpeed = calculateOscillationPeakSpeedPure(frequency, amplitude);
    
    if (amplitude > 0 && theoreticalSpeed > MAX_SPEED_MM_S) {
      const maxAllowedFreq = MAX_SPEED_MM_S / (2.0 * Math.PI * amplitude);
      showNotification(
        `⚠️ Fréquence limitée: ${frequency.toFixed(2)} Hz → ${maxAllowedFreq.toFixed(2)} Hz (vitesse max: ${MAX_SPEED_MM_S.toFixed(0)} mm/s)`,
        'error',
        4000
      );
    }
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
  
  // Delegate to pure function if available
  let config;
  if (typeof buildOscillationConfigPure === 'function') {
    config = buildOscillationConfigPure(formValues);
  } else {
    // Fallback
    config = {
      centerPositionMM: parseFloat(formValues.centerPos) || 0,
      amplitudeMM: parseFloat(formValues.amplitude) || 0,
      waveform: parseInt(formValues.waveform) || 0,
      frequencyHz: parseFloat(formValues.frequency) || 0.5,
      cycleCount: parseInt(formValues.cycleCount) || 0,
      enableRampIn: formValues.enableRampIn,
      rampInDurationMs: parseFloat(formValues.rampInDuration) || 2000,
      enableRampOut: formValues.enableRampOut,
      rampOutDurationMs: parseFloat(formValues.rampOutDuration) || 2000,
      returnToCenter: formValues.returnToCenter
    };
  }
  
  sendCommand(WS_CMD.SET_OSCILLATION, config);
}

/**
 * Update visual state of oscillation preset buttons
 */
function updateOscillationPresets() {
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  if (effectiveMax === 0) return;
  
  const currentCenter = parseFloat(document.getElementById('oscCenter').value) || 0;
  const currentAmplitude = parseFloat(document.getElementById('oscAmplitude').value) || 0;
  
  // 🚀 MAX_SPEED_LEVEL constant (must match backend)
  const MAX_SPEED_MM_S = maxSpeedLevel * 20.0; // 300 mm/s by default
  
  // Validate center presets (must allow current amplitude)
  document.querySelectorAll('[data-osc-center]').forEach(btn => {
    const centerValue = parseFloat(btn.getAttribute('data-osc-center'));
    const minPos = centerValue - currentAmplitude;
    const maxPos = centerValue + currentAmplitude;
    const isValid = minPos >= 0 && maxPos <= effectiveMax;
    
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.3';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
  
  // Validate amplitude presets (must respect current center)
  document.querySelectorAll('[data-osc-amplitude]').forEach(btn => {
    const amplitudeValue = parseFloat(btn.getAttribute('data-osc-amplitude'));
    const minPos = currentCenter - amplitudeValue;
    const maxPos = currentCenter + amplitudeValue;
    const isValid = minPos >= 0 && maxPos <= effectiveMax;
    
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.3';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
  
  // 🚀 Validate frequency presets (must not exceed speed limit)
  document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
    const frequencyValue = parseFloat(btn.getAttribute('data-osc-frequency'));
    
    // Calculate theoretical speed for this frequency
    if (currentAmplitude > 0) {
      const theoreticalSpeed = 2 * Math.PI * frequencyValue * currentAmplitude;
      const isValid = theoreticalSpeed <= MAX_SPEED_MM_S;
      
      btn.disabled = !isValid;
      btn.style.opacity = isValid ? '1' : '0.3';
      btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      btn.title = isValid 
        ? `${frequencyValue} Hz (${theoreticalSpeed.toFixed(0)} mm/s)` 
        : `⚠️ ${frequencyValue} Hz dépasserait ${MAX_SPEED_MM_S} mm/s (${theoreticalSpeed.toFixed(0)} mm/s calculé)`;
    }
  });
}

/**
 * Send oscillation cycle pause configuration
 */
function sendOscCyclePauseConfig() {
  const enabled = document.getElementById('oscCyclePauseEnabled')?.checked || false;
  const isRandom = document.getElementById('oscCyclePauseRandom')?.checked || false;
  const durationSec = parseFloat(document.getElementById('oscCyclePauseDuration')?.value || 0);
  let minSec = parseFloat(document.getElementById('oscCyclePauseMin')?.value || 0.5);
  let maxSec = parseFloat(document.getElementById('oscCyclePauseMax')?.value || 3.0);
  
  // 🆕 VALIDATION: Min doit être ≤ Max (seulement si random activé)
  if (isRandom && minSec > maxSec) {
    showNotification('⚠️ Pause Min (' + minSec.toFixed(1) + 's) doit être ≤ Max (' + maxSec.toFixed(1) + 's)', 'warning');
    // Auto-correction: ajuster Max = Min
    maxSec = minSec;
    document.getElementById('oscCyclePauseMax').value = maxSec;
  }
  
  sendCommand(WS_CMD.SET_CYCLE_PAUSE, {
    mode: 'oscillation',
    enabled: enabled,
    isRandom: isRandom,
    durationSec: durationSec,
    minSec: minSec,
    maxSec: maxSec
  });
}

/**
 * Start oscillation mode
 */
function startOscillation() {
  if (!validateOscillationLimits()) {
    showNotification('Limites invalides: ajustez le centre ou l\'amplitude', 'error');
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
 * Extracted from main.js updateUI() for better modularity
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
      data.oscillationState.currentAmplitude.toFixed(2);
    DOM.oscCompletedCycles.textContent = 
      data.oscillationState.completedCycles;
    
    let rampStatus = 'Aucune';
    if (data.oscillationState.isTransitioning) {
      rampStatus = '🔄 Transition fréquence...';
    } else if (data.oscillationState.isRampingIn) {
      rampStatus = '📈 Rampe entrée';
    } else if (data.oscillationState.isRampingOut) {
      rampStatus = '📉 Rampe sortie';
    } else if (data.operationMode === 3 && data.state === SystemState.RUNNING) {
      rampStatus = '✅ Stabilisé';
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
        DOM.oscFrequency.title = `⚠️ Fréquence limitée de ${data.oscillation.frequencyHz.toFixed(2)} Hz à ${displayFreq.toFixed(2)} Hz (vitesse max: 300 mm/s)`;
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
  if (data.oscillation && data.oscillation.cyclePause) {
    const pauseStatusOsc = document.getElementById('cyclePauseStatusOsc');
    const pauseRemainingOsc = document.getElementById('cyclePauseRemainingOsc');
    
    if (data.oscillation.cyclePause.isPausing && pauseStatusOsc && pauseRemainingOsc) {
      const remainingSec = (data.oscillation.cyclePause.remainingMs / 1000).toFixed(1);
      pauseStatusOsc.style.display = 'block';
      pauseRemainingOsc.textContent = remainingSec + 's';
    } else if (pauseStatusOsc) {
      pauseStatusOsc.style.display = 'none';
    }
    
    // Sync UI to backend state (only if section is expanded)
    const sectionOsc = getCyclePauseOscSection();
    const headerTextOsc = document.getElementById('cyclePauseOscHeaderText');
    if (sectionOsc && headerTextOsc) {
      const isEnabled = data.oscillation.cyclePause.enabled;
      const isCollapsed = sectionOsc.classList.contains('collapsed');
      
      // Sync collapsed state with backend enabled state
      if (isEnabled && isCollapsed) {
        sectionOsc.classList.remove('collapsed');
        headerTextOsc.textContent = '⏸️ Pause entre cycles - activée';
      } else if (!isEnabled && !isCollapsed) {
        sectionOsc.classList.add('collapsed');
        headerTextOsc.textContent = '⏸️ Pause entre cycles - désactivée';
      }
      
      // Sync radio buttons
      if (data.oscillation.cyclePause.isRandom) {
        document.getElementById('pauseModeRandomOsc').checked = true;
        document.getElementById('pauseFixedControlsOsc').style.display = 'none';
        document.getElementById('pauseRandomControlsOsc').style.display = 'block';
      } else {
        document.getElementById('pauseModeFixedOsc').checked = true;
        document.getElementById('pauseFixedControlsOsc').style.display = 'flex';
        document.getElementById('pauseRandomControlsOsc').style.display = 'none';
      }
      
      // Sync input values (avoid overwriting if user is editing)
      if (document.activeElement !== document.getElementById('cyclePauseDurationOsc')) {
        document.getElementById('cyclePauseDurationOsc').value = data.oscillation.cyclePause.pauseDurationSec.toFixed(1);
      }
      if (document.activeElement !== document.getElementById('cyclePauseMinOsc')) {
        document.getElementById('cyclePauseMinOsc').value = data.oscillation.cyclePause.minPauseSec.toFixed(1);
      }
      if (document.activeElement !== document.getElementById('cyclePauseMaxOsc')) {
        document.getElementById('cyclePauseMaxOsc').value = data.oscillation.cyclePause.maxPauseSec.toFixed(1);
      }
    }
  }
  
  // ===== PAUSE/STOP BUTTONS =====
  const btnPauseOsc = document.getElementById('btnPauseOscillation');
  if (btnPauseOsc) {
    btnPauseOsc.disabled = !isRunningOrPaused;
    if (isPaused) {
      btnPauseOsc.innerHTML = '▶ Reprendre';
    } else {
      btnPauseOsc.innerHTML = '⏸ Pause';
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
  // Oscillation input editing protection + real-time update
  // Use mousedown to catch spinner clicks BEFORE focus + force focus immediately
  
  // Center position
  document.getElementById('oscCenter').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscCenter';
    this.focus();
  });
  document.getElementById('oscCenter').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscCenter';
  });
  document.getElementById('oscCenter').addEventListener('blur', function() {
    AppState.editing.oscField = null;
    validateOscillationLimits();
    sendOscillationConfig();
  });
  document.getElementById('oscCenter').addEventListener('input', function() {
    clearTimeout(AppState.oscillation.validationTimer);
    AppState.oscillation.validationTimer = setTimeout(validateOscillationLimits, 300);
  });
  
  // Amplitude
  document.getElementById('oscAmplitude').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscAmplitude';
    this.focus();
  });
  document.getElementById('oscAmplitude').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscAmplitude';
  });
  document.getElementById('oscAmplitude').addEventListener('blur', function() {
    AppState.editing.oscField = null;
    validateOscillationLimits();
    sendOscillationConfig();
  });
  document.getElementById('oscAmplitude').addEventListener('input', function() {
    clearTimeout(AppState.oscillation.validationTimer);
    AppState.oscillation.validationTimer = setTimeout(validateOscillationLimits, 300);
  });
  
  // Waveform
  document.getElementById('oscWaveform').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscWaveform';
    this.focus();
  });
  document.getElementById('oscWaveform').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscWaveform';
  });
  document.getElementById('oscWaveform').addEventListener('change', function() {
    AppState.editing.oscField = null;
    sendOscillationConfig();
  });
  
  // Frequency
  document.getElementById('oscFrequency').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscFrequency';
    this.focus();
  });
  document.getElementById('oscFrequency').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscFrequency';
  });
  document.getElementById('oscFrequency').addEventListener('blur', function() {
    AppState.editing.oscField = null;
    validateOscillationLimits();
    sendOscillationConfig();
  });
  document.getElementById('oscFrequency').addEventListener('input', function() {
    clearTimeout(AppState.oscillation.validationTimer);
    AppState.oscillation.validationTimer = setTimeout(() => {
      validateOscillationLimits();
      updateOscillationPresets();
    }, 300);
  });
  
  // Ramp In Duration
  document.getElementById('oscRampInDuration').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscRampInDuration';
    this.focus();
  });
  document.getElementById('oscRampInDuration').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscRampInDuration';
  });
  document.getElementById('oscRampInDuration').addEventListener('blur', function() {
    AppState.editing.oscField = null;
    sendOscillationConfig();
  });
  
  // Ramp Out Duration
  document.getElementById('oscRampOutDuration').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscRampOutDuration';
    this.focus();
  });
  document.getElementById('oscRampOutDuration').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscRampOutDuration';
  });
  document.getElementById('oscRampOutDuration').addEventListener('blur', function() {
    AppState.editing.oscField = null;
    sendOscillationConfig();
  });
  
  // Cycle Count
  document.getElementById('oscCycleCount').addEventListener('mousedown', function() {
    AppState.editing.oscField = 'oscCycleCount';
    this.focus();
  });
  document.getElementById('oscCycleCount').addEventListener('focus', function() {
    AppState.editing.oscField = 'oscCycleCount';
  });
  document.getElementById('oscCycleCount').addEventListener('blur', function() {
    AppState.editing.oscField = null;
    sendOscillationConfig();
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
  
  // Cycle Pause listeners (Oscillation mode)
  if (document.getElementById('oscCyclePauseEnabled')) {
    document.getElementById('oscCyclePauseEnabled').addEventListener('change', sendOscCyclePauseConfig);
  }
  if (document.getElementById('oscCyclePauseRandom')) {
    document.getElementById('oscCyclePauseRandom').addEventListener('change', sendOscCyclePauseConfig);
  }
  if (document.getElementById('oscCyclePauseDuration')) {
    document.getElementById('oscCyclePauseDuration').addEventListener('change', sendOscCyclePauseConfig);
  }
  if (document.getElementById('oscCyclePauseMin')) {
    document.getElementById('oscCyclePauseMin').addEventListener('change', sendOscCyclePauseConfig);
  }
  if (document.getElementById('oscCyclePauseMax')) {
    document.getElementById('oscCyclePauseMax').addEventListener('change', sendOscCyclePauseConfig);
  }
  
  // Start/Stop/Pause buttons
  document.getElementById('btnStartOscillation').addEventListener('click', startOscillation);
  document.getElementById('btnStopOscillation').addEventListener('click', stopOscillation);
  document.getElementById('btnPauseOscillation').addEventListener('click', pauseOscillation);
  
  // Preset buttons - Center
  document.querySelectorAll('[data-osc-center]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        document.getElementById('oscCenter').value = this.getAttribute('data-osc-center');
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
        const newAmplitude = this.getAttribute('data-osc-amplitude');
        console.log('🎯 Preset amplitude clicked: ' + newAmplitude + 'mm');
        document.getElementById('oscAmplitude').value = newAmplitude;
        console.log('📤 Sending oscillation config with amplitude=' + newAmplitude);
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
        document.getElementById('oscFrequency').value = this.getAttribute('data-osc-frequency');
        sendOscillationConfig();
      }
    });
  });
  
  console.log('🌊 OscillationController initialized');
}

// ============================================================================
// EXPOSED API
// ============================================================================
// Functions available globally:
// - toggleOscHelp()
// - validateOscillationLimits()
// - sendOscillationConfig()
// - updateOscillationPresets()
// - sendOscCyclePauseConfig()
// - startOscillation(), stopOscillation(), pauseOscillation()
// - updateOscillationUI(data) - Called from main.js updateUI()
// - initOscillationListeners()
