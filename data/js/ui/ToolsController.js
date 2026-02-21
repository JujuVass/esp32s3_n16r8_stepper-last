/**
 * ToolsController.js - Common Tools Control Module
 * 
 * Handles all common toolbar functionality:
 * - Calibration button
 * - Reset distance button
 * - Logs panel (show/hide, clear, file list, debug level)
 * - System panel (show/hide, WiFi reconnect, reboot)
 * - Logging preferences (enable/disable, debug level)
 * 
 * Note: Stats panel moved to StatsController.js (December 2025)
 * 
 * Dependencies: DOM, AppState, WS_CMD, sendCommand, showNotification, connectWebSocket
 */

// ============================================================================
// PANEL MANAGEMENT - Exclusive panels (only one open at a time)
// ============================================================================

/**
 * Close all tool panels (Logs, System, Stats)
 * Used to implement exclusive panel behavior (accordion-style)
 * @param {string} exceptPanel - Panel ID to keep open ('logs', 'system', 'stats', or null)
 */
function closeAllToolPanels(exceptPanel = null) {
  if (exceptPanel !== 'logs') {
    closeLogsPanel();
  }
  if (exceptPanel !== 'system') {
    closeSystemPanel();
  }
  if (exceptPanel !== 'stats' && typeof closeStatsPanel === 'function') {
    closeStatsPanel();
  }
}

// ============================================================================
// CALIBRATION & RESET
// ============================================================================

/**
 * Trigger motor calibration
 */
function calibrateMotor() {
  sendCommand(WS_CMD.CALIBRATE);
}

/**
 * Reset total distance counter
 */
async function resetTotalDistance() {
  const confirmed = await showConfirm(t('tools.resetDistanceConfirm'), {
    title: t('tools.resetDistanceTitle'),
    type: 'warning',
    confirmText: t('common.reset'),
    dangerous: true
  });
  if (confirmed) {
    sendCommand(WS_CMD.RESET_TOTAL_DISTANCE);
  }
}

// ============================================================================
// LOGS PANEL MANAGEMENT
// ============================================================================

/**
 * Toggle logs panel visibility
 */
function toggleLogsPanel() {
  const panel = DOM.logsPanel;
  const btn = DOM.btnShowLogs;
  
  if (panel.style.display === 'none') {
    // Close other panels first (accordion behavior)
    closeAllToolPanels('logs');
    
    panel.style.display = 'block';
    btn.innerHTML = '📋 ' + t('status.logs');
    btn.style.background = '#e74c3c';
    btn.style.color = 'white';
    
    // Load log files list
    loadLogFilesList();
    
    // Load current debug level state
    getWithRetry('/api/system/logging/preferences', { silent: true })
      .then(data => {
        const chkDebug = DOM.debugLevelCheckbox;
        if (chkDebug) {
          chkDebug.checked = (data.logLevel === 3); // DEBUG = 3
        }
      })
      .catch(error => {
        console.error('❌ Error loading log level:', error);
      });
  } else {
    closeLogsPanel();
  }
}

/**
 * Close logs panel
 */
function closeLogsPanel() {
  DOM.logsPanel.style.display = 'none';
  DOM.btnShowLogs.innerHTML = '📋 ' + t('status.logs');
  DOM.btnShowLogs.style.background = '#17a2b8';
  DOM.btnShowLogs.style.color = 'white';
}

/**
 * Clear logs console panel
 */
function clearLogsPanel() {
  const logEl = DOM.logConsolePanel;
  if (logEl) logEl.textContent = t('tools.logsCleared');
}

/**
 * Handle debug level checkbox change in logs panel
 */
