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
 * - SimpleUtils.js (calculateSlowdownFactorPure, drawDecelPreviewPure, getCyclePauseSection)
 */

// ============================================================================
// SIMPLE MODE - Cycle Pause Configuration
// ============================================================================

/**
 * Send cycle pause configuration for Simple mode
 */
function sendSimpleCyclePauseConfig() {
  const enabled = document.getElementById('cyclePauseEnabled')?.checked || false;
  const isRandom = document.getElementById('cyclePauseRandom')?.checked || false;
  const durationSec = parseFloat(document.getElementById('cyclePauseDuration')?.value || 0);
  let minSec = parseFloat(document.getElementById('cyclePauseMin')?.value || 0.5);
  let maxSec = parseFloat(document.getElementById('cyclePauseMax')?.value || 3.0);
  
  // Validation: Min must be ≤ Max (only if random mode enabled)
  if (isRandom && minSec > maxSec) {
    showNotification('⚠️ Pause Min (' + minSec.toFixed(1) + 's) doit être ≤ Max (' + maxSec.toFixed(1) + 's)', 'warning');
    // Auto-correction: adjust Max = Min
    maxSec = minSec;
    document.getElementById('cyclePauseMax').value = maxSec;
  }
  
  sendCommand(WS_CMD.SET_CYCLE_PAUSE, {
    mode: 'simple',
    enabled: enabled,
    isRandom: isRandom,
    durationSec: durationSec,
    minSec: minSec,
    maxSec: maxSec
  });
}

// ============================================================================
// SIMPLE MODE - Start/Stop Actions
// ============================================================================

/**
 * Start simple mode movement
 */
