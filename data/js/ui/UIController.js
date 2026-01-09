/**
 * ============================================================================
 * UIController.js - UI Control Module
 * ============================================================================
 * Handles:
 * - Tab management (mode switching between Simple, Pursuit, Oscillation, Chaos, Sequencer)
 * - Modal dialogs (mode change confirmation, stop confirmation, sequencer limit)
 * 
 * Dependencies:
 * - app.js (AppState, SystemState, WS_CMD)
 * - utils.js (sendCommand, showNotification)
 * - PursuitController.js (isPursuitActive, disablePursuitMode, setGaugeTarget)
 * - OscillationController.js (sendOscillationConfig, validateOscillationLimits, updateOscillationPresets)
 * - ChaosController.js (validateChaosLimits, updateChaosPresets)
 * 
 * Created: December 2025 (extracted from main.js)
 * ============================================================================
 */

// ============================================================================
// TAB MANAGEMENT & MODE SWITCHING
// ============================================================================

/**
 * Switch to a different mode tab
 * @param {string} tabName - Target tab name (simple, pursuit, oscillation, chaos, tableau)
 */
function switchTab(tabName) {
  // Save statistics before mode change
  sendCommand(WS_CMD.SAVE_STATS, {});
  
  // SAFETY: Stop any running movement before switching tabs
  // This prevents confusion when switching modes while paused
  const isRunningOrPaused = (AppState.system.currentState === SystemState.RUNNING || 
                              AppState.system.currentState === SystemState.PAUSED);
  
  if (isRunningOrPaused) {
    console.log('Stopping movement before tab switch');
    sendCommand(WS_CMD.STOP);
    // Note: UI will update via WebSocket status message
  }
  
  // PRE-CHECK: If switching to sequencer with limit active, show modal and abort
  if (tabName === 'tableau') {
    const currentLimit = AppState.pursuit.maxDistLimitPercent || 100;
    
    if (currentLimit < 100) {
      const limitedCourse = AppState.pursuit.effectiveMaxDistMM || 0;
      const totalCourse = AppState.pursuit.totalDistanceMM || 0;
      
      // Update modal content with current values
      document.getElementById('seqModalCurrentLimit').textContent = 
        currentLimit.toFixed(0) + '% (' + limitedCourse.toFixed(1) + ' mm)';
      document.getElementById('seqModalAfterLimit').textContent = 
        '100% (' + totalCourse.toFixed(1) + ' mm)';
      
      // Show modal and ABORT tab switch
      document.getElementById('sequencerLimitModal').classList.add('active');
      return; // Don't switch tab yet!
    }
  }
  
  // Hide all tab contents
  document.querySelectorAll('.tab-content').forEach(content => {
    content.classList.remove('active');
  });
  
  // Remove active class from all tabs
  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.remove('active');
  });
  
  // Show selected tab content
  const tabMap = {
    'simple': 'tabSimple',
    'pursuit': 'tabPursuit',
    'oscillation': 'tabOscillation',
    'chaos': 'tabChaos',
    'tableau': 'tabTableau'
  };
  
  document.getElementById(tabMap[tabName]).classList.add('active');
  
  // Add active class to selected tab
  document.querySelector('[data-tab="' + tabName + '"]').classList.add('active');
  
  AppState.system.currentMode = tabName;
  console.log('Switched to mode: ' + tabName);
  
  // Handle mode-specific initialization
  if (tabName === 'pursuit') {
    // Switching TO pursuit mode
    setGaugeTarget(0);  // Start at 0mm
  } else if (tabName === 'simple') {
    // Switching TO simple mode - disable pursuit if active
    if (isPursuitActive()) {
      disablePursuitMode();
    }
  } else if (tabName === 'oscillation') {
    // Switching TO oscillation mode - always update center with effective max
    const totalMM = AppState.pursuit.totalDistanceMM || 0;
    const effectiveMax = (AppState.pursuit.maxDistLimitPercent && AppState.pursuit.maxDistLimitPercent < 100)
      ? (totalMM * AppState.pursuit.maxDistLimitPercent / 100)
      : totalMM;
    
    const oscCenterField = document.getElementById('oscCenter');
    if (oscCenterField && effectiveMax > 0) {
      // Always set to effective center
      oscCenterField.value = (effectiveMax / 2).toFixed(1);
      // Also send to backend (use sendOscillationConfig function)
      sendOscillationConfig();
    }
    
    validateOscillationLimits();
    updateOscillationPresets();  // Update preset buttons state
  } else if (tabName === 'chaos') {
    // Switching TO chaos mode - always update center with effective max
    const totalMM = AppState.pursuit.totalDistanceMM || 0;
    const effectiveMax = (AppState.pursuit.maxDistLimitPercent && AppState.pursuit.maxDistLimitPercent < 100)
      ? (totalMM * AppState.pursuit.maxDistLimitPercent / 100)
      : totalMM;
    
    const chaosCenterField = document.getElementById('chaosCenterPos');
    if (chaosCenterField && effectiveMax > 0) {
      // Always set to effective center
      chaosCenterField.value = (effectiveMax / 2).toFixed(1);
    }
    
    validateChaosLimits();
    updateChaosPresets();  // Update preset buttons state
  }
  // Note: tableau (sequencer) pre-check is done at the start of switchTab()
}

