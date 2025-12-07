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
function resetTotalDistance() {
  if (confirm('Réinitialiser le compteur de distance parcourue ?')) {
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
  const panel = document.getElementById('logsPanel');
  const btn = document.getElementById('btnShowLogs');
  
  if (panel.style.display === 'none') {
    panel.style.display = 'block';
    btn.innerHTML = '📋 Logs';
    btn.style.background = '#e74c3c';
    btn.style.color = 'white';
    
    // Load log files list
    loadLogFilesList();
    
    // Load current debug level state
    fetch('/api/system/logging/preferences')
      .then(response => response.json())
      .then(data => {
        const chkDebug = document.getElementById('debugLevelCheckbox');
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
  document.getElementById('logsPanel').style.display = 'none';
  document.getElementById('btnShowLogs').innerHTML = '📋 Logs';
  document.getElementById('btnShowLogs').style.background = '#17a2b8';
  document.getElementById('btnShowLogs').style.color = 'white';
}

/**
 * Clear logs console panel
 */
function clearLogsPanel() {
  const logEl = document.getElementById('logConsolePanel');
  if (logEl) logEl.textContent = '(logs effacés)';
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
  
  fetch('/api/system/logging/preferences', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(preferences)
  })
    .then(response => response.json())
    .then(data => {
      console.log('💾 Log level saved:', isChecked ? 'DEBUG' : 'INFO');
      
      // Also update the checkbox in Sys panel
      const chkDebug = document.getElementById('chkDebugLevel');
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
function clearAllLogFiles() {
  if (confirm('Supprimer TOUS les fichiers de logs?\n\nCette action est irréversible.')) {
    fetch('/logs/clear', { method: 'POST' })
      .then(response => response.json())
      .then(data => {
        alert(data.message || 'Logs supprimés avec succès!');
        loadLogFilesList();  // Refresh list
      })
      .catch(error => {
        console.error('Erreur suppression logs:', error);
        alert('Erreur: ' + error);
      });
  }
}

/**
 * Load and display log files list
 */
function loadLogFilesList() {
  fetch('/logs')
    .then(response => response.text())
    .then(html => {
      // Parse HTML to extract file links
      const parser = new DOMParser();
      const doc = parser.parseFromString(html, 'text/html');
      const links = doc.querySelectorAll('a');
      
      if (links.length === 0) {
        DOM.logFilesList.innerHTML = '<div style="color: #999; font-style: italic; font-size: 11px;">Aucun fichier de log</div>';
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
        itemDiv.style.cssText = 'display: flex; justify-content: space-between; align-items: center; padding: 6px; background: #f9f9f9; border-radius: 4px; border: 1px solid #ddd;';
        
        // Create filename span (safe from XSS with textContent)
        const filenameSpan = document.createElement('span');
        filenameSpan.style.cssText = 'font-family: monospace; font-size: 12px;';
        filenameSpan.textContent = filename;  // Safe: uses textContent instead of innerHTML
        
        // Create download link
        const downloadLink = document.createElement('a');
        downloadLink.href = url;
        downloadLink.target = '_blank';
        downloadLink.style.cssText = 'background: #2196F3; color: white; padding: 4px 10px; border-radius: 3px; text-decoration: none; font-size: 11px;';
        downloadLink.textContent = '📥 Télécharger';
        
        itemDiv.appendChild(filenameSpan);
        itemDiv.appendChild(downloadLink);
        container.appendChild(itemDiv);
      });
      
      DOM.logFilesList.innerHTML = '';  // Clear first
      DOM.logFilesList.appendChild(container);
    })
    .catch(error => {
      DOM.logFilesList.innerHTML = '<div style="color: #f44336; font-size: 11px;">Erreur de chargement</div>';
    });
}

// ============================================================================
// SYSTEM PANEL MANAGEMENT
// ============================================================================

/**
 * Toggle system panel visibility
 */
function toggleSystemPanel() {
  const panel = document.getElementById('systemPanel');
  const btn = document.getElementById('btnShowSystem');
  
  if (panel.style.display === 'none') {
    panel.style.display = 'block';
    btn.innerHTML = '⚙️ Sys';
    btn.style.background = '#e74c3c';
    btn.style.color = 'white';
    
    // Enable system stats in backend (same as Stats panel)
    if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
      AppState.ws.send(JSON.stringify({
        cmd: 'requestStats',
        enable: true
      }));
      console.log('📊 System stats requested from backend');
    }
    
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
  const panel = document.getElementById('systemPanel');
  const btn = document.getElementById('btnShowSystem');
  
  panel.style.display = 'none';
  btn.innerHTML = '⚙️ Sys';
  btn.style.background = '#2196F3';
  btn.style.color = 'white';
  
  // Disable system stats when closing (optional optimization)
  if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
    AppState.ws.send(JSON.stringify({
      cmd: 'requestStats',
      enable: false
    }));
  }
}

/**
 * Refresh WiFi connection
 */
function refreshWifi() {
  if (confirm('📶 Reconnecter le WiFi?\n\nCela peut interrompre brièvement la connexion (~2-3 secondes).\n\nVoulez-vous continuer?')) {
    const btn = document.getElementById('btnRefreshWifi');
    const originalText = btn.innerHTML;
    
    // Disable button and show loading
    btn.disabled = true;
    btn.innerHTML = '⏳';
    btn.style.opacity = '0.5';
    
    console.log('📶 Sending WiFi reconnect command...');
    
    // Send reconnect command (expect network error as WiFi disconnects)
    fetch('/api/system/wifi/reconnect', { method: 'POST' })
      .then(response => response.json())
      .then(data => {
        console.log('📶 WiFi reconnect command sent:', data);
      })
      .catch(error => {
        // Expected error: network will be interrupted during WiFi reconnect
        console.log('📶 WiFi reconnect in progress (network interruption expected)');
      });
    
    // Wait for WiFi to reconnect (3 seconds)
    setTimeout(function() {
      btn.disabled = false;
      btn.innerHTML = '✅';
      btn.style.opacity = '1';
      
      // Reset button after 2 seconds
      setTimeout(function() {
        btn.innerHTML = originalText;
      }, 2000);
    }, 3000);
  }
}

/**
 * Reboot ESP32
 */
function rebootESP32() {
  if (confirm('⚠️ Redémarrer l\'ESP32?\n\nLa connexion sera interrompue pendant ~10-15 secondes.\n\nVoulez-vous continuer?')) {
    // Show reboot overlay
    document.getElementById('rebootOverlay').style.display = 'flex';
    
    // Send reboot command
    fetch('/api/system/reboot', { method: 'POST' })
      .then(response => response.json())
      .then(data => {
        console.log('🔄 Reboot command sent:', data);
      })
      .catch(error => {
        console.log('🔄 Reboot initiated (connection lost as expected)');
      });
    
    // Close WebSocket properly
    if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
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
  let httpConnected = false;
  let wsConnected = false;
  
  const updateRebootStatus = function(message, subMessage) {
    const msgEl = document.getElementById('rebootMessage');
    const statusEl = document.getElementById('rebootStatus');
    if (msgEl) msgEl.textContent = message;
    if (statusEl) statusEl.textContent = subMessage || '';
  };
  
  const tryReconnect = function() {
    attempts++;
    console.log('🔄 Reconnection attempt ' + attempts + '/' + maxAttempts);
    updateRebootStatus('Tentative de reconnexion...', 'Essai ' + attempts + '/' + maxAttempts);
    
    // Test HTTP connection first using ping endpoint
    fetch('/api/ping', { 
      method: 'GET',
      cache: 'no-cache'
    })
      .then(response => {
        if (!response.ok) throw new Error('HTTP not ready');
        return response.json();
      })
      .then(data => {
        console.log('✅ HTTP connection restored! Uptime:', data.uptime, 'ms');
        httpConnected = true;
        updateRebootStatus('HTTP OK - Reconnexion WebSocket...', '🌐 Connexion en cours...');
        
        // Now try WebSocket connection
        if (!wsConnected) {
          console.log('🔌 Attempting WebSocket reconnection...');
          
          // Try to reconnect WebSocket
          try {
            connectWebSocket();
            
            // Wait a bit to see if WebSocket connects
            setTimeout(function() {
              if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
                wsConnected = true;
                console.log('✅ WebSocket reconnected!');
                updateRebootStatus('Connexion rétablie !', '✅ Rechargement de la page...');
                
                // Both HTTP and WS are connected - wait 2 more seconds for stability
                console.log('⏳ Waiting for system stability...');
                setTimeout(function() {
                  console.log('✅ System stable - reloading page...');
                  document.getElementById('rebootOverlay').style.display = 'none';
                  location.reload(true); // Force reload from server
                }, 2000);
              } else {
                // WebSocket not ready yet, keep trying
                console.log('⚠️ WebSocket not ready, retrying...');
                if (attempts < maxAttempts) {
                  setTimeout(tryReconnect, 1000);
                } else {
                  document.getElementById('rebootOverlay').style.display = 'none';
                  alert('❌ HTTP OK mais WebSocket ne répond pas.\n\nRecherger la page manuellement (F5).');
                }
              }
            }, 1500);
            
          } catch (err) {
            console.error('❌ WebSocket connection error:', err);
            if (attempts < maxAttempts) {
              setTimeout(tryReconnect, 1000);
            } else {
              document.getElementById('rebootOverlay').style.display = 'none';
              alert('❌ Impossible de reconnecter le WebSocket.\n\nVeuillez recharger la page manuellement (F5).');
            }
          }
        }
      })
      .catch(error => {
        console.log('⚠️ ESP32 not ready yet:', error.message);
        if (attempts < maxAttempts) {
          setTimeout(tryReconnect, 1000);
        } else {
          document.getElementById('rebootOverlay').style.display = 'none';
          alert('❌ Impossible de se reconnecter à l\'ESP32.\n\nVeuillez recharger la page manuellement (F5).');
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
  fetch('/api/system/logging/preferences')
    .then(response => response.json())
    .then(data => {
      // Set checkboxes
      const chkEnabled = document.getElementById('chkLoggingEnabled');
      const chkDebug = document.getElementById('chkDebugLevel');
      const btnShowLogs = document.getElementById('btnShowLogs');
      
      chkEnabled.checked = data.loggingEnabled;
      chkDebug.checked = (data.logLevel === 3); // LOG_DEBUG = 3
      
      // Update AppState.logging for console wrapper FIRST
      AppState.logging.enabled = data.loggingEnabled;
      AppState.logging.debugEnabled = (data.logLevel === 3);
      
      // Hide/show Logs button based on logging enabled state
      if (!data.loggingEnabled) {
        btnShowLogs.style.display = 'none';
      } else {
        btnShowLogs.style.display = '';
      }
      
      // Debug log (only if enabled)
      if (data.loggingEnabled) {
        console.log('📂 Loaded logging preferences:', data);
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
  const chkEnabled = document.getElementById('chkLoggingEnabled');
  const chkDebug = document.getElementById('chkDebugLevel');
  
  const preferences = {
    loggingEnabled: chkEnabled.checked,
    logLevel: chkDebug.checked ? 3 : 2  // 3=DEBUG, 2=INFO
  };
  
  // Update AppState.logging for console wrapper
  AppState.logging.enabled = preferences.loggingEnabled;
  AppState.logging.debugEnabled = (preferences.logLevel === 3);
  
  fetch('/api/system/logging/preferences', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(preferences)
  })
    .then(response => response.json())
    .then(data => {
      console.log('💾 Logging preferences saved:', data);
      
      // Update Logs button visibility
      const btnShowLogs = document.getElementById('btnShowLogs');
      if (!preferences.loggingEnabled) {
        btnShowLogs.style.display = 'none';
      } else {
        btnShowLogs.style.display = '';
      }
    })
    .catch(error => {
      console.error('❌ Error saving logging preferences:', error);
      alert('❌ Erreur lors de la sauvegarde des préférences de log');
    });
}

// ============================================================================
// SYSTEM STATS UPDATE (ESP32 Hardware Stats)
// ============================================================================

/**
 * Update system hardware statistics display (CPU, RAM, WiFi, etc.)
 * Extracted from main.js - displays ESP32 system info, not app statistics
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
    document.getElementById('sysCpuFreq').textContent = system.cpuFreqMHz + ' MHz';
  }
  
  // Temperature
  if (system.temperatureC !== undefined) {
    const temp = parseFloat(system.temperatureC);
    const tempEl = document.getElementById('sysTemp');
    tempEl.textContent = temp.toFixed(1) + ' °C';
    // Color coding based on temperature
    if (temp > 80) {
      tempEl.style.color = '#f44336'; // Red - hot
    } else if (temp > 70) {
      tempEl.style.color = '#FF9800'; // Orange - warm
    } else {
      tempEl.style.color = '#333'; // Normal
    }
  }
  
  // RAM
  if (system.heapFree !== undefined && system.heapTotal !== undefined && system.heapUsedPercent !== undefined) {
    const ramFreeMB = (system.heapFree / 1024).toFixed(1);
    const ramTotalMB = (system.heapTotal / 1024).toFixed(1);
    const ramUsedPercent = parseFloat(system.heapUsedPercent);
    document.getElementById('sysRam').textContent = ramFreeMB + ' KB libre / ' + ramTotalMB + ' KB';
    document.getElementById('sysRamPercent').textContent = ramUsedPercent.toFixed(1) + '% utilisé';
  }
  
  // PSRAM
  if (system.psramFree !== undefined && system.psramTotal !== undefined && system.psramUsedPercent !== undefined) {
    const psramFreeMB = (system.psramFree / 1024 / 1024).toFixed(1);
    const psramTotalMB = (system.psramTotal / 1024 / 1024).toFixed(1);
    const psramUsedPercent = parseFloat(system.psramUsedPercent);
    document.getElementById('sysPsram').textContent = psramFreeMB + ' MB libre / ' + psramTotalMB + ' MB';
    document.getElementById('sysPsramPercent').textContent = psramUsedPercent.toFixed(1) + '% utilisé';
  }
  
  // WiFi - delegate to pure function
  if (system.wifiRssi !== undefined) {
    const rssi = system.wifiRssi;
    document.getElementById('sysWifi').textContent = rssi + ' dBm';
    
    // Use pure function if available (from formatting.js)
    let quality, qualityColor;
    if (typeof getWifiQualityPure === 'function') {
      const wifiInfo = getWifiQualityPure(rssi);
      quality = wifiInfo.quality;
      qualityColor = wifiInfo.color;
    } else {
      // Fallback
      if (rssi >= -50) { quality = 'Excellent'; qualityColor = '#4CAF50'; }
      else if (rssi >= -60) { quality = 'Très bon'; qualityColor = '#8BC34A'; }
      else if (rssi >= -70) { quality = 'Bon'; qualityColor = '#FFC107'; }
      else if (rssi >= -80) { quality = 'Faible'; qualityColor = '#FF9800'; }
      else { quality = 'Très faible'; qualityColor = '#f44336'; }
    }
    
    const qualityEl = document.getElementById('sysWifiQuality');
    qualityEl.textContent = quality;
    qualityEl.style.color = qualityColor;
  }
  
  // Uptime - delegate to pure function
  if (system.uptimeSeconds !== undefined) {
    let uptimeStr;
    if (typeof formatUptimePure === 'function') {
      uptimeStr = formatUptimePure(system.uptimeSeconds);
    } else {
      // Fallback
      const uptimeSec = system.uptimeSeconds;
      const hours = Math.floor(uptimeSec / 3600);
      const minutes = Math.floor((uptimeSec % 3600) / 60);
      const seconds = uptimeSec % 60;
      uptimeStr = hours > 0 
        ? `${hours}h ${minutes}m ${seconds}s`
        : minutes > 0
          ? `${minutes}m ${seconds}s`
          : `${seconds}s`;
    }
    document.getElementById('sysUptime').textContent = uptimeStr;
  }
}

// ============================================================================
// INITIALIZE TOOLS LISTENERS
// ============================================================================

/**
 * Initialize all Tools event listeners
 * Called once on page load
 */
function initToolsListeners() {
  console.log('🔧 Initializing Tools listeners...');
  
  // ===== CALIBRATION & RESET =====
  document.getElementById('btnCalibrateCommon').addEventListener('click', calibrateMotor);
  document.getElementById('btnResetDistanceCommon').addEventListener('click', resetTotalDistance);
  
  // ===== LOGS PANEL =====
  document.getElementById('btnShowLogs').addEventListener('click', toggleLogsPanel);
  document.getElementById('btnCloseLogs').addEventListener('click', closeLogsPanel);
  document.getElementById('btnClearLogsPanel').addEventListener('click', clearLogsPanel);
  document.getElementById('debugLevelCheckbox').addEventListener('change', function() {
    handleDebugLevelChange(this.checked);
  });
  document.getElementById('btnClearAllLogFiles').addEventListener('click', clearAllLogFiles);
  
  // ===== SYSTEM PANEL =====
  document.getElementById('btnShowSystem').addEventListener('click', toggleSystemPanel);
  document.getElementById('btnCloseSystem').addEventListener('click', closeSystemPanel);
  document.getElementById('btnRefreshWifi').addEventListener('click', refreshWifi);
  document.getElementById('btnReboot').addEventListener('click', rebootESP32);
  
  // ===== LOGGING PREFERENCES =====
  document.getElementById('chkLoggingEnabled').addEventListener('change', function() {
    saveLoggingPreferences();
    
    // Disable DEBUG checkbox if logging is disabled
    const chkDebug = document.getElementById('chkDebugLevel');
    chkDebug.disabled = !this.checked;
    
    if (!this.checked) {
      console.log('🔇 Logging disabled - all logs (console + files) stopped');
    } else {
      console.log('🔊 Logging enabled');
    }
  });
  
  document.getElementById('chkDebugLevel').addEventListener('change', function() {
    saveLoggingPreferences();
    
    if (this.checked) {
      console.log('📊 Log level: DEBUG (verbose mode)');
    } else {
      console.log('📊 Log level: INFO (normal mode)');
    }
  });
  
  // Load logging preferences on startup
  loadLoggingPreferences();
  
  console.log('✅ Tools listeners initialized');
}
