// ============================================================================
// CHAOS CONTROLLER MODULE
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

/**
 * Get current checked state of all chaos pattern checkboxes
 * @returns {boolean[]} Array of 11 booleans matching CHAOS_PATTERNS order
 */
function getPatternStates() {
  return CHAOS_PATTERNS.map(id => document.getElementById(id).checked);
}

/**
 * Set checked state of all chaos pattern checkboxes from an array or a Set of enabled IDs
 * @param {boolean[]|Set<string>} states - Array of booleans (by index) or Set of pattern IDs to enable
 */
function setPatternStates(states) {
  if (states instanceof Set) {
    CHAOS_PATTERNS.forEach(id => { document.getElementById(id).checked = states.has(id); });
  } else if (Array.isArray(states) && states.length >= CHAOS_PATTERNS.length) {
    CHAOS_PATTERNS.forEach((id, i) => { document.getElementById(id).checked = states[i]; });
  }
  updatePatternToggleButton();
}

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
 * Toggle chaos advanced options section (patterns, seed)
 */
function toggleChaosAdvancedSection() {
  const section = document.getElementById('chaosAdvancedSection');
  section.classList.toggle('collapsed');
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
    patternsEnabled: getPatternStates()
  };
  
  // Build config from form values
  const duration = Number.parseInt(formValues.duration);
  const config = {
    centerPositionMM: Number.parseFloat(formValues.centerPos) || 0,
    amplitudeMM: Number.parseFloat(formValues.amplitude) || 0,
    maxSpeedLevel: Number.parseFloat(formValues.maxSpeed) || 10,
    crazinessPercent: Number.parseInt(formValues.craziness) || 50,
    durationSeconds: Number.isNaN(duration) ? 30 : duration,  // 0 = infinite, don't default to 30
    seed: Number.parseInt(formValues.seed) || 0,
    patternsEnabled: formValues.patternsEnabled
  };
  
  sendCommand(WS_CMD.SET_CHAOS_CONFIG, config);
}

/**
 * Validate chaos limits
 * @returns {boolean} true if limits are valid
 */
function validateChaosLimits() {
  const centerPos = Number.parseFloat(document.getElementById('chaosCenterPos').value);
  const amplitude = Number.parseFloat(document.getElementById('chaosAmplitude').value);
  const totalDistMM = AppState.pursuit.totalDistanceMM || 0;
  
  // Validate limits
  return centerPos - amplitude >= 0 && centerPos + amplitude <= totalDistMM;
}

/**
 * Update visual state of chaos preset buttons
 */
function updateChaosPresets() {
  // Shared center/amplitude/linked preset validation
  updateCenterAmplitudePresets({
    centerInputId: 'chaosCenterPos',
    amplitudeInputId: 'chaosAmplitude',
    linkedCheckboxId: 'chaosAmplitudeLinked',
    dataPrefix: 'chaos',
    i18nPrefix: 'chaos'
  });
}

/**
 * Update pattern toggle button text (All/None)
 */
function updatePatternToggleButton() {
  const checkedCount = CHAOS_PATTERNS.filter(id => document.getElementById(id).checked).length;
  const btn = document.getElementById('btnEnableAllPatterns');
  const span = btn.querySelector('span');
  
  if (checkedCount === CHAOS_PATTERNS.length) {
    // All checked → show "None" (next action will uncheck all)
    btn.firstChild.textContent = '❌ ';
    if (span) span.textContent = t('common.none');
  } else {
    // Some or none checked → show "All" (next action will check all)
    btn.firstChild.textContent = '✅ ';
    if (span) span.textContent = t('common.all');
  }
}

/**
 * Update chaos UI with live data from WebSocket status
 * @param {object} data - Status data from backend
 */