/**
 * Switch to a different mode tab WITHOUT stopping movement
 * Used for auto-switch on reconnection when movement is already in progress
 * @param {string} tabName - Target tab name (simple, pursuit, oscillation, chaos, tableau)
 */
function switchTabWithoutStop(tabName) {
  // Hide all tab contents
  document.querySelectorAll('.tab-content').forEach(content => {
    content.classList.remove('active');
  });
  
  // Remove active class from all tabs
  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.remove('active');
  });
  
  // Show selected tab content
  const tabMap = {
    'simple': 'tabSimple',
    'pursuit': 'tabPursuit',
    'oscillation': 'tabOscillation',
    'chaos': 'tabChaos',
    'tableau': 'tabTableau'
  };
  
  const targetContent = document.getElementById(tabMap[tabName]);
  if (targetContent) {
    targetContent.classList.add('active');
  }
  
  // Add active class to selected tab
  const targetTab = document.querySelector('[data-tab="' + tabName + '"]');
  if (targetTab) {
    targetTab.classList.add('active');
  }
  
  AppState.system.currentMode = tabName;
  console.log('🔄 Auto-switched to mode: ' + tabName + ' (movement in progress)');
}

/**
 * Check if system is currently running any operation
 * @returns {boolean} True if system is running or paused
 */
function isSystemRunning() {
  // State 3 = RUNNING, 4 = PAUSED (for simple mode)
  // Also check if pursuit mode is active (isPursuitActive from PursuitController.js)
  return AppState.system.currentState === SystemState.RUNNING || 
         AppState.system.currentState === SystemState.PAUSED || 
         isPursuitActive();
}

// ============================================================================
// MODE CHANGE MODAL FUNCTIONS
// ============================================================================

/**
 * Cancel mode change and close modal
 */
function cancelModeChange() {
  document.getElementById('modeChangeModal').classList.remove('active');
  AppState.system.pendingModeSwitch = null;
  
  // Reset checkbox
  document.getElementById('bypassCalibrationCheckbox').checked = false;
  
  // Restore previous tab selection
  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.remove('active');
  });
  document.querySelector('[data-tab="' + AppState.system.currentMode + '"]').classList.add('active');
}

/**
 * Confirm mode change and proceed with calibration/return
 */
function confirmModeChange() {
  const bypassCalibration = document.getElementById('bypassCalibrationCheckbox').checked;
  document.getElementById('modeChangeModal').classList.remove('active');
  
  // If pursuit is active, disable it first
  if (isPursuitActive()) {
    console.log('Mode change: Disabling pursuit mode first');
    disablePursuitMode();
  }
  
  // Stop movement (for simple mode)
  sendCommand(WS_CMD.STOP, {});
  
  // Wait a bit for stop to complete, then either quick return or full calibration
  setTimeout(function() {
    if (bypassCalibration) {
      // Quick return to start only
      console.log('Mode change: Quick return to start (bypass full calibration)');
      sendCommand(WS_CMD.RETURN_TO_START, {});
    } else {
      // Full calibration
      console.log('Mode change: Full calibration');
      sendCommand(WS_CMD.CALIBRATE, {});
    }
    
    // Reset checkbox for next time
    document.getElementById('bypassCalibrationCheckbox').checked = false;
    
    // Actually switch the tab after starting calibration/return
    if (AppState.system.pendingModeSwitch) {
      switchTab(AppState.system.pendingModeSwitch);
      AppState.system.pendingModeSwitch = null;
    }
  }, 500);
}

