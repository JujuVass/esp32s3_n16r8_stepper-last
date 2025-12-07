// ============================================================================
// CHAOS CONTROLLER MODULE
// Extracted from main.js - Chaos mode control and UI management
// ============================================================================

// ============================================================================
// CHAOS MODE - CONSTANTS
// ============================================================================

// Pattern IDs list (for reference)
const CHAOS_PATTERNS = [
  'patternZigzag',      // 0
  'patternSweep',       // 1
  'patternPulse',       // 2
  'patternDrift',       // 3
  'patternBurst',       // 4
  'patternWave',        // 5
  'patternPendulum',    // 6
  'patternSpiral',      // 7
  'patternCalm',        // 8 (Breathing)
  'patternBruteForce',  // 9
  'patternLiberator'    // 10
];

// ============================================================================
// CHAOS MODE - HELPER FUNCTIONS
// ============================================================================

/**
 * Toggle chaos help section visibility
 */
function toggleChaosHelp() {
  const helpSection = document.getElementById('chaosHelpSection');
  helpSection.style.display = helpSection.style.display === 'none' ? 'block' : 'none';
}

/**
 * Send chaos configuration to backend
 */
function sendChaosConfig() {
  // Collect form values
  const formValues = {
    centerPos: document.getElementById('chaosCenterPos').value,
    amplitude: document.getElementById('chaosAmplitude').value,
    maxSpeed: document.getElementById('chaosMaxSpeed').value,
    craziness: document.getElementById('chaosCraziness').value,
    duration: document.getElementById('chaosDuration').value,
    seed: document.getElementById('chaosSeed').value,
    patternsEnabled: [
      document.getElementById('patternZigzag').checked,
      document.getElementById('patternSweep').checked,
      document.getElementById('patternPulse').checked,
      document.getElementById('patternDrift').checked,
      document.getElementById('patternBurst').checked,
      document.getElementById('patternWave').checked,
      document.getElementById('patternPendulum').checked,
      document.getElementById('patternSpiral').checked,
      document.getElementById('patternCalm').checked,
      document.getElementById('patternBruteForce').checked,
      document.getElementById('patternLiberator').checked
    ]
  };
  
  // Delegate to pure function if available (from chaos.js)
  let config;
  if (typeof buildChaosConfigPure === 'function') {
    config = buildChaosConfigPure(formValues);
  } else {
    // Fallback
    config = {
      centerPositionMM: parseFloat(formValues.centerPos) || 0,
      amplitudeMM: parseFloat(formValues.amplitude) || 0,
      maxSpeedLevel: parseFloat(formValues.maxSpeed) || 10,
      crazinessPercent: parseInt(formValues.craziness) || 50,
      durationSeconds: parseInt(formValues.duration) || 30,
      seed: parseInt(formValues.seed) || 0,
      patternsEnabled: formValues.patternsEnabled
    };
  }
  
  sendCommand(WS_CMD.SET_CHAOS_CONFIG, config);
}

/**
 * Validate chaos limits
 * @returns {boolean} true if limits are valid
 */
function validateChaosLimits() {
  const centerPos = parseFloat(document.getElementById('chaosCenterPos').value);
  const amplitude = parseFloat(document.getElementById('chaosAmplitude').value);
  const totalDistMM = AppState.pursuit.totalDistanceMM || 0;
  
  // Use pure function if available (from context.js)
  if (typeof validateChaosLimitsPure === 'function') {
    const result = validateChaosLimitsPure(centerPos, amplitude, totalDistMM);
    return result.valid;
  }
  
  // Fallback to inline logic
  if (centerPos - amplitude < 0 || centerPos + amplitude > totalDistMM) {
    return false;
  }
  return true;
}

/**
 * Update visual state of chaos preset buttons
 */
