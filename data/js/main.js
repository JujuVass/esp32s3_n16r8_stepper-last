    // ========================================================================
    // MAIN APPLICATION CODE
    // ========================================================================

    // ========================================================================
    // GLOBAL STATE - Position Tracking
    // ========================================================================
    let currentPositionMM = 0; // Current motor position in millimeters
    
    // ============================================================================
    // UI UPDATE - HELPER FUNCTIONS
    // ============================================================================
    
    /**
     * Update speed display based on current active mode
     * @param {Object} data - Status data from backend
     */
    function updateSpeedDisplay(data) {
      const speedElement = DOM.currentSpeed;
      const cpmSpan = speedElement ? speedElement.nextElementSibling : null;
      const currentMode = AppState.system.currentMode;
      
      if (currentMode === 'oscillation' && data.oscillation?.frequencyHz !== undefined && data.oscillation?.amplitudeMM !== undefined) {
        // OSCILLATION MODE: Show ACTUAL speed (backend calculates with hardware limits)
        let displaySpeed;
        let isLimited = false;
        
        if (data.oscillation.actualSpeedMMS !== undefined && data.oscillation.actualSpeedMMS > 0) {
          displaySpeed = Number.parseFloat(data.oscillation.actualSpeedMMS);
          const theoreticalSpeed = 2 * Math.PI * data.oscillation.frequencyHz * data.oscillation.amplitudeMM;
          isLimited = (displaySpeed < theoreticalSpeed - 1);
        } else {
          displaySpeed = 2 * Math.PI * data.oscillation.frequencyHz * data.oscillation.amplitudeMM;
        }
        
        speedElement.innerHTML = '🌊 ' + displaySpeed.toFixed(0) + ' mm/s' + (isLimited ? ' ⚠️' : '');
        if (cpmSpan) {
          cpmSpan.textContent = '(pic, f=' + data.oscillation.frequencyHz.toFixed(2) + ' Hz' + (isLimited ? ', ' + t('status.speedLimited') : '') + ')';
        }
      } else if (currentMode === 'chaos' && data.chaos?.maxSpeedLevel !== undefined) {
        // CHAOS MODE: Show max speed level
        const speedMMPerSec = data.chaos.maxSpeedLevel * SPEED_LEVEL_TO_MM_S;
        speedElement.innerHTML = '⚡ ' + data.chaos.maxSpeedLevel.toFixed(1);
        if (cpmSpan) {
          cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
        }
      } else if (currentMode === 'pursuit' && AppState.pursuit.maxSpeedLevel !== undefined) {
        // PURSUIT MODE: Show max speed level from UI variable
        const speedMMPerSec = AppState.pursuit.maxSpeedLevel * SPEED_LEVEL_TO_MM_S;
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
      } else if (currentMode === 'simple' && data.motion?.cyclesPerMinForward !== undefined && data.motion?.cyclesPerMinBackward !== undefined) {
        // SIMPLE MODE: Show forward/backward speeds with cycles/min
        if (data.motion.speedLevelForward !== undefined && data.motion.speedLevelBackward !== undefined) {
          const avgCpm = ((Number.parseFloat(data.motion.cyclesPerMinForward) + Number.parseFloat(data.motion.cyclesPerMinBackward)) / 2).toFixed(0);
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
      const speedIconEl = DOM.speedIcon;
      const realSpeedEl = DOM.realSpeed;
      
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
          DOM.distance.value = Number.parseFloat(data.motion.targetDistanceMM).toFixed(1);
        }
      }
      
      // Update speed values based on unified/separate mode
      const isSeparateMode = DOM.speedModeSeparate?.checked || false;
      
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
            DOM.speedForwardInfo.textContent = '≈ ' + Number.parseFloat(data.motion.cyclesPerMinForward).toFixed(0) + ' cycles/min';
          }
          if (DOM.speedBackwardInfo && data.motion.cyclesPerMinBackward !== undefined) {
            DOM.speedBackwardInfo.textContent = '≈ ' + Number.parseFloat(data.motion.cyclesPerMinBackward).toFixed(0) + ' cycles/min';
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
            const avgCyclesPerMin = (Number.parseFloat(data.motion.cyclesPerMinForward) + Number.parseFloat(data.motion.cyclesPerMinBackward)) / 2;
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
        const startPos = data.motion?.startPositionMM ?? 0;
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
        if (data.canCalibrate) {
          DOM.btnCalibrateCommon.disabled = false;
          DOM.btnCalibrateCommon.style.opacity = '1';
          DOM.btnCalibrateCommon.style.cursor = 'pointer';
        } else {
          DOM.btnCalibrateCommon.disabled = true;
          DOM.btnCalibrateCommon.style.opacity = '0.5';
          DOM.btnCalibrateCommon.style.cursor = 'not-allowed';
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
        return;
      }
      
      // Guard: DOM cache must be initialized before we can update the UI
      if (!DOM.state) {
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
          console.debug('🔄 Auto-switching to ' + targetTab + ' tab (movement in progress)');
          // Use direct tab switch without stopping movement (since it's already running)
          switchTabWithoutStop(targetTab);
        }
      }
      
      const stateText = t('states') || ['Needs calibration', 'Calibrating', 'Ready', 'Running', 'Paused', 'Error'];
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
        const tabsContainer = DOM.tabsContainer;
        const allTabContents = document.querySelectorAll('.tab-content');
        const welcomeMessage = DOM.welcomeMessage;
        
        // Hide welcome message
        if (welcomeMessage && !welcomeMessage.classList.contains('hidden')) {
          welcomeMessage.classList.add('hidden');
        }
        
        // Show tabs
        if (tabsContainer?.classList.contains('hidden-until-calibrated')) {
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
        const progressMini = DOM.progressMini;
        const progressPct = DOM.progressPct;
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
        const startPos = Number.parseFloat(pm.startPositionMM);
        const dist = Number.parseFloat(pm.distanceMM);
        pendingChanges.style.display = 'block';
        pendingChanges.textContent = '⏳ ' + t('status.pendingChanges') + ': ' + 
          startPos.toFixed(1) + ' mm → ' + 
          (startPos + dist).toFixed(1) + ' mm (' +
          dist.toFixed(1) + 'mm) @ ' + 
          t('simple.forward') + ' ' + Number.parseFloat(pm.speedLevelForward).toFixed(1) + '/20, ' +
          t('simple.backward') + ' ' + Number.parseFloat(pm.speedLevelBackward).toFixed(1) + '/20';
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
        if (DOM.btnStartSequence?.disabled) {
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
        const invertedIcon = DOM.sensorsInvertedIcon;
        if (invertedIcon) {
          invertedIcon.style.display = data.sensorsInverted ? 'inline' : 'none';
        }
      }
    }
    
    // ============================================================================
    // TAB MANAGEMENT & MODE SWITCHING - Loaded from UIController.js
    // ============================================================================
    
    // Initialize application immediately (DOM is already parsed, all scripts loaded)
    // Note: No need for 'load' event — the script loader guarantees all scripts
    // are executed in order, and main.js is last. The DOM is ready.
    (async function boot() {
      try {
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
        
        // Initialize numeric input constraints (from utils.js)
        initMainNumericConstraints();
        
        // Initialize cycle pause handlers (from SimpleController.js)
        initCyclePauseHandlers();
        
        // Connect WebSocket AFTER DOM cache is ready
        // This ensures updateUI() can access DOM.state, DOM.position, etc.
        // when the first status response arrives from the ESP32
        
        // Use IP resolved by the script loader (avoids mDNS for WebSocket port 81)
        // Only use if on same subnet as current page (prevents STA IP when on AP)
        if (globalThis.__espIp && typeof isSameSubnet === 'function' 
            && isSameSubnet(globalThis.__espIp, globalThis.location.hostname)) {
          AppState.espIpAddress = globalThis.__espIp;
          console.debug('📡 Using pre-resolved ESP32 IP:', globalThis.__espIp);
        } else if (globalThis.__espIp) {
          console.debug('⚠️ Pre-resolved IP', globalThis.__espIp, 'not on same subnet - using', globalThis.location.hostname);
        }
        
        connectWebSocket();
        
        // Request sequence table on load (wait for WebSocket connection)
        function requestSequenceTableWhenReady() {
          if (AppState.ws?.readyState === WebSocket.OPEN) {
            sendCommand(WS_CMD.GET_SEQUENCE_TABLE, {});
          } else {
            setTimeout(requestSequenceTableWhenReady, 200);
          }
        }
        requestSequenceTableWhenReady();
        
        console.debug('🚀 Boot complete');
      } catch(e) {
        console.error('🚀 Boot FAILED:', e);
      }
    })();