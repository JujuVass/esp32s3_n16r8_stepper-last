/**
 * SimpleController.js - Simple Mode Control Module
 * 
 * Handles all Simple mode functionality:
 * - Start/Pause/Stop controls
 * - Position and distance inputs
 * - Speed controls (unified and separate)
 * - Cycle pause configuration
 * - Preset buttons
 * - Deceleration zone control
 * 
 * Dependencies: 
 * - DOM, AppState, WS_CMD, sendCommand, showNotification, showStopModal, currentPositionMM
 * - SimpleUtils.js (calculateSlowdownFactorPure, drawZoneEffectPreviewPure, getCyclePauseSection)
 */

// ============================================================================
// SIMPLE MODE - Start/Stop Actions
// ============================================================================

/**
 * Start simple mode movement
 */
function startSimpleMode() {
  const distance = Number.parseFloat(document.getElementById('distance').value);
  const isSeparateMode = document.getElementById('speedModeSeparate')?.checked || false;
  
  let speedForward, speedBackward;
  if (isSeparateMode) {
    speedForward = Number.parseFloat(document.getElementById('speedForward').value);
    speedBackward = Number.parseFloat(document.getElementById('speedBackward').value);
  } else {
    const unifiedSpeed = Number.parseFloat(document.getElementById('speedUnified').value);
    speedForward = unifiedSpeed;
    speedBackward = unifiedSpeed;
  }
  
  sendCommand(WS_CMD.START, {distance: distance, speed: speedForward});
  // Set backward speed separately after start
  setTimeout(() => {
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speedBackward});
  }, 100);
}

/**
 * Pause simple mode movement
 */
function pauseSimpleMode() {
  sendCommand(WS_CMD.PAUSE);
}

/**
 * Stop simple mode movement (with modal if needed)
 */
function stopSimpleMode() {
  // Only show modal if motor has moved (currentStep > 0)
  if (currentPositionMM > 0.5) {
    showStopModal();
  } else {
    // Direct stop if at position 0
    sendCommand(WS_CMD.STOP);
  }
}

/**
 * Update visual state of simple mode relative preset buttons
 * 🆕 Added for relative presets feature
 */
