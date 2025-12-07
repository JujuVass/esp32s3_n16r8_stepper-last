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
    
    // ========================================================================
    // HELPER FUNCTIONS - Browser Compatibility
    // ========================================================================
    // Use .closest() instead of :has() CSS selector for broader browser support
    function getCyclePauseSection() {
      const header = document.getElementById('cyclePauseHeaderText');
      return header ? header.closest('.section-collapsible') : null;
    }
    
    function getCyclePauseOscSection() {
      const header = document.getElementById('cyclePauseOscHeaderText');
      return header ? header.closest('.section-collapsible') : null;
    }

    // ========================================================================
    // GLOBAL STATE - Position Tracking
    // ========================================================================
    let currentPositionMM = 0; // Current motor position in millimeters
    
    // ========================================================================
    // MAXGLOSPE GLOBAL STATE (MAX_SPEED_LEVEL) - Max speed level (don't forget to update .ino if changed))
    // ========================================================================
    let maxSpeedLevel = 35; // Max speed level in mm/s

    // Initialize speed input max attributes based on maxSpeedLevel
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
    function updateMilestones(totalTraveledMM) {
      DOM.totalTraveled.textContent = (totalTraveledMM / 1000.0).toFixed(3) + " m";
      
      const milestoneInfo = getMilestoneInfo(totalTraveledMM / 1000.0); // Convert mm to m
      
      if (milestoneInfo.current) {
        // Build tooltip with progress info
        let tooltip = `${milestoneInfo.current.emoji} ${milestoneInfo.current.name} (${milestoneInfo.current.threshold}m)`;
        if (milestoneInfo.current.location !== "-") {
          tooltip += ` - ${milestoneInfo.current.location}`;
        }
        
        if (milestoneInfo.next) {
          tooltip += `\n\n⏭️ Prochain: ${milestoneInfo.next.emoji} ${milestoneInfo.next.name} (${milestoneInfo.next.threshold}m)`;
          tooltip += `\n📊 Progression: ${milestoneInfo.progress.toFixed(0)}%`;
        } else {
          tooltip += `\n\n🎉 Dernier jalon atteint!`;
        }
        
        DOM.milestoneIcon.textContent = milestoneInfo.current.emoji;
        DOM.milestoneIcon.title = tooltip;
        
        // Milestone tracking logic
        const newThreshold = milestoneInfo.current.threshold;
        const totalTraveledM = totalTraveledMM / 1000.0;
        
        // Handle distance reset (user cleared stats) - reset tracking if distance dropped significantly
        if (totalTraveledM < AppState.milestone.lastThreshold * 0.9) {
          AppState.milestone.lastThreshold = 0;
          AppState.milestone.initialized = false;
        }
        
        // Check for milestone change
        if (newThreshold > AppState.milestone.lastThreshold) {
          if (!AppState.milestone.initialized) {
            // First load - sync without notification
            AppState.milestone.initialized = true;
            AppState.milestone.lastThreshold = newThreshold;
          } else {
            // New milestone achieved during session!
            AppState.milestone.lastThreshold = newThreshold;
            
            // Trigger icon animation
            DOM.milestoneIcon.classList.remove('milestone-achievement');
            void DOM.milestoneIcon.offsetWidth;
            DOM.milestoneIcon.classList.add('milestone-achievement');
            
            // Show celebration notification
            let message = `🎉 Jalon atteint: ${milestoneInfo.current.emoji} (${newThreshold}m)`;
            if (milestoneInfo.next) {
              message += `\n⏭️ Prochain: ${milestoneInfo.next.emoji} (${milestoneInfo.next.threshold}m) - ${milestoneInfo.progress.toFixed(0)}%`;
            }
            showNotification(message, 'milestone');
          }
        } else if (!AppState.milestone.initialized) {
          // First load, same milestone - just mark initialized
          AppState.milestone.initialized = true;
          AppState.milestone.lastThreshold = newThreshold;
        }
        
        AppState.milestone.current = milestoneInfo.current;
      } else {
        // No milestone reached yet - mark as initialized anyway
        AppState.milestone.initialized = true;
        if (milestoneInfo.next) {
          const tooltip = `⏭️ Prochain: ${milestoneInfo.next.emoji} ${milestoneInfo.next.name} (${milestoneInfo.next.threshold}m)\n📊 Progression: ${milestoneInfo.progress.toFixed(0)}%`;
          DOM.milestoneIcon.textContent = '🐜';
          DOM.milestoneIcon.title = tooltip;
        } else {
          DOM.milestoneIcon.textContent = '';
          DOM.milestoneIcon.title = '';
        }
      }
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
          // Clear the request flag to avoid showing notification again
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
    
    // ============================================================================
    // UI UPDATE
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
      
      const stateText = ['Initialisation', 'Calibration...', 'Prêt', 'En marche', 'En pause', 'Erreur'];
      const stateClass = ['state-init', 'state-calibrating', 'state-ready', 'state-running', 'state-paused', 'state-error'];
      
      let displayText = stateText[data.state] || 'Inconnu';
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
        if (data.maxDistLimitPercent && !isEditingMaxDistLimit) {
          DOM.maxDistLimitSlider.value = data.maxDistLimitPercent.toFixed(0);
          updateMaxDistLimitUI();
        }
      }
      
      // Update pursuit mode variables
      if (data.totalDistMM !== undefined) {
        AppState.pursuit.totalDistanceMM = data.totalDistMM;
        
        // Store max distance limit percent in AppState (but not while user is editing!)
        if (data.maxDistLimitPercent !== undefined && !isEditingMaxDistLimit) {
          AppState.pursuit.maxDistLimitPercent = data.maxDistLimitPercent;
        }
        
        // Update gauge limit line
        if (data.maxDistLimitPercent && data.maxDistLimitPercent < 100 && data.effectiveMaxDistMM) {
          const limitPercent = (data.effectiveMaxDistMM / data.totalDistMM);
          const containerHeight = DOM.gaugeContainer ? DOM.gaugeContainer.offsetHeight : 500;
          const limitPixelPosition = containerHeight - (limitPercent * containerHeight);
          
          DOM.gaugeLimitLine.style.top = limitPixelPosition + 'px';
          DOM.gaugeLimitLine.style.display = 'block';
        } else if (DOM.gaugeLimitLine) {
          DOM.gaugeLimitLine.style.display = 'none';
        }
      }
      if (data.positionMM !== undefined) {
        AppState.pursuit.currentPositionMM = data.positionMM;
        updateGaugePosition(AppState.pursuit.currentPositionMM);
      }
      
      // Update speed display based on CURRENT ACTIVE MODE (not just data availability)
      const speedElement = document.getElementById('currentSpeed');
      const cpmSpan = speedElement ? speedElement.nextElementSibling : null;
      const currentMode = AppState.system.currentMode;
      
      if (currentMode === 'oscillation' && data.oscillation && data.oscillation.frequencyHz !== undefined && data.oscillation.amplitudeMM !== undefined) {
        // OSCILLATION MODE: Show ACTUAL speed (backend calculates with hardware limits)
        // If actualSpeedMMS is provided, use it (accounts for adaptive delay)
        // Otherwise fallback to theoretical peak speed calculation
        let displaySpeed;
        let isLimited = false;
        
        if (data.oscillation.actualSpeedMMS !== undefined && data.oscillation.actualSpeedMMS > 0) {
          // Use actual speed from backend (considers hardware limits)
          displaySpeed = parseFloat(data.oscillation.actualSpeedMMS);
          
          // Check if speed was limited
          const theoreticalSpeed = 2 * Math.PI * data.oscillation.frequencyHz * data.oscillation.amplitudeMM;
          isLimited = (displaySpeed < theoreticalSpeed - 1); // 1 mm/s tolerance
        } else {
          // Fallback: calculate theoretical peak speed
          displaySpeed = 2 * Math.PI * data.oscillation.frequencyHz * data.oscillation.amplitudeMM;
        }
        
        speedElement.innerHTML = '🌊 ' + displaySpeed.toFixed(0) + ' mm/s' + (isLimited ? ' ⚠️' : '');
        
        if (cpmSpan) {
          cpmSpan.textContent = '(pic, f=' + data.oscillation.frequencyHz.toFixed(2) + ' Hz' + 
                                (isLimited ? ', limité' : '') + ')';
        }
      } else if (currentMode === 'chaos' && data.chaos && data.chaos.maxSpeedLevel !== undefined) {
        // CHAOS MODE: Show max speed level
        const speedMMPerSec = data.chaos.maxSpeedLevel * 10.0;
        speedElement.innerHTML = '⚡ ' + data.chaos.maxSpeedLevel.toFixed(1);
        
        if (cpmSpan) {
          cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
        }
      } else if (currentMode === 'pursuit' && pursuitMaxSpeedLevel !== undefined) {
        // PURSUIT MODE: Show max speed level from UI variable
        const speedMMPerSec = pursuitMaxSpeedLevel * 10.0;
        speedElement.innerHTML = '⚡ ' + pursuitMaxSpeedLevel.toFixed(1);
        
        if (cpmSpan) {
          cpmSpan.textContent = '(max ' + speedMMPerSec.toFixed(0) + ' mm/s)';
        }
      } else if (currentMode === 'sequencer') {
        // SEQUENCER MODE: Show mode indicator
        speedElement.innerHTML = '- (mode séquence)';
        if (cpmSpan) {
          cpmSpan.textContent = '';
        }
      } else if (currentMode === 'simple' && data.motion && data.cyclesPerMinForward !== undefined && data.cyclesPerMinBackward !== undefined) {
        // SIMPLE MODE: Show forward/backward speeds with cycles/min
        // Defense: Check motion fields exist before accessing
        if (data.motion.speedLevelForward !== undefined && data.motion.speedLevelBackward !== undefined) {
          const avgCpm = ((data.cyclesPerMinForward + data.cyclesPerMinBackward) / 2).toFixed(0);
          speedElement.innerHTML = 
            '↗️ ' + data.motion.speedLevelForward.toFixed(1) + 
            '&nbsp;&nbsp;•&nbsp;&nbsp;' +
            '↙️ ' + data.motion.speedLevelBackward.toFixed(1);
          
          if (cpmSpan) {
            cpmSpan.textContent = '(' + avgCpm + ' c/min)';
          }
        }
      }
      
      // Update milestones (delegated to helper function)
      if (data.totalTraveled !== undefined) {
        updateMilestones(data.totalTraveled);
      }
      
      if (data.totalDistMM > 0 && data.positionMM !== undefined) {
        const progress = (data.positionMM / data.totalDistMM) * 100;
        const progressMini = document.getElementById('progressMini');
        const progressPct = document.getElementById('progressPct');
        if (progressMini) progressMini.style.width = progress + '%';
        if (progressPct) progressPct.textContent = progress.toFixed(1) + '%';
      }
      
      // Sync input values with server state (but not if user is editing)
      // Also check activeElement to prevent overwriting during typing
      // Defense: Check data.motion exists before accessing fields
      if (data.motion) {
        if (AppState.editing.input !== 'startPosition' && document.activeElement !== DOM.startPosition && data.motion.startPositionMM !== undefined) {
          DOM.startPosition.value = data.motion.startPositionMM.toFixed(1);
        }
      }
      
      if (data.targetDistMM !== undefined && AppState.editing.input !== 'distance' && document.activeElement !== DOM.distance) {
        DOM.distance.value = data.targetDistMM.toFixed(1);
      }
      
      // Update speed values based on unified/separate mode
      const isSeparateMode = document.getElementById('speedModeSeparate')?.checked || false;
      
      // Defense: Check data.motion exists before accessing speed fields
      if (data.motion) {
        if (isSeparateMode) {
          // SEPARATE MODE: update forward and backward individually
          if (AppState.editing.input !== 'speedForward' && document.activeElement !== DOM.speedForward && data.motion.speedLevelForward !== undefined) {
            DOM.speedForward.value = data.motion.speedLevelForward.toFixed(1);
          }
          if (AppState.editing.input !== 'speedBackward' && document.activeElement !== DOM.speedBackward && data.motion.speedLevelBackward !== undefined) {
            DOM.speedBackward.value = data.motion.speedLevelBackward.toFixed(1);
          }
          // Speed info removed in compact mode
          if (DOM.speedForwardInfo && data.cyclesPerMinForward !== undefined) {
            DOM.speedForwardInfo.textContent = 
              '≈ ' + data.cyclesPerMinForward.toFixed(0) + ' cycles/min';
          }
          if (DOM.speedBackwardInfo && data.cyclesPerMinBackward !== undefined) {
            DOM.speedBackwardInfo.textContent = 
              '≈ ' + data.cyclesPerMinBackward.toFixed(0) + ' cycles/min';
          }
        } else {
          // UNIFIED MODE: show current speed (should be same for both directions)
          if (AppState.editing.input !== 'speedUnified' && document.activeElement !== DOM.speedUnified) {
            // Use backward speed as reference (shows what user just changed)
            // In unified mode, both speeds should be identical, but we show backward
            // to ensure the displayed value reflects the most recent change
            if (data.motion.speedLevelBackward !== undefined) {
              DOM.speedUnified.value = data.motion.speedLevelBackward.toFixed(1);
            }
            
            // Also keep separate fields in sync (hidden but used when switching modes)
            if (data.motion.speedLevelForward !== undefined) {
              DOM.speedForward.value = data.motion.speedLevelForward.toFixed(1);
            }
            if (data.motion.speedLevelBackward !== undefined) {
              DOM.speedBackward.value = data.motion.speedLevelBackward.toFixed(1);
            }
          }
          
          // Speed info removed in compact mode
          if (DOM.speedUnifiedInfo && data.cyclesPerMinForward !== undefined && data.cyclesPerMinBackward !== undefined) {
            const avgCyclesPerMin = (data.cyclesPerMinForward + data.cyclesPerMinBackward) / 2.0;
            DOM.speedUnifiedInfo.textContent = 
              '≈ ' + avgCyclesPerMin.toFixed(0) + ' cycles/min';
          }
        }
      }
      
      // Update max values and presets
      // Use effective max distance (factored) if available, otherwise total
      // Defense: Check data.motion exists before accessing startPositionMM
      if (data.totalDistMM !== undefined) {
        const effectiveMax = (data.effectiveMaxDistMM && data.effectiveMaxDistMM > 0) ? data.effectiveMaxDistMM : data.totalDistMM;
        const startPos = (data.motion && data.motion.startPositionMM !== undefined) ? data.motion.startPositionMM : 0;
        const maxAvailable = effectiveMax - startPos;
        
        DOM.startPosition.max = effectiveMax;
        DOM.distance.max = maxAvailable;
        
        // Show factored value if limit < 100% (only if elements exist)
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
      }
      
      // Enable/disable start button
      const isRunning = data.state === SystemState.RUNNING;
      const isPausedState = data.state === SystemState.PAUSED;
      const canStart = canStartOperation() && !isRunning && !isPausedState;
      
      setButtonState(DOM.btnStart, canStart);
      
      // Enable/disable calibrate button (now in common tools section)
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
      
      // Update input fields state
      [DOM.startPosition, DOM.distance, DOM.speedUnified, DOM.speedForward, DOM.speedBackward].forEach(input => {
        if (input) {
          input.disabled = !inputsEnabled;
          input.style.opacity = inputsEnabled ? '1' : '0.6';
        }
      });
      
      // Update pursuit controls
      if (DOM.pursuitActiveCheckbox) DOM.pursuitActiveCheckbox.disabled = !inputsEnabled;
      setButtonState(DOM.btnActivatePursuit, inputsEnabled);
      
      // Update deceleration zone configuration from server (delegated to helper function)
      if (data.decelZone) {
        updateDecelZoneUI(data.decelZone);
      }
      
      // Show pending changes indicator
      const pendingChanges = document.getElementById('pendingChanges');
      if (data.hasPending) {
        pendingChanges.style.display = 'block';
        pendingChanges.textContent = '⏳ Changements en attente: ' + 
          data.pendingStartPos.toFixed(1) + ' mm → ' + 
          (data.pendingStartPos + data.pendingDist).toFixed(1) + ' mm (' +
          data.pendingDist.toFixed(1) + 'mm) @ ' + 
          'Aller: ' + data.pendingMotion.speedLevelForward.toFixed(1) + '/20, ' +
          'Retour: ' + data.pendingMotion.speedLevelBackward.toFixed(1) + '/20 (fin de cycle)';
      } else {
        pendingChanges.style.display = 'none';
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
        if (window.isTestingLine) {
          window.isTestingLine = false;
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
      
      // Update system stats
      if (data.system) {
        updateSystemStats(data.system);
      }
    }
    
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
      
      // WiFi
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
    // - sendSimpleCyclePauseConfig(), startSimpleMode(), pauseSimpleMode(), stopSimpleMode()
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
    // TAB MANAGEMENT & MODE SWITCHING
    // ============================================================================
    
    // Note: SystemState enum loaded from app.js
    // Note: Mode switching variables now in AppState.system
    // Access via: AppState.system.currentMode, AppState.system.pendingModeSwitch, etc.
    
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
    
    function isSystemRunning() {
      // State 3 = RUNNING, 4 = PAUSED (for simple mode)
      // Also check if pursuit mode is active (isPursuitActive from PursuitController.js)
      return AppState.system.currentState === SystemState.RUNNING || 
             AppState.system.currentState === SystemState.PAUSED || 
             isPursuitActive();
    }
    
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
    
    function showStopModal() {
      document.getElementById('stopModal').classList.add('active');
    }
    
    function cancelStopModal() {
      document.getElementById('stopModal').classList.remove('active');
      // Reset checkbox for next time (keep checked by default)
      document.getElementById('returnToStartCheckbox').checked = true;
    }
    
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
    
    // Sequencer Limit Modal Functions
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
    
    // Force initial state on page load (prevent browser cache issues)
    window.addEventListener('load', function() {
      // Initialize DOM cache FIRST (performance optimization)
      initDOMCache();
      
      // Initialize speed limits based on maxSpeedLevel constant
      initSpeedLimits();
      
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
    // MODE Séquenceur - Event Listeners
    // ============================================================================
    
    document.getElementById('btnAddLine').addEventListener('click', addSequenceLine);
    document.getElementById('btnClearAll').addEventListener('click', clearSequence);
    document.getElementById('btnImportSeq').addEventListener('click', importSequence);
    document.getElementById('btnExportSeq').addEventListener('click', exportSequence);
    document.getElementById('btnDownloadTemplate').addEventListener('click', downloadTemplate);
    
    // Phase 2: Keyboard shortcuts for multi-select
    document.addEventListener('keydown', function(e) {
      // Only handle when on sequencer tab
      if (AppState.system.currentMode !== 'tableau') return;
      
      // Escape: Clear selection
      if (e.key === 'Escape' && selectedLineIds.size > 0) {
        clearSelection();
        e.preventDefault();
      }
      
      // Ctrl+A: Select all
      if ((e.ctrlKey || e.metaKey) && e.key === 'a' && sequenceLines.length > 0) {
        selectedLineIds.clear();
        sequenceLines.forEach(line => selectedLineIds.add(line.lineId));
        lastSelectedIndex = 0;
        updateBatchToolbar();
        renderSequenceTable({ lines: sequenceLines });
        e.preventDefault();
      }
      
      // Delete: Delete selected lines
      if (e.key === 'Delete' && selectedLineIds.size > 0) {
        batchDeleteLines();
        e.preventDefault();
      }
    });
    
    // ============================================================================
    // LOGS, STATS, SYSTEM PANELS - Loaded from ToolsController.js
    // ============================================================================
    // Note: Panel management functions loaded from external module:
    // - toggleLogsPanel(), closeLogsPanel(), clearLogsPanel(), loadLogFilesList()
    // - toggleStatsPanel(), closeStatsPanel(), clearAllStats(), exportStats()
    // - triggerStatsImport(), handleStatsFileImport() (import stats)
    // - toggleSystemPanel(), closeSystemPanel(), refreshWifi(), rebootESP32()
    // - loadLoggingPreferences(), saveLoggingPreferences(), reconnectAfterReboot()
    // Event listeners initialized via initToolsListeners() in window.load

    // Note: loadStatsData, displayStatsTable, displayStatsChart loaded from stats.js
    
    document.getElementById('btnStartSequence').addEventListener('click', function() {
      // Reset test mode flag in case it was left on from a failed test
      window.isTestingLine = false;
      // Disable both start buttons immediately (instant feedback)
      setButtonState(DOM.btnStartSequence, false);
      setButtonState(DOM.btnLoopSequence, false);
      sendCommand(WS_CMD.START_SEQUENCE, {});
    });
    
    document.getElementById('btnLoopSequence').addEventListener('click', function() {
      // Reset test mode flag in case it was left on from a failed test
      window.isTestingLine = false;
      // Disable both start buttons immediately (instant feedback)
      setButtonState(DOM.btnStartSequence, false);
      setButtonState(DOM.btnLoopSequence, false);
      sendCommand(WS_CMD.LOOP_SEQUENCE, {});
    });
    
    document.getElementById('btnPauseSequence').addEventListener('click', function() {
      sendCommand(WS_CMD.TOGGLE_SEQUENCE_PAUSE, {});
    });
    
    document.getElementById('btnStopSequence').addEventListener('click', function() {
      // Only show modal if motor has moved (currentStep > 0)
      if (currentPositionMM > 0.5) {
        showStopModal();
      } else {
        // Direct stop if at position 0
        sendCommand(WS_CMD.STOP_SEQUENCE, {});
      }
    });
    
    // ============================================================================
    // MODE OSCILLATION - Loaded from OscillationController.js
    // ============================================================================
    // Note: Oscillation mode functions loaded from external module:
    // - toggleOscHelp()
    // - validateOscillationLimits(), sendOscillationConfig()
    // - updateOscillationPresets(), sendOscCyclePauseConfig()
    // - startOscillation(), stopOscillation(), pauseOscillation()
    // - initOscillationListeners()

    // ============================================================================
    // CHAOS MODE - Loaded from ChaosController.js
    // ============================================================================
    // Note: Chaos mode functions loaded from external module:
    // - toggleChaosHelp()
    // - sendChaosConfig(), validateChaosLimits()
    // - updateChaosPresets(), updatePatternToggleButton()
    // - updateChaosUI(data)
    // - startChaos(), stopChaos(), pauseChaos()
    // - enableAllPatterns(), disableAllPatterns(), toggleAllPatterns()
    // - enableSoftPatterns(), enableDynamicPatterns()
    // - initChaosListeners()
    
    document.getElementById('btnSkipLine').addEventListener('click', function() {
      sendCommand(WS_CMD.SKIP_SEQUENCE_LINE, {});
    });
    
    // Modal handlers
    document.getElementById('editLineForm').addEventListener('submit', saveLineEdit);
    document.getElementById('btnCancelEdit').addEventListener('click', closeEditModal);
    document.getElementById('btnCloseModal').addEventListener('click', closeEditModal);
    
    // Movement type radio buttons
    document.getElementById('editTypeVaet').addEventListener('change', updateMovementTypeFields);
    document.getElementById('editTypeOsc').addEventListener('change', updateMovementTypeFields);
    document.getElementById('editTypeChaos').addEventListener('change', updateMovementTypeFields);
    document.getElementById('editTypeCalibration').addEventListener('change', updateMovementTypeFields);
    
    // Cycle Pause toggles (VA-ET-VIENT)
    document.getElementById('editVaetPauseEnabled').addEventListener('change', function() {
      const enabled = this.checked;
      document.getElementById('vaetPauseFixedDiv').style.display = enabled ? 'grid' : 'none';
      document.getElementById('vaetPauseRandomDiv').style.display = (enabled && document.getElementById('editVaetPauseRandom').checked) ? 'grid' : 'none';
    });
    document.getElementById('editVaetPauseRandom').addEventListener('change', function() {
      const isRandom = this.checked;
      const enabled = document.getElementById('editVaetPauseEnabled').checked;
      document.getElementById('vaetPauseFixedDiv').style.display = (enabled && !isRandom) ? 'grid' : 'none';
      document.getElementById('vaetPauseRandomDiv').style.display = (enabled && isRandom) ? 'grid' : 'none';
    });
    
    // Cycle Pause toggles (OSCILLATION)
    document.getElementById('editOscPauseEnabled').addEventListener('change', function() {
      const enabled = this.checked;
      document.getElementById('oscPauseFixedDiv').style.display = enabled ? 'grid' : 'none';
      document.getElementById('oscPauseRandomDiv').style.display = (enabled && document.getElementById('editOscPauseRandom').checked) ? 'grid' : 'none';
    });
    document.getElementById('editOscPauseRandom').addEventListener('change', function() {
      const isRandom = this.checked;
      const enabled = document.getElementById('editOscPauseEnabled').checked;
      document.getElementById('oscPauseFixedDiv').style.display = (enabled && !isRandom) ? 'grid' : 'none';
      document.getElementById('oscPauseRandomDiv').style.display = (enabled && isRandom) ? 'grid' : 'none';
    });
    
    // Enforce numeric input constraints on all number fields (includes real-time validation)
    function enforceNumericConstraints(input) {
      // Filter invalid characters after input AND trigger validation
      input.addEventListener('input', function(e) {
        const oldValue = this.value;
        // Allow: digits, decimal point, minus sign
        // Remove all non-numeric characters except . and -
        let newValue = this.value.replace(/[^\d.-]/g, '');
        
        // Ensure only one decimal point
        const parts = newValue.split('.');
        if (parts.length > 2) {
          newValue = parts[0] + '.' + parts.slice(1).join('');
        }
        
        // Ensure minus only at start
        if (newValue.indexOf('-') > 0) {
          newValue = newValue.replace(/-/g, '');
        }
        
        // Update value if changed
        if (newValue !== oldValue) {
          this.value = newValue;
        }
        
        // Trigger real-time validation (red border + error messages)
        // Use setTimeout to ensure value is updated before validation
        setTimeout(function() {
          if (typeof validateEditForm === 'function') {
            validateEditForm();
          }
        }, 10);
      });
      
      // Enforce min/max on blur
      input.addEventListener('blur', function() {
        const min = parseFloat(this.getAttribute('min'));
        const max = parseFloat(this.getAttribute('max'));
        const val = parseFloat(this.value);
        
        if (!isNaN(min) && val < min) {
          this.value = min;
          validateEditForm();
        }
        if (!isNaN(max) && val > max) {
          this.value = max;
          validateEditForm();
        }
      });
    }
    
    // Apply numeric constraints to all number inputs in edit modal
    const numericInputs = [
      'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack', 'editDecelZone',
      'editOscCenter', 'editOscAmplitude', 'editOscFrequency',
      'editOscRampInDur', 'editOscRampOutDur',
      'editChaosCenter', 'editChaosAmplitude', 'editChaosSpeed', 'editChaosCraziness',
      'editChaosDuration', 'editChaosSeed',
      'editCycles', 'editPause'
    ];
    numericInputs.forEach(id => {
      const input = document.getElementById(id);
      if (input) enforceNumericConstraints(input);
    });
    
    // Apply same numeric constraints to MAIN CONTROLS (classic modes)
    const mainNumericInputs = [
      // VA-ET-VIENT
      'startPosition', 'distance', 'speedUnified', 'speedForward', 'speedBackward',
      'decelZone', 'decelEffect',
      // OSCILLATION
      'oscCenterPosition', 'oscAmplitude', 'oscFrequency', 'oscSpeed',
      'oscRampInDuration', 'oscRampOutDuration',
      // CHAOS
      'chaosCenter', 'chaosAmplitude', 'chaosSpeed', 'chaosCraziness',
      'chaosDuration', 'chaosSeed'
    ];
    mainNumericInputs.forEach(id => {
      const input = document.getElementById(id);
      if (input) enforceNumericConstraints(input);
    });
    
    // Close modal on outside click
    document.getElementById('editLineModal').addEventListener('click', function(e) {
      if (e.target === this) {
        closeEditModal();
      }
    });
    
    // Update effect value display in modal
    document.getElementById('editDecelEffect').addEventListener('input', function() {
      document.getElementById('editEffectValue').textContent = this.value;
    });
    
    // ============================================================================
    // DECELERATION ZONE HANDLERS
    // ============================================================================
    
    // ============================================================================
    // DECELERATION ZONE HELPERS
    // ============================================================================
    
    // JavaScript implementation of C++ calculateSlowdownFactor()
    // Delegates to pure function from presets.js
    function calculateSlowdownFactorJS(zoneProgress, maxSlowdown, mode) {
      // Delegate to pure function if available
      if (typeof calculateSlowdownFactorPure === 'function') {
        return calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode);
      }
      
      // Fallback - linear only
      return 1.0 + (1.0 - zoneProgress) * (maxSlowdown - 1.0);
    }
    
    // ============================================================================
    // DECELERATION ZONE EVENT LISTENERS
    // ============================================================================
    
    // Show/hide decel zone options
    // Deceleration section toggle (replaces checkbox)
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
        // Send config with zones disabled
        sendCommand(WS_CMD.SET_DECEL_ZONE, {
          enabled: false
        });
      }
    }
    
    // ===== CYCLE PAUSE SECTION (MODE SIMPLE) =====
    function toggleCyclePauseSection() {
      const section = getCyclePauseSection();
      const headerText = document.getElementById('cyclePauseHeaderText');
      const isCollapsed = section.classList.contains('collapsed');
      
      section.classList.toggle('collapsed');
      
      if (isCollapsed) {
        // Expanding = activating
        headerText.textContent = '⏸️ Pause entre cycles - activée';
        sendCyclePauseConfig();
      } else {
        // Collapsing = deactivating
        headerText.textContent = '⏸️ Pause entre cycles - désactivée';
        sendCommand(WS_CMD.UPDATE_CYCLE_PAUSE, {
          enabled: false
        });
      }
    }
    
    // Radio button change handlers (Mode Simple)
    document.querySelectorAll('input[name="cyclePauseMode"]').forEach(radio => {
      radio.addEventListener('change', function() {
        const isFixed = this.value === 'fixed';
        document.getElementById('pauseFixedControls').style.display = isFixed ? 'flex' : 'none';
        document.getElementById('pauseRandomControls').style.display = isFixed ? 'none' : 'block';
        
        // Send updated config
        const section = getCyclePauseSection();
        if (!section.classList.contains('collapsed')) {
          sendCyclePauseConfig();
        }
      });
    });
    
    // Preset buttons for fixed duration (Mode Simple)
    document.querySelectorAll('[data-pause-duration]').forEach(btn => {
      btn.addEventListener('click', function() {
        const value = this.getAttribute('data-pause-duration');
        document.getElementById('cyclePauseDuration').value = value;
        
        // Update active state
        document.querySelectorAll('[data-pause-duration]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        sendCyclePauseConfig();
      });
    });
    
    // Preset buttons for min (Mode Simple)
    document.querySelectorAll('[data-pause-min]').forEach(btn => {
      btn.addEventListener('click', function() {
        const value = this.getAttribute('data-pause-min');
        document.getElementById('cyclePauseMin').value = value;
        
        // Update active state
        document.querySelectorAll('[data-pause-min]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        sendCyclePauseConfig();
      });
    });
    
    // Preset buttons for max (Mode Simple)
    document.querySelectorAll('[data-pause-max]').forEach(btn => {
      btn.addEventListener('click', function() {
        const value = this.getAttribute('data-pause-max');
        document.getElementById('cyclePauseMax').value = value;
        
        // Update active state
        document.querySelectorAll('[data-pause-max]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        sendCyclePauseConfig();
      });
    });
    
    // Input change listeners (Mode Simple)
    ['cyclePauseDuration', 'cyclePauseMin', 'cyclePauseMax'].forEach(id => {
      const input = document.getElementById(id);
      if (input) {
        input.addEventListener('change', function() {
          const section = getCyclePauseSection();
          if (!section.classList.contains('collapsed')) {
            sendCyclePauseConfig();
          }
        });
      }
    });
    
    // Send cycle pause config (Mode Simple)
    function sendCyclePauseConfig() {
      const section = getCyclePauseSection();
      const enabled = !section.classList.contains('collapsed');
      const isRandom = document.getElementById('pauseModeRandom').checked;
      
      const config = {
        enabled: enabled,
        isRandom: isRandom,
        pauseDurationSec: parseFloat(document.getElementById('cyclePauseDuration').value),
        minPauseSec: parseFloat(document.getElementById('cyclePauseMin').value),
        maxPauseSec: parseFloat(document.getElementById('cyclePauseMax').value)
      };
      
      sendCommand(WS_CMD.UPDATE_CYCLE_PAUSE, config);
    }
    
    // ===== CYCLE PAUSE SECTION (MODE OSCILLATION) =====
    function toggleCyclePauseOscSection() {
      const section = getCyclePauseOscSection();
      const headerText = document.getElementById('cyclePauseOscHeaderText');
      const isCollapsed = section.classList.contains('collapsed');
      
      section.classList.toggle('collapsed');
      
      if (isCollapsed) {
        // Expanding = activating
        headerText.textContent = '⏸️ Pause entre cycles - activée';
        sendCyclePauseConfigOsc();
      } else {
        // Collapsing = deactivating
        headerText.textContent = '⏸️ Pause entre cycles - désactivée';
        sendCommand(WS_CMD.UPDATE_CYCLE_PAUSE_OSC, {
          enabled: false
        });
      }
    }
    
    // Radio button change handlers (Mode Oscillation)
    document.querySelectorAll('input[name="cyclePauseModeOsc"]').forEach(radio => {
      radio.addEventListener('change', function() {
        const isFixed = this.value === 'fixed';
        document.getElementById('pauseFixedControlsOsc').style.display = isFixed ? 'flex' : 'none';
        document.getElementById('pauseRandomControlsOsc').style.display = isFixed ? 'none' : 'block';
        
        // Send updated config
        const section = getCyclePauseOscSection();
        if (!section.classList.contains('collapsed')) {
          sendCyclePauseConfigOsc();
        }
      });
    });
    
    // Preset buttons for fixed duration (Mode Oscillation)
    document.querySelectorAll('[data-pause-duration-osc]').forEach(btn => {
      btn.addEventListener('click', function() {
        const value = this.getAttribute('data-pause-duration-osc');
        document.getElementById('cyclePauseDurationOsc').value = value;
        
        // Update active state
        document.querySelectorAll('[data-pause-duration-osc]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        sendCyclePauseConfigOsc();
      });
    });
    
    // Preset buttons for min (Mode Oscillation)
    document.querySelectorAll('[data-pause-min-osc]').forEach(btn => {
      btn.addEventListener('click', function() {
        const value = this.getAttribute('data-pause-min-osc');
        document.getElementById('cyclePauseMinOsc').value = value;
        
        // Update active state
        document.querySelectorAll('[data-pause-min-osc]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        sendCyclePauseConfigOsc();
      });
    });
    
    // Preset buttons for max (Mode Oscillation)
    document.querySelectorAll('[data-pause-max-osc]').forEach(btn => {
      btn.addEventListener('click', function() {
        const value = this.getAttribute('data-pause-max-osc');
        document.getElementById('cyclePauseMaxOsc').value = value;
        
        // Update active state
        document.querySelectorAll('[data-pause-max-osc]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
        
        sendCyclePauseConfigOsc();
      });
    });
    
    // Input change listeners (Mode Oscillation)
    ['cyclePauseDurationOsc', 'cyclePauseMinOsc', 'cyclePauseMaxOsc'].forEach(id => {
      const input = document.getElementById(id);
      if (input) {
        input.addEventListener('change', function() {
          const section = getCyclePauseOscSection();
          if (!section.classList.contains('collapsed')) {
            sendCyclePauseConfigOsc();
          }
        });
      }
    });
    
    // Send cycle pause config (Mode Oscillation)
    function sendCyclePauseConfigOsc() {
      const section = getCyclePauseOscSection();
      const enabled = !section.classList.contains('collapsed');
      const isRandom = document.getElementById('pauseModeRandomOsc').checked;
      
      const config = {
        enabled: enabled,
        isRandom: isRandom,
        pauseDurationSec: parseFloat(document.getElementById('cyclePauseDurationOsc').value),
        minPauseSec: parseFloat(document.getElementById('cyclePauseMinOsc').value),
        maxPauseSec: parseFloat(document.getElementById('cyclePauseMaxOsc').value)
      };
      
      sendCommand(WS_CMD.UPDATE_CYCLE_PAUSE_OSC, config);
    }
    
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
    
    // Send deceleration configuration to ESP32
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
    
    // Draw deceleration curve preview on canvas
    function drawDecelPreview() {
      const canvas = document.getElementById('decelPreview');
      if (!canvas) return;
      
      const ctx = canvas.getContext('2d');
      const width = canvas.width;
      const height = canvas.height;
      const padding = 20;
      const plotWidth = width - 2 * padding;
      const plotHeight = height - 2 * padding;
      
      // Clear canvas
      ctx.clearRect(0, 0, width, height);
      
      // Get current config
      const section = document.getElementById('decelSection');
      const enabled = !section.classList.contains('collapsed');
      const enableStart = document.getElementById('decelZoneStart').checked;
      const enableEnd = document.getElementById('decelZoneEnd').checked;
      const zoneMM = parseFloat(document.getElementById('decelZoneMM').value) || 50;
      const effectPercent = parseFloat(document.getElementById('decelEffectPercent').value) || 75;
      const mode = parseInt(document.getElementById('decelModeSelect')?.value || 1);
      
      if (!enabled) {
        ctx.font = '14px Arial';
        ctx.fillStyle = '#999';
        ctx.textAlign = 'center';
        ctx.fillText('Décélération désactivée', width / 2, height / 2);
        return;
      }
      
      // Assume a movement amplitude of 150mm for preview
      const movementAmplitude = 150;
      const maxSlowdown = 1.0 + (effectPercent / 100.0) * 9.0;  // 1× to 10×
      
      // Draw axes
      ctx.strokeStyle = '#ccc';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(padding, padding);
      ctx.lineTo(padding, height - padding);
      ctx.lineTo(width - padding, height - padding);
      ctx.stroke();
      
      // Draw curve
      ctx.strokeStyle = '#4CAF50';
      ctx.lineWidth = 2;
      ctx.beginPath();
      
      for (let x = 0; x <= plotWidth; x++) {
        const positionMM = (x / plotWidth) * movementAmplitude;
        let speedFactor = 1.0;  // Normal speed
        
        // Check START zone
        if (enableStart && positionMM <= zoneMM) {
          const zoneProgress = positionMM / zoneMM;
          speedFactor = calculateSlowdownFactorJS(zoneProgress, maxSlowdown, mode);
        }
        // Check END zone
        if (enableEnd && positionMM >= (movementAmplitude - zoneMM)) {
          const distanceFromEnd = movementAmplitude - positionMM;
          const zoneProgress = distanceFromEnd / zoneMM;
          speedFactor = calculateSlowdownFactorJS(zoneProgress, maxSlowdown, mode);
        }
        
        // Convert speed factor to Y coordinate (inverted: slower = higher on graph)
        const normalizedSpeed = 1.0 / speedFactor;  // 1.0 = normal, 0.1 = 10× slower
        const y = height - padding - (normalizedSpeed * plotHeight);
        
        if (x === 0) {
          ctx.moveTo(padding + x, y);
        } else {
          ctx.lineTo(padding + x, y);
        }
      }
      
      ctx.stroke();
      
      // Draw zone boundaries
      if (enableStart || enableEnd) {
        ctx.setLineDash([5, 3]);
        ctx.strokeStyle = '#FF9800';
        ctx.lineWidth = 1;
        
        if (enableStart) {
          const startX = padding + (zoneMM / movementAmplitude) * plotWidth;
          ctx.beginPath();
          ctx.moveTo(startX, padding);
          ctx.lineTo(startX, height - padding);
          ctx.stroke();
        }
        
        if (enableEnd) {
          const endX = padding + ((movementAmplitude - zoneMM) / movementAmplitude) * plotWidth;
          ctx.beginPath();
          ctx.moveTo(endX, padding);
          ctx.lineTo(endX, height - padding);
          ctx.stroke();
        }
        
        ctx.setLineDash([]);
      }
      
      // Draw labels
      ctx.font = '10px Arial';
      ctx.fillStyle = '#666';
      ctx.textAlign = 'center';
      ctx.fillText('Départ', padding, height - 5);
      ctx.fillText('Arrivée', width - padding, height - 5);
      
      // Draw speed indicators
      ctx.textAlign = 'left';
      ctx.fillText('Rapide', padding + 5, padding + 10);
      ctx.fillText('Lent', padding + 5, height - padding - 5);
    }
    
    connectWebSocket();
    
    // Initialize dependency injection context
    if (typeof initContext === 'function') {
      initContext();
    }
    
    loadPlaylists();  // Load playlist presets from backend