function updateChaosPresets() {
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  if (effectiveMax === 0) return;
  
  const currentCenter = parseFloat(document.getElementById('chaosCenterPos').value) || 0;
  const currentAmplitude = parseFloat(document.getElementById('chaosAmplitude').value) || 0;
  
  // Validate center presets (must allow current amplitude)
  document.querySelectorAll('[data-chaos-center]').forEach(btn => {
    const centerValue = parseFloat(btn.getAttribute('data-chaos-center'));
    const minPos = centerValue - currentAmplitude;
    const maxPos = centerValue + currentAmplitude;
    const isValid = minPos >= 0 && maxPos <= effectiveMax;
    
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.3';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
  
  // Validate amplitude presets (must respect current center)
  document.querySelectorAll('[data-chaos-amplitude]').forEach(btn => {
    const amplitudeValue = parseFloat(btn.getAttribute('data-chaos-amplitude'));
    const minPos = currentCenter - amplitudeValue;
    const maxPos = currentCenter + amplitudeValue;
    const isValid = minPos >= 0 && maxPos <= effectiveMax;
    
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.3';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
}

/**
 * Update pattern toggle button text (Tout/Aucun)
 */
function updatePatternToggleButton() {
  const checkedCount = CHAOS_PATTERNS.filter(id => document.getElementById(id).checked).length;
  const btn = document.getElementById('btnEnableAllPatterns');
  
  if (checkedCount === CHAOS_PATTERNS.length) {
    // All checked → show "Aucun" (next action will uncheck all)
    btn.textContent = '❌ Aucun';
  } else {
    // Some or none checked → show "Tout" (next action will check all)
    btn.textContent = '✅ Tout';
  }
}

/**
 * Update chaos UI with live data from WebSocket status
 * @param {object} data - Status data from backend
 */
function updateChaosUI(data) {
  if (!data.chaosState) return;
  
  const isRunning = data.chaosState.isRunning;
  const wasRunning = DOM.chaosStats.style.display === 'block';  // Track previous state
  const isCalibrating = data.state === SystemState.CALIBRATING;
  const isPaused = data.state === SystemState.PAUSED;
  const isRunningOrPaused = isRunning || isPaused;
  const isError = data.state === SystemState.ERROR;
  
  // Show/hide stats panel
  DOM.chaosStats.style.display = isRunning ? 'block' : 'none';
  
  // CRITICAL FIX: Reset patterns flag when chaos stops
  // This allows patterns to be re-synced from backend after each run
  if (wasRunning && !isRunning) {
    console.log('🔄 Chaos stopped - resetting patterns flag for next sync');
    AppState.flags.patternsInitialized = false;
  }
  
  // Update button states (disable if not calibrated or calibrating)
  const canStart = canStartOperation() && !isRunning;
  setButtonState(DOM.btnStartChaos, canStart);
  
  // Pause button
  const btnPauseChaos = document.getElementById('btnPauseChaos');
  if (btnPauseChaos) {
    btnPauseChaos.disabled = !isRunningOrPaused;
    if (isPaused) {
      btnPauseChaos.innerHTML = '▶ Reprendre';
    } else {
      btnPauseChaos.innerHTML = '⏸ Pause';
    }
  }
  
  // Stop button - ALSO enabled in ERROR state for recovery
  DOM.btnStopChaos.disabled = !(isRunningOrPaused || isError);
  
  // Allow live config changes while running (except seed)
  // Config inputs remain enabled for real-time adjustments
  document.getElementById('chaosCenterPos').disabled = false;
  document.getElementById('chaosAmplitude').disabled = false;
  document.getElementById('chaosMaxSpeed').disabled = false;
  document.getElementById('chaosCraziness').disabled = false;
  document.getElementById('chaosDuration').disabled = false;
  document.getElementById('chaosSeed').disabled = isRunning;  // Only disable seed while running
  
  // Enable/disable pattern checkboxes and preset buttons based on running state
  // Checkboxes: ALWAYS disabled while running, ALWAYS enabled when stopped
  document.getElementById('patternZigzag').disabled = isRunning || isCalibrating;
  document.getElementById('patternSweep').disabled = isRunning || isCalibrating;
  document.getElementById('patternPulse').disabled = isRunning || isCalibrating;
  document.getElementById('patternDrift').disabled = isRunning || isCalibrating;
  document.getElementById('patternBurst').disabled = isRunning || isCalibrating;
  document.getElementById('patternWave').disabled = isRunning || isCalibrating;
  document.getElementById('patternPendulum').disabled = isRunning || isCalibrating;
  document.getElementById('patternSpiral').disabled = isRunning || isCalibrating;
  document.getElementById('btnEnableAllPatterns').disabled = isRunning || isCalibrating;
  document.getElementById('btnEnableSoftPatterns').disabled = isRunning || isCalibrating;
  document.getElementById('btnEnableDynamicPatterns').disabled = isRunning || isCalibrating;
  
  // Restore pattern states from backend ONLY on first load (not on every status update)
  // This prevents user's checkbox changes from being overwritten during runtime config changes
  if (!AppState.flags.patternsInitialized && !isRunning && data.chaos && data.chaos.patternsEnabled) {
    document.getElementById('patternZigzag').checked = data.chaos.patternsEnabled[0];
    document.getElementById('patternSweep').checked = data.chaos.patternsEnabled[1];
    document.getElementById('patternPulse').checked = data.chaos.patternsEnabled[2];
    document.getElementById('patternDrift').checked = data.chaos.patternsEnabled[3];
    document.getElementById('patternBurst').checked = data.chaos.patternsEnabled[4];
    document.getElementById('patternWave').checked = data.chaos.patternsEnabled[5];
    document.getElementById('patternPendulum').checked = data.chaos.patternsEnabled[6];
    document.getElementById('patternSpiral').checked = data.chaos.patternsEnabled[7];
    AppState.flags.patternsInitialized = true;  // Mark as initialized
  }
  
  if (isRunning) {
    // Update stats
    document.getElementById('statPattern').textContent = data.chaosState.patternName;
    document.getElementById('statPosition').textContent = data.positionMM.toFixed(2) + ' mm';
    
    const range = data.chaosState.maxReachedMM - data.chaosState.minReachedMM;
    document.getElementById('statRange').textContent = 
      data.chaosState.minReachedMM.toFixed(1) + ' - ' + 
      data.chaosState.maxReachedMM.toFixed(1) + ' mm (' + 
      range.toFixed(1) + ' mm)';
    
    document.getElementById('statCount').textContent = data.chaosState.patternsExecuted;
    
    // Update timer
    if (data.chaos.durationSeconds > 0 && data.chaosState.elapsedSeconds !== undefined) {
      document.getElementById('statTimer').style.display = 'block';
      document.getElementById('statElapsed').textContent = 
        data.chaosState.elapsedSeconds + ' / ' + data.chaos.durationSeconds;
    } else if (data.chaosState.elapsedSeconds !== undefined) {
      document.getElementById('statTimer').style.display = 'block';
      document.getElementById('statElapsed').textContent = data.chaosState.elapsedSeconds;
    } else {
      document.getElementById('statTimer').style.display = 'none';
    }
  }
  
  // Update preset buttons visual state
  updateChaosPresets();
}

/**
 * Collect chaos parameters from form and start chaos mode
 */
function startChaos() {
  if (!validateChaosLimits()) {
    showNotification('Limites invalides: la zone chaos dépasse les limites calibrées', 'error');
    return;
  }
  
  const centerPos = parseFloat(document.getElementById('chaosCenterPos').value);
  const amplitude = parseFloat(document.getElementById('chaosAmplitude').value);
  const maxSpeed = parseFloat(document.getElementById('chaosMaxSpeed').value);
  const craziness = parseFloat(document.getElementById('chaosCraziness').value);
  const duration = parseInt(document.getElementById('chaosDuration').value);
  const seed = parseInt(document.getElementById('chaosSeed').value);
  
  // Collect pattern selections (11 patterns)
  const patternsEnabled = [
    document.getElementById('patternZigzag').checked,
    document.getElementById('patternSweep').checked,
    document.getElementById('patternPulse').checked,
    document.getElementById('patternDrift').checked,
    document.getElementById('patternBurst').checked,
    document.getElementById('patternWave').checked,
    document.getElementById('patternPendulum').checked,
    document.getElementById('patternSpiral').checked,
    document.getElementById('patternCalm').checked,
    document.getElementById('patternBruteForce').checked,
    document.getElementById('patternLiberator').checked
  ];
  
  // Validate at least one pattern selected
  if (!patternsEnabled.some(p => p)) {
    showNotification('⚠️ Au moins un pattern doit être activé', 'error');
    return;
  }
  
  sendCommand(WS_CMD.START_CHAOS, {
    centerPositionMM: centerPos,
    amplitudeMM: amplitude,
    maxSpeedLevel: maxSpeed,
    crazinessPercent: craziness,
    durationSeconds: duration,
    seed: seed,
    patternsEnabled: patternsEnabled
  });
}

/**
 * Stop chaos mode (with optional return to start modal)
 */
function stopChaos() {
  // Only show modal if motor has moved (currentStep > 0)
  if (currentPositionMM > 0.5) {
    showStopModal();
  } else {
    // Direct stop if at position 0
    sendCommand(WS_CMD.STOP_CHAOS, {});
  }
}

/**
 * Pause/resume chaos mode
 */
function pauseChaos() {
  sendCommand(WS_CMD.PAUSE);
}

/**
 * Enable all patterns
 */
function enableAllPatterns() {
  CHAOS_PATTERNS.forEach(id => {
    document.getElementById(id).checked = true;
  });
  updatePatternToggleButton();
}

/**
 * Disable all patterns
 */
function disableAllPatterns() {
  CHAOS_PATTERNS.forEach(id => {
    document.getElementById(id).checked = false;
  });
  updatePatternToggleButton();
}

/**
 * Toggle all patterns (if all checked → uncheck all, otherwise check all)
 */
function toggleAllPatterns() {
  const allChecked = CHAOS_PATTERNS.every(id => document.getElementById(id).checked);
  const newState = !allChecked;
  CHAOS_PATTERNS.forEach(id => {
    document.getElementById(id).checked = newState;
  });
  updatePatternToggleButton();
}

/**
 * Enable soft patterns only (WAVE, PENDULUM, SPIRAL, BREATHING/CALM)
 */
function enableSoftPatterns() {
  document.getElementById('patternZigzag').checked = false;
  document.getElementById('patternSweep').checked = false;
  document.getElementById('patternPulse').checked = false;
  document.getElementById('patternDrift').checked = false;
  document.getElementById('patternBurst').checked = false;
  document.getElementById('patternWave').checked = true;
  document.getElementById('patternPendulum').checked = true;
  document.getElementById('patternSpiral').checked = true;
  document.getElementById('patternCalm').checked = true;
  document.getElementById('patternBruteForce').checked = false;
  document.getElementById('patternLiberator').checked = false;
  updatePatternToggleButton();
}

/**
 * Enable dynamic patterns only (ZIGZAG, SWEEP, PULSE, DRIFT, BURST, BRUTE_FORCE, LIBERATOR)
 */
function enableDynamicPatterns() {
  document.getElementById('patternZigzag').checked = true;
  document.getElementById('patternSweep').checked = true;
  document.getElementById('patternPulse').checked = true;
  document.getElementById('patternDrift').checked = true;
  document.getElementById('patternBurst').checked = true;
  document.getElementById('patternWave').checked = false;
  document.getElementById('patternPendulum').checked = false;
  document.getElementById('patternSpiral').checked = false;
  document.getElementById('patternCalm').checked = false;
  document.getElementById('patternBruteForce').checked = true;
  document.getElementById('patternLiberator').checked = true;
  updatePatternToggleButton();
}

// ============================================================================
// CHAOS MODE - INITIALIZATION
// ============================================================================

/**
 * Initialize all chaos mode event listeners
 * Called from main.js on window load
 */
function initChaosListeners() {
  // Start chaos button
  document.getElementById('btnStartChaos').addEventListener('click', startChaos);
  
  // Stop chaos button
  document.getElementById('btnStopChaos').addEventListener('click', stopChaos);
  
  // Pause chaos button
  document.getElementById('btnPauseChaos').addEventListener('click', pauseChaos);
  
  // Chaos preset buttons - Center
  document.querySelectorAll('[data-chaos-center]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        document.getElementById('chaosCenterPos').value = this.dataset.chaosCenter;
        sendChaosConfig();
        updateChaosPresets();
      }
    });
  });
  
  // Chaos preset buttons - Amplitude
  document.querySelectorAll('[data-chaos-amplitude]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        document.getElementById('chaosAmplitude').value = this.dataset.chaosAmplitude;
        sendChaosConfig();
        updateChaosPresets();
      }
    });
  });
  
  // Chaos preset buttons - Speed
  document.querySelectorAll('[data-chaos-speed]').forEach(btn => {
    btn.addEventListener('click', function() {
      document.getElementById('chaosMaxSpeed').value = this.dataset.chaosSpeed;
      sendChaosConfig();
    });
  });
  
  // Chaos preset buttons - Duration
  document.querySelectorAll('[data-chaos-duration]').forEach(btn => {
    btn.addEventListener('click', function() {
      document.getElementById('chaosDuration').value = this.dataset.chaosDuration;
      sendChaosConfig();
    });
  });
  
  // Chaos preset buttons - Craziness
  document.querySelectorAll('[data-chaos-craziness]').forEach(btn => {
    btn.addEventListener('click', function() {
      document.getElementById('chaosCraziness').value = this.dataset.chaosCraziness;
      document.getElementById('crazinessValue').textContent = this.dataset.chaosCraziness;
      sendChaosConfig();
    });
  });
  
  // Craziness slider - update value display
  document.getElementById('chaosCraziness').addEventListener('input', function() {
    document.getElementById('crazinessValue').textContent = this.value;
  });
  
  // Send config on blur (when user finishes editing)
  document.getElementById('chaosCenterPos').addEventListener('blur', function() {
    sendChaosConfig();
    updateChaosPresets();
  });
  document.getElementById('chaosAmplitude').addEventListener('blur', function() {
    sendChaosConfig();
    updateChaosPresets();
  });
  document.getElementById('chaosMaxSpeed').addEventListener('blur', function() {
    sendChaosConfig();
  });
  document.getElementById('chaosCraziness').addEventListener('blur', function() {
    sendChaosConfig();
  });
  document.getElementById('chaosDuration').addEventListener('blur', function() {
    sendChaosConfig();
  });
  document.getElementById('chaosSeed').addEventListener('blur', function() {
    sendChaosConfig();
  });
  
  // Pattern preset buttons
  document.getElementById('btnEnableAllPatterns').addEventListener('click', toggleAllPatterns);
  document.getElementById('btnEnableSoftPatterns').addEventListener('click', enableSoftPatterns);
  document.getElementById('btnEnableDynamicPatterns').addEventListener('click', enableDynamicPatterns);
  
  // Individual pattern checkboxes - update toggle button text
  CHAOS_PATTERNS.forEach(id => {
    document.getElementById(id).addEventListener('change', updatePatternToggleButton);
  });
  
  console.log('🎲 ChaosController initialized');
}

// ============================================================================
// EXPOSED API
// ============================================================================
// Functions available globally:
// - toggleChaosHelp()
// - sendChaosConfig()
// - validateChaosLimits()
// - updateChaosPresets()
// - updatePatternToggleButton()
// - updateChaosUI(data)
// - startChaos(), stopChaos(), pauseChaos()
// - enableAllPatterns(), disableAllPatterns(), toggleAllPatterns()
// - enableSoftPatterns(), enableDynamicPatterns()
// - initChaosListeners()