function updateSimpleRelativePresets() {
  const maxStart = Number.parseFloat(document.getElementById('startPosition').max) || 999;
  const maxDist = Number.parseFloat(document.getElementById('distance').max) || 999;
  const currentStart = Number.parseFloat(document.getElementById('startPosition').value) || 0;
  const currentDist = Number.parseFloat(document.getElementById('distance').value) || 0;
  
  // Validate relative start position presets
  document.querySelectorAll('[data-start-rel]').forEach(btn => {
    const relValue = Number.parseInt(btn.dataset.startRel);
    const newStart = currentStart + relValue;
    const isValid = newStart >= 0 && newStart <= maxStart;
    
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.5';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
  
  // Validate relative distance presets
  document.querySelectorAll('[data-distance-rel]').forEach(btn => {
    const relValue = Number.parseInt(btn.dataset.distanceRel);
    const newDist = currentDist + relValue;
    const isValid = newDist >= 1 && newDist <= maxDist;
    
    btn.disabled = !isValid;
    btn.style.opacity = isValid ? '1' : '0.5';
    btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
  });
}

// ============================================================================
// SIMPLE MODE - UI UPDATE (called from main.js updateUI)
// ============================================================================

/**
 * Update Simple mode UI from WebSocket status data
 * @param {Object} data - Status data from WebSocket
 */
function updateSimpleUI(data) {
  if (!data) return;
  
  const isRunning = data.state === SystemState.RUNNING;
  const isPaused = data.state === SystemState.PAUSED;
  const isRunningOrPaused = isRunning || isPaused;
  const isError = data.state === SystemState.ERROR;
  
  // ===== PAUSE/STOP BUTTONS =====
  const btnPause = document.getElementById('btnPause');
  if (btnPause) {
    btnPause.disabled = !isRunningOrPaused;
    if (isPaused) {
      btnPause.innerHTML = '▶ ' + t('common.resume');
    } else {
      btnPause.innerHTML = '⏸ ' + t('common.pause');
    }
  }
  
  const btnStop = document.getElementById('btnStop');
  if (btnStop) {
    btnStop.disabled = !(isRunningOrPaused || isError);
  }
  
  // ===== CYCLE PAUSE DISPLAY =====
  if (data.motion?.cyclePause) {
    syncCyclePauseUI(data.motion.cyclePause, '', getCyclePauseSection, 'simple');
  }
}

// ============================================================================
// SIMPLE MODE - Speed Mode Toggle
// ============================================================================

/**
 * Handle speed mode toggle between unified and separate
 */
function handleSpeedModeChange() {
  const isSeparate = document.getElementById('speedModeSeparate').checked;
  const unifiedGroup = document.getElementById('speedUnifiedGroup');
  const separateGroup = document.getElementById('speedSeparateGroup');
  
  if (isSeparate) {
    // UNIFIED → SEPARATE: Copy unified value to BOTH forward AND backward
    unifiedGroup.style.display = 'none';
    separateGroup.style.display = 'block';
    
    const unifiedSpeed = Number.parseFloat(document.getElementById('speedUnified').value);
    document.getElementById('speedForward').value = unifiedSpeed;
    document.getElementById('speedBackward').value = unifiedSpeed;
    
    // Send both commands to ESP32 to ensure sync
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: unifiedSpeed});
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: unifiedSpeed});
    
    // Update preset button highlighting
    document.querySelectorAll('[data-speed-forward]').forEach(btn => {
      if (Number.parseFloat(btn.dataset.speedForward) === unifiedSpeed) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    document.querySelectorAll('[data-speed-backward]').forEach(btn => {
      if (Number.parseFloat(btn.dataset.speedBackward) === unifiedSpeed) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    
    console.debug('Switched to SEPARATE mode: both speeds set to ' + unifiedSpeed);
    
  } else {
    // SEPARATE → UNIFIED: Use forward speed value for both
    unifiedGroup.style.display = 'flex';
    separateGroup.style.display = 'none';
    
    const forwardSpeed = Number.parseFloat(document.getElementById('speedForward').value);
    
    // Use forward speed as the unified value
    document.getElementById('speedUnified').value = forwardSpeed;
    
    // Also update backward to match forward
    document.getElementById('speedBackward').value = forwardSpeed;
    
    // Apply forward speed to BOTH directions immediately
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: forwardSpeed});
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: forwardSpeed});
    
    // Update preset button highlighting
    document.querySelectorAll('[data-speed-unified]').forEach(btn => {
      if (Number.parseFloat(btn.dataset.speedUnified) === forwardSpeed) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    
    console.debug('Switched to UNIFIED mode: using forward speed ' + forwardSpeed + ' for both directions');
  }
}

// ============================================================================
// SIMPLE MODE - Initialize Listeners
// ============================================================================

/**
 * Initialize all Simple mode event listeners
 * Called once on page load
 */