// ============================================================================
// STOP CONFIRMATION MODAL FUNCTIONS
// ============================================================================

/**
 * Show stop confirmation modal
 */
function showStopModal() {
  document.getElementById('stopModal').classList.add('active');
}

/**
 * Cancel stop and close modal
 */
function cancelStopModal() {
  document.getElementById('stopModal').classList.remove('active');
  // Reset checkbox for next time (keep checked by default)
  document.getElementById('returnToStartCheckbox').checked = true;
}

/**
 * Confirm stop with mode-specific handling
 */
function confirmStopModal() {
  const shouldReturnToStart = document.getElementById('returnToStartCheckbox').checked;
  document.getElementById('stopModal').classList.remove('active');
  
  // CRITICAL FIX: Detect current mode and send mode-specific stop command
  const currentMode = AppState.system.currentMode;
  
  if (currentMode === 'tableau') {  // 'tableau' is the sequencer tab name
    // SEQUENCER MODE FIX: Send stopSequence command
    console.log('Stop confirmed (Sequencer mode): Stopping sequence');
    sendCommand(WS_CMD.STOP_SEQUENCE, {});
    
    // Return to start if requested (same as other modes)
    if (shouldReturnToStart) {
      setTimeout(function() {
        console.log('Stop confirmed: Returning to start position');
        sendCommand(WS_CMD.RETURN_TO_START, {});
      }, 500);
    }
  } else if (currentMode === 'chaos') {
    // Chaos mode: send stopChaos command
    console.log('Stop confirmed (Chaos mode): Stopping chaos movement');
    sendCommand(WS_CMD.STOP_CHAOS, {});
    
    // Return to start if requested
    if (shouldReturnToStart) {
      setTimeout(function() {
        console.log('Stop confirmed: Returning to start position');
        sendCommand(WS_CMD.RETURN_TO_START, {});
      }, 500);
    }
  } else if (currentMode === 'oscillation') {
    // Oscillation mode: send stopOscillation command
    console.log('Stop confirmed (Oscillation mode): Stopping oscillation');
    sendCommand(WS_CMD.STOP_OSCILLATION, {});
    
    // Return to start if requested
    if (shouldReturnToStart) {
      setTimeout(function() {
        console.log('Stop confirmed: Returning to start position');
        sendCommand(WS_CMD.RETURN_TO_START, {});
      }, 500);
    }
  } else {
    // Simple mode or others: send generic stop
    console.log('Stop confirmed: Stopping movement');
    sendCommand(WS_CMD.STOP, {});
    
    // Return to start if requested
    if (shouldReturnToStart) {
      setTimeout(function() {
        console.log('Stop confirmed: Returning to start position');
        sendCommand(WS_CMD.RETURN_TO_START, {});
      }, 500);
    }
  }
  
  if (!shouldReturnToStart) {
    console.log('Stop confirmed: Staying at current position');
  }
  
  // Reset checkbox for next time (keep checked by default)
  document.getElementById('returnToStartCheckbox').checked = true;
}

// ============================================================================
// SEQUENCER LIMIT MODAL FUNCTIONS
// ============================================================================

/**
 * Cancel sequencer limit change and return to previous tab
 */
function cancelSequencerLimitChange() {
  document.getElementById('sequencerLimitModal').classList.remove('active');
  
  // Return to previous tab
  const prevTab = AppState.system.currentMode || 'simple';
  setTimeout(function() {
    // Restore previous tab selection
    document.querySelectorAll('.tab').forEach(tab => {
      tab.classList.remove('active');
    });
    document.querySelector('[data-tab="' + prevTab + '"]').classList.add('active');
    
    // Restore previous tab content
    document.querySelectorAll('.mode-content').forEach(content => {
      content.classList.remove('active');
    });
    document.getElementById(prevTab + '-content').classList.add('active');
  }, 10);
}

