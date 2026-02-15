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
    console.debug('Stopping movement before tab switch');
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
      DOM.sequencerLimitModal.classList.add('active');
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
  console.debug('Switched to mode: ' + tabName);
  
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
    
    const oscCenterField = DOM.oscCenter;
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
  console.debug('🔄 Auto-switched to mode: ' + tabName + ' (movement in progress)');
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
  DOM.modeChangeModal.classList.remove('active');
  AppState.system.pendingModeSwitch = null;
  
  // Reset checkbox
  DOM.bypassCalibrationCheckbox.checked = false;
  
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
  const bypassCalibration = DOM.bypassCalibrationCheckbox.checked;
  DOM.modeChangeModal.classList.remove('active');
  
  // If pursuit is active, disable it first
  if (isPursuitActive()) {
    console.debug('Mode change: Disabling pursuit mode first');
    disablePursuitMode();
  }
  
  // Stop movement (for simple mode)
  sendCommand(WS_CMD.STOP, {});
  
  // Wait a bit for stop to complete, then either quick return or full calibration
  setTimeout(function() {
    if (bypassCalibration) {
      // Quick return to start only
      console.debug('Mode change: Quick return to start (bypass full calibration)');
      sendCommand(WS_CMD.RETURN_TO_START, {});
    } else {
      // Full calibration
      console.debug('Mode change: Full calibration');
      sendCommand(WS_CMD.CALIBRATE, {});
    }
    
    // Reset checkbox for next time
    DOM.bypassCalibrationCheckbox.checked = false;
    
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
  DOM.stopModal.classList.add('active');
}

/**
 * Cancel stop and close modal
 */
function cancelStopModal() {
  DOM.stopModal.classList.remove('active');
  // Reset checkbox for next time (keep checked by default)
  DOM.returnToStartCheckbox.checked = true;
}

/**
 * Confirm stop with mode-specific handling
 */
function confirmStopModal() {
  const shouldReturnToStart = DOM.returnToStartCheckbox.checked;
  DOM.stopModal.classList.remove('active');
  
  // Mode-specific stop command lookup
  const STOP_COMMANDS = {
    tableau: WS_CMD.STOP_SEQUENCE,
    chaos: WS_CMD.STOP_CHAOS,
    oscillation: WS_CMD.STOP_OSCILLATION
  };
  
  const currentMode = AppState.system.currentMode;
  const stopCmd = STOP_COMMANDS[currentMode] || WS_CMD.STOP;
  
  console.debug('Stop confirmed (' + currentMode + '): Sending ' + stopCmd);
  sendCommand(stopCmd, {});
  
  if (shouldReturnToStart) {
    setTimeout(function() {
      console.debug('Stop confirmed: Returning to start position');
      sendCommand(WS_CMD.RETURN_TO_START, {});
    }, 500);
  } else {
    console.debug('Stop confirmed: Staying at current position');
  }
  
  // Reset checkbox for next time (keep checked by default)
  DOM.returnToStartCheckbox.checked = true;
}

// ============================================================================
// SEQUENCER LIMIT MODAL FUNCTIONS
// ============================================================================

/**
 * Cancel sequencer limit change and return to previous tab
 */
function cancelSequencerLimitChange() {
  DOM.sequencerLimitModal.classList.remove('active');
  
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
  DOM.sequencerLimitModal.classList.remove('active');
  
  // Reset limit to 100%
  sendCommand(WS_CMD.SET_MAX_DISTANCE_LIMIT, { limitPercent: 100 });
  
  // Show confirmation notification
  setTimeout(function() {
    showNotification('✅ ' + t('sequencer.limitReset'), 3000);
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
  console.debug('🖥️ Initializing UI listeners...');
  
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
            t('modal.pursuitRunning') + '<br>' +
            '• ' + t('modal.pursuitDisable') + '<br>' +
            '• ' + t('modal.pursuitReturn') + '<br>' +
            '• ' + t('modal.pursuitCalibrate') + '<br><br>' +
            '<strong>' + t('modal.modeChangeContinue') + '</strong>';
        } else {
          modalMessage.innerHTML = 
            t('modal.modeChangeRunning') + '<br><br>' +
            '<strong>' + t('modal.modeChangeContinue') + '</strong>';
        }
        
        // Show confirmation modal
        AppState.system.pendingModeSwitch = targetMode;
        DOM.modeChangeModal.classList.add('active');
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
  
  console.debug('✅ UI listeners initialized');
}

// ============================================================================
// UNIFIED MODAL SYSTEM - Alert & Confirm replacements
// ============================================================================

/**
 * State for the unified modal system
 */
const ModalState = {
  alertResolveCallback: null,
  confirmResolveCallback: null,
  promptResolveCallback: null,
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
      info: t('common.info'),
      success: t('common.success'),
      warning: t('common.warning'),
      error: t('common.error')
    }[type] || t('common.info');
    
    // Update modal content
    DOM.unifiedAlertIcon.textContent = config.icon;
    DOM.unifiedAlertTitle.textContent = modalTitle;
    DOM.unifiedAlertTitle.style.color = config.color;
    DOM.unifiedAlertMessage.innerHTML = message.replace(/\n/g, '<br>');
    DOM.unifiedAlertOkBtn.textContent = buttonText;
    
    // Set callback
    ModalState.alertResolveCallback = resolve;
    
    // Show modal
    DOM.unifiedAlertModal.classList.add('active');
  });
}