function initSimpleListeners() {
  console.debug('🔧 Initializing Simple mode listeners...');
  
  // ===== START/PAUSE/STOP BUTTONS =====
  document.getElementById('btnStart').addEventListener('click', startSimpleMode);
  document.getElementById('btnPause').addEventListener('click', pauseSimpleMode);
  document.getElementById('btnStop').addEventListener('click', stopSimpleMode);
  
  // ===== EDITABLE INPUTS (using setupEditableInput from utils.js) =====
  setupEditableInput('startPosition', 'startPosition', function(value) {
    sendCommand(WS_CMD.SET_START_POSITION, {startPosition: Number.parseFloat(value)});
    updateSimpleRelativePresets();
  });
  
  setupEditableInput('distance', 'distance', function(value) {
    sendCommand(WS_CMD.SET_DISTANCE, {distance: Number.parseFloat(value)});
    updateSimpleRelativePresets();
  });
  
  setupEditableInput('speedUnified', 'speedUnified', function(value) {
    const speed = Number.parseFloat(value);
    document.getElementById('speedForward').value = speed;
    document.getElementById('speedBackward').value = speed;
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
  });
  
  setupEditableInput('speedForward', 'speedForward', function(value) {
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: Number.parseFloat(value)});
  });
  
  setupEditableInput('speedBackward', 'speedBackward', function(value) {
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: Number.parseFloat(value)});
  });
  
  // Note: Cycle Pause listeners are handled by createCyclePauseHandlers() in initCyclePauseHandlers()
  
  // ===== START POSITION PRESETS =====
  document.querySelectorAll('[data-start]').forEach(btn => {
    btn.addEventListener('click', function() {
      const startPos = Number.parseFloat(this.dataset.start);
      const maxStart = Number.parseFloat(document.getElementById('startPosition').max);
      
      if (startPos <= maxStart) {
        document.getElementById('startPosition').value = startPos;
        sendCommand(WS_CMD.SET_START_POSITION, {startPosition: startPos});
        
        document.querySelectorAll('[data-start]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        updateSimpleRelativePresets();
      }
    });
  });
  
  // 🆕 ===== START POSITION RELATIVE PRESETS =====
  document.querySelectorAll('[data-start-rel]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const relValue = Number.parseInt(this.dataset.startRel);
        const currentStart = Number.parseFloat(document.getElementById('startPosition').value) || 0;
        const maxStart = Number.parseFloat(document.getElementById('startPosition').max) || 999;
        const newStart = Math.max(0, Math.min(maxStart, currentStart + relValue));
        
        document.getElementById('startPosition').value = newStart;
        sendCommand(WS_CMD.SET_START_POSITION, {startPosition: newStart});
        
        document.querySelectorAll('[data-start]').forEach(b => b.classList.remove('active'));
        updateSimpleRelativePresets();
      }
    });
  });
  
  // ===== DISTANCE PRESETS =====
  document.querySelectorAll('[data-distance]').forEach(btn => {
    btn.addEventListener('click', function() {
      const distance = Number.parseFloat(this.dataset.distance);
      const maxDist = Number.parseFloat(document.getElementById('distance').max);
      
      if (distance <= maxDist) {
        document.getElementById('distance').value = distance;
        sendCommand(WS_CMD.SET_DISTANCE, {distance: distance});
        
        document.querySelectorAll('[data-distance]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        updateSimpleRelativePresets();
      }
    });
  });
  
  // 🆕 ===== DISTANCE RELATIVE PRESETS =====
  document.querySelectorAll('[data-distance-rel]').forEach(btn => {
    btn.addEventListener('click', function() {
      if (!this.disabled) {
        const relValue = Number.parseInt(this.dataset.distanceRel);
        const currentDist = Number.parseFloat(document.getElementById('distance').value) || 0;
        const maxDist = Number.parseFloat(document.getElementById('distance').max) || 999;
        const newDist = Math.max(1, Math.min(maxDist, currentDist + relValue));
        
        document.getElementById('distance').value = newDist;
        sendCommand(WS_CMD.SET_DISTANCE, {distance: newDist});
        
        document.querySelectorAll('[data-distance]').forEach(b => b.classList.remove('active'));
        updateSimpleRelativePresets();
      }
    });
  });
  
  // ===== UNIFIED SPEED PRESETS =====
  document.querySelectorAll('[data-speed-unified]').forEach(btn => {
    btn.addEventListener('click', function() {
      const speed = Number.parseFloat(this.dataset.speedUnified);
      
      // Update visible unified field
      document.getElementById('speedUnified').value = speed;
      
      // Also update hidden separate fields for consistency when switching modes
      document.getElementById('speedForward').value = speed;
      document.getElementById('speedBackward').value = speed;
      
      // Send both commands to ESP32
      sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
      sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
      
      // Update preset button highlighting
      document.querySelectorAll('[data-speed-unified]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
    });
  });
  
  // ===== FORWARD SPEED PRESETS =====
  document.querySelectorAll('[data-speed-forward]').forEach(btn => {
    btn.addEventListener('click', function() {
      const speed = Number.parseFloat(this.dataset.speedForward);
      document.getElementById('speedForward').value = speed;
      sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
      
      document.querySelectorAll('[data-speed-forward]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
    });
  });
  
  // ===== BACKWARD SPEED PRESETS =====
  document.querySelectorAll('[data-speed-backward]').forEach(btn => {
    btn.addEventListener('click', function() {
      const speed = Number.parseFloat(this.dataset.speedBackward);
      document.getElementById('speedBackward').value = speed;
      sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
      
      document.querySelectorAll('[data-speed-backward]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
    });
  });
  
  // ===== SPEED MODE TOGGLE (UNIFIED/SEPARATE) =====
  document.querySelectorAll('input[name="speedMode"]').forEach(radio => {
    radio.addEventListener('change', handleSpeedModeChange);
  });
  
  // ===== ZONE EFFECTS EVENT LISTENERS =====
  initZoneEffectListeners();
  
  console.debug('✅ Simple mode listeners initialized');
}

// ============================================================================
// ZONE EFFECTS - Simple Mode Only
// ============================================================================

/**
 * Toggle zone effects section collapsed state
 */
function toggleZoneEffectSection() {
  const section = document.getElementById('zoneEffectSection');
  const headerText = document.getElementById('zoneEffectHeaderText');
  const isCollapsed = section.classList.contains('collapsed');
  
  section.classList.toggle('collapsed');
  
  if (isCollapsed) {
    // Expanding = activating
    updateZoneEffectHeaderText();
    sendZoneEffectConfig(true);  // true = initial open, don't track zone request
    drawZoneEffectPreview();
  } else {
    // Collapsing = deactivating
    headerText.textContent = t('simple.zoneEffectsDisabled');
    sendCommand(WS_CMD.SET_ZONE_EFFECT, { enabled: false });
  }
}

/**
 * Update the header text based on active effects
 */
function updateZoneEffectHeaderText() {
  const headerText = document.getElementById('zoneEffectHeaderText');
  if (!headerText) return;
  
  const speedEffect = Number.parseInt(document.getElementById('speedEffectType')?.value || 0);
  const randomTurnback = document.getElementById('randomTurnbackEnabled')?.checked;
  const endPause = document.getElementById('endPauseEnabled')?.checked;
  const mirrorOnReturn = document.getElementById('zoneEffectMirror')?.checked;
  
  const effects = [];
  if (speedEffect === 1) effects.push(t('simple.decel'));
  if (speedEffect === 2) effects.push(t('simple.accel'));
  if (randomTurnback) effects.push(t('simple.turnback'));
  if (endPause) effects.push(t('common.pause'));
  if (mirrorOnReturn) effects.push(t('simple.physicalPos'));
  
  if (effects.length === 0) {
    headerText.textContent = t('simple.zoneEffectsNoEffect');
  } else {
    headerText.textContent = `🎯 ${t('simple.zoneEffects')} - ${effects.join(' + ')}`;
  }
}

/**
 * Send zone effects configuration to ESP32
 * @param {boolean} isInitialOpen - If true, don't track zone request (avoids false popup on first open)
 */
function sendZoneEffectConfig(isInitialOpen = false) {
  const section = document.getElementById('zoneEffectSection');
  const isEnabled = !section.classList.contains('collapsed');
  
  const zoneMM = Number.parseFloat(document.getElementById('zoneEffectMM')?.value) || 50;
  
  // Parse values carefully - 0 is a valid value for speedEffect and speedCurve
  const speedEffectEl = document.getElementById('speedEffectType');
  const speedCurveEl = document.getElementById('speedCurveSelect');
  const speedIntensityEl = document.getElementById('speedIntensity');
  const turnbackChanceEl = document.getElementById('turnbackChance');
  
  const config = {
    enabled: isEnabled,
    enableStart: document.getElementById('zoneEffectStart')?.checked ?? true,
    enableEnd: document.getElementById('zoneEffectEnd')?.checked ?? true,
    mirrorOnReturn: document.getElementById('zoneEffectMirror')?.checked ?? false,
    zoneMM: zoneMM,
    // Speed effect - use explicit check for null/undefined, 0 is valid
    speedEffect: speedEffectEl ? Number.parseInt(speedEffectEl.value) : 1,
    speedCurve: speedCurveEl ? Number.parseInt(speedCurveEl.value) : 1,
    speedIntensity: speedIntensityEl ? Number.parseFloat(speedIntensityEl.value) : 75,
    // Random turnback
    randomTurnbackEnabled: document.getElementById('randomTurnbackEnabled')?.checked ?? false,
    turnbackChance: turnbackChanceEl ? Number.parseInt(turnbackChanceEl.value) : 30,
    // End pause
    endPauseEnabled: document.getElementById('endPauseEnabled')?.checked ?? false,
    endPauseIsRandom: document.getElementById('endPauseModeRandom')?.checked ?? false,
    endPauseDurationSec: Number.parseFloat(document.getElementById('endPauseDuration')?.value) || 1,
    endPauseMinSec: Number.parseFloat(document.getElementById('endPauseMin')?.value) || 0.5,
    endPauseMaxSec: Number.parseFloat(document.getElementById('endPauseMax')?.value) || 2
  };
  
  // Store requested zone value for comparison - but NOT on initial open
  // This avoids the false "zone adjusted" popup when first opening the section
  if (!isInitialOpen) {
    AppState.lastZoneEffectRequest = zoneMM;
  }
  
  // Send zone effect config to backend
  sendCommand(WS_CMD.SET_ZONE_EFFECT, config);
  
  // Update header text
  updateZoneEffectHeaderText();
}

// ── Zone Effect UI sub-helpers (reduce S3776 cognitive complexity) ──

/** Update start/end/mirror checkboxes (only if value actually changed to avoid flickering) */
function updateZoneCheckboxes(ze) {
  const startCb = document.getElementById('zoneEffectStart');
  const endCb = document.getElementById('zoneEffectEnd');
  if (startCb && ze.enableStart !== undefined && startCb.checked !== ze.enableStart) {
    startCb.checked = ze.enableStart;
  }
  if (endCb && ze.enableEnd !== undefined && endCb.checked !== ze.enableEnd) {
    endCb.checked = ze.enableEnd;
  }
  const mirrorCb = document.getElementById('zoneEffectMirror');
  if (mirrorCb && ze.mirrorOnReturn !== undefined && mirrorCb.checked !== ze.mirrorOnReturn) {
    mirrorCb.checked = ze.mirrorOnReturn;
  }
}

/** Update zone size input + show notification if ESP32 adapted the value */
function updateZoneSize(ze) {
  const zoneInput = document.getElementById('zoneEffectMM');
  if (!zoneInput || ze.zoneMM === undefined) return;

  const requestedZone = AppState.lastZoneEffectRequest;
  const receivedZone = ze.zoneMM;
  if (requestedZone !== undefined && Math.abs(requestedZone - receivedZone) > 0.1) {
    showNotification(`⚠️ ${t('simple.zoneAdjusted', {requested: requestedZone.toFixed(0), received: receivedZone.toFixed(0)})}`, 'warning', 4000);
    AppState.lastZoneEffectRequest = undefined;
  }
  zoneInput.value = receivedZone;
}

/** Update speed effect type / curve / intensity controls */
function updateZoneSpeedEffect(ze) {
  if (ze.speedEffect !== undefined) {
    const sel = document.getElementById('speedEffectType');
    if (sel) sel.value = ze.speedEffect.toString();
  }
  if (ze.speedCurve !== undefined) {
    const sel = document.getElementById('speedCurveSelect');
    if (sel) sel.value = ze.speedCurve.toString();
  }
  if (ze.speedIntensity !== undefined) {
    const inp = document.getElementById('speedIntensity');
    const val = document.getElementById('speedIntensityValue');
    if (inp) inp.value = ze.speedIntensity;
    if (val) val.textContent = ze.speedIntensity.toFixed(0) + '%';
  }
}

/** Update random turnback enabled + chance controls */
function updateZoneTurnback(ze) {
  if (ze.randomTurnbackEnabled !== undefined) {
    const cb = document.getElementById('randomTurnbackEnabled');
    if (cb) cb.checked = ze.randomTurnbackEnabled;
  }
  if (ze.turnbackChance !== undefined) {
    const inp = document.getElementById('turnbackChance');
    const val = document.getElementById('turnbackChanceValue');
    if (inp) inp.value = ze.turnbackChance;
    if (val) val.textContent = ze.turnbackChance + '%';
  }
}

/** Update end pause mode / duration / min / max controls */
function updateZoneEndPause(ze) {
  if (ze.endPauseEnabled !== undefined) {
    const cb = document.getElementById('endPauseEnabled');
    if (cb) cb.checked = ze.endPauseEnabled;
  }
  if (ze.endPauseIsRandom !== undefined) {
    updateEndPauseModeRadios(ze.endPauseIsRandom);
  }
  setInputIfDefined('endPauseDuration', ze.endPauseDurationSec);
  setInputIfDefined('endPauseMin', ze.endPauseMinSec);
  setInputIfDefined('endPauseMax', ze.endPauseMaxSec);
}

/** Set radio pair + toggle fixed/random controls for end pause mode */
function updateEndPauseModeRadios(isRandom) {
  const fixedRadio = document.getElementById('endPauseModeFixed');
  const randomRadio = document.getElementById('endPauseModeRandom');
  if (!fixedRadio || !randomRadio) return;
  fixedRadio.checked = !isRandom;
  randomRadio.checked = isRandom;
  const fixedCtrl = document.getElementById('endPauseFixedControls');
  const randomCtrl = document.getElementById('endPauseRandomControls');
  if (fixedCtrl && randomCtrl) {
    fixedCtrl.classList.toggle('hidden', isRandom);
    randomCtrl.classList.toggle('hidden', !isRandom);
  }
}

/** Set an input's value if the data field is defined */
function setInputIfDefined(elementId, value) {
  if (value === undefined) return;
  const inp = document.getElementById(elementId);
  if (inp) inp.value = value;
}

/**
 * Update zone effects UI from server data
 * @param {Object} zoneEffect - Zone effects data from backend
 */
function updateZoneEffectUI(zoneEffect) {
  if (!zoneEffect || AppState.editing.input === 'zoneEffect') return;
  
  const section = document.getElementById('zoneEffectSection');
  const headerText = document.getElementById('zoneEffectHeaderText');
  
  if (zoneEffect.enabled) {
    if (section) section.classList.remove('collapsed');
    
    updateZoneCheckboxes(zoneEffect);
    updateZoneSize(zoneEffect);
    updateZoneSpeedEffect(zoneEffect);
    updateZoneTurnback(zoneEffect);
    updateZoneEndPause(zoneEffect);
    updateZonePresetButtons(zoneEffect);
    refreshZonePreviewIfChanged(zoneEffect);
  } else if (section && headerText) {
    section.classList.add('collapsed');
    headerText.textContent = t('simple.zoneEffectsDisabled');
  }
}

/** Update zone size preset buttons active state */
function updateZonePresetButtons(zoneEffect) {
  document.querySelectorAll('[data-zone-mm]').forEach(btn => {
    const btnValue = Number.parseInt(btn.dataset.zoneMm);
    btn.classList.toggle('active', btnValue === Math.round(zoneEffect.zoneMM));
  });
}

/** Redraw zone preview only if config actually changed */
function refreshZonePreviewIfChanged(zoneEffect) {
  const zoneConfigKey = `${zoneEffect.enableStart}-${zoneEffect.enableEnd}-${zoneEffect.mirrorOnReturn}-${zoneEffect.speedEffect}-${zoneEffect.speedCurve}-${zoneEffect.speedIntensity}-${zoneEffect.zoneMM}-${zoneEffect.randomTurnbackEnabled}-${zoneEffect.endPauseEnabled}`;
  if (zoneConfigKey !== AppState.zoneEffect.lastConfigKey) {
    AppState.zoneEffect.lastConfigKey = zoneConfigKey;
    updateZoneEffectHeaderText();
    drawZoneEffectPreview();
  }
}

/**
 * Draw zone effects preview on canvas
 * Delegates to pure function from SimpleUtils.js
 */
function drawZoneEffectPreview() {
  const canvas = document.getElementById('zoneEffectPreview');
  if (!canvas) return;
  
  const config = getZoneEffectConfigFromDOM();
  drawZoneEffectPreviewPure(canvas, config);
}

/**
 * Get zone effect configuration from DOM elements
 */
function getZoneEffectConfigFromDOM() {
  const speedEffectEl = document.getElementById('speedEffectType');
  const speedCurveEl = document.getElementById('speedCurveSelect');
  const speedIntensityEl = document.getElementById('speedIntensity');
  const turnbackChanceEl = document.getElementById('turnbackChance');
  
  return {
    zoneMM: Number.parseFloat(document.getElementById('zoneEffectMM')?.value) || 50,
    enableStart: document.getElementById('zoneEffectStart')?.checked ?? true,
    enableEnd: document.getElementById('zoneEffectEnd')?.checked ?? true,
    mirrorOnReturn: document.getElementById('zoneEffectMirror')?.checked ?? false,
    speedEffect: speedEffectEl ? Number.parseInt(speedEffectEl.value) : 1,
    speedCurve: speedCurveEl ? Number.parseInt(speedCurveEl.value) : 1,
    speedIntensity: speedIntensityEl ? Number.parseFloat(speedIntensityEl.value) : 75,
    randomTurnbackEnabled: document.getElementById('randomTurnbackEnabled')?.checked ?? false,
    turnbackChance: turnbackChanceEl ? Number.parseInt(turnbackChanceEl.value) : 30,
    endPauseEnabled: document.getElementById('endPauseEnabled')?.checked ?? false
  };
}

/**
 * Initialize zone effects event listeners
 */
function initZoneEffectListeners() {
  // Zone size presets
  document.querySelectorAll('[data-zone-mm]').forEach(btn => {
    btn.addEventListener('click', function() {
      const value = this.dataset.zoneMm;
      const zoneInput = document.getElementById('zoneEffectMM');
      if (zoneInput) zoneInput.value = value;
      
      // Update active state
      document.querySelectorAll('[data-zone-mm]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
      
      sendZoneEffectConfig();
      drawZoneEffectPreview();
    });
  });
  
  // Zone start/end checkboxes
  ['zoneEffectStart', 'zoneEffectEnd', 'zoneEffectMirror'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.addEventListener('change', () => {
      sendZoneEffectConfig();
      drawZoneEffectPreview();
    });
  });
  
  // Zone size input
  const zoneInput = document.getElementById('zoneEffectMM');
  if (zoneInput) zoneInput.addEventListener('input', () => {
    sendZoneEffectConfig();
    drawZoneEffectPreview();
  });
  
  // Speed effect type
  const speedEffectType = document.getElementById('speedEffectType');
  if (speedEffectType) speedEffectType.addEventListener('change', () => {
    sendZoneEffectConfig();
    drawZoneEffectPreview();
    updateZoneEffectHeaderText();
  });
  
  // Speed curve
  const speedCurve = document.getElementById('speedCurveSelect');
  if (speedCurve) speedCurve.addEventListener('change', () => {
    sendZoneEffectConfig();
    drawZoneEffectPreview();
  });
  
  // Speed intensity slider
  const speedIntensity = document.getElementById('speedIntensity');
  const speedIntensityValue = document.getElementById('speedIntensityValue');
  if (speedIntensity) speedIntensity.addEventListener('input', function() {
    if (speedIntensityValue) speedIntensityValue.textContent = this.value + '%';
    sendZoneEffectConfig();
    drawZoneEffectPreview();
  });
  
  // Random turnback checkbox
  const randomTurnback = document.getElementById('randomTurnbackEnabled');
  if (randomTurnback) randomTurnback.addEventListener('change', () => {
    sendZoneEffectConfig();
    drawZoneEffectPreview();
    updateZoneEffectHeaderText();
  });
  
  // Turnback chance slider
  const turnbackChance = document.getElementById('turnbackChance');
  const turnbackChanceValue = document.getElementById('turnbackChanceValue');
  if (turnbackChance) turnbackChance.addEventListener('input', function() {
    if (turnbackChanceValue) turnbackChanceValue.textContent = this.value + '%';
    sendZoneEffectConfig();
  });
  
  // End pause checkbox
  const endPauseEnabled = document.getElementById('endPauseEnabled');
  if (endPauseEnabled) endPauseEnabled.addEventListener('change', () => {
    sendZoneEffectConfig();
    updateZoneEffectHeaderText();
  });
  
  // End pause mode radio buttons
  document.querySelectorAll('input[name="endPauseMode"]').forEach(radio => {
    radio.addEventListener('change', function() {
      const isRandom = document.getElementById('endPauseModeRandom')?.checked;
      const fixedControls = document.getElementById('endPauseFixedControls');
      const randomControls = document.getElementById('endPauseRandomControls');
      
      if (fixedControls && randomControls) {
        fixedControls.classList.toggle('hidden', isRandom);
        randomControls.classList.toggle('hidden', !isRandom);
      }
      
      sendZoneEffectConfig();
    });
  });
  
  // End pause duration presets
  document.querySelectorAll('[data-end-pause]').forEach(btn => {
    btn.addEventListener('click', function() {
      const value = this.dataset.endPause;
      const durationInput = document.getElementById('endPauseDuration');
      if (durationInput) durationInput.value = value;
      
      // Update active state
      document.querySelectorAll('[data-end-pause]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
      
      sendZoneEffectConfig();
    });
  });
  
  // End pause duration inputs
  ['endPauseDuration', 'endPauseMin', 'endPauseMax'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.addEventListener('input', () => sendZoneEffectConfig());
  });
}

// ============================================================================
// CYCLE PAUSE - FACTORY FUNCTION (used by Simple and Oscillation modes)
// Note: getCyclePauseSection() and getCyclePauseOscSection() are in SimpleUtils.js
// ============================================================================

/**
 * Setup preset buttons for cycle pause controls (extracted to reduce nesting)
 * @param {string} attr - Data attribute name
 * @param {string} inputId - Target input element ID
 * @param {Function} sendConfigFn - Config sender callback
 */
function setupPausePresets(attr, inputId, sendConfigFn) {
  document.querySelectorAll(`[${attr}]`).forEach(btn => {
    btn.addEventListener('click', function() {
      document.getElementById(inputId).value = this.getAttribute(attr);
      document.querySelectorAll(`[${attr}]`).forEach(b => b.classList.remove('active'));
      this.classList.add('active');
      sendConfigFn();
    });
  });
}

/**
 * Factory function to create Cycle Pause handlers for a mode (Simple or Oscillation)
 * @param {Object} cfg - Configuration object
 * @param {string} cfg.suffix - Suffix for element IDs ('' for Simple, 'Osc' for Oscillation)
 * @param {Function} cfg.getSectionFn - Function to get the section element
 * @param {string} cfg.wsCmd - WebSocket command to send
 * @param {string} cfg.radioName - Name of the radio button group
 * @param {string} cfg.dataAttrSuffix - Suffix for data attributes ('' for Simple, '-osc' for Oscillation)
 * @returns {Function} sendConfig function for external use
 */
function createCyclePauseHandlers(cfg) {
  const s = cfg.suffix;  // '' for Simple, 'Osc' for Oscillation
  const dataS = cfg.dataAttrSuffix;  // '' for Simple, '-osc' for Oscillation
  
  // Send config function
  const sendConfig = () => {
    const section = cfg.getSectionFn();
    const enabled = !section.classList.contains('collapsed');
    const isRandom = document.getElementById('pauseModeRandom' + s).checked;
    let minPauseSec = Number.parseFloat(document.getElementById('cyclePauseMin' + s).value);
    let maxPauseSec = Number.parseFloat(document.getElementById('cyclePauseMax' + s).value);
    
    // Validation: Min must be ≤ Max (only in random mode)
    if (isRandom && minPauseSec > maxPauseSec) {
      const modeKey = s ? 'oscillation' : 'simple';
      showNotification('⚠️ ' + t(modeKey + '.pauseMinMax', {min: minPauseSec.toFixed(1), max: maxPauseSec.toFixed(1)}), 'warning');
      maxPauseSec = minPauseSec;
      document.getElementById('cyclePauseMax' + s).value = maxPauseSec;
    }
    
    sendCommand(cfg.wsCmd, {
      enabled: enabled,
      isRandom: isRandom,
      pauseDurationSec: Number.parseFloat(document.getElementById('cyclePauseDuration' + s).value),
      minPauseSec: minPauseSec,
      maxPauseSec: maxPauseSec
    });
  };
  
  // Toggle function (exposed globally)
  const toggleName = s ? 'toggleCyclePause' + s + 'Section' : 'toggleCyclePauseSection';
  window[toggleName] = function() {
    const section = cfg.getSectionFn();
    const headerText = document.getElementById('cyclePause' + s + 'HeaderText');
    const isCollapsed = section.classList.contains('collapsed');
    
    section.classList.toggle('collapsed');
    
    if (isCollapsed) {
      headerText.textContent = t('simple.cyclePauseEnabled');
      sendConfig();
    } else {
      headerText.textContent = t('simple.cyclePauseDisabled');
      sendCommand(cfg.wsCmd, { enabled: false });
    }
  };
  
  // Radio button handlers
  document.querySelectorAll(`input[name="${cfg.radioName}"]`).forEach(radio => {
    radio.addEventListener('change', function() {
      const isFixed = this.value === 'fixed';
      document.getElementById('pauseFixedControls' + s).style.display = isFixed ? 'flex' : 'none';
      document.getElementById('pauseRandomControls' + s).style.display = isFixed ? 'none' : 'block';
      if (!cfg.getSectionFn().classList.contains('collapsed')) sendConfig();
    });
  });
  
  // Preset buttons
  setupPausePresets('data-pause-duration' + dataS, 'cyclePauseDuration' + s, sendConfig);
  setupPausePresets('data-pause-min' + dataS, 'cyclePauseMin' + s, sendConfig);
  setupPausePresets('data-pause-max' + dataS, 'cyclePauseMax' + s, sendConfig);
  
  // Input change listeners
  ['cyclePauseDuration' + s, 'cyclePauseMin' + s, 'cyclePauseMax' + s].forEach(id => {
    const input = document.getElementById(id);
    if (input) {
      input.addEventListener('change', () => {
        if (!cfg.getSectionFn().classList.contains('collapsed')) sendConfig();
      });
    }
  });
  
  // Expose sendConfig for external use
  return sendConfig;
}

/**
 * Initialize Cycle Pause handlers for both Simple and Oscillation modes
 * Called from main.js on page load
 */
function initCyclePauseHandlers() {
  // Initialize Simple mode Cycle Pause
  createCyclePauseHandlers({
    suffix: '',
    getSectionFn: getCyclePauseSection,
    wsCmd: WS_CMD.UPDATE_CYCLE_PAUSE,
    radioName: 'cyclePauseMode',
    dataAttrSuffix: ''
  });
  
  // Initialize Oscillation mode Cycle Pause
  createCyclePauseHandlers({
    suffix: 'Osc',
    getSectionFn: getCyclePauseOscSection,
    wsCmd: WS_CMD.UPDATE_CYCLE_PAUSE_OSC,
    radioName: 'cyclePauseModeOsc',
    dataAttrSuffix: '-osc'
  });
}