function handleDebugLevelChange(isChecked) {
  // Save debug level preference to EEPROM via API
  const preferences = {
    loggingEnabled: true,  // Keep logging enabled
    logLevel: isChecked ? 3 : 2  // 3=DEBUG, 2=INFO
  };
  
  postWithRetry('/api/system/logging/preferences', preferences, { silent: true })
    .then(data => {
      console.debug('💾 Log level saved:', isChecked ? 'DEBUG' : 'INFO');
      
      // Also update the checkbox in Sys panel
      const chkDebug = DOM.chkDebugLevel;
      if (chkDebug) {
        chkDebug.checked = isChecked;
      }
    })
    .catch(error => {
      console.error('❌ Error saving log level:', error);
    });
}

/**
 * Clear all log files
 */
async function clearAllLogFiles() {
  const confirmed = await showConfirm(t('tools.deleteAllLogs'), {
    title: t('tools.deleteLogsTitle'),
    type: 'danger',
    confirmText: '🗑️ ' + t('common.delete'),
    dangerous: true
  });
  
  if (confirmed) {
    postWithRetry('/logs/clear', {})
      .then(data => {
        showAlert(data.message || t('tools.logsDeletedSuccess'), { type: 'success', title: t('common.success') });
        loadLogFilesList();  // Refresh list
      })
      .catch(error => {
        console.error('Error deleting logs:', error);
        showAlert(t('common.error') + ': ' + error, { type: 'error' });
      });
  }
}

/**
 * Load and display log files list
 */
async function loadLogFilesList() {
  try {
    const response = await fetchWithRetry('/logs', {}, { silent: true });
    const html = await response.text();
    
    // Parse HTML to extract file links
    const parser = new DOMParser();
    const doc = parser.parseFromString(html, 'text/html');
    const links = doc.querySelectorAll('a');
    
    if (links.length === 0) {
      DOM.logFilesList.innerHTML = '<div class="text-light italic text-sm">' + t('tools.noLogFiles') + '</div>';
      return;
    }
    
    // Create container with DOM methods (XSS safe)
    const container = document.createElement('div');
    container.style.cssText = 'display: flex; flex-direction: column; gap: 5px;';
    
    links.forEach(link => {
      const filename = link.textContent;
      const url = link.href;
      
      // Create item div
      const itemDiv = document.createElement('div');
      itemDiv.className = 'preset-item-box flex-between';
      
      // Create filename span (safe from XSS with textContent)
      const filenameSpan = document.createElement('span');
      filenameSpan.className = 'font-mono text-md';
      filenameSpan.textContent = filename;  // Safe: uses textContent instead of innerHTML
      
      // Create download link
      const downloadLink = document.createElement('a');
      downloadLink.href = url;
      downloadLink.target = '_blank';
      downloadLink.style.cssText = 'background: #2196F3; color: white; padding: 4px 10px; border-radius: 3px; text-decoration: none; font-size: 11px;';
      downloadLink.textContent = '📥 ' + t('tools.downloadBtn');
      
      itemDiv.appendChild(filenameSpan);
      itemDiv.appendChild(downloadLink);
      container.appendChild(itemDiv);
    });
    
    DOM.logFilesList.innerHTML = '';  // Clear first
    DOM.logFilesList.appendChild(container);
  } catch (error) {
    console.error('Error loading log files:', error);
    DOM.logFilesList.innerHTML = '<div class="text-error text-sm">' + t('tools.loadError') + '</div>';
  }
}

// ============================================================================
// SYSTEM PANEL MANAGEMENT
// ============================================================================

/**
 * Toggle system panel visibility
 */
function toggleSystemPanel() {
  const panel = DOM.systemPanel;
  const btn = DOM.btnShowSystem;
  
  if (panel.style.display === 'none') {
    // Close other panels first (accordion behavior)
    closeAllToolPanels('system');
    
    panel.style.display = 'block';
    btn.innerHTML = '⚙️ ' + t('status.sys');
    btn.style.background = '#e74c3c';
    btn.style.color = 'white';
    
    // Enable system stats in backend (same as Stats panel)
    sendCommand(WS_CMD.REQUEST_STATS, { enable: true });
    console.debug('📊 System stats requested from backend');
    
    // Request system status to populate fields
    sendCommand(WS_CMD.GET_STATUS, {});
    
    // Load logging preferences when opening
    loadLoggingPreferences();
  } else {
    closeSystemPanel();
  }
}