function startSimpleMode() {
  const distance = parseFloat(document.getElementById('distance').value);
  const isSeparateMode = document.getElementById('speedModeSeparate')?.checked || false;
  
  let speedForward, speedBackward;
  if (isSeparateMode) {
    speedForward = parseFloat(document.getElementById('speedForward').value);
    speedBackward = parseFloat(document.getElementById('speedBackward').value);
  } else {
    const unifiedSpeed = parseFloat(document.getElementById('speedUnified').value);
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

// ============================================================================
// SIMPLE MODE - UI UPDATE (called from main.js updateUI)
// ============================================================================

/**
 * Update Simple mode UI from WebSocket status data
 * Extracted from main.js updateUI() for better modularity
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
      btnPause.innerHTML = '▶ Reprendre';
    } else {
      btnPause.innerHTML = '⏸ Pause';
    }
  }
  
  const btnStop = document.getElementById('btnStop');
  if (btnStop) {
    btnStop.disabled = !(isRunningOrPaused || isError);
  }
  
  // ===== CYCLE PAUSE DISPLAY =====
  if (data.motion && data.motion.cyclePause) {
    const pauseStatus = document.getElementById('cyclePauseStatus');
    const pauseRemaining = document.getElementById('cyclePauseRemaining');
    
    if (data.motion.cyclePause.isPausing && pauseStatus && pauseRemaining) {
      const remainingSec = (data.motion.cyclePause.remainingMs / 1000).toFixed(1);
      pauseStatus.style.display = 'block';
      pauseRemaining.textContent = remainingSec + 's';
    } else if (pauseStatus) {
      pauseStatus.style.display = 'none';
    }
    
    // Sync UI to backend state (only if section is expanded)
    const section = getCyclePauseSection();
    const headerText = document.getElementById('cyclePauseHeaderText');
    if (section && headerText) {
      const isEnabled = data.motion.cyclePause.enabled;
      const isCollapsed = section.classList.contains('collapsed');
      
      // Sync collapsed state with backend enabled state
      if (isEnabled && isCollapsed) {
        section.classList.remove('collapsed');
        headerText.textContent = '⏸️ Pause entre cycles - activée';
      } else if (!isEnabled && !isCollapsed) {
        section.classList.add('collapsed');
        headerText.textContent = '⏸️ Pause entre cycles - désactivée';
      }
      
      // Sync radio buttons
      if (data.motion.cyclePause.isRandom) {
        document.getElementById('pauseModeRandom').checked = true;
        document.getElementById('pauseFixedControls').style.display = 'none';
        document.getElementById('pauseRandomControls').style.display = 'block';
      } else {
        document.getElementById('pauseModeFixed').checked = true;
        document.getElementById('pauseFixedControls').style.display = 'flex';
        document.getElementById('pauseRandomControls').style.display = 'none';
      }
      
      // Sync input values (avoid overwriting if user is editing)
      if (document.activeElement !== document.getElementById('cyclePauseDuration')) {
        document.getElementById('cyclePauseDuration').value = data.motion.cyclePause.pauseDurationSec.toFixed(1);
      }
      if (document.activeElement !== document.getElementById('cyclePauseMin')) {
        document.getElementById('cyclePauseMin').value = data.motion.cyclePause.minPauseSec.toFixed(1);
      }
      if (document.activeElement !== document.getElementById('cyclePauseMax')) {
        document.getElementById('cyclePauseMax').value = data.motion.cyclePause.maxPauseSec.toFixed(1);
      }
    }
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
    
    const unifiedSpeed = parseFloat(document.getElementById('speedUnified').value);
    document.getElementById('speedForward').value = unifiedSpeed;
    document.getElementById('speedBackward').value = unifiedSpeed;
    
    // Send both commands to ESP32 to ensure sync
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: unifiedSpeed});
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: unifiedSpeed});
    
    // Update preset button highlighting
    document.querySelectorAll('[data-speed-forward]').forEach(btn => {
      if (parseFloat(btn.getAttribute('data-speed-forward')) === unifiedSpeed) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    document.querySelectorAll('[data-speed-backward]').forEach(btn => {
      if (parseFloat(btn.getAttribute('data-speed-backward')) === unifiedSpeed) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    
    console.log('Switched to SEPARATE mode: both speeds set to ' + unifiedSpeed);
    
  } else {
    // SEPARATE → UNIFIED: Use forward speed value for both
    unifiedGroup.style.display = 'flex';
    separateGroup.style.display = 'none';
    
    const forwardSpeed = parseFloat(document.getElementById('speedForward').value);
    
    // Use forward speed as the unified value
    document.getElementById('speedUnified').value = forwardSpeed;
    
    // Also update backward to match forward
    document.getElementById('speedBackward').value = forwardSpeed;
    
    // Apply forward speed to BOTH directions immediately
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: forwardSpeed});
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: forwardSpeed});
    
    // Update preset button highlighting
    document.querySelectorAll('[data-speed-unified]').forEach(btn => {
      if (parseFloat(btn.getAttribute('data-speed-unified')) === forwardSpeed) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    
    console.log('Switched to UNIFIED mode: using forward speed ' + forwardSpeed + ' for both directions');
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
  console.log('🔧 Initializing Simple mode listeners...');
  
  // ===== START/PAUSE/STOP BUTTONS =====
  document.getElementById('btnStart').addEventListener('click', startSimpleMode);
  document.getElementById('btnPause').addEventListener('click', pauseSimpleMode);
  document.getElementById('btnStop').addEventListener('click', stopSimpleMode);
  
  // ===== START POSITION INPUT =====
  const startPositionEl = document.getElementById('startPosition');
  startPositionEl.addEventListener('mousedown', function() {
    AppState.editing.input = 'startPosition';
    this.focus();
  });
  startPositionEl.addEventListener('focus', function() {
    AppState.editing.input = 'startPosition';
  });
  startPositionEl.addEventListener('blur', function() {
    AppState.editing.input = null;
  });
  startPositionEl.addEventListener('change', function() {
    const startPos = parseFloat(this.value);
    sendCommand(WS_CMD.SET_START_POSITION, {startPosition: startPos});
    AppState.editing.input = null;
  });
  
  // ===== DISTANCE INPUT =====
  const distanceEl = document.getElementById('distance');
  distanceEl.addEventListener('mousedown', function() {
    AppState.editing.input = 'distance';
    this.focus();
  });
  distanceEl.addEventListener('focus', function() {
    AppState.editing.input = 'distance';
  });
  distanceEl.addEventListener('blur', function() {
    AppState.editing.input = null;
  });
  distanceEl.addEventListener('change', function() {
    const distance = parseFloat(this.value);
    sendCommand(WS_CMD.SET_DISTANCE, {distance: distance});
    AppState.editing.input = null;
  });
  
  // ===== UNIFIED SPEED INPUT =====
  const speedUnifiedEl = document.getElementById('speedUnified');
  speedUnifiedEl.addEventListener('mousedown', function() {
    AppState.editing.input = 'speedUnified';
    this.focus();
  });
  speedUnifiedEl.addEventListener('focus', function() {
    AppState.editing.input = 'speedUnified';
  });
  speedUnifiedEl.addEventListener('blur', function() {
    AppState.editing.input = null;
  });
  speedUnifiedEl.addEventListener('change', function() {
    const speed = parseFloat(this.value);
    
    // Update hidden separate fields for consistency
    document.getElementById('speedForward').value = speed;
    document.getElementById('speedBackward').value = speed;
    
    // Send both commands to ESP32
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
    
    AppState.editing.input = null;
  });
  
  // ===== FORWARD SPEED INPUT =====
  const speedForwardEl = document.getElementById('speedForward');
  speedForwardEl.addEventListener('mousedown', function() {
    AppState.editing.input = 'speedForward';
    this.focus();
  });
  speedForwardEl.addEventListener('focus', function() {
    AppState.editing.input = 'speedForward';
  });
  speedForwardEl.addEventListener('blur', function() {
    AppState.editing.input = null;
  });
  speedForwardEl.addEventListener('change', function() {
    const speed = parseFloat(this.value);
    sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
    AppState.editing.input = null;
  });
  
  // ===== BACKWARD SPEED INPUT =====
  const speedBackwardEl = document.getElementById('speedBackward');
  speedBackwardEl.addEventListener('mousedown', function() {
    AppState.editing.input = 'speedBackward';
    this.focus();
  });
  speedBackwardEl.addEventListener('focus', function() {
    AppState.editing.input = 'speedBackward';
  });
  speedBackwardEl.addEventListener('blur', function() {
    AppState.editing.input = null;
  });
  speedBackwardEl.addEventListener('change', function() {
    const speed = parseFloat(this.value);
    sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
    AppState.editing.input = null;
  });
  
  // ===== CYCLE PAUSE LISTENERS =====
  if (document.getElementById('cyclePauseEnabled')) {
    document.getElementById('cyclePauseEnabled').addEventListener('change', sendSimpleCyclePauseConfig);
  }
  if (document.getElementById('cyclePauseRandom')) {
    document.getElementById('cyclePauseRandom').addEventListener('change', sendSimpleCyclePauseConfig);
  }
  if (document.getElementById('cyclePauseDuration')) {
    document.getElementById('cyclePauseDuration').addEventListener('change', sendSimpleCyclePauseConfig);
  }
  if (document.getElementById('cyclePauseMin')) {
    document.getElementById('cyclePauseMin').addEventListener('change', sendSimpleCyclePauseConfig);
  }
  if (document.getElementById('cyclePauseMax')) {
    document.getElementById('cyclePauseMax').addEventListener('change', sendSimpleCyclePauseConfig);
  }
  
  // ===== START POSITION PRESETS =====
  document.querySelectorAll('[data-start]').forEach(btn => {
    btn.addEventListener('click', function() {
      const startPos = parseFloat(this.getAttribute('data-start'));
      const maxStart = parseFloat(document.getElementById('startPosition').max);
      
      if (startPos <= maxStart) {
        document.getElementById('startPosition').value = startPos;
        sendCommand(WS_CMD.SET_START_POSITION, {startPosition: startPos});
        
        document.querySelectorAll('[data-start]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
      }
    });
  });
  
  // ===== DISTANCE PRESETS =====
  document.querySelectorAll('[data-distance]').forEach(btn => {
    btn.addEventListener('click', function() {
      const distance = parseFloat(this.getAttribute('data-distance'));
      const maxDist = parseFloat(document.getElementById('distance').max);
      
      if (distance <= maxDist) {
        document.getElementById('distance').value = distance;
        sendCommand(WS_CMD.SET_DISTANCE, {distance: distance});
        
        document.querySelectorAll('[data-distance]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
      }
    });
  });
  
  // ===== UNIFIED SPEED PRESETS =====
  document.querySelectorAll('[data-speed-unified]').forEach(btn => {
    btn.addEventListener('click', function() {
      const speed = parseFloat(this.getAttribute('data-speed-unified'));
      
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
      const speed = parseFloat(this.getAttribute('data-speed-forward'));
      document.getElementById('speedForward').value = speed;
      sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
      
      document.querySelectorAll('[data-speed-forward]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
    });
  });
  
  // ===== BACKWARD SPEED PRESETS =====
  document.querySelectorAll('[data-speed-backward]').forEach(btn => {
    btn.addEventListener('click', function() {
      const speed = parseFloat(this.getAttribute('data-speed-backward'));
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
  
  // ===== DECELERATION ZONE EVENT LISTENERS =====
  initDecelZoneListeners();
  
  console.log('✅ Simple mode listeners initialized');
}

// ============================================================================
// DECELERATION ZONE - Simple Mode Only
// ============================================================================

/**
 * JavaScript implementation of C++ calculateSlowdownFactor()
 * Delegates to pure function from SimpleUtils.js
 */
function calculateSlowdownFactorJS(zoneProgress, maxSlowdown, mode) {
  return calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode);
}

/**
 * Toggle deceleration section collapsed state
 */
function toggleDecelSection() {
  const section = document.getElementById('decelSection');
  const headerText = document.getElementById('decelHeaderText');
  const isCollapsed = section.classList.contains('collapsed');
  
  section.classList.toggle('collapsed');
  
  if (isCollapsed) {
    // Expanding = activating
    headerText.textContent = '🎯 Décélération - activée';
    sendDecelConfig();
    drawDecelPreview();
  } else {
    // Collapsing = deactivating
    headerText.textContent = '🎯 Décélération - désactivée';
    sendCommand(WS_CMD.SET_DECEL_ZONE, { enabled: false });
  }
}

/**
 * Send deceleration configuration to ESP32
 */
function sendDecelConfig() {
  const section = document.getElementById('decelSection');
  const isEnabled = !section.classList.contains('collapsed');
  
  const zoneMM = parseFloat(document.getElementById('decelZoneMM').value) || 50;
  
  const config = {
    enabled: isEnabled,
    enableStart: document.getElementById('decelZoneStart').checked,
    enableEnd: document.getElementById('decelZoneEnd').checked,
    zoneMM: zoneMM,
    effectPercent: parseFloat(document.getElementById('decelEffectPercent').value) || 75,
    mode: parseInt(document.getElementById('decelModeSelect')?.value || 1)
  };
  
  // Store requested zone value for comparison
  AppState.lastDecelZoneRequest = zoneMM;
  
  sendCommand(WS_CMD.SET_DECEL_ZONE, config);
}

/**
 * Update deceleration zone UI from server data
 * @param {Object} decelZone - Deceleration zone data from backend
 */
function updateDecelZoneUI(decelZone) {
  if (!decelZone || AppState.editing.input === 'decelZone') return;
  
  const section = document.getElementById('decelSection');
  const headerText = document.getElementById('decelHeaderText');
  
  // Defense: Only update full decelZone fields if enabled (Phase 1 optimization)
  // When disabled, backend sends only {enabled: false} to save bandwidth
  if (decelZone.enabled && decelZone.zoneMM !== undefined) {
    // Update section collapsed state and header text based on enabled
    if (section && headerText) {
      section.classList.remove('collapsed');
      headerText.textContent = '🎯 Décélération - activée';
    }
    
    // Safe access to optional fields
    if (decelZone.enableStart !== undefined) {
      const startCheckbox = document.getElementById('decelZoneStart');
      if (startCheckbox) startCheckbox.checked = decelZone.enableStart;
    }
    if (decelZone.enableEnd !== undefined) {
      const endCheckbox = document.getElementById('decelZoneEnd');
      if (endCheckbox) endCheckbox.checked = decelZone.enableEnd;
    }
    
    // Check if zone value was adapted by ESP32 (only if we just sent a request)
    const decelZoneInput = document.getElementById('decelZoneMM');
    const requestedZone = AppState.lastDecelZoneRequest;
    const receivedZone = decelZone.zoneMM;
    
    if (requestedZone !== undefined && Math.abs(requestedZone - receivedZone) > 0.1) {
      // Value was adapted - show notification once
      showNotification(`⚠️ Zone ajustée: ${requestedZone.toFixed(0)}mm → ${receivedZone.toFixed(0)}mm (limite du mouvement)`, 'warning', 4000);
      AppState.lastDecelZoneRequest = undefined;
    }
    
    if (decelZoneInput) {
      decelZoneInput.value = receivedZone;
    }
    
    // Effect percent (safe access)
    if (decelZone.effectPercent !== undefined) {
      const effectPercentInput = document.getElementById('decelEffectPercent');
      const effectValueSpan = document.getElementById('effectValue');
      if (effectPercentInput) {
        effectPercentInput.value = decelZone.effectPercent;
      }
      if (effectValueSpan) {
        effectValueSpan.textContent = decelZone.effectPercent.toFixed(0) + '%';
      }
    }
    
    // Update select dropdown for mode
    if (decelZone.mode !== undefined) {
      const decelModeSelect = document.getElementById('decelModeSelect');
      if (decelModeSelect) {
        decelModeSelect.value = decelZone.mode.toString();
      }
    }
    
    // Update zone preset active state
    document.querySelectorAll('[data-decel-zone]').forEach(btn => {
      const btnValue = parseInt(btn.getAttribute('data-decel-zone'));
      if (btnValue === decelZone.zoneMM) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    
    // Redraw preview if enabled
    drawDecelPreview();
  } else {
    // Disabled state
    if (section && headerText) {
      section.classList.add('collapsed');
      headerText.textContent = '🎯 Décélération - désactivée';
    }
  }
}

/**
 * Draw deceleration curve preview on canvas
 * Delegates to pure function from SimpleUtils.js
 */
function drawDecelPreview() {
  const canvas = document.getElementById('decelPreview');
  if (!canvas) return;
  
  const config = getDecelConfigFromDOM();
  drawDecelPreviewPure(canvas, config);
}

/**
 * Initialize deceleration zone event listeners
 */
function initDecelZoneListeners() {
  // Decel zone presets
  document.querySelectorAll('[data-decel-zone]').forEach(btn => {
    btn.addEventListener('click', function() {
      const value = this.getAttribute('data-decel-zone');
      document.getElementById('decelZoneMM').value = value;
      
      // Update active state
      document.querySelectorAll('[data-decel-zone]').forEach(b => b.classList.remove('active'));
      this.classList.add('active');
      
      sendDecelConfig();
      drawDecelPreview();
    });
  });
  
  // Decel zone start/end checkboxes
  document.getElementById('decelZoneStart').addEventListener('change', function() {
    sendDecelConfig();
    drawDecelPreview();
  });
  
  document.getElementById('decelZoneEnd').addEventListener('change', function() {
    sendDecelConfig();
    drawDecelPreview();
  });
  
  // Zone size input
  document.getElementById('decelZoneMM').addEventListener('input', function() {
    sendDecelConfig();
    drawDecelPreview();
  });
  
  // Effect percent slider
  document.getElementById('decelEffectPercent').addEventListener('input', function() {
    document.getElementById('effectValue').textContent = this.value + '%';
    sendDecelConfig();
    drawDecelPreview();
  });
  
  // Deceleration mode select dropdown
  document.getElementById('decelModeSelect').addEventListener('change', function() {
    sendDecelConfig();
    drawDecelPreview();
  });
}

// ============================================================================
// CYCLE PAUSE - FACTORY FUNCTION (used by Simple and Oscillation modes)
// Note: getCyclePauseSection() and getCyclePauseOscSection() are in SimpleUtils.js
// ============================================================================

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
  const s = cfg.suffix;  // '' pour Simple, 'Osc' pour Oscillation
  const dataS = cfg.dataAttrSuffix;  // '' pour Simple, '-osc' pour Oscillation
  
  // Send config function
  const sendConfig = () => {
    const section = cfg.getSectionFn();
    const enabled = !section.classList.contains('collapsed');
    const isRandom = document.getElementById('pauseModeRandom' + s).checked;
    
    sendCommand(cfg.wsCmd, {
      enabled: enabled,
      isRandom: isRandom,
      pauseDurationSec: parseFloat(document.getElementById('cyclePauseDuration' + s).value),
      minPauseSec: parseFloat(document.getElementById('cyclePauseMin' + s).value),
      maxPauseSec: parseFloat(document.getElementById('cyclePauseMax' + s).value)
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
      headerText.textContent = '⏸️ Pause entre cycles - activée';
      sendConfig();
    } else {
      headerText.textContent = '⏸️ Pause entre cycles - désactivée';
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
  
  // Preset buttons helper
  const setupPresets = (attr, inputId) => {
    document.querySelectorAll(`[${attr}]`).forEach(btn => {
      btn.addEventListener('click', function() {
        document.getElementById(inputId).value = this.getAttribute(attr);
        document.querySelectorAll(`[${attr}]`).forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        sendConfig();
      });
    });
  };
  
  setupPresets('data-pause-duration' + dataS, 'cyclePauseDuration' + s);
  setupPresets('data-pause-min' + dataS, 'cyclePauseMin' + s);
  setupPresets('data-pause-max' + dataS, 'cyclePauseMax' + s);
  
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
  const sendCyclePauseConfig = createCyclePauseHandlers({
    suffix: '',
    getSectionFn: getCyclePauseSection,
    wsCmd: WS_CMD.UPDATE_CYCLE_PAUSE,
    radioName: 'cyclePauseMode',
    dataAttrSuffix: ''
  });
  
  // Initialize Oscillation mode Cycle Pause
  const sendCyclePauseConfigOsc = createCyclePauseHandlers({
    suffix: 'Osc',
    getSectionFn: getCyclePauseOscSection,
    wsCmd: WS_CMD.UPDATE_CYCLE_PAUSE_OSC,
    radioName: 'cyclePauseModeOsc',
    dataAttrSuffix: '-osc'
  });
  
  // Expose for external use if needed
  window.sendCyclePauseConfig = sendCyclePauseConfig;
  window.sendCyclePauseConfigOsc = sendCyclePauseConfigOsc;
}