/**
 * Confirm sequencer limit change and switch to sequencer tab
 */
function confirmSequencerLimitChange() {
  document.getElementById('sequencerLimitModal').classList.remove('active');
  
  // Reset limit to 100%
  sendCommand(WS_CMD.SET_MAX_DISTANCE_LIMIT, { limitPercent: 100 });
  
  // Show confirmation notification
  setTimeout(function() {
    showNotification('✅ Limite réinitialisée à 100% (mode séquenceur)', 3000);
  }, 100);
  
  // Now actually switch to sequencer tab
  // We need to temporarily bypass the check by setting limit to 100 in AppState
  AppState.pursuit.maxDistLimitPercent = 100;
  switchTab('tableau');
}

// ============================================================================
// TAB CLICK HANDLERS INITIALIZATION
// ============================================================================

/**
 * Initialize tab click event listeners
 * Called once on page load
 */
function initUIListeners() {
  console.log('🖥️ Initializing UI listeners...');
  
  // Tab click handlers
  document.querySelectorAll('.tab').forEach(tab => {
    tab.addEventListener('click', function() {
      const targetMode = this.getAttribute('data-tab');
      
      // Don't do anything if clicking on already active tab
      if (targetMode === AppState.system.currentMode) {
        return;
      }
      
      // Check if system is running
      if (isSystemRunning()) {
        // Update modal message based on current mode
        const modalMessage = document.getElementById('modalMessage');
        if (isPursuitActive()) {
          modalMessage.innerHTML = 
            'Le mode poursuite est actuellement actif. Le changement de mode va :<br>' +
            '• Désactiver le mode poursuite<br>' +
            '• Retourner au point de départ pour vérifier le contact<br>' +
            '• Lancer une calibration si nécessaire<br><br>' +
            '<strong>Voulez-vous continuer ?</strong>';
        } else {
          modalMessage.innerHTML = 
            'Une opération est en cours. Le changement de mode va arrêter le mouvement et lancer une recalibration.<br><br>' +
            '<strong>Voulez-vous continuer ?</strong>';
        }
        
        // Show confirmation modal
        AppState.system.pendingModeSwitch = targetMode;
        document.getElementById('modeChangeModal').classList.add('active');
      } else {
        // Safe to switch immediately
        switchTab(targetMode);
      }
    });
  });
  
  // WiFi Config Modal
  const btnEditWifi = document.getElementById('btnEditWifi');
  if (btnEditWifi) {
    btnEditWifi.addEventListener('click', openWifiConfigModal);
  }
  
  const btnSaveWifiConfig = document.getElementById('btnSaveWifiConfig');
  if (btnSaveWifiConfig) {
    btnSaveWifiConfig.addEventListener('click', saveWifiConfig);
  }
  
  const wifiShowPassword = document.getElementById('wifiShowPassword');
  if (wifiShowPassword) {
    wifiShowPassword.addEventListener('change', toggleWifiPasswordVisibility);
  }
  
  console.log('✅ UI listeners initialized');
}

// ============================================================================
// UNIFIED MODAL SYSTEM - Alert & Confirm replacements
// ============================================================================

/**
 * State for the unified modal system
 */
const ModalState = {
  resolveCallback: null,
  rejectCallback: null
};

/**
 * Show a stylized alert modal (replaces native alert())
 * @param {string} message - Message to display
 * @param {Object} options - Optional configuration
 * @param {string} options.title - Modal title (default: based on type)
 * @param {string} options.type - 'info', 'success', 'warning', 'error' (default: 'info')
 * @param {string} options.buttonText - OK button text (default: 'OK')
 * @returns {Promise<void>} Resolves when user closes modal
 */