/**
 * Close system panel
 */
function closeSystemPanel() {
  const panel = DOM.systemPanel;
  const btn = DOM.btnShowSystem;
  
  panel.style.display = 'none';
  btn.innerHTML = '⚙️ ' + t('status.sys');
  btn.style.background = '#2196F3';
  btn.style.color = 'white';
  
  // Disable system stats when closing (optional optimization)
  sendCommand(WS_CMD.REQUEST_STATS, { enable: false });
}

/**
 * Refresh WiFi connection
 */
async function refreshWifi() {
  const confirmed = await showConfirm('📶 ' + t('tools.reconnectWifi'), {
    title: t('tools.reconnectWifiTitle'),
    type: 'info',
    confirmText: t('tools.reconnectBtn')
  });
  
  if (confirmed) {
    const btn = DOM.btnRefreshWifi;
    const originalText = btn.innerHTML;
    
    // Set flag to prevent WebSocket auto-reconnect during WiFi refresh
    AppState.wifiReconnectInProgress = true;
    
    // Disable button and show loading
    btn.disabled = true;
    btn.innerHTML = '⏳';
    btn.style.opacity = '0.5';
    
    // Show persistent modal during reconnection (like calibration)
    const modal = DOM.unifiedAlertModal;
    const modalIcon = DOM.unifiedAlertIcon;
    const modalTitle = DOM.unifiedAlertTitle;
    const modalMessage = DOM.unifiedAlertMessage;
    const modalButton = DOM.unifiedAlertOkBtn;
    
    modalIcon.textContent = '📶';
    modalTitle.textContent = t('tools.reconnectWifiTitle');
    modalMessage.innerHTML = t('tools.reconnecting') + '<br><br><span id="wifiReconnectStatus">' + t('tools.sendingCommand') + '</span>';
    modalButton.style.display = 'none'; // Hide button during process
    modal.classList.add('active');
    
    const statusSpan = document.getElementById('wifiReconnectStatus');
    
    console.debug('📶 Sending WiFi reconnect command...');
    
    // Close existing WebSocket BEFORE sending command (prevent auto-reconnect spam)
    if (AppState.ws) {
      console.debug('📶 Closing existing WebSocket for clean reconnect...');
      AppState.ws.close();
      AppState.ws = null;
    }
    
    // Send reconnect command (expect network error as WiFi disconnects)
    fetch('/api/system/wifi/reconnect', { method: 'POST', signal: AbortSignal.timeout(5000) })
      .then(response => response.json())
      .then(data => {
        console.debug('📶 WiFi reconnect command acknowledged:', data);
        statusSpan.textContent = t('tools.wifiDisconnecting');
      })
      .catch(error => {
        // Expected error: network will be interrupted during WiFi reconnect
        console.debug('📶 WiFi reconnect in progress (network interruption expected)');
        statusSpan.textContent = t('tools.wifiDisconnecting');
      });
    
    // Wait for WiFi to reconnect, then verify connection
    let attempts = 0;
    const maxAttempts = 15;
    const retryDelay = 2000; // 2 seconds between attempts
    
    const checkConnection = function() {
      attempts++;
      console.debug(`📶 Checking connection (attempt ${attempts}/${maxAttempts})...`);
      statusSpan.textContent = t('tools.checkingConnection', {attempts: attempts, max: maxAttempts});
      
      fetch('/api/ping', { method: 'GET', signal: AbortSignal.timeout(3000) })
        .then(response => {
          if (response.ok) {
            console.debug('✅ WiFi reconnected successfully!');
            
            // Re-enable WebSocket auto-reconnect
            AppState.wifiReconnectInProgress = false;
            
            // Update modal to success
            modalIcon.textContent = '✅';
            modalTitle.textContent = t('tools.reconnectSuccess');
            modalMessage.innerHTML = t('tools.wifiReconnected');
            modalButton.textContent = 'OK';
            modalButton.style.display = '';
            modalButton.onclick = function() { modal.classList.remove('active'); };
            
            btn.disabled = false;
            btn.innerHTML = '✅';
            btn.style.opacity = '1';
            
            // Reconnect WebSocket NOW
            if (typeof connectWebSocket === 'function') {
              console.debug('📶 Reconnecting WebSocket...');
              connectWebSocket();
            }
            
            // Reset button after 2 seconds
            setTimeout(function() {
              btn.innerHTML = originalText;
            }, 2000);
            
            // Auto-close modal after 3 seconds
            setTimeout(function() {
              modal.classList.remove('active');
            }, 3000);
          } else {
            throw new Error('Ping failed');
          }
        })
        .catch(error => {
          if (attempts < maxAttempts) {
            setTimeout(checkConnection, retryDelay);
          } else {
            console.error('❌ WiFi reconnect verification failed');
            
            // Re-enable WebSocket auto-reconnect even on failure
            AppState.wifiReconnectInProgress = false;
            
            // Update modal to error
            modalIcon.textContent = '❌';
            modalTitle.textContent = t('tools.reconnectFailed');
            modalMessage.innerHTML = t('tools.reconnectFailedMsg');
            modalButton.textContent = t('common.close');
            modalButton.style.display = '';
            modalButton.onclick = function() { modal.classList.remove('active'); };
            
            btn.disabled = false;
            btn.innerHTML = '❌';
            btn.style.opacity = '1';
            
            // Try to reconnect WebSocket anyway
            if (typeof connectWebSocket === 'function') {
              connectWebSocket();
            }
            
            // Reset button after 3 seconds
            setTimeout(function() {
              btn.innerHTML = originalText;
            }, 3000);
          }
        });
    };
    
    // Start checking after 3 seconds (give WiFi time to disconnect/reconnect)
    setTimeout(function() {
      statusSpan.textContent = t('tools.waitingReconnect');
      setTimeout(checkConnection, 1000);
    }, 2000);
  }
}

