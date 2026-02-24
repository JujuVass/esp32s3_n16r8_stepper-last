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
      
      if (currentMode === 'oscillation') {
        updateSpeedDisplayOscillation(data, speedElement, cpmSpan);
      } else if (currentMode === 'chaos') {
        updateSpeedDisplayChaos(data, speedElement, cpmSpan);
      } else if (currentMode === 'pursuit') {
        updateSpeedDisplayPursuit(speedElement, cpmSpan);
      } else if (currentMode === 'sequencer') {
        speedElement.innerHTML = '- ' + t('status.sequenceMode');
        if (cpmSpan) cpmSpan.textContent = '';
      } else if (currentMode === 'simple') {
        updateSpeedDisplaySimple(data, speedElement, cpmSpan);
      }
    }

    /** Oscillation mode speed display */
    function updateSpeedDisplayOscillation(data, speedElement, cpmSpan) {
      if (!(data.oscillation?.frequencyHz !== undefined && data.oscillation?.amplitudeMM !== undefined)) return;
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
    }

    /** Chaos mode speed display */
    function updateSpeedDisplayChaos(data, speedElement, cpmSpan) {
      if (data.chaos?.maxSpeedLevel === undefined) return;
      const speedMMPerSec = data.chaos.maxSpeedLevel * SPEED_LEVEL_TO_MM_S;
      speedElement.innerHTML = '⚡ ' + data.chaos.maxSpeedLevel.toFixed(1);
      if (cpmSpan) cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
    }

    /** Pursuit mode speed display */
    function updateSpeedDisplayPursuit(speedElement, cpmSpan) {
      if (AppState.pursuit.maxSpeedLevel === undefined) return;
      const speedMMPerSec = AppState.pursuit.maxSpeedLevel * SPEED_LEVEL_TO_MM_S;
      speedElement.innerHTML = '⚡ ' + AppState.pursuit.maxSpeedLevel.toFixed(1);
      if (cpmSpan) cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
    }

    /** Simple mode speed display */
    function updateSpeedDisplaySimple(data, speedElement, cpmSpan) {
      if (!(data.motion?.cyclesPerMinForward !== undefined && data.motion?.cyclesPerMinBackward !== undefined)) return;
      if (data.motion.speedLevelForward === undefined || data.motion.speedLevelBackward === undefined) return;
      const avgCpm = ((Number.parseFloat(data.motion.cyclesPerMinForward) + Number.parseFloat(data.motion.cyclesPerMinBackward)) / 2).toFixed(0);
      speedElement.innerHTML = 
        '↗️ ' + data.motion.speedLevelForward.toFixed(1) + 
        '&nbsp;&nbsp;•&nbsp;&nbsp;' +
        '↙️ ' + data.motion.speedLevelBackward.toFixed(1);
      if (cpmSpan) cpmSpan.textContent = '(' + avgCpm + ' c/min)';
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
    
    // ── syncInputsFromBackend sub-helpers (reduce S3776 cognitive complexity) ──

    /** Sync speed inputs for separate or unified mode */
    function syncSpeedInputs(motion) {
      const isSeparateMode = DOM.speedModeSeparate?.checked || false;
      if (isSeparateMode) {
        syncSeparateSpeedInputs(motion);
      } else {
        syncUnifiedSpeedInputs(motion);
      }
    }

    /** Sync separate forward/backward speed inputs + info */
    function syncSeparateSpeedInputs(motion) {
      if (AppState.editing.input !== 'speedForward' && document.activeElement !== DOM.speedForward && motion.speedLevelForward !== undefined) {
        DOM.speedForward.value = motion.speedLevelForward.toFixed(1);
      }
      if (AppState.editing.input !== 'speedBackward' && document.activeElement !== DOM.speedBackward && motion.speedLevelBackward !== undefined) {
        DOM.speedBackward.value = motion.speedLevelBackward.toFixed(1);
      }
      if (DOM.speedForwardInfo && motion.cyclesPerMinForward !== undefined) {
        DOM.speedForwardInfo.textContent = '≈ ' + Number.parseFloat(motion.cyclesPerMinForward).toFixed(0) + ' cycles/min';
      }
      if (DOM.speedBackwardInfo && motion.cyclesPerMinBackward !== undefined) {
        DOM.speedBackwardInfo.textContent = '≈ ' + Number.parseFloat(motion.cyclesPerMinBackward).toFixed(0) + ' cycles/min';
      }
    }

    /** Sync unified speed input + info */
    function syncUnifiedSpeedInputs(motion) {
      if (AppState.editing.input !== 'speedUnified' && document.activeElement !== DOM.speedUnified) {
        if (motion.speedLevelBackward !== undefined) {
          DOM.speedUnified.value = motion.speedLevelBackward.toFixed(1);
        }
        if (motion.speedLevelForward !== undefined) {
          DOM.speedForward.value = motion.speedLevelForward.toFixed(1);
        }
        if (motion.speedLevelBackward !== undefined) {
          DOM.speedBackward.value = motion.speedLevelBackward.toFixed(1);
        }
      }
      if (DOM.speedUnifiedInfo && motion.cyclesPerMinForward !== undefined && motion.cyclesPerMinBackward !== undefined) {
        const avgCyclesPerMin = (Number.parseFloat(motion.cyclesPerMinForward) + Number.parseFloat(motion.cyclesPerMinBackward)) / 2;
        DOM.speedUnifiedInfo.textContent = '≈ ' + avgCyclesPerMin.toFixed(0) + ' cycles/min';
      }
    }

    /**
     * Sync input values with server state (but not if user is editing)
     * @param {Object} data - Status data from backend
     */
    function syncInputsFromBackend(data) {
      if (!data.motion) return;
      if (AppState.editing.input !== 'startPosition' && document.activeElement !== DOM.startPosition && data.motion.startPositionMM !== undefined) {
        DOM.startPosition.value = data.motion.startPositionMM.toFixed(1);
      }
      if (data.motion.targetDistanceMM !== undefined && AppState.editing.input !== 'distance' && document.activeElement !== DOM.distance) {
        DOM.distance.value = Number.parseFloat(data.motion.targetDistanceMM).toFixed(1);
      }
      syncSpeedInputs(data.motion);
    }
    
    /**
     * Update max values, presets and button states
     * @param {Object} data - Status data from backend
     */
    function updateControlsState(data) {
      if (data.totalDistMM !== undefined) {
        updateMaxValuesAndPresets(data);
      }
      
      // Enable/disable start button
      const isRunning = data.state === SystemState.RUNNING;
      const isPausedState = data.state === SystemState.PAUSED;
      const canStart = canStartOperation() && !isRunning && !isPausedState;
      setButtonState(DOM.btnStart, canStart);
      
      updateCalibrateButton(data);
      
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

    /** Update max values, distance presets and label text */
    function updateMaxValuesAndPresets(data) {
      const effectiveMax = (data.effectiveMaxDistMM && data.effectiveMaxDistMM > 0) ? data.effectiveMaxDistMM : data.totalDistMM;
      const startPos = data.motion?.startPositionMM ?? 0;
      const maxAvailable = effectiveMax - startPos;
      
      DOM.startPosition.max = effectiveMax;
      DOM.distance.max = maxAvailable;
      
      if (DOM.maxStart) {
        DOM.maxStart.textContent = (data.maxDistLimitPercent && data.maxDistLimitPercent < 100)
          ? effectiveMax.toFixed(2) + ' (' + data.maxDistLimitPercent.toFixed(0) + '% de ' + data.totalDistMM.toFixed(2) + ')'
          : effectiveMax.toFixed(2);
      }
      
      if (DOM.maxDist) {
        DOM.maxDist.textContent = maxAvailable.toFixed(2);
      }
      updateStartPresets(effectiveMax);
      updateDistancePresets(maxAvailable);
      
      if (typeof updateSimpleRelativePresets === 'function') {
        updateSimpleRelativePresets();
      }
    }

    /** Update calibrate button enabled/disabled state */
    function updateCalibrateButton(data) {
      if (!DOM.btnCalibrateCommon) return;
      const canCalibrate = !!data.canCalibrate;
      DOM.btnCalibrateCommon.disabled = !canCalibrate;
      DOM.btnCalibrateCommon.style.opacity = canCalibrate ? '1' : '0.5';
      DOM.btnCalibrateCommon.style.cursor = canCalibrate ? 'pointer' : 'not-allowed';
    }
    
    // ============================================================================
    // UI UPDATE - HELPERS (reduce S3776 cognitive complexity of updateUI)
    // ============================================================================

    /** Auto-switch to the tab corresponding to the active movement type */
    function autoSwitchToActiveTab(data) {
      const isActiveMovement = (data.state === SystemState.RUNNING || data.state === SystemState.PAUSED);
      if (!isActiveMovement || data.movementType === undefined || data.executionContext === 1) return;
      const tabFromMovement = { 0: 'simple', 1: 'oscillation', 2: 'chaos', 3: 'pursuit' };
      const targetTab = tabFromMovement[data.movementType];
      if (targetTab && AppState.system.currentMode !== targetTab) {
        console.debug('🔄 Auto-switching to ' + targetTab + ' tab (movement in progress)');
        switchTabWithoutStop(targetTab);
      }
    }

    /** Show tabs and controls after first successful calibration */
    function revealInterfaceIfCalibrated(data) {
      if (!AppState.system.canStart || data.totalDistMM <= 0) return;
      const welcomeMessage = DOM.welcomeMessage;
      if (welcomeMessage && !welcomeMessage.classList.contains('hidden')) {
        welcomeMessage.classList.add('hidden');
      }
      if (DOM.tabsContainer?.classList.contains('hidden-until-calibrated')) {
        DOM.tabsContainer.classList.remove('hidden-until-calibrated');
      }
      document.querySelectorAll('.tab-content').forEach(tc => {
        if (tc.classList.contains('hidden-until-calibrated')) tc.classList.remove('hidden-until-calibrated');
      });
    }

    /** Update total distance text, max dist slider, and pursuit gauge limit line */
    function updateDistanceAndGauge(data) {
      if (data.totalDistMM !== undefined) {
        let totalDistText = data.totalDistMM.toFixed(2) + ' mm';
        if (data.maxDistLimitPercent && data.maxDistLimitPercent < 100) {
          totalDistText += ' (' + data.effectiveMaxDistMM.toFixed(2) + ' mm @ ' +
                          data.maxDistLimitPercent.toFixed(0) + '%)';
        }
        DOM.totalDist.textContent = totalDistText;
        if (DOM.maxDist) DOM.maxDist.textContent = data.totalDistMM.toFixed(2);

        if (data.maxDistLimitPercent && !AppState.pursuit.isEditingMaxDistLimit) {
          DOM.maxDistLimitSlider.value = data.maxDistLimitPercent.toFixed(0);
          updateMaxDistLimitUI();
        }

        updatePursuitStateFromData(data);
        updateGaugeLimitLine(data);
      }
      if (data.positionMM !== undefined) {
        AppState.pursuit.currentPositionMM = data.positionMM;
        updateGaugePosition(AppState.pursuit.currentPositionMM);
      }
    }

    /** Sync pursuit AppState variables from incoming data */
    function updatePursuitStateFromData(data) {
      AppState.pursuit.totalDistanceMM = data.totalDistMM;
      if (data.maxDistLimitPercent !== undefined && !AppState.pursuit.isEditingMaxDistLimit) {
        AppState.pursuit.maxDistLimitPercent = data.maxDistLimitPercent;
      }
    }

    /** Show/hide and position the gauge limit line */
    function updateGaugeLimitLine(data) {
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

    /** Show/hide pending changes indicator */
    function updatePendingChangesDisplay(data) {
      const el = DOM.pendingChanges;
      if (!el) return;
      if (data.hasPending && data.pendingMotion) {
        const pm = data.pendingMotion;
        const startPos = Number.parseFloat(pm.startPositionMM);
        const dist = Number.parseFloat(pm.distanceMM);
        el.style.display = 'block';
        el.textContent = '⏳ ' + t('status.pendingChanges') + ': ' +
          startPos.toFixed(1) + ' mm → ' +
          (startPos + dist).toFixed(1) + ' mm (' +
          dist.toFixed(1) + 'mm) @ ' +
          t('simple.forward') + ' ' + Number.parseFloat(pm.speedLevelForward).toFixed(1) + '/20, ' +
          t('simple.backward') + ' ' + Number.parseFloat(pm.speedLevelBackward).toFixed(1) + '/20';
      } else {
        el.style.display = 'none';
      }
    }

    /** Re-enable sequencer start buttons if system is READY and on sequencer tab */
    function syncSequencerButtons(data) {
      const isReady = (data.state === SystemState.READY);
      if (!isReady || !AppState.system.canStart || AppState.system.currentMode !== 'sequencer') return;
      if (AppState.sequence.isTestingLine) {
        AppState.sequence.isTestingLine = false;
      }
      if (DOM.btnStartSequence?.disabled) {
        setButtonState(DOM.btnStartSequence, true);
        setButtonState(DOM.btnLoopSequence, true);
      }
    }

    // ============================================================================
    // UI UPDATE - MAIN FUNCTION
    // ============================================================================
    
    function updateUI(data) {
      if (!data || !('positionMM' in data)) return;
      if (!DOM.state) return;

      // Update global state for mode change logic
      AppState.system.currentState = data.state;
      AppState.system.canStart = data.canStart || false;
      
      autoSwitchToActiveTab(data);
      updateStateDisplay(data);
      revealInterfaceIfCalibrated(data);
      updatePositionDisplay(data);
      updateDistanceAndGauge(data);
      updateSpeedDisplay(data);
      
      if (data.totalTraveled !== undefined) {
        updateMilestones(data.totalTraveled);
        updateRealSpeed(data.totalTraveled);
      }
      
      updateProgressBar(data);
      syncInputsFromBackend(data);
      updateControlsState(data);
      if (data.decelZone) updateZoneEffectUI(data.decelZone);
      updatePendingChangesDisplay(data);
      
      // Delegated controller updates
      updateOscillationUI(data);
      updateSimpleUI(data);
      updateChaosUI(data);
      
      syncSequencerButtons(data);
      
      if (data.system) updateSystemStats(data.system);
      if (data.statsRecordingEnabled !== undefined) updateStatsRecordingUI(data.statsRecordingEnabled);
      updateSensorsInvertedUI(data);
    }

    /** Update state text, CSS class, and error message display */
    function updateStateDisplay(data) {
      const stateText = t('states') || ['Needs calibration', 'Calibrating', 'Ready', 'Running', 'Paused', 'Error'];
      const stateClass = ['state-init', 'state-calibrating', 'state-ready', 'state-running', 'state-paused', 'state-error'];
      let displayText = stateText[data.state] || t('common.error');
      if (data.errorMessage && data.errorMessage !== '') {
        displayText += ' ⚠️ ' + data.errorMessage;
      }
      DOM.state.textContent = displayText;
      DOM.state.className = 'status-value ' + (stateClass[data.state] || '');
      if (DOM.calibrationOverlay) {
        DOM.calibrationOverlay.classList.toggle('active', data.state === SystemState.CALIBRATING);
      }
      if (DOM.updateOverlay) {
        const wasActive = DOM.updateOverlay.classList.contains('active');
        const isActive = !!data.updating;
        if (!wasActive && isActive) {
          console.log('📦 Upload started — overlay shown');
        } else if (wasActive && !isActive) {
          console.log('✅ Upload finished — overlay hidden');
        }
        DOM.updateOverlay.classList.toggle('active', isActive);
      }
    }

    /** Update position text display */
    function updatePositionDisplay(data) {
      if (data.positionMM !== undefined && data.currentStep !== undefined) {
        currentPositionMM = data.positionMM;
        DOM.position.textContent = data.positionMM.toFixed(2) + ' mm (' + data.currentStep + ' steps)';
      }
    }

    /** Update progress bar width and percentage text */
    function updateProgressBar(data) {
      if (data.totalDistMM > 0 && data.positionMM !== undefined) {
        const progress = (data.positionMM / data.totalDistMM) * 100;
        if (DOM.progressMini) DOM.progressMini.style.width = progress + '%';
        if (DOM.progressPct) DOM.progressPct.textContent = progress.toFixed(1) + '%';
      }
    }

    /** Update sensors inverted checkbox and indicator */
    function updateSensorsInvertedUI(data) {
      if (data.sensorsInverted === undefined) return;
      DOM.chkSensorsInverted.checked = data.sensorsInverted;
      DOM.sensorsInvertedStatus.textContent = data.sensorsInverted ? '(' + t('common.inverted') + ')' : '(' + t('common.normal') + ')';
      const invertedIcon = DOM.sensorsInvertedIcon;
      if (invertedIcon) invertedIcon.style.display = data.sensorsInverted ? 'inline' : 'none';
    }
    
    // ============================================================================
    // TAB MANAGEMENT & MODE SWITCHING - Loaded from UIController.js
    // ============================================================================
    
    // Initialize application immediately (DOM is already parsed, all scripts loaded)
    // Note: No need for 'load' event — the script loader guarantees all scripts
    // are executed in order, and main.js is last. The DOM is ready.
    (async function boot() { // NOSONAR(javascript:S7785) Classic script, not ES module
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
        
        // Use IP resolved by the script loader (avoids mDNS resolution delay)
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