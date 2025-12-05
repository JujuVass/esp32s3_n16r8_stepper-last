/**
 * ============================================================================
 * websocket.js - WebSocket Connection & Message Handlers
 * ============================================================================
 * Manages WebSocket connection to ESP32 and routes incoming messages
 * 
 * Dependencies:
 * - app.js (AppState, WS_CMD)
 * - utils.js (showNotification)
 * - ui.js (updateUI) - loaded after this file
 * 
 * Created: December 2024 (extracted from index.html)
 * ============================================================================
 */

// ============================================================================
// WEBSOCKET CONNECTION
// ============================================================================

/**
 * Establish WebSocket connection to ESP32
 * Auto-reconnects on disconnect with 2 second delay
 */
function connectWebSocket() {
  const wsUrl = 'ws://' + window.location.hostname + ':81';
  console.log('🔌 Connecting to WebSocket:', wsUrl);
  
  AppState.ws = new WebSocket(wsUrl);
  
  // ========================================================================
  // ON OPEN - Connection established
  // ========================================================================
  AppState.ws.onopen = function() {
    console.log('✅ WebSocket connected');
    
    // Update status display
    const stateEl = document.getElementById('state');
    if (stateEl) {
      stateEl.textContent = 'Connecté au contrôleur';
    }
    
    // Initialize UI elements that depend on connection
    if (typeof updatePatternToggleButton === 'function') {
      updatePatternToggleButton();
    }
    
    // Request initial status after connection stabilizes
    setTimeout(function() {
      sendCommand(WS_CMD.GET_STATUS, {});
    }, 50);
  };
  
  // ========================================================================
  // ON MESSAGE - Handle incoming data
  // ========================================================================
  AppState.ws.onmessage = function(event) {
    try {
      const data = JSON.parse(event.data);
      
      // Route message based on type
      handleWebSocketMessage(data);
      
    } catch (e) {
      console.error('WebSocket parse error:', e, 'Raw data:', event.data);
      showNotification('Erreur de communication avec l\'ESP32', 'error');
    }
  };
  
  // ========================================================================
  // ON CLOSE - Connection lost
  // ========================================================================
  AppState.ws.onclose = function() {
    console.log('❌ WebSocket disconnected. Reconnecting in 2s...');
    
    const stateEl = document.getElementById('state');
    if (stateEl) {
      stateEl.textContent = 'Déconnecté - Reconnexion...';
    }
    
    // Auto-reconnect after delay
    setTimeout(connectWebSocket, 2000);
  };
  
  // ========================================================================
  // ON ERROR - Connection error
  // ========================================================================
  AppState.ws.onerror = function(error) {
    console.error('WebSocket error:', error);
    
    const stateEl = document.getElementById('state');
    if (stateEl) {
      stateEl.textContent = 'Erreur de connexion';
    }
    
    showNotification('Erreur WebSocket - Vérifiez la connexion', 'error');
  };
}

// ============================================================================
// MESSAGE ROUTER - Route messages to appropriate handlers
// ============================================================================

/**
 * Route incoming WebSocket messages to appropriate handlers
 * @param {object} data - Parsed JSON message from ESP32
 */
function handleWebSocketMessage(data) {
  // Error messages (high priority)
  if (data.type === 'error') {
    showNotification(data.message, 'error');
    return;
  }
  
  // Sequence table data
  if (data.type === 'sequenceTable') {
    if (typeof renderSequenceTable === 'function') {
      renderSequenceTable(data.data);
    }
    return;
  }
  
  // Sequence execution status
  if (data.type === 'sequenceStatus') {
    if (typeof updateSequenceStatus === 'function') {
      updateSequenceStatus(data);
    }
    return;
  }
  
  // Export data (trigger download)
  if (data.type === 'exportData') {
    handleExportData(data.data);
    return;
  }
  
  // Filesystem listing
  if (data.type === 'fsList') {
    handleFileSystemList(data.files);
    return;
  }
  
  // Log messages from backend
  if (data.type === 'log') {
    handleLogMessage(data);
    return;
  }
  
  // Default: Status update for main UI
  if (typeof updateUI === 'function') {
    updateUI(data);
  }
}

// ============================================================================
// MESSAGE HANDLERS - Specific message type processors
// ============================================================================

/**
 * Handle export data - trigger file download
 * @param {object} exportData - Data to export as JSON
 */
function handleExportData(exportData) {
  const jsonStr = JSON.stringify(exportData, null, 2);
  const blob = new Blob([jsonStr], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  
  const a = document.createElement('a');
  a.href = url;
  a.download = 'sequence_' + new Date().toISOString().slice(0,10) + '.json';
  a.click();
  
  URL.revokeObjectURL(url);
  console.log('📥 Export downloaded');
}

/**
 * Handle filesystem listing - update index.html timestamp display
 * @param {array} files - List of files with names and timestamps
 */
function handleFileSystemList(files) {
  if (!files || files.length === 0) return;
  
  const idxFile = files.find(f => f.name === '/index.html' || f.name === 'index.html');
  if (idxFile) {
    const el = document.getElementById('fsIndexTimestamp');
    if (el) {
      el.textContent = 'index.html: ' + idxFile.time;
    }
  }
}

/**
 * Handle log messages from backend - display in console panel
 * @param {object} logData - Log message with level and content
 */
function handleLogMessage(logData) {
  const logPanel = document.getElementById('logConsolePanel');
  if (!logPanel) return;
  
  const level = logData.level || 'INFO';
  const msg = logData.message || '';
  
  // Color coding based on log level (VSCode Dark+ theme inspired)
  const colors = {
    'ERROR': '#f48771',   // Red
    'WARN': '#dcdcaa',    // Yellow
    'INFO': '#4ec9b0',    // Cyan
    'DEBUG': '#9cdcfe'    // Blue
  };
  const color = colors[level] || '#d4d4d4';
  
  const timestamp = new Date().toLocaleTimeString('fr-FR', { 
    hour: '2-digit', 
    minute: '2-digit', 
    second: '2-digit' 
  });
  
  const lineEl = document.createElement('div');
  lineEl.style.color = color;
  lineEl.textContent = `[${timestamp}] [${level}] ${msg}`;
  
  logPanel.appendChild(lineEl);
  logPanel.scrollTop = logPanel.scrollHeight;
  
  // Limit to 500 lines to prevent memory issues
  while (logPanel.children.length > 500) {
    logPanel.removeChild(logPanel.firstChild);
  }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Check if WebSocket is connected and ready
 * @returns {boolean} True if connected
 */
function isWebSocketConnected() {
  return AppState.ws && AppState.ws.readyState === WebSocket.OPEN;
}

/**
 * Send raw message through WebSocket
 * @param {string} message - Raw string message to send
 * @returns {boolean} True if sent successfully
 */
function sendRawMessage(message) {
  if (!isWebSocketConnected()) {
    console.warn('WebSocket not connected, cannot send message');
    return false;
  }
  
  AppState.ws.send(message);
  return true;
}

// Log initialization
console.log('✅ websocket.js loaded - WebSocket handlers ready');