/**
 * Reboot ESP32
 */
async function rebootESP32() {
  const confirmed = await showConfirm(t('tools.rebootConfirm'), {
    title: t('tools.rebootTitle'),
    type: 'warning',
    confirmText: '🔄 ' + t('tools.rebootBtn'),
    dangerous: true
  });
  
  if (confirmed) {
    // Show reboot overlay
    DOM.rebootOverlay.style.display = 'flex';
    
    // Send reboot command
    fetch('/api/system/reboot', { method: 'POST', signal: AbortSignal.timeout(5000) })
      .then(response => response.json())
      .then(data => {
        console.debug('🔄 Reboot command sent:', data);
      })
      .catch(error => {
        console.debug('🔄 Reboot initiated (connection lost as expected)');
      });
    
    // Close WebSocket properly
    if (AppState.ws?.readyState === WebSocket.OPEN) {
      AppState.ws.close();
    }
    
    // Wait 10 seconds then try to reconnect
    setTimeout(function() {
      reconnectAfterReboot();
    }, 10000);
  }
}

/**
 * Reconnect after reboot with retry logic
 */
function reconnectAfterReboot() {
  let attempts = 0;
  const maxAttempts = 30; // Try for 30 seconds
  let wsConnected = false;
  
  const updateRebootStatus = function(message, subMessage) {
    const msgEl = DOM.rebootMessage;
    const statusEl = DOM.rebootStatus;
    if (msgEl) msgEl.textContent = message;
    if (statusEl) statusEl.textContent = subMessage || '';
  };
  
  const tryReconnect = function() {
    attempts++;
    console.debug('🔄 Reconnection attempt ' + attempts + '/' + maxAttempts);
    updateRebootStatus(t('tools.reconnectAttempt'), t('tools.reconnectAttemptSub', {attempts: attempts, max: maxAttempts}));
    
    // Test HTTP connection first using ping endpoint
    fetch('/api/ping', { 
      method: 'GET',
      cache: 'no-cache',
      signal: AbortSignal.timeout(3000)
    })
      .then(response => {
        if (!response.ok) throw new Error('HTTP not ready');
        return response.json();
      })
      .then(data => {
        console.debug('✅ HTTP connection restored! Uptime:', data.uptime, 'ms');
        updateRebootStatus(t('tools.httpOkWsConnecting'), '🌐 ...');
        
        // Now try WebSocket connection
        if (!wsConnected) {
          console.debug('🔌 Attempting WebSocket reconnection...');
          
          // Try to reconnect WebSocket
          try {
            connectWebSocket();
            
            // Wait a bit to see if WebSocket connects
            setTimeout(function() {
              if (AppState.ws?.readyState === WebSocket.OPEN) {
                wsConnected = true;
                console.debug('✅ WebSocket reconnected!');
                updateRebootStatus(t('tools.connectionRestored'), '✅ ' + t('tools.reloadingPage'));
                
                // Both HTTP and WS are connected - wait 2 more seconds for stability
                console.debug('⏳ Waiting for system stability...');
                setTimeout(function() {
                  console.debug('✅ System stable - reloading page...');
                  DOM.rebootOverlay.style.display = 'none';
                  location.reload(true); // Force reload from server
                }, 2000);
              } else {
                // WebSocket not ready yet, keep trying
                console.debug('⚠️ WebSocket not ready, retrying...');
                if (attempts < maxAttempts) {
                  setTimeout(tryReconnect, 1000);
                } else {
                  DOM.rebootOverlay.style.display = 'none';
                  showAlert(t('tools.httpOkWsFailed'), { type: 'error' });
                }
              }
            }, 1500);
            
          } catch (err) {
            console.error('❌ WebSocket connection error:', err);
            if (attempts < maxAttempts) {
              setTimeout(tryReconnect, 1000);
            } else {
              DOM.rebootOverlay.style.display = 'none';
              showAlert(t('tools.wsReconnectFailed'), { type: 'error' });
            }
          }
        }
      })
      .catch(error => {
        console.debug('⚠️ ESP32 not ready yet:', error.message);
        if (attempts < maxAttempts) {
          setTimeout(tryReconnect, 1000);
        } else {
          DOM.rebootOverlay.style.display = 'none';
          showAlert(t('tools.espReconnectFailed'), { type: 'error' });
        }
      });
  };
  
  tryReconnect();
}

