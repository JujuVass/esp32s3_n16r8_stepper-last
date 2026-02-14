    // ========================================================================
    // MAIN APPLICATION CODE
    // ========================================================================
    // Note: AppState, WS_CMD, PlaylistState, SystemState loaded from app.js
    // Note: showNotification, validateNumericInput, etc. loaded from utils.js
    // Note: connectWebSocket, handleWebSocketMessage loaded from websocket.js
    // Note: loadStatsData, displayStatsTable, displayStatsChart loaded from stats.js
    // Note: Playlist functions loaded from PlaylistController.js:
    //   - loadPlaylists(), updatePlaylistButtonCounters()
    //   - generatePresetName(), generatePresetTooltip()
    //   - getCurrentModeConfig(), openPlaylistModal(), closePlaylistModal()
    //   - refreshPlaylistPresets(), addToPlaylist(), deleteFromPlaylist()
    //   - renamePlaylistPreset(), loadPresetInMode(), quickAddToSequencer()
    //   - loadPresetIntoSequencerModal(), initPlaylistListeners()

    // Note: MILESTONES array and getMilestoneInfo() loaded from app.js
    // Note: SPEED_MILESTONES array and getSpeedMilestoneInfo() loaded from speedMilestones.js
    
    // ========================================================================
    // CYCLE PAUSE HELPERS - Moved to SimpleController.js
    // ========================================================================
    // Note: getCyclePauseSection(), getCyclePauseOscSection() now in SimpleController.js

    // ========================================================================
    // GLOBAL STATE - Position Tracking
    // ========================================================================
    let currentPositionMM = 0; // Current motor position in millimeters
    
    // ========================================================================
    // SPEED LIMITS - Moved to ToolsController.js
    // ========================================================================
    // Note: maxSpeedLevel and initSpeedLimits() are now in ToolsController.js

    // ========================================================================
    // DOM CACHE - Performance Optimization
    // ========================================================================
    // DOM CACHE - Loaded from DOMManager.js
    // ========================================================================
    // Note: DOM object and initDOMCache() function are defined in DOMManager.js
    // The file must be loaded BEFORE main.js in index.html
    
    // ========================================================================
    // MODE Séquenceur - Moved to SequenceController.js
    // ========================================================================
    // Variables and functions for sequence table management are now in:
    //   SequenceController.js
    // Including: sequenceLines, editingLineId, isLoadingEditForm, draggedLineId,
    //   selectedLineIds, validateSequencerLine(), addSequenceLine(), editSequenceLine(),
    //   saveLineEdit(), validateEditForm(), renderSequenceTable(), testSequenceLine(),
    //   restoreSequenceAfterTest(), updateSequenceStatus(), clearSequence(),
    //   importSequence(), exportSequence(), downloadTemplate(), batchEnableLines(),
    //   batchDeleteLines(), clearSelection(), initializeTrashZones(), etc.

    // ========================================================================
    // WEBSOCKET CONNECTION - Moved to /js/websocket.js
    // ========================================================================
    // connectWebSocket() is now loaded from external module
    // Handles: onopen, onmessage, onclose, onerror
    // Message routing: handleWebSocketMessage() dispatches to appropriate handlers
    
    // ============================================================================
    // UI UPDATE - HELPER FUNCTIONS
    // ============================================================================
    
    /**
     * Update milestone display from total traveled distance
     * @param {number} totalTraveledMM - Total traveled distance in mm
     */
    // ============================================================================
    // UI UPDATE - HELPER FUNCTIONS
    // ============================================================================
    
    // Note: updateMilestones() moved to StatsController.js
    
    /**
     * Update speed display based on current active mode
     * @param {Object} data - Status data from backend
     */
    function updateSpeedDisplay(data) {
      const speedElement = document.getElementById('currentSpeed');
      const cpmSpan = speedElement ? speedElement.nextElementSibling : null;
      const currentMode = AppState.system.currentMode;
      
      if (currentMode === 'oscillation' && data.oscillation && data.oscillation.frequencyHz !== undefined && data.oscillation.amplitudeMM !== undefined) {
        // OSCILLATION MODE: Show ACTUAL speed (backend calculates with hardware limits)
        let displaySpeed;
        let isLimited = false;
        
        if (data.oscillation.actualSpeedMMS !== undefined && data.oscillation.actualSpeedMMS > 0) {
          displaySpeed = parseFloat(data.oscillation.actualSpeedMMS);
          const theoreticalSpeed = 2 * Math.PI * data.oscillation.frequencyHz * data.oscillation.amplitudeMM;
          isLimited = (displaySpeed < theoreticalSpeed - 1);
        } else {
          displaySpeed = 2 * Math.PI * data.oscillation.frequencyHz * data.oscillation.amplitudeMM;
        }
        
        speedElement.innerHTML = '🌊 ' + displaySpeed.toFixed(0) + ' mm/s' + (isLimited ? ' ⚠️' : '');
        if (cpmSpan) {
          cpmSpan.textContent = '(pic, f=' + data.oscillation.frequencyHz.toFixed(2) + ' Hz' + (isLimited ? ', ' + t('status.speedLimited') : '') + ')';
        }
      } else if (currentMode === 'chaos' && data.chaos && data.chaos.maxSpeedLevel !== undefined) {
        // CHAOS MODE: Show max speed level
        const speedMMPerSec = data.chaos.maxSpeedLevel * 10.0;
        speedElement.innerHTML = '⚡ ' + data.chaos.maxSpeedLevel.toFixed(1);
        if (cpmSpan) {
          cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
        }
      } else if (currentMode === 'pursuit' && AppState.pursuit.maxSpeedLevel !== undefined) {
        // PURSUIT MODE: Show max speed level from UI variable
        const speedMMPerSec = AppState.pursuit.maxSpeedLevel * 10.0;
        speedElement.innerHTML = '⚡ ' + AppState.pursuit.maxSpeedLevel.toFixed(1);
        if (cpmSpan) {
          cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
        }
      } else if (currentMode === 'sequencer') {
        // SEQUENCER MODE: Show mode indicator
        speedElement.innerHTML = '- ' + t('status.sequenceMode');
        if (cpmSpan) {
          cpmSpan.textContent = '';
        }
      } else if (currentMode === 'simple' && data.motion && data.motion.cyclesPerMinForward !== undefined && data.motion.cyclesPerMinBackward !== undefined) {
        // SIMPLE MODE: Show forward/backward speeds with cycles/min
        if (data.motion.speedLevelForward !== undefined && data.motion.speedLevelBackward !== undefined) {
          const avgCpm = ((parseFloat(data.motion.cyclesPerMinForward) + parseFloat(data.motion.cyclesPerMinBackward)) / 2).toFixed(0);
          speedElement.innerHTML = 
            '↗️ ' + data.motion.speedLevelForward.toFixed(1) + 
            '&nbsp;&nbsp;•&nbsp;&nbsp;' +
            '↙️ ' + data.motion.speedLevelBackward.toFixed(1);
          if (cpmSpan) {
            cpmSpan.textContent = '(' + avgCpm + ' c/min)';
          }
        }
      }
    }
    
    /**
     * Calculate and display real speed from totalTraveled delta
     * Uses exponential smoothing to avoid jitter
     * @param {number} totalTraveledMM - Current total traveled distance in mm
     */
    function updateRealSpeed(totalTraveledMM) {
      const now = Date.now();
      const speedState = AppState.speed;
      
      // First call: initialize tracking, don't display yet
      if (speedState.lastUpdateTime === 0) {
        speedState.lastTotalTraveledMM = totalTraveledMM;
        speedState.lastUpdateTime = now;
        return;
      }
      
      const deltaTimeMs = now - speedState.lastUpdateTime;
      
      // Need at least 200ms between samples for meaningful calculation
      if (deltaTimeMs < 200) return;
      
      const deltaDistMM = totalTraveledMM - speedState.lastTotalTraveledMM;
      const rawSpeedMMS = deltaDistMM / (deltaTimeMs / 1000); // mm/s
      const rawSpeedCmS = rawSpeedMMS / 10; // cm/s
      
      // Exponential moving average for smoothing
      if (speedState.currentCmPerSec === 0 && rawSpeedCmS > 0) {
        // Jump start when going from 0 to moving
        speedState.currentCmPerSec = rawSpeedCmS;
      } else {
        speedState.currentCmPerSec = 
          speedState.smoothingFactor * rawSpeedCmS + 
          (1 - speedState.smoothingFactor) * speedState.currentCmPerSec;
      }
      
      // If very small, clamp to 0
      if (speedState.currentCmPerSec < 0.05) {
        speedState.currentCmPerSec = 0;
      }
      
      // Update tracking
      speedState.lastTotalTraveledMM = totalTraveledMM;
      speedState.lastUpdateTime = now;
      
      // Convert to display units
      const cmPerSec = speedState.currentCmPerSec;
      const kmPerHour = (cmPerSec / 100) * 3.6; // cm/s → m/s → km/h
      
      // Get speed milestone
      const speedInfo = getSpeedMilestoneInfo(cmPerSec);
      
      // Update DOM
      const speedIconEl = document.getElementById('speedIcon');
      const realSpeedEl = document.getElementById('realSpeed');
      
      if (speedIconEl) {
        speedIconEl.textContent = speedInfo.current.emoji;
        speedIconEl.title = speedInfo.current.name + 
          (speedInfo.next ? ' → ' + t('common.next', {}) + ': ' + speedInfo.next.emoji + ' ' + speedInfo.next.name + ' (' + speedInfo.next.threshold + ' cm/s)' : ' (max!)');
      }
      
      if (realSpeedEl) {
        if (cmPerSec < 0.05) {
          realSpeedEl.textContent = '0 cm/s';
        } else if (kmPerHour >= 0.1) {
          realSpeedEl.textContent = cmPerSec.toFixed(1) + ' cm/s (' + kmPerHour.toFixed(2) + ' km/h)';
        } else {
          realSpeedEl.textContent = cmPerSec.toFixed(1) + ' cm/s';
        }
      }
    }
    
    /**
     * Sync input values with server state (but not if user is editing)
     * @param {Object} data - Status data from backend
     */
    function syncInputsFromBackend(data) {
      // Defense: Check data.motion exists before accessing fields
      if (data.motion) {
        if (AppState.editing.input !== 'startPosition' && document.activeElement !== DOM.startPosition && data.motion.startPositionMM !== undefined) {
          DOM.startPosition.value = data.motion.startPositionMM.toFixed(1);
        }
        if (data.motion.targetDistanceMM !== undefined && AppState.editing.input !== 'distance' && document.activeElement !== DOM.distance) {
          DOM.distance.value = parseFloat(data.motion.targetDistanceMM).toFixed(1);
        }
      }
      
      // Update speed values based on unified/separate mode
      const isSeparateMode = document.getElementById('speedModeSeparate')?.checked || false;
      
      if (data.motion) {
        if (isSeparateMode) {
          // SEPARATE MODE: update forward and backward individually
          if (AppState.editing.input !== 'speedForward' && document.activeElement !== DOM.speedForward && data.motion.speedLevelForward !== undefined) {
            DOM.speedForward.value = data.motion.speedLevelForward.toFixed(1);
          }
          if (AppState.editing.input !== 'speedBackward' && document.activeElement !== DOM.speedBackward && data.motion.speedLevelBackward !== undefined) {
            DOM.speedBackward.value = data.motion.speedLevelBackward.toFixed(1);
          }
          if (DOM.speedForwardInfo && data.motion.cyclesPerMinForward !== undefined) {
            DOM.speedForwardInfo.textContent = '≈ ' + parseFloat(data.motion.cyclesPerMinForward).toFixed(0) + ' cycles/min';
          }
          if (DOM.speedBackwardInfo && data.motion.cyclesPerMinBackward !== undefined) {
            DOM.speedBackwardInfo.textContent = '≈ ' + parseFloat(data.motion.cyclesPerMinBackward).toFixed(0) + ' cycles/min';
          }
        } else {
          // UNIFIED MODE: show current speed (should be same for both directions)
          if (AppState.editing.input !== 'speedUnified' && document.activeElement !== DOM.speedUnified) {
            if (data.motion.speedLevelBackward !== undefined) {
              DOM.speedUnified.value = data.motion.speedLevelBackward.toFixed(1);
            }
            if (data.motion.speedLevelForward !== undefined) {
              DOM.speedForward.value = data.motion.speedLevelForward.toFixed(1);
            }
            if (data.motion.speedLevelBackward !== undefined) {
              DOM.speedBackward.value = data.motion.speedLevelBackward.toFixed(1);
            }
          }
          if (DOM.speedUnifiedInfo && data.motion.cyclesPerMinForward !== undefined && data.motion.cyclesPerMinBackward !== undefined) {
            const avgCyclesPerMin = (parseFloat(data.motion.cyclesPerMinForward) + parseFloat(data.motion.cyclesPerMinBackward)) / 2.0;
            DOM.speedUnifiedInfo.textContent = '≈ ' + avgCyclesPerMin.toFixed(0) + ' cycles/min';
          }
        }
      }
    }
    
    /**
     * Update max values, presets and button states
     * @param {Object} data - Status data from backend
     */
    function updateControlsState(data) {
      // Update max values and presets
      if (data.totalDistMM !== undefined) {
        const effectiveMax = (data.effectiveMaxDistMM && data.effectiveMaxDistMM > 0) ? data.effectiveMaxDistMM : data.totalDistMM;
        const startPos = (data.motion && data.motion.startPositionMM !== undefined) ? data.motion.startPositionMM : 0;
        const maxAvailable = effectiveMax - startPos;
        
        DOM.startPosition.max = effectiveMax;
        DOM.distance.max = maxAvailable;
        
        if (DOM.maxStart) {
          if (data.maxDistLimitPercent && data.maxDistLimitPercent < 100) {
            DOM.maxStart.textContent = effectiveMax.toFixed(2) + ' (' + data.maxDistLimitPercent.toFixed(0) + '% de ' + data.totalDistMM.toFixed(2) + ')';
          } else {
            DOM.maxStart.textContent = effectiveMax.toFixed(2);
          }
        }
        
        if (DOM.maxDist) {
          DOM.maxDist.textContent = maxAvailable.toFixed(2);
        }
        updateStartPresets(effectiveMax);
        updateDistancePresets(maxAvailable);
        
        // 🆕 Update relative presets for Simple mode
        if (typeof updateSimpleRelativePresets === 'function') {
          updateSimpleRelativePresets();
        }
      }
      
      // Enable/disable start button
      const isRunning = data.state === SystemState.RUNNING;
      const isPausedState = data.state === SystemState.PAUSED;
      const canStart = canStartOperation() && !isRunning && !isPausedState;
      setButtonState(DOM.btnStart, canStart);
      
      // Enable/disable calibrate button
      if (DOM.btnCalibrateCommon) {
        if (!data.canCalibrate) {
          DOM.btnCalibrateCommon.disabled = true;
          DOM.btnCalibrateCommon.style.opacity = '0.5';
          DOM.btnCalibrateCommon.style.cursor = 'not-allowed';
        } else {
          DOM.btnCalibrateCommon.disabled = false;
          DOM.btnCalibrateCommon.style.opacity = '1';
          DOM.btnCalibrateCommon.style.cursor = 'pointer';
        }
      }
      
      // Disable inputs during calibration (but allow changes during running)
      const inputsEnabled = canStartOperation();
      
      [DOM.startPosition, DOM.distance, DOM.speedUnified, DOM.speedForward, DOM.speedBackward].forEach(input => {
        if (input) {
          input.disabled = !inputsEnabled;
          input.style.opacity = inputsEnabled ? '1' : '0.6';
        }
      });
      
      // Update pursuit controls
      if (DOM.pursuitActiveCheckbox) DOM.pursuitActiveCheckbox.disabled = !inputsEnabled;
      setButtonState(DOM.btnActivatePursuit, inputsEnabled);
    }
    
    // ============================================================================
    // UI UPDATE - MAIN FUNCTION
    // ============================================================================
    
    function updateUI(data) {
      // Defensive check: only process status-like messages which contain positionMM
      // Some incoming messages (logs, fsList, etc.) can be generic JSON and should
      // not be passed to updateUI. Guard against missing fields to avoid runtime
      // errors like "cannot read property 'toFixed' of undefined".
      if (!data || !('positionMM' in data)) {
        console.warn('updateUI: unexpected message, skipping', data);
        return;
      }
      // Update global state for mode change logic
      AppState.system.currentState = data.state;
      AppState.system.canStart = data.canStart || false;
      
      // AUTO-SWITCH TAB: If movement is running/paused, switch to corresponding tab
      // This handles reconnection scenarios where user returns to find movement in progress
      // Skip if running from sequencer (executionContext === 1) — stay on sequencer tab
      const isActiveMovement = (data.state === SystemState.RUNNING || data.state === SystemState.PAUSED);
      if (isActiveMovement && data.movementType !== undefined && data.executionContext !== 1) {
        const tabFromMovement = {
          0: 'simple',      // MOVEMENT_VAET
          1: 'oscillation', // MOVEMENT_OSC
          2: 'chaos',       // MOVEMENT_CHAOS
          3: 'pursuit'      // MOVEMENT_PURSUIT
        };
        const targetTab = tabFromMovement[data.movementType];
        if (targetTab && AppState.system.currentMode !== targetTab) {
          console.log('🔄 Auto-switching to ' + targetTab + ' tab (movement in progress)');
          // Use direct tab switch without stopping movement (since it's already running)
          switchTabWithoutStop(targetTab);
        }
      }
      
      const stateText = t('states') || ['Initialisation', 'Calibration...', 'Prêt', 'En marche', 'En pause', 'Erreur'];
      const stateClass = ['state-init', 'state-calibrating', 'state-ready', 'state-running', 'state-paused', 'state-error'];
      
      let displayText = stateText[data.state] || t('common.error');
      if (data.errorMessage && data.errorMessage !== '') {
        displayText += ' ⚠️ ' + data.errorMessage;
      }
      
      DOM.state.textContent = displayText;
      DOM.state.className = 'status-value ' + (stateClass[data.state] || '');
      
      // Check if calibrating (used in multiple places below)
      const isCalibrating = data.state === SystemState.CALIBRATING;
      
      // Show/hide calibration overlay
      if (DOM.calibrationOverlay) {
        if (isCalibrating) {
          DOM.calibrationOverlay.classList.add('active');
        } else {
          DOM.calibrationOverlay.classList.remove('active');
        }
      }
      
      // Show tabs and controls after first successful calibration
      // Once calibrated (canStart = true), reveal the interface
      if (AppState.system.canStart && data.totalDistMM > 0) {
        const tabsContainer = document.getElementById('tabsContainer');
        const allTabContents = document.querySelectorAll('.tab-content');
        const welcomeMessage = document.getElementById('welcomeMessage');
        
        // Hide welcome message
        if (welcomeMessage && !welcomeMessage.classList.contains('hidden')) {
          welcomeMessage.classList.add('hidden');
        }
        
        // Show tabs
        if (tabsContainer && tabsContainer.classList.contains('hidden-until-calibrated')) {
          tabsContainer.classList.remove('hidden-until-calibrated');
        }
        
        // Show all tab contents
        allTabContents.forEach(tabContent => {
          if (tabContent.classList.contains('hidden-until-calibrated')) {
            tabContent.classList.remove('hidden-until-calibrated');
          }
        });
      }
      
      // Extra safety: check fields exist before accessing (defense-in-depth)
      if (data.positionMM !== undefined && data.currentStep !== undefined) {
        currentPositionMM = data.positionMM; // Update global position tracker
        DOM.position.textContent = 
          data.positionMM.toFixed(2) + ' mm (' + data.currentStep + ' steps)';
      }
      
      if (data.totalDistMM !== undefined) {
        // Display total distance with limit info if applicable
        let totalDistText = data.totalDistMM.toFixed(2) + ' mm';
        if (data.maxDistLimitPercent && data.maxDistLimitPercent < 100) {
          totalDistText += ' (' + data.effectiveMaxDistMM.toFixed(2) + ' mm @ ' + 
                          data.maxDistLimitPercent.toFixed(0) + '%)';
        }
        DOM.totalDist.textContent = totalDistText;
        if (DOM.maxDist) {
          DOM.maxDist.textContent = data.totalDistMM.toFixed(2);
        }
        
        // Update max dist limit slider if data received (but NOT while user is editing!)
        if (data.maxDistLimitPercent && !AppState.pursuit.isEditingMaxDistLimit) {
          DOM.maxDistLimitSlider.value = data.maxDistLimitPercent.toFixed(0);
          updateMaxDistLimitUI();
        }
      }
      
      // Update pursuit mode variables
      if (data.totalDistMM !== undefined) {
        AppState.pursuit.totalDistanceMM = data.totalDistMM;
        
        // Store max distance limit percent in AppState (but not while user is editing!)
        if (data.maxDistLimitPercent !== undefined && !AppState.pursuit.isEditingMaxDistLimit) {
          AppState.pursuit.maxDistLimitPercent = data.maxDistLimitPercent;
        }
        
        // Update gauge limit line
        if (data.maxDistLimitPercent && data.maxDistLimitPercent < 100 && data.effectiveMaxDistMM) {
          const limitPercent = (data.effectiveMaxDistMM / data.totalDistMM);
          const containerHeight = DOM.gaugeContainer ? DOM.gaugeContainer.offsetHeight : 500;
          const limitPixelPosition = containerHeight - (limitPercent * containerHeight);
          
          if (DOM.gaugeLimitLine) {
          DOM.gaugeLimitLine.style.top = limitPixelPosition + 'px';
          DOM.gaugeLimitLine.style.display = 'block';
          }
        } else if (DOM.gaugeLimitLine) {
          DOM.gaugeLimitLine.style.display = 'none';
        }
      }
      if (data.positionMM !== undefined) {
        AppState.pursuit.currentPositionMM = data.positionMM;
        updateGaugePosition(AppState.pursuit.currentPositionMM);
      }
      
      // Update speed display (delegated to helper function)
      updateSpeedDisplay(data);
      
      // Update milestones (delegated to helper function)
      if (data.totalTraveled !== undefined) {
        updateMilestones(data.totalTraveled);
        updateRealSpeed(data.totalTraveled);
      }
      
      if (data.totalDistMM > 0 && data.positionMM !== undefined) {
        const progress = (data.positionMM / data.totalDistMM) * 100;
        const progressMini = document.getElementById('progressMini');
        const progressPct = document.getElementById('progressPct');
        if (progressMini) progressMini.style.width = progress + '%';
        if (progressPct) progressPct.textContent = progress.toFixed(1) + '%';
      }
      
      // Sync input values with server state (delegated to helper function)
      syncInputsFromBackend(data);
      
      // Update max values, presets and button states (delegated to helper function)
      updateControlsState(data);
      
      // Update zone effect configuration from server
      if (data.decelZone) {
        updateZoneEffectUI(data.decelZone);
      }
      
      // Show pending changes indicator
      const pendingChanges = DOM.pendingChanges;
      if (pendingChanges) {
      if (data.hasPending && data.pendingMotion) {
        const pm = data.pendingMotion;
        const startPos = parseFloat(pm.startPositionMM);
        const dist = parseFloat(pm.distanceMM);
        pendingChanges.style.display = 'block';
        pendingChanges.textContent = '⏳ ' + t('status.pendingChanges') + ': ' + 
          startPos.toFixed(1) + ' mm → ' + 
          (startPos + dist).toFixed(1) + ' mm (' +
          dist.toFixed(1) + 'mm) @ ' + 
          t('simple.forward') + ' ' + parseFloat(pm.speedLevelForward).toFixed(1) + '/20, ' +
          t('simple.backward') + ' ' + parseFloat(pm.speedLevelBackward).toFixed(1) + '/20';
      } else {
        pendingChanges.style.display = 'none';
      }
      }
      
      // Update oscillation UI (delegated to OscillationController.js)
      updateOscillationUI(data);
      
      // Update Simple mode UI (delegated to SimpleController.js)
      updateSimpleUI(data);
      
      // Note: Cycle pause oscillation UI update is handled by updateOscillationUI() above
      
      // Update chaos UI (delegated to ChaosController.js)
      updateChaosUI(data);
      
      // SEQUENCER MODE: Re-enable start buttons when system is READY and not running
      // This catches cases where sequenceStatus WebSocket message was missed
      const isReady = (data.state === SystemState.READY);
      if (isReady && AppState.system.canStart && AppState.system.currentMode === 'sequencer') {
        // Reset test mode flag if system is ready (safety cleanup)
        if (AppState.sequence.isTestingLine) {
          AppState.sequence.isTestingLine = false;
        }
        // Re-enable sequencer buttons
        if (DOM.btnStartSequence && DOM.btnStartSequence.disabled) {
          setButtonState(DOM.btnStartSequence, true);
          setButtonState(DOM.btnLoopSequence, true);
        }
      }
      
      // Note: Sequence start buttons are NOT managed here anymore
      // They are controlled ONLY by:
      // 1. User click (immediate disable)
      // 2. Backend sequenceStatus (re-enable when stopped)
      // This prevents flickering/desync issues
      
      // Update system stats (delegated to ToolsController.js)
      if (data.system) {
        updateSystemStats(data.system);
      }
      
      // Update stats recording UI (delegated to StatsController.js)
      if (data.statsRecordingEnabled !== undefined) {
        updateStatsRecordingUI(data.statsRecordingEnabled);
      }
      
      // Update sensors inverted UI
      if (data.sensorsInverted !== undefined) {
        DOM.chkSensorsInverted.checked = data.sensorsInverted;
        DOM.sensorsInvertedStatus.textContent = data.sensorsInverted ? '(' + t('common.inverted') + ')' : '(' + t('common.normal') + ')';
        // Update icon next to "Course:"
        const invertedIcon = document.getElementById('sensorsInvertedIcon');
        if (invertedIcon) {
          invertedIcon.style.display = data.sensorsInverted ? 'inline' : 'none';
        }
      }
    }
    
    // ============================================================================
    // PLAYLIST MANAGEMENT FUNCTIONS - Loaded from PlaylistController.js
    // ============================================================================
    // Note: Playlist functions loaded from external module:
    // - loadPlaylists(), updatePlaylistButtonCounters()
    // - generatePresetName(), generatePresetTooltip(), generateSequenceLineTooltip()
    // - getCurrentModeConfig(mode), openPlaylistModal(mode), closePlaylistModal()
    // - refreshPlaylistPresets(mode), filterPlaylistPresets(searchTerm)
    // - addToPlaylist(mode), deleteFromPlaylist(mode, id), renamePlaylistPreset(mode, id)
    // - loadPresetInMode(mode, id), quickAddToSequencer(mode, presetId)
    // - loadPresetIntoSequencerModal(mode)
    // - updateStartPresets(maxDist), updateDistancePresets(maxAvailable)
    // Event listeners initialized via initPlaylistListeners() in window.load
    
    // Note: canStartOperation, setButtonState, setupEditableInput, setupPresetButtons, sendCommand loaded from utils.js
    
    // ============================================================================
    // COMMON TOOLS - Loaded from ToolsController.js
    // ============================================================================
    // Note: Tools functions loaded from external module:
    // - calibrateMotor(), resetTotalDistance()
    // - toggleLogsPanel(), closeLogsPanel(), clearLogsPanel(), loadLogFilesList()
    // - toggleStatsPanel(), closeStatsPanel(), clearAllStats(), exportStats()
    // - toggleSystemPanel(), closeSystemPanel(), refreshWifi(), rebootESP32()
    // - loadLoggingPreferences(), saveLoggingPreferences()
    // Event listeners initialized via initToolsListeners() in window.load
    
    // ============================================================================
    // MAX DISTANCE LIMIT CONFIGURATION - Loaded from PursuitController.js
    // ============================================================================
    // Note: updateMaxDistLimitUI(), initMaxDistLimitListeners() in PursuitController.js
    
    // ============================================================================
    // SIMPLE MODE - Loaded from SimpleController.js
    // ============================================================================
    // Note: Simple mode functions loaded from external module:
    // - startSimpleMode(), pauseSimpleMode(), stopSimpleMode()
    // - handleSpeedModeChange(), initSimpleListeners()
    // Event listeners initialized via initSimpleListeners() in window.load
    
    // ========================================================================
    // PLAYLIST BUTTON EVENT LISTENERS - Loaded from PlaylistController.js
    // ========================================================================
    // Note: Event listeners initialized via initPlaylistListeners() in window.load

    // ============================================================================
    // PURSUIT MODE - Loaded from PursuitController.js
    // ============================================================================
    // Note: Pursuit mode functions loaded from external module:
    // - updateGaugePosition(), setGaugeTarget(), sendPursuitCommand()
    // - enablePursuitMode(), disablePursuitMode(), togglePursuitMode()
    // - initPursuitListeners(), initPursuitModeOnLoad()
    // - updateMaxDistLimitUI(), initMaxDistLimitListeners()
    // - isPursuitActive(), getPursuitMaxSpeed(), isEditingMaxDistanceLimit()

    // ============================================================================
    // TAB MANAGEMENT & MODE SWITCHING - Loaded from UIController.js
    // ============================================================================
    // Note: UI/Tab/Modal functions loaded from external module:
    // - switchTab(), isSystemRunning()
    // - cancelModeChange(), confirmModeChange()
    // - showStopModal(), cancelStopModal(), confirmStopModal()
    // - cancelSequencerLimitChange(), confirmSequencerLimitChange()
    // - initUIListeners() (tab click handlers)
    
    // Force initial state on page load (prevent browser cache issues)
    window.addEventListener('load', async function() {
      // Initialize DOM cache FIRST (performance optimization)
      initDOMCache();
      
      // Initialize i18n system (loads translations, applies data-i18n attributes)
      await I18n.init();
      
      // Initialize speed limits based on maxSpeedLevel constant
      initSpeedLimits();
      
      // Initialize UI (tabs & modals) from UIController.js
      initUIListeners();
      
      // Initialize stats panel (from StatsController.js)
      initStatsListeners();
      
      // Initialize tools (from ToolsController.js)
      initToolsListeners();
      
      // Initialize playlist management (from PlaylistController.js)
      initPlaylistListeners();
      
      // Initialize simple mode (from SimpleController.js)
      initSimpleListeners();
      
      // Initialize pursuit mode (from PursuitController.js)
      initPursuitListeners();
      initPursuitModeOnLoad();
      
      // Initialize oscillation mode (from OscillationController.js)
      initOscillationListeners();
      
      // Initialize chaos mode (from ChaosController.js)
      initChaosListeners();
      
      // Initialize sequencer mode (from SequenceController.js)
      initSequenceListeners();
      
      // Request sequence table on load (wait for WebSocket connection)
      function requestSequenceTableWhenReady() {
        if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
          sendCommand(WS_CMD.GET_SEQUENCE_TABLE, {});
        } else {
          setTimeout(requestSequenceTableWhenReady, 200);
        }
      }
      requestSequenceTableWhenReady();
    });
    
    // ============================================================================
    // MODE Séquenceur - Moved to SequenceController.js
    // ============================================================================
    // Note: All sequencer event listeners moved to initSequenceListeners() in SequenceController.js
    // Including: btnAddLine, btnClearAll, btnImportSeq, btnExportSeq, btnDownloadTemplate,
    //   btnStartSequence, btnLoopSequence, btnPauseSequence, btnStopSequence, btnSkipLine,
    //   editLineForm, movement type radios, cycle pause toggles, keyboard shortcuts

    // ============================================================================
    // MAIN NUMERIC INPUTS - Moved to utils.js
    // ============================================================================
    // Note: initMainNumericConstraints() now in utils.js
    initMainNumericConstraints();
    
    // ===== CYCLE PAUSE - Moved to SimpleController.js =====
    // Note: createCyclePauseHandlers(), getCyclePauseSection/Osc() now in SimpleController.js
    // Initialize handlers for Simple and Oscillation modes
    initCyclePauseHandlers();
    
    connectWebSocket();
    
    // Initialize dependency injection context
    if (typeof initContext === 'function') {
      initContext();
    }
    
    loadPlaylists();  // Load playlist presets from backend