/**
 * Close alert modal and resolve promise
 */
function closeAlertModal() {
  DOM.unifiedAlertModal.classList.remove('active');
  if (ModalState.alertResolveCallback) {
    ModalState.alertResolveCallback();
    ModalState.alertResolveCallback = null;
  }
}

/**
 * Show a stylized confirm modal (replaces native confirm())
 * @param {string} message - Message to display
 * @param {Object} options - Optional configuration
 * @param {string} options.title - Modal title (default: 'Confirmation')
 * @param {string} options.type - 'info', 'warning', 'danger' (default: 'warning')
 * @param {string} options.confirmText - Confirm button text (default: t('common.confirm'))
 * @param {string} options.cancelText - Cancel button text (default: t('common.cancel'))
 * @param {boolean} options.dangerous - If true, confirm button is red (default: false)
 * @returns {Promise<boolean>} Resolves to true if confirmed, false if cancelled
 */
function showConfirm(message, options = {}) {
  return new Promise((resolve) => {
    const {
      title = t('common.confirmation'),
      type = 'warning',
      confirmText = t('common.confirm'),
      cancelText = t('common.cancel'),
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
    DOM.unifiedConfirmIcon.textContent = config.icon;
    DOM.unifiedConfirmTitle.textContent = title;
    DOM.unifiedConfirmTitle.style.color = config.color;
    DOM.unifiedConfirmMessage.innerHTML = message.replace(/\n/g, '<br>');
    DOM.unifiedConfirmCancelBtn.textContent = cancelText;
    
    const confirmBtn = DOM.unifiedConfirmOkBtn;
    confirmBtn.textContent = confirmText;
    confirmBtn.className = dangerous ? 'button btn-danger' : 'button btn-success';
    
    // Set callback
    ModalState.confirmResolveCallback = resolve;
    
    // Show modal
    DOM.unifiedConfirmModal.classList.add('active');
  });
}

/**
 * Close confirm modal with result
 * @param {boolean} confirmed - Whether user confirmed
 */
function closeConfirmModal(confirmed) {
  DOM.unifiedConfirmModal.classList.remove('active');
  if (ModalState.confirmResolveCallback) {
    ModalState.confirmResolveCallback(confirmed);
    ModalState.confirmResolveCallback = null;
  }
}

/**
 * Show a stylized prompt modal (replaces native prompt())
 * @param {string} message - Message to display
 * @param {Object} options - Optional configuration
 * @param {string} options.title - Modal title (default: 'Input')
 * @param {string} options.defaultValue - Default input value
 * @param {string} options.placeholder - Input placeholder text
 * @param {string} options.okText - OK button text (default: 'OK')
 * @param {string} options.cancelText - Cancel button text
 * @returns {Promise<string|null>} Resolves to input value or null if cancelled
 */
function showPrompt(message, options = {}) {
  return new Promise((resolve) => {
    const {
      title = '✏️ ' + t('common.input'),
      defaultValue = '',
      placeholder = '',
      okText = 'OK',
      cancelText = t('common.cancel')
    } = options;

    DOM.unifiedPromptIcon.textContent = '✏️';
    DOM.unifiedPromptTitle.textContent = title;
    DOM.unifiedPromptMessage.textContent = message;
    DOM.unifiedPromptInput.value = defaultValue;
    DOM.unifiedPromptInput.placeholder = placeholder;
    DOM.unifiedPromptOkBtn.textContent = okText;
    DOM.unifiedPromptCancelBtn.textContent = cancelText;

    ModalState.promptResolveCallback = resolve;

    DOM.unifiedPromptModal.classList.add('active');
    // Focus and select input for quick editing
    setTimeout(() => {
      DOM.unifiedPromptInput.focus();
      DOM.unifiedPromptInput.select();
    }, 100);
  });
}

/**
 * Close prompt modal with result
 * @param {string|null} value - Input value or null if cancelled
 */
function closePromptModal(value) {
  DOM.unifiedPromptModal.classList.remove('active');
  if (ModalState.promptResolveCallback) {
    ModalState.promptResolveCallback(value);
    ModalState.promptResolveCallback = null;
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
  fetchWithRetry('/api/wifi/config', {}, { maxRetries: 2, silent: true })
    .then(response => response.json())
    .then(data => {
      const currentConfigDiv = document.getElementById('wifiCurrentSsid');
      if (data.configured && data.ssid) {
        currentConfigDiv.textContent = '✅ ' + data.ssid;
        document.getElementById('wifiEditSsid').value = data.ssid;
      } else {
        currentConfigDiv.innerHTML = '<em style="color: #999;">' + t('wifi.notConnected') + '</em>';
        document.getElementById('wifiEditSsid').value = '';
      }
      document.getElementById('wifiEditPassword').value = '';
      hideWifiEditStatus();
    })
    .catch(error => {
      console.error('Failed to load WiFi config:', error);
      document.getElementById('wifiCurrentSsid').innerHTML = '<em style="color: #F44336;">❌ ' + t('tools.loadError') + '</em>';
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
    showWifiEditStatus('❌ ' + t('wifi.ssidEmpty'), 'error');
    return;
  }
  
  if (ssid.length > 32) {
    showWifiEditStatus('❌ ' + t('wifi.ssidTooLong'), 'error');
    return;
  }
  
  if (password.length > 64) {
    showWifiEditStatus('❌ ' + t('wifi.passwordTooLong'), 'error');
    return;
  }
  
  // Disable save button during request
  const saveBtn = document.getElementById('btnSaveWifiConfig');
  saveBtn.disabled = true;
  saveBtn.textContent = '⏳ ' + t('common.loading');
  
  showWifiEditStatus('💾 ' + t('common.save') + '...', 'info');
  
  // Send to backend
  fetchWithRetry('/api/wifi/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, password })
  }, { maxRetries: 2, silent: true })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showWifiEditStatus('✅ ' + t('wifi.configSaved'), 'success');
        setTimeout(() => {
          closeWifiConfigModal();
          // Show notification
          if (typeof showNotification === 'function') {
            showNotification('📶 ' + t('wifi.wifiConfigured', {ssid: ssid}), 'info');
          }
        }, 1500);
      } else {
        showWifiEditStatus('❌ ' + t('wifi.saveFailed', {msg: data.message || ''}), 'error');
        saveBtn.disabled = false;
        saveBtn.textContent = '💾 ' + t('common.save');
      }
    })
    .catch(error => {
      console.error('WiFi save error:', error);
      showWifiEditStatus('❌ ' + t('common.error'), 'error');
      saveBtn.disabled = false;
      saveBtn.textContent = '💾 ' + t('common.save');
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
console.debug('✅ UIController.js loaded - UI control ready');