// ============================================================================
// LOGGING PREFERENCES - EEPROM
// ============================================================================

/**
 * Load logging preferences from ESP32
 */
function loadLoggingPreferences() {
  getWithRetry('/api/system/logging/preferences', { silent: true })
    .then(data => {
      // Set checkboxes
      const chkEnabled = DOM.chkLoggingEnabled;
      const chkDebug = DOM.chkDebugLevel;
      const btnShowLogs = DOM.btnShowLogs;
      
      chkEnabled.checked = data.loggingEnabled;
      chkDebug.checked = (data.logLevel === 3); // LOG_DEBUG = 3
      
      // Update AppState.logging for console wrapper FIRST
      AppState.logging.enabled = data.loggingEnabled;
      AppState.logging.debugEnabled = (data.logLevel === 3);
      
      // Hide/show Logs button based on logging enabled state
      if (data.loggingEnabled) {
        btnShowLogs.style.display = '';
      } else {
        btnShowLogs.style.display = 'none';
      }
      
      // Debug log (only if enabled)
      if (data.loggingEnabled) {
        console.debug('📂 Loaded logging preferences:', data);
      }
    })
    .catch(error => {
      console.error('❌ Error loading logging preferences:', error);
    });
}

/**
 * Save logging preferences to ESP32 EEPROM
 */