function showAlert(message, options = {}) {
  return new Promise((resolve) => {
    const {
      title = null,
      type = 'info',
      buttonText = 'OK'
    } = options;
    
    // Determine icon and colors based on type
    const typeConfig = {
      info: { icon: 'ℹ️', color: '#2196F3', bgColor: '#E3F2FD' },
      success: { icon: '✅', color: '#4CAF50', bgColor: '#E8F5E9' },
      warning: { icon: '⚠️', color: '#FF9800', bgColor: '#FFF3E0' },
      error: { icon: '❌', color: '#F44336', bgColor: '#FFEBEE' }
    };
    const config = typeConfig[type] || typeConfig.info;
    
    // Auto-generate title if not provided
    const modalTitle = title || {
      info: 'Information',
      success: 'Succès',
      warning: 'Attention',
      error: 'Erreur'
    }[type] || 'Information';
    
    // Update modal content
    const modal = document.getElementById('unifiedAlertModal');
    document.getElementById('unifiedAlertIcon').textContent = config.icon;
    document.getElementById('unifiedAlertTitle').textContent = modalTitle;
    document.getElementById('unifiedAlertTitle').style.color = config.color;
    document.getElementById('unifiedAlertMessage').innerHTML = message.replace(/\n/g, '<br>');
    document.getElementById('unifiedAlertOkBtn').textContent = buttonText;
    
    // Set callback
    ModalState.resolveCallback = resolve;
    
    // Show modal
    modal.classList.add('active');
  });
}

/**
 * Close alert modal and resolve promise
 */
function closeAlertModal() {
  document.getElementById('unifiedAlertModal').classList.remove('active');
  if (ModalState.resolveCallback) {
    ModalState.resolveCallback();
    ModalState.resolveCallback = null;
  }
}

/**
 * Show a stylized confirm modal (replaces native confirm())
 * @param {string} message - Message to display
 * @param {Object} options - Optional configuration
 * @param {string} options.title - Modal title (default: 'Confirmation')
 * @param {string} options.type - 'info', 'warning', 'danger' (default: 'warning')
 * @param {string} options.confirmText - Confirm button text (default: 'Confirmer')
 * @param {string} options.cancelText - Cancel button text (default: 'Annuler')
 * @param {boolean} options.dangerous - If true, confirm button is red (default: false)
 * @returns {Promise<boolean>} Resolves to true if confirmed, false if cancelled
 */
function showConfirm(message, options = {}) {
  return new Promise((resolve) => {
    const {
      title = 'Confirmation',
      type = 'warning',
      confirmText = 'Confirmer',
      cancelText = 'Annuler',
      dangerous = false
    } = options;
    
    // Determine icon and colors based on type
    const typeConfig = {
      info: { icon: 'ℹ️', color: '#2196F3' },
      warning: { icon: '⚠️', color: '#FF9800' },
      danger: { icon: '🗑️', color: '#F44336' }
    };
    const config = typeConfig[type] || typeConfig.warning;
    
    // Update modal content
    const modal = document.getElementById('unifiedConfirmModal');
    document.getElementById('unifiedConfirmIcon').textContent = config.icon;
    document.getElementById('unifiedConfirmTitle').textContent = title;
    document.getElementById('unifiedConfirmTitle').style.color = config.color;
    document.getElementById('unifiedConfirmMessage').innerHTML = message.replace(/\n/g, '<br>');
    document.getElementById('unifiedConfirmCancelBtn').textContent = cancelText;
    
    const confirmBtn = document.getElementById('unifiedConfirmOkBtn');
    confirmBtn.textContent = confirmText;
    confirmBtn.className = dangerous ? 'button btn-danger' : 'button btn-success';
    
    // Set callback
    ModalState.resolveCallback = resolve;
    
    // Show modal
    modal.classList.add('active');
  });
}

/**
 * Close confirm modal with result
 * @param {boolean} confirmed - Whether user confirmed
 */
function closeConfirmModal(confirmed) {
  document.getElementById('unifiedConfirmModal').classList.remove('active');
  if (ModalState.resolveCallback) {
    ModalState.resolveCallback(confirmed);
    ModalState.resolveCallback = null;
  }
}

// ============================================================================
// WIFI CONFIGURATION MODAL
// ============================================================================

/**
 * Open WiFi configuration modal
 */