function updateChaosUI(data) {
  // chaosState may be absent when chaos is stopped - handle both cases
  const chaosState = data.chaosState || {};
  
  const isRunning = chaosState.isRunning || false;
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
    console.debug('🔄 Chaos stopped - resetting patterns flag for next sync');
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
      btnPauseChaos.innerHTML = '▶ ' + t('common.resume');
    } else {
      btnPauseChaos.innerHTML = '⏸ ' + t('common.pause');
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
  const patternDisabled = isRunning || isCalibrating;
  CHAOS_PATTERNS.forEach(patternId => {
    const el = document.getElementById(patternId);
    if (el) el.disabled = patternDisabled;
  });
  document.getElementById('btnEnableAllPatterns').disabled = patternDisabled;
  document.getElementById('btnEnableSoftPatterns').disabled = patternDisabled;
  document.getElementById('btnEnableDynamicPatterns').disabled = patternDisabled;
  
  // Restore pattern states from backend ONLY on first load (not on every status update)
  // This prevents user's checkbox changes from being overwritten during runtime config changes
  if (!AppState.flags.patternsInitialized && !isRunning && data.chaos?.patternsEnabled) {
    CHAOS_PATTERNS.forEach((patternId, index) => {
      const el = document.getElementById(patternId);
      if (el && data.chaos.patternsEnabled[index] !== undefined) {
        el.checked = data.chaos.patternsEnabled[index];
      }
    });
    AppState.flags.patternsInitialized = true;  // Mark as initialized
  }
  
  if (isRunning && data.chaosState) {
    // Update stats
    document.getElementById('statPattern').textContent = chaosState.patternName || '-';
    document.getElementById('statPosition').textContent = data.positionMM.toFixed(2) + ' mm';
    
    const range = (chaosState.maxReachedMM || 0) - (chaosState.minReachedMM || 0);
    document.getElementById('statRange').textContent = 
      (chaosState.minReachedMM || 0).toFixed(1) + ' - ' + 
      (chaosState.maxReachedMM || 0).toFixed(1) + ' mm (' + 
      range.toFixed(1) + ' mm)';
    
    document.getElementById('statCount').textContent = chaosState.patternsExecuted || 0;
    
    // Update timer
    if (data.chaos && data.chaos.durationSeconds > 0 && chaosState.elapsedSeconds !== undefined) {
      document.getElementById('statTimer').style.display = 'block';
      document.getElementById('statElapsed').textContent = 
        chaosState.elapsedSeconds + ' / ' + data.chaos.durationSeconds;
    } else if (chaosState.elapsedSeconds === undefined) {
      document.getElementById('statTimer').style.display = 'none';
    } else {
      document.getElementById('statTimer').style.display = 'block';
      document.getElementById('statElapsed').textContent = chaosState.elapsedSeconds;
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
    showNotification(t('chaos.invalidLimits'), 'error');
    return;
  }
  
  const centerPos = Number.parseFloat(document.getElementById('chaosCenterPos').value);
  const amplitude = Number.parseFloat(document.getElementById('chaosAmplitude').value);
  const maxSpeed = Number.parseFloat(document.getElementById('chaosMaxSpeed').value);
  const craziness = Number.parseFloat(document.getElementById('chaosCraziness').value);
  const duration = Number.parseInt(document.getElementById('chaosDuration').value);
  const seed = Number.parseInt(document.getElementById('chaosSeed').value);
  
  // Collect pattern selections using shared helper
  const patternsEnabled = getPatternStates();
  
  // Validate at least one pattern selected
  if (!patternsEnabled.some(Boolean)) {
    showNotification('⚠️ ' + t('chaos.atLeastOnePattern'), 'error');
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
const SOFT_PATTERNS = new Set(['patternWave', 'patternPendulum', 'patternSpiral', 'patternCalm']);
function enableSoftPatterns() {
  setPatternStates(SOFT_PATTERNS);
}

/**
 * Enable dynamic patterns only (ZIGZAG, SWEEP, PULSE, DRIFT, BURST, BRUTE_FORCE, LIBERATOR)
 */
const DYNAMIC_PATTERNS = new Set(['patternZigzag', 'patternSweep', 'patternPulse', 'patternDrift', 'patternBurst', 'patternBruteForce', 'patternLiberator']);
function enableDynamicPatterns() {
  setPatternStates(DYNAMIC_PATTERNS);
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
  
  // 🆕 Relative preset buttons - Center
  document.querySelectorAll('[data-chaos-center-rel]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const relValue = Number.parseInt(this.dataset.chaosCenterRel);
        const currentCenter = Number.parseFloat(document.getElementById('chaosCenterPos').value) || 0;
        const newCenter = Math.max(0, currentCenter + relValue);
        document.getElementById('chaosCenterPos').value = newCenter;
        
        // If linked, also update amplitude
        if (document.getElementById('chaosAmplitudeLinked')?.checked) {
          document.getElementById('chaosAmplitude').value = newCenter;
        }
        
        sendChaosConfig();
        updateChaosPresets();
      }
    });
  });
  
  // 🆕 Relative preset buttons - Amplitude
  document.querySelectorAll('[data-chaos-amplitude-rel]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const relValue = Number.parseInt(this.dataset.chaosAmplitudeRel);
        const currentAmplitude = Number.parseFloat(document.getElementById('chaosAmplitude').value) || 0;
        const newAmplitude = Math.max(1, currentAmplitude + relValue);
        document.getElementById('chaosAmplitude').value = newAmplitude;
        sendChaosConfig();
        updateChaosPresets();
      }
    });
  });
  
  // 🆕 Amplitude linked checkbox (amplitude = center)
  const chaosAmplitudeLinked = document.getElementById('chaosAmplitudeLinked');
  if (chaosAmplitudeLinked) {
    chaosAmplitudeLinked.addEventListener('change', function() {
      if (this.checked) {
        // Link: set amplitude = center
        const center = Number.parseFloat(document.getElementById('chaosCenterPos').value) || 0;
        document.getElementById('chaosAmplitude').value = center;
        sendChaosConfig();
      }
      updateChaosPresets();
    });
  }
  
  // 🆕 When center changes and linked, update amplitude too
  const chaosCenterInput = document.getElementById('chaosCenterPos');
  if (chaosCenterInput) {
    chaosCenterInput.addEventListener('input', function() {
      if (document.getElementById('chaosAmplitudeLinked')?.checked) {
        document.getElementById('chaosAmplitude').value = this.value;
      }
    });
  }
  
  console.debug('🎲 ChaosController initialized');
}