function saveLoggingPreferences() {
  const chkEnabled = DOM.chkLoggingEnabled;
  const chkDebug = DOM.chkDebugLevel;
  
  const preferences = {
    loggingEnabled: chkEnabled.checked,
    logLevel: chkDebug.checked ? 3 : 2  // 3=DEBUG, 2=INFO
  };
  
  // Update AppState.logging for console wrapper
  AppState.logging.enabled = preferences.loggingEnabled;
  AppState.logging.debugEnabled = (preferences.logLevel === 3);
  
  postWithRetry('/api/system/logging/preferences', preferences)
    .then(data => {
      console.debug('💾 Logging preferences saved:', data);
      
      // Update Logs button visibility
      const btnShowLogs = DOM.btnShowLogs;
      if (preferences.loggingEnabled) {
        btnShowLogs.style.display = '';
      } else {
        btnShowLogs.style.display = 'none';
      }
    })
    .catch(error => {
      console.error('❌ Error saving logging preferences:', error);
      showAlert(t('tools.logPrefSaveError'), { type: 'error' });
    });
}

// ============================================================================
// SYSTEM STATS UPDATE (ESP32 Hardware Stats)
// ============================================================================

/**
 * Update system hardware statistics display (CPU, RAM, WiFi, etc.)
 * @param {Object} system - System stats object from backend status message
 */