function openWifiConfigModal() {
  const modal = document.getElementById('wifiConfigModal');
  
  // Load current EEPROM config
  fetch('/api/wifi/config')
    .then(response => response.json())
    .then(data => {
      const currentConfigDiv = document.getElementById('wifiCurrentSsid');
      if (data.configured && data.ssid) {
        currentConfigDiv.textContent = '✅ ' + data.ssid;
        document.getElementById('wifiEditSsid').value = data.ssid;
      } else {
        currentConfigDiv.innerHTML = '<em style="color: #999;">Aucune configuration en EEPROM</em>';
        document.getElementById('wifiEditSsid').value = '';
      }
      document.getElementById('wifiEditPassword').value = '';
      hideWifiEditStatus();
    })
    .catch(error => {
      console.error('Failed to load WiFi config:', error);
      document.getElementById('wifiCurrentSsid').innerHTML = '<em style="color: #F44336;">❌ Erreur de chargement</em>';
    });
  
  modal.classList.add('active');
}

/**
 * Close WiFi configuration modal
 */
function closeWifiConfigModal() {
  document.getElementById('wifiConfigModal').classList.remove('active');
  document.getElementById('wifiEditSsid').value = '';
  document.getElementById('wifiEditPassword').value = '';
  document.getElementById('wifiShowPassword').checked = false;
  hideWifiEditStatus();
}

/**
 * Save WiFi configuration to EEPROM
 */
function saveWifiConfig() {
  const ssid = document.getElementById('wifiEditSsid').value.trim();
  const password = document.getElementById('wifiEditPassword').value;
  
  // Validation
  if (!ssid) {
    showWifiEditStatus('❌ Le SSID ne peut pas être vide', 'error');
    return;
  }
  
  if (ssid.length > 32) {
    showWifiEditStatus('❌ Le SSID est trop long (max 32 caractères)', 'error');
    return;
  }
  
  if (password.length > 64) {
    showWifiEditStatus('❌ Le mot de passe est trop long (max 64 caractères)', 'error');
    return;
  }
  
  // Disable save button during request
  const saveBtn = document.getElementById('btnSaveWifiConfig');
  saveBtn.disabled = true;
  saveBtn.textContent = '⏳ Enregistrement...';
  
  showWifiEditStatus('💾 Enregistrement en EEPROM...', 'info');
  
  // Send to backend
  fetch('/api/wifi/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, password })
  })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showWifiEditStatus('✅ Configuration enregistrée avec succès !', 'success');
        setTimeout(() => {
          closeWifiConfigModal();
          // Show notification
          if (typeof showNotification === 'function') {
            showNotification('📶 WiFi configuré : ' + ssid, 'info');
          }
        }, 1500);
      } else {
        showWifiEditStatus('❌ Erreur : ' + (data.message || 'Échec de l\'enregistrement'), 'error');
        saveBtn.disabled = false;
        saveBtn.textContent = '💾 Enregistrer';
      }
    })
    .catch(error => {
      console.error('WiFi save error:', error);
      showWifiEditStatus('❌ Erreur de communication avec l\'ESP32', 'error');
      saveBtn.disabled = false;
      saveBtn.textContent = '💾 Enregistrer';
    });
}

/**
 * Show status message in WiFi edit modal
 */
function showWifiEditStatus(message, type) {
  const statusDiv = document.getElementById('wifiEditStatus');
  statusDiv.textContent = message;
  statusDiv.style.display = 'block';
  
  // Color based on type
  const colors = {
    success: { bg: '#E8F5E9', border: '#4CAF50', text: '#2E7D32' },
    error: { bg: '#FFEBEE', border: '#F44336', text: '#C62828' },
    info: { bg: '#E3F2FD', border: '#2196F3', text: '#1565C0' }
  };
  
  const color = colors[type] || colors.info;
  statusDiv.style.background = color.bg;
  statusDiv.style.border = '2px solid ' + color.border;
  statusDiv.style.color = color.text;
}

/**
 * Hide status message in WiFi edit modal
 */
function hideWifiEditStatus() {
  const statusDiv = document.getElementById('wifiEditStatus');
  statusDiv.style.display = 'none';
  statusDiv.textContent = '';
}

/**
 * Toggle password visibility in WiFi modal
 */
function toggleWifiPasswordVisibility() {
  const passwordField = document.getElementById('wifiEditPassword');
  const checkbox = document.getElementById('wifiShowPassword');
  passwordField.type = checkbox.checked ? 'text' : 'password';
}

// Log initialization
console.log('✅ UIController.js loaded - UI control ready');
