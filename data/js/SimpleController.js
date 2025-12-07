/**
 * SimpleController.js - Simple Mode Control Module
 * 
 * Handles all Simple mode functionality:
 * - Start/Pause/Stop controls
 * - Position and distance inputs
 * - Speed controls (unified and separate)
 * - Cycle pause configuration
 * - Preset buttons
 * 
 * Dependencies: DOM, AppState, WS_CMD, sendCommand, showNotification, showStopModal, currentPositionMM
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
  
  console.log('✅ Simple mode listeners initialized');
}