function updateSystemStats(system) {
  // Defense: Check system object exists before accessing fields
  if (!system) {
    console.warn('updateSystemStats: system object is undefined');
    return;
  }
  
  // CPU frequency
  if (system.cpuFreqMHz !== undefined) {
    DOM.sysCpuFreq.textContent = system.cpuFreqMHz + ' MHz';
  }
  
  // Temperature
  if (system.temperatureC !== undefined) {
    const temp = Number.parseFloat(system.temperatureC);
    DOM.sysTemp.textContent = temp.toFixed(1) + ' °C';
    // Color coding based on temperature
    if (temp > 80) {
      DOM.sysTemp.style.color = '#f44336'; // Red - hot
    } else if (temp > 70) {
      DOM.sysTemp.style.color = '#FF9800'; // Orange - warm
    } else {
      DOM.sysTemp.style.color = '#333'; // Normal
    }
  }
  
  // RAM
  if (system.heapFree !== undefined && system.heapTotal !== undefined && system.heapUsedPercent !== undefined) {
    const ramFreeKB = (system.heapFree / 1024).toFixed(1);
    const ramTotalKB = (system.heapTotal / 1024).toFixed(1);
    const ramUsedPercent = Number.parseFloat(system.heapUsedPercent);
    DOM.sysRam.textContent = ramFreeKB + ' KB ' + t('tools.free') + ' / ' + ramTotalKB + ' KB';
    DOM.sysRamPercent.textContent = ramUsedPercent.toFixed(1) + '% ' + t('tools.used');
  }
  
  // PSRAM
  if (system.psramFree !== undefined && system.psramTotal !== undefined && system.psramUsedPercent !== undefined) {
    const psramFreeMB = (system.psramFree / 1024 / 1024).toFixed(1);
    const psramTotalMB = (system.psramTotal / 1024 / 1024).toFixed(1);
    const psramUsedPercent = Number.parseFloat(system.psramUsedPercent);
    DOM.sysPsram.textContent = psramFreeMB + ' MB ' + t('tools.free') + ' / ' + psramTotalMB + ' MB';
    DOM.sysPsramPercent.textContent = psramUsedPercent.toFixed(1) + '% ' + t('tools.used');
  }
  
  // WiFi - delegate to pure function
  if (system.wifiRssi !== undefined) {
    const rssi = system.wifiRssi;
    DOM.sysWifi.textContent = rssi + ' dBm';
    
    let quality, qualityColor;
    if (rssi >= -50) { quality = t('wifi.qualityExcellent'); qualityColor = '#4CAF50'; }
    else if (rssi >= -60) { quality = t('wifi.qualityVeryGood'); qualityColor = '#8BC34A'; }
    else if (rssi >= -70) { quality = t('wifi.qualityGood'); qualityColor = '#FFC107'; }
    else if (rssi >= -80) { quality = t('wifi.qualityWeak'); qualityColor = '#FF9800'; }
    else { quality = t('wifi.qualityVeryWeak'); qualityColor = '#f44336'; }
    
    DOM.sysWifiQuality.textContent = quality;
    DOM.sysWifiQuality.style.color = qualityColor;
  }
  
  // Uptime - delegate to pure function
  if (system.uptimeSeconds !== undefined) {
    const uptimeSec = system.uptimeSeconds;
    const hours = Math.floor(uptimeSec / 3600);
    const minutes = Math.floor((uptimeSec % 3600) / 60);
    const seconds = uptimeSec % 60;
    let uptimeStr;
    if (hours > 0) {
      uptimeStr = `${hours}h ${minutes}m ${seconds}s`;
    } else if (minutes > 0) {
      uptimeStr = `${minutes}m ${seconds}s`;
    } else {
      uptimeStr = `${seconds}s`;
    }
    DOM.sysUptime.textContent = uptimeStr;
  }
  
  // Network info (IP addresses, hostname)
  if (system.ipSta !== undefined) {
    DOM.sysIpSta.textContent = system.ipSta;
    // Gray out if degraded mode (0.0)
    DOM.sysIpSta.style.color = (system.ipSta === '0.0') ? '#999' : '#333';
    
    // Cache IP for faster WebSocket reconnection (avoids mDNS resolution delay)
    // Only cache if on same subnet as current page (prevents STA IP on AP network)
    if (system.ipSta !== '0.0' && !AppState.espIpAddress) {
      if (typeof isSameSubnet === 'function' && isSameSubnet(system.ipSta, globalThis.location.hostname)) {
        AppState.espIpAddress = system.ipSta;
        console.debug('📍 Cached ESP32 IP for WS reconnection:', system.ipSta);
      }
    }
  }
  if (system.ipAp !== undefined) {
    DOM.sysIpAp.textContent = system.ipAp;
  }
  if (system.hostname !== undefined) {
    DOM.sysHostname.textContent = system.hostname + '.local';
    DOM.sysHostname.title = 'http://' + system.hostname + '.local';
    // Disable mDNS link in AP mode
    if (system.apMode) {
      DOM.sysHostname.style.color = '#999';
      DOM.sysHostname.title = t('wifi.mdnsUnavailable');
    } else {
      DOM.sysHostname.style.color = '#2196F3';
    }
  }
  if (system.ssid !== undefined) {
    DOM.sysSsid.textContent = system.ssid || t('wifi.notConnected');
  }
  if (system.apClients !== undefined) {
    DOM.sysApClients.textContent = system.apClients;
    DOM.sysApClients.style.color = system.apClients > 0 ? '#4CAF50' : '#999';
  }
  
  // AP Mode indicator
  if (system.apMode !== undefined) {
    if (system.apMode) {
      DOM.sysDegradedBadge.style.display = '';
      DOM.sysDegradedBadge.textContent = t('tools.apModeBadge');
      DOM.networkInfoSection.style.borderLeftColor = '#FF9800';
      DOM.networkInfoSection.style.background = '#fff3e0';
    } else {
      DOM.sysDegradedBadge.style.display = 'none';
      DOM.networkInfoSection.style.borderLeftColor = '#2196F3';
      DOM.networkInfoSection.style.background = '#e3f2fd';
    }
  }
}

// ============================================================================
// SPEED LIMITS INITIALIZATION
// ============================================================================

/**
 * Global max speed level (must match .ino MAX_SPEED_LEVEL constant)
 */
const maxSpeedLevel = 35;

/**
 * Speed conversion factors (must match .ino Config.h constants)
 * SPEED_LEVEL_TO_MM_S: speedLevel × factor = mm/s (simple, chaos, pursuit)
 * OSC_SPEED_MULTIPLIER: maxSpeedLevel × factor = oscillation max speed mm/s
 */
const SPEED_LEVEL_TO_MM_S = 10;  // Config.h: SPEED_LEVEL_TO_MM_S
const OSC_SPEED_MULTIPLIER = 20; // Config.h: OSC_MAX_SPEED_MM_S = MAX_SPEED_LEVEL * 20

/**
 * Initialize speed input max attributes based on maxSpeedLevel
 * Called on page load to set consistent speed limits across all inputs
 */
function initSpeedLimits() {
  const speedInputs = [
    'speedUnified',
    'speedForward', 
    'speedBackward',
    'pursuitMaxSpeed',
    'chaosMaxSpeed',
    'editSpeedFwd',
    'editSpeedBack',
    'editChaosSpeed'
  ];
  
  speedInputs.forEach(id => {
    const input = document.getElementById(id);
    if (input) {
      input.setAttribute('max', maxSpeedLevel);
    }
  });
  
  // Update labels that show the max value
  const maxLabels = document.querySelectorAll('.unit-label');
  maxLabels.forEach(label => {
    if (label.textContent.includes('0-20')) {
      label.textContent = `(0-${maxSpeedLevel})`;
    }
    if (label.textContent.includes('/20')) {
      label.textContent = label.textContent.replace('/20', `/${maxSpeedLevel}`);
    }
  });
}

// ============================================================================
// INITIALIZE TOOLS LISTENERS
// ============================================================================

/**
 * Initialize all Tools event listeners
 * Called once on page load
 */
function initToolsListeners() {
  console.debug('🔧 Initializing Tools listeners...');
  
  // ===== CALIBRATION & RESET =====
  DOM.btnCalibrateCommon.addEventListener('click', calibrateMotor);
  DOM.btnResetDistanceCommon.addEventListener('click', resetTotalDistance);
  
  // ===== LOGS PANEL =====
  DOM.btnShowLogs.addEventListener('click', toggleLogsPanel);
  DOM.btnCloseLogs.addEventListener('click', closeLogsPanel);
  DOM.btnClearLogsPanel.addEventListener('click', clearLogsPanel);
  DOM.debugLevelCheckbox.addEventListener('change', function() {
    handleDebugLevelChange(this.checked);
  });
  DOM.btnClearAllLogFiles.addEventListener('click', clearAllLogFiles);
  
  // ===== SYSTEM PANEL =====
  DOM.btnShowSystem.addEventListener('click', toggleSystemPanel);
  DOM.btnCloseSystem.addEventListener('click', closeSystemPanel);
  DOM.btnRefreshWifi.addEventListener('click', refreshWifi);
  DOM.btnReboot.addEventListener('click', rebootESP32);
  
  // ===== LOGGING PREFERENCES =====
  DOM.chkLoggingEnabled.addEventListener('change', function() {
    saveLoggingPreferences();
    
    // Disable DEBUG checkbox if logging is disabled
    DOM.chkDebugLevel.disabled = !this.checked;
    
    if (this.checked) {
      console.debug('🔊 Logging enabled');
    } else {
      console.debug('🔇 Logging disabled - all logs (console + files) stopped');
    }
  });
  
  DOM.chkDebugLevel.addEventListener('change', function() {
    saveLoggingPreferences();
    
    if (this.checked) {
      console.debug('📊 Log level: DEBUG (verbose mode)');
    } else {
      console.debug('📊 Log level: INFO (normal mode)');
    }
  });
  
  // Load logging preferences on startup
  loadLoggingPreferences();
  
  console.debug('✅ Tools listeners initialized');
}
