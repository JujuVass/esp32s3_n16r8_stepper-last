    // ========================================================================
    // MAIN APPLICATION CODE
    // ========================================================================
    // Note: AppState, WS_CMD, PlaylistState, SystemState loaded from app.js
    // Note: showNotification, validateNumericInput, etc. loaded from utils.js
    // Note: connectWebSocket, handleWebSocketMessage loaded from websocket.js
    // Note: loadStatsData, displayStatsTable, displayStatsChart loaded from stats.js
    
    // ========================================================================
    // PLAYLIST LOAD FROM BACKEND
    // ========================================================================
    
    // Load playlists from backend
    function loadPlaylists(callback) {
      fetch('/api/playlists')
        .then(response => response.json())
        .then(data => {
          PlaylistState.simple = data.simple || [];
          PlaylistState.oscillation = data.oscillation || [];
          PlaylistState.chaos = data.chaos || [];
          PlaylistState.loaded = true;
          
          // Update button counters
          updatePlaylistButtonCounters();
          
          console.log('📋 Playlists loaded:', 
            'Simple=' + PlaylistState.simple.length,
            'Oscillation=' + PlaylistState.oscillation.length,
            'Chaos=' + PlaylistState.chaos.length);
          
          // Execute callback if provided
          if (callback && typeof callback === 'function') {
            callback();
          }
        })
        .catch(error => {
          console.error('❌ Error loading playlists:', error);
          PlaylistState.loaded = false;
        });
    }
    
    // Update playlist button counters
    function updatePlaylistButtonCounters() {
      const btnSimple = document.getElementById('btnManagePlaylistSimple');
      const btnOsc = document.getElementById('btnManagePlaylistOscillation');
      const btnChaos = document.getElementById('btnManagePlaylistChaos');
      
      if (btnSimple) {
        btnSimple.innerHTML = '📋 Playlist (' + PlaylistState.simple.length + '/20)';
      }
      if (btnOsc) {
        btnOsc.innerHTML = '📋 Playlist (' + PlaylistState.oscillation.length + '/20)';
      }
      if (btnChaos) {
        btnChaos.innerHTML = '📋 Playlist (' + PlaylistState.chaos.length + '/20)';
      }
    }
    
    // Note: MILESTONES array and getMilestoneInfo() loaded from app.js
    
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
    // DOM CACHE - Loaded from /js/core/DOMManager.js
    // ========================================================================
    // DOM object and initDOMCache() are now defined in DOMManager.js
    // This allows other modules to access cached DOM elements
    
    // ========================================================================
    // MODE Séquenceur - Global Variables
    // ========================================================================
    let sequenceLines = [];
    let editingLineId = null;
    let isLoadingEditForm = false;  // Flag to prevent validation during initial load
    let currentLineIdMap = {};  // Map table index to lineId
    
    // Phase 2: Drag & Drop state
    let draggedLineId = null;
    let draggedLineIndex = null;
    let lastDragEnterTime = 0; // Throttle dragenter to avoid flicker
    
    // Phase 2: Multi-select state
    let selectedLineIds = new Set();
    let lastSelectedIndex = null;
    
    // ========================================================================
    // MODE Séquenceur - Sequence Table Management
    // ========================================================================
    
    // Validate sequencer line against effectiveMaxDistanceMM
    // Validate sequencer line - delegates to pure function in sequencer.js
    function validateSequencerLine(line, movementType) {
      const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
      
      // Delegate to pure function if available (from sequencer.js)
      if (typeof validateSequencerLinePure === 'function') {
        return validateSequencerLinePure(line, movementType, effectiveMax);
      }
      
      // Fallback: return empty errors if pure function not loaded
      console.warn('validateSequencerLinePure not available, skipping validation');
      return [];
    }
    
    function addSequenceLine() {
      const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
      
      // Use pure function to build defaults if available (from sequencer.js)
      let newLine;
      if (typeof buildSequenceLineDefaultsPure === 'function') {
        newLine = buildSequenceLineDefaultsPure(effectiveMax);
      } else {
        // Fallback if sequencer.js not loaded
        console.warn('buildSequenceLineDefaultsPure not available, using inline defaults');
        const center = effectiveMax / 2;
        newLine = {
          enabled: true,
          movementType: 0,
          startPositionMM: 0,
          distanceMM: Math.min(100, effectiveMax),
          speedForward: 5.0,
          speedBackward: 5.0,
          decelStartEnabled: false,
          decelEndEnabled: true,
          decelZoneMM: 20,
          decelEffectPercent: 50,
          decelMode: 1,
          vaetCyclePauseEnabled: false,
          vaetCyclePauseIsRandom: false,
          vaetCyclePauseDurationSec: 0.0,
          vaetCyclePauseMinSec: 0.5,
          vaetCyclePauseMaxSec: 3.0,
          oscCenterPositionMM: center,
          oscAmplitudeMM: Math.min(50.0, center),
          oscWaveform: 0,
          oscFrequencyHz: 0.5,
          oscEnableRampIn: false,
          oscEnableRampOut: false,
          oscRampInDurationMs: 1000.0,
          oscRampOutDurationMs: 1000.0,
          oscCyclePauseEnabled: false,
          oscCyclePauseIsRandom: false,
          oscCyclePauseDurationSec: 0.0,
          oscCyclePauseMinSec: 0.5,
          oscCyclePauseMaxSec: 3.0,
          chaosCenterPositionMM: center,
          chaosAmplitudeMM: Math.min(50.0, center),
          chaosMaxSpeedLevel: 10.0,
          chaosCrazinessPercent: 50.0,
          chaosDurationSeconds: 30,
          chaosSeed: 0,
          chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true],
          cycleCount: 1,
          pauseAfterMs: 0
        };
      }
      
      // Validate before sending
      const errors = validateSequencerLine(newLine, newLine.movementType);
      if (errors.length > 0) {
        alert('❌ Impossible d\'ajouter la ligne :\n\n' + errors.join('\n'));
        return;
      }
      
      sendCommand(WS_CMD.ADD_SEQUENCE_LINE, newLine);
    }
    
    function deleteSequenceLine(lineId) {
      if (confirm('Supprimer cette ligne?')) {
        sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
      }
    }
    
    function editSequenceLine(lineId) {
      console.log('🔍 editSequenceLine called with lineId:', lineId);
      console.log('📋 Available lines:', sequenceLines.map(l => ({id: l.lineId, start: l.startPositionMM})));
      
      const line = sequenceLines.find(l => l.lineId === lineId);
      if (!line) {
        console.error('❌ Line not found! lineId:', lineId);
        return;
      }
      
      console.log('✅ Found line:', line);
      editingLineId = lineId;
      isLoadingEditForm = true;  // Disable validation during load
      
      // Clear any previous validation errors before loading
      clearErrorFields();
      const errorContainer = document.getElementById('editValidationErrors');
      if (errorContainer) errorContainer.style.display = 'none';
      
      // Populate form
      document.getElementById('editLineNumber').textContent = sequenceLines.indexOf(line) + 1;
      
      // Movement type (default to VAET if not set)
      const movementType = line.movementType !== undefined ? line.movementType : 0;
      if (movementType === 0) {
        document.getElementById('editTypeVaet').checked = true;
      } else if (movementType === 1) {
        document.getElementById('editTypeOsc').checked = true;
      } else if (movementType === 2) {
        document.getElementById('editTypeChaos').checked = true;
      } else if (movementType === 4) {
        document.getElementById('editTypeCalibration').checked = true;
      }
      updateMovementTypeFields();  // Show/hide appropriate fields
      
      // VA-ET-VIENT fields
      document.getElementById('editStartPos').value = line.startPositionMM || 0;
      document.getElementById('editDistance').value = line.distanceMM || 100;
      document.getElementById('editSpeedFwd').value = line.speedForward || 5.0;
      document.getElementById('editSpeedBack').value = line.speedBackward || 5.0;
      document.getElementById('editDecelStart').checked = line.decelStartEnabled || false;
      document.getElementById('editDecelEnd').checked = line.decelEndEnabled !== undefined ? line.decelEndEnabled : true;
      document.getElementById('editDecelZone').value = line.decelZoneMM || 20;
      document.getElementById('editDecelEffect').value = line.decelEffectPercent || 50;
      document.getElementById('editEffectValue').textContent = line.decelEffectPercent || 50;
      document.getElementById('editDecelMode').value = line.decelMode !== undefined ? line.decelMode : 1;
      
      // OSCILLATION fields
      document.getElementById('editOscCenter').value = line.oscCenterPositionMM || 100;
      document.getElementById('editOscAmplitude').value = line.oscAmplitudeMM || 50;
      document.getElementById('editOscWaveform').value = line.oscWaveform !== undefined ? line.oscWaveform : 0;
      document.getElementById('editOscFrequency').value = line.oscFrequencyHz || 0.5;
      document.getElementById('editOscRampIn').checked = line.oscEnableRampIn || false;
      document.getElementById('editOscRampOut').checked = line.oscEnableRampOut || false;
      document.getElementById('editOscRampInDur').value = line.oscRampInDurationMs || 1000;
      document.getElementById('editOscRampOutDur').value = line.oscRampOutDurationMs || 1000;
      
      // VA-ET-VIENT Cycle Pause fields
      document.getElementById('editVaetPauseEnabled').checked = line.vaetCyclePauseEnabled || false;
      document.getElementById('editVaetPauseRandom').checked = line.vaetCyclePauseIsRandom || false;
      document.getElementById('editVaetPauseDuration').value = line.vaetCyclePauseDurationSec || 0.0;
      document.getElementById('editVaetPauseMin').value = line.vaetCyclePauseMinSec || 0.5;
      document.getElementById('editVaetPauseMax').value = line.vaetCyclePauseMaxSec || 3.0;
      
      // OSCILLATION Cycle Pause fields
      document.getElementById('editOscPauseEnabled').checked = line.oscCyclePauseEnabled || false;
      document.getElementById('editOscPauseRandom').checked = line.oscCyclePauseIsRandom || false;
      document.getElementById('editOscPauseDuration').value = line.oscCyclePauseDurationSec || 0.0;
      document.getElementById('editOscPauseMin').value = line.oscCyclePauseMinSec || 0.5;
      document.getElementById('editOscPauseMax').value = line.oscCyclePauseMaxSec || 3.0;
      
      // Trigger pause visibility updates
      document.getElementById('editVaetPauseEnabled').dispatchEvent(new Event('change'));
      document.getElementById('editOscPauseEnabled').dispatchEvent(new Event('change'));
      
      // CHAOS fields
      document.getElementById('editChaosCenter').value = line.chaosCenterPositionMM || 110;
      document.getElementById('editChaosAmplitude').value = line.chaosAmplitudeMM || 50;
      document.getElementById('editChaosSpeed').value = line.chaosMaxSpeedLevel || 10;
      document.getElementById('editChaosCraziness').value = line.chaosCrazinessPercent || 50;
      document.getElementById('editChaosDuration').value = line.chaosDurationSeconds || 30;
      document.getElementById('editChaosSeed').value = line.chaosSeed || 0;
      
      // Load chaos patterns (array of 8 booleans)
      if (line.chaosPatternsEnabled && line.chaosPatternsEnabled.length === 11) {
        for (let i = 0; i < 11; i++) {
          const checkbox = document.querySelector(`input[name="chaosPattern${i}"]`);
          if (checkbox) checkbox.checked = line.chaosPatternsEnabled[i];
        }
      }
      
      // COMMON fields
      document.getElementById('editCycles').value = line.cycleCount || 1;
      document.getElementById('editPause').value = ((line.pauseAfterMs || 0) / 1000).toFixed(1);  // Convert ms to seconds
      
      // Populate playlist dropdowns if playlists are loaded
      if (PlaylistState.loaded) {
        populateSequencerDropdown('simple');
        populateSequencerDropdown('oscillation');
        populateSequencerDropdown('chaos');
      }
      
      // Show modal
      document.getElementById('editLineModal').style.display = 'block';
      
      // Re-enable validation after form is fully loaded
      isLoadingEditForm = false;
    }
    
    function saveLineEdit(event) {
      event.preventDefault();
      
      const form = document.getElementById('editLineForm');
      const movementType = parseInt(form.movementType.value);
      
      const updatedLine = {
        lineId: editingLineId,
        enabled: true,  // Keep enabled when editing
        movementType: movementType,
        
        // VA-ET-VIENT fields
        startPositionMM: parseFloat(form.startPositionMM.value),
        distanceMM: parseFloat(form.distanceMM.value),
        speedForward: parseFloat(form.speedForward.value),
        speedBackward: parseFloat(form.speedBackward.value),
        decelStartEnabled: form.decelStartEnabled.checked,
        decelEndEnabled: form.decelEndEnabled.checked,
        decelZoneMM: parseFloat(form.decelZoneMM.value),
        decelEffectPercent: parseFloat(form.decelEffectPercent.value),
        decelMode: parseInt(form.decelMode.value),
        
        // OSCILLATION fields
        oscCenterPositionMM: parseFloat(form.oscCenterPositionMM.value),
        oscAmplitudeMM: parseFloat(form.oscAmplitudeMM.value),
        oscWaveform: parseInt(form.oscWaveform.value),
        oscFrequencyHz: parseFloat(form.oscFrequencyHz.value),
        oscEnableRampIn: form.oscEnableRampIn.checked,
        oscEnableRampOut: form.oscEnableRampOut.checked,
        oscRampInDurationMs: parseFloat(form.oscRampInDurationMs.value),
        oscRampOutDurationMs: parseFloat(form.oscRampOutDurationMs.value),
        
        // VA-ET-VIENT Cycle Pause
        vaetCyclePauseEnabled: form.vaetCyclePauseEnabled.checked,
        vaetCyclePauseIsRandom: form.vaetCyclePauseIsRandom.checked,
        vaetCyclePauseDurationSec: parseFloat(form.vaetCyclePauseDurationSec.value),
        vaetCyclePauseMinSec: parseFloat(form.vaetCyclePauseMinSec.value),
        vaetCyclePauseMaxSec: parseFloat(form.vaetCyclePauseMaxSec.value),
        
        // OSCILLATION Cycle Pause
        oscCyclePauseEnabled: form.oscCyclePauseEnabled.checked,
        oscCyclePauseIsRandom: form.oscCyclePauseIsRandom.checked,
        oscCyclePauseDurationSec: parseFloat(form.oscCyclePauseDurationSec.value),
        oscCyclePauseMinSec: parseFloat(form.oscCyclePauseMinSec.value),
        oscCyclePauseMaxSec: parseFloat(form.oscCyclePauseMaxSec.value),
        
        // CHAOS fields
        chaosCenterPositionMM: parseFloat(form.chaosCenterPositionMM.value),
        chaosAmplitudeMM: parseFloat(form.chaosAmplitudeMM.value),
        chaosMaxSpeedLevel: parseFloat(form.chaosMaxSpeedLevel.value),
        chaosCrazinessPercent: parseFloat(form.chaosCrazinessPercent.value),
        chaosDurationSeconds: parseInt(form.chaosDurationSeconds.value),
        chaosSeed: parseInt(form.chaosSeed.value),
        chaosPatternsEnabled: [
          form.chaosPattern0.checked,
          form.chaosPattern1.checked,
          form.chaosPattern2.checked,
          form.chaosPattern3.checked,
          form.chaosPattern4.checked,
          form.chaosPattern5.checked,
          form.chaosPattern6.checked,
          form.chaosPattern7.checked,
          form.chaosPattern8.checked,
          form.chaosPattern9.checked,
          form.chaosPattern10.checked
        ],
        
        // COMMON fields
        cycleCount: parseInt(form.cycleCount.value),
        pauseAfterMs: Math.round(parseFloat(form.pauseAfterSec.value) * 1000)  // Convert seconds to ms
      };
      
      // Validate before sending (should already be validated by real-time validation)
      const errors = validateSequencerLine(updatedLine, movementType);
      if (errors.length > 0) {
        // This shouldn't happen if validation is working, but just in case
        console.error('Validation errors on save:', errors);
        validateEditForm();  // Update UI
        return; // Don't close modal, let user fix
      }
      
      sendCommand(WS_CMD.UPDATE_SEQUENCE_LINE, updatedLine);
      closeEditModal();
    }
    
    // Validate edit form in real-time
    function validateEditForm() {
      // Skip validation during initial form load
      if (isLoadingEditForm) return;
      
      const form = document.getElementById('editLineForm');
      const movementType = parseInt(form.movementType.value);
      
      // Check for empty fields (indicates invalid character like "é" was typed)
      const emptyFieldErrors = [];
      
      // VA-ET-VIENT fields
      if (movementType === 0) {
        if (form.startPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ Position de départ est incorrect');
        if (form.distanceMM.value.trim() === '') emptyFieldErrors.push('⚠️ Distance est incorrect');
        if (form.speedForward.value.trim() === '') emptyFieldErrors.push('⚠️ Vitesse aller est incorrect');
        if (form.speedBackward.value.trim() === '') emptyFieldErrors.push('⚠️ Vitesse retour est incorrect');
        if (form.decelZoneMM.value.trim() === '') emptyFieldErrors.push('⚠️ Zone décélération est incorrect');
      }
      
      // OSCILLATION fields
      if (movementType === 1) {
        if (form.oscCenterPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ Centre oscillation est incorrect');
        if (form.oscAmplitudeMM.value.trim() === '') emptyFieldErrors.push('⚠️ Amplitude oscillation est incorrect');
        if (form.oscFrequencyHz.value.trim() === '') emptyFieldErrors.push('⚠️ Fréquence est incorrect');
        if (form.oscRampInDurationMs.value.trim() === '') emptyFieldErrors.push('⚠️ Durée rampe IN est incorrect');
        if (form.oscRampOutDurationMs.value.trim() === '') emptyFieldErrors.push('⚠️ Durée rampe OUT est incorrect');
      }
      
      // CHAOS fields
      if (movementType === 2) {
        if (form.chaosCenterPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ Centre chaos est incorrect');
        if (form.chaosAmplitudeMM.value.trim() === '') emptyFieldErrors.push('⚠️ Amplitude chaos est incorrect');
        if (form.chaosMaxSpeedLevel.value.trim() === '') emptyFieldErrors.push('⚠️ Vitesse max chaos est incorrect');
        if (form.chaosCrazinessPercent.value.trim() === '') emptyFieldErrors.push('⚠️ Degré de folie est incorrect');
        if (form.chaosDurationSeconds.value.trim() === '') emptyFieldErrors.push('⚠️ Durée chaos est incorrect');
        if (form.chaosSeed.value.trim() === '') emptyFieldErrors.push('⚠️ Seed est incorrect');
      }
      
      // COMMON fields (for all types except CALIBRATION)
      if (movementType !== 4) {
        if (movementType !== 2 && form.cycleCount.value.trim() === '') {
          emptyFieldErrors.push('⚠️ Nombre de cycles est incorrect');
        }
        if (form.pauseAfterSec.value.trim() === '') {
          emptyFieldErrors.push('⚠️ Pause est incorrect');
        }
      }
      
      // Build line object from current form values (ALL fields for complete validation)
      const line = {
        movementType: movementType,
        
        // VA-ET-VIENT
        startPositionMM: parseFloat(form.startPositionMM.value) || 0,
        distanceMM: parseFloat(form.distanceMM.value) || 0,
        speedForward: parseFloat(form.speedForward.value) || 0,
        speedBackward: parseFloat(form.speedBackward.value) || 0,
        decelZoneMM: parseFloat(form.decelZoneMM.value) || 0,
        
        // OSCILLATION
        oscCenterPositionMM: parseFloat(form.oscCenterPositionMM.value) || 0,
        oscAmplitudeMM: parseFloat(form.oscAmplitudeMM.value) || 0,
        oscFrequencyHz: parseFloat(form.oscFrequencyHz.value) || 0,
        oscRampInDurationMs: parseFloat(form.oscRampInDurationMs.value) || 0,
        oscRampOutDurationMs: parseFloat(form.oscRampOutDurationMs.value) || 0,
        
        // CHAOS
        chaosCenterPositionMM: parseFloat(form.chaosCenterPositionMM.value) || 0,
        chaosAmplitudeMM: parseFloat(form.chaosAmplitudeMM.value) || 0,
        chaosMaxSpeedLevel: parseFloat(form.chaosMaxSpeedLevel.value) || 0,
        chaosCrazinessPercent: parseFloat(form.chaosCrazinessPercent.value) || 0,
        chaosDurationSeconds: parseInt(form.chaosDurationSeconds.value) || 0,
        chaosSeed: parseInt(form.chaosSeed.value) || 0,
        
        // COMMON
        cycleCount: parseInt(form.cycleCount.value) || 0,
        pauseAfterMs: Math.round(parseFloat(form.pauseAfterSec.value) * 1000) || 0
      };
      
      // Validate (combine empty field errors with validation errors)
      const validationErrors = validateSequencerLine(line, movementType);
      const errors = emptyFieldErrors.concat(validationErrors);
      
      // Update UI
      const errorContainer = document.getElementById('editValidationErrors');
      const errorList = document.getElementById('editValidationErrorsList');
      const saveButton = document.getElementById('btnSaveEdit');
      
      if (errors.length > 0) {
        // Show errors
        errorContainer.style.display = 'block';
        errorList.innerHTML = errors.map(err => '<li>' + err + '</li>').join('');
        
        // Disable save button
        saveButton.disabled = true;
        saveButton.style.opacity = '0.5';
        saveButton.style.cursor = 'not-allowed';
        
        // Highlight error fields (including empty ones)
        highlightErrorFields(movementType, line, emptyFieldErrors);
      } else {
        // Hide errors
        errorContainer.style.display = 'none';
        errorList.innerHTML = '';
        
        // Enable save button
        saveButton.disabled = false;
        saveButton.style.opacity = '1';
        saveButton.style.cursor = 'pointer';
        
        // Clear all highlights
        clearErrorFields();
      }
    }
    
    // Highlight fields that have validation errors
    function highlightErrorFields(movementType, line, emptyFieldErrors) {
      clearErrorFields();
      
      const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
      
      // Use pure function to get all invalid field IDs
      let invalidFieldIds = [];
      if (typeof getAllInvalidFieldsPure === 'function') {
        invalidFieldIds = getAllInvalidFieldsPure(line, movementType, effectiveMax, emptyFieldErrors);
      } else {
        // Fallback: use error field mapping for empty fields
        if (typeof getErrorFieldIdsPure === 'function') {
          invalidFieldIds = getErrorFieldIdsPure(emptyFieldErrors);
        }
      }
      
      // Apply error styling to all invalid fields
      const errorStyle = '2px solid #f44336';
      invalidFieldIds.forEach(fieldId => {
        const field = document.getElementById(fieldId);
        if (field) {
          field.style.border = errorStyle;
        }
      });
    }
    
    // Clear all error field highlights
    function clearErrorFields() {
      // Use ALL_EDIT_FIELDS from validation.js if available
      const fields = (typeof ALL_EDIT_FIELDS !== 'undefined') ? ALL_EDIT_FIELDS : [
        // VA-ET-VIENT
        'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack', 'editDecelZone',
        // OSCILLATION
        'editOscCenter', 'editOscAmplitude', 'editOscFrequency',
        'editOscRampInDur', 'editOscRampOutDur',
        // CHAOS
        'editChaosCenter', 'editChaosAmplitude', 'editChaosSpeed', 'editChaosCraziness',
        'editChaosDuration', 'editChaosSeed',
        // COMMON
        'editCycles', 'editPause'
      ];
      fields.forEach(fieldId => {
        const field = document.getElementById(fieldId);
        if (field) field.style.border = '2px solid #ddd';
      });
    }
    
    // Toggle visibility of fields based on movement type
    function updateMovementTypeFields() {
      const isVaet = document.getElementById('editTypeVaet').checked;
      const isOsc = document.getElementById('editTypeOsc').checked;
      const isChaos = document.getElementById('editTypeChaos').checked;
      const isCalibration = document.getElementById('editTypeCalibration').checked;
      
      document.getElementById('vaetFields').style.display = isVaet ? 'block' : 'none';
      document.getElementById('oscFields').style.display = isOsc ? 'block' : 'none';
      document.getElementById('chaosFields').style.display = isChaos ? 'block' : 'none';
      
      // Show/hide playlist loaders based on type
      document.getElementById('playlistLoaderSimple').style.display = isVaet ? 'block' : 'none';
      document.getElementById('playlistLoaderOscillation').style.display = isOsc ? 'block' : 'none';
      document.getElementById('playlistLoaderChaos').style.display = isChaos ? 'block' : 'none';
      
      // Hide cycles field for CHAOS (uses duration) and CALIBRATION (always 1 cycle)
      document.getElementById('cyclesFieldDiv').style.display = (isChaos || isCalibration) ? 'none' : 'block';
      // Pause is available for all types
      document.getElementById('pauseFieldDiv').style.display = 'block';
      
      // Revalidate when type changes
      validateEditForm();
    }
    
    function closeEditModal() {
      document.getElementById('editLineModal').style.display = 'none';
      editingLineId = null;
      
      // Clear any validation errors when closing modal
      clearErrorFields();
      const errorContainer = document.getElementById('editValidationErrors');
      if (errorContainer) errorContainer.style.display = 'none';
      
      // Re-enable save button
      const saveButton = document.getElementById('btnSaveEdit');
      if (saveButton) {
        saveButton.disabled = false;
        saveButton.style.opacity = '1';
        saveButton.style.cursor = 'pointer';
      }
    }
    
    function closeEditLineModalOnOverlayClick(event) {
      // Only close if clicking on the overlay itself (not the content)
      if (event.target.id === 'editLineModal') {
        closeEditModal();
      }
    }
    
    // ========================================================================
    // PLAYLIST INTEGRATION IN SEQUENCER
    // ========================================================================
    
    /**
     * Populate sequencer modal dropdown with presets for a specific mode
     */
    function populateSequencerDropdown(mode) {
      const selectId = 'edit' + mode.charAt(0).toUpperCase() + mode.slice(1) + 'PresetSelect';
      const select = document.getElementById(selectId);
      if (!select) return;
      
      const presets = PlaylistState[mode] || [];
      
      // Clear existing options (except first "-- Sélectionner --")
      select.innerHTML = '<option value="">-- Sélectionner un preset --</option>';
      
      // Add presets (sorted by timestamp desc - most recent first)
      const sortedPresets = [...presets].sort((a, b) => b.timestamp - a.timestamp);
      sortedPresets.forEach(preset => {
        const option = document.createElement('option');
        option.value = preset.id;
        option.textContent = preset.name;
        select.appendChild(option);
      });
    }
    
    /**
     * Preview a preset from sequencer dropdown
     */
    function previewSequencerPreset(mode, presetId) {
      if (!presetId) {
        hidePlaylistTooltip();
        return;
      }
      
      const id = parseInt(presetId);
      const preset = PlaylistState[mode].find(p => p.id === id);
      
      if (!preset) {
        hidePlaylistTooltip();
        return;
      }
      
      const tooltipContent = generatePresetTooltip(mode, preset.config);
      const overlay = document.getElementById('playlistTooltipOverlay');
      
      if (overlay && tooltipContent) {
        overlay.innerHTML = `<div style="font-weight: 600; margin-bottom: 8px; font-size: 13px;">📋 ${preset.name}</div>` + tooltipContent;
        overlay.classList.add('visible');
        
        // Auto-hide after 5 seconds
        setTimeout(() => {
          overlay.classList.remove('visible');
        }, 5000);
      }
    }
    
    /**
     * Quick Add preset directly to sequencer (from playlist modal)
     */
    function quickAddToSequencer(mode, presetId) {
      const preset = PlaylistState[mode].find(p => p.id === presetId);
      if (!preset) {
        showNotification('❌ Preset introuvable', 'error', 2000);
        return;
      }
      
      const config = preset.config;
      const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 200;
      
      // Build sequencer line - delegate to pure function if available
      let newLine;
      if (typeof buildSequenceLineFromPresetPure === 'function') {
        newLine = buildSequenceLineFromPresetPure(mode, config, effectiveMax);
      } else {
        // Fallback - minimal construction (should not happen if sequencer.js loaded)
        console.warn('buildSequenceLineFromPresetPure not available');
        const center = effectiveMax / 2;
        newLine = {
          enabled: true,
          movementType: mode === 'simple' ? 0 : mode === 'oscillation' ? 1 : 2,
          cycleCount: 1,
          pauseAfterMs: 0,
          startPositionMM: config.startPositionMM || 0,
          distanceMM: config.distanceMM || 50,
          speedForward: config.speedLevelForward || 5,
          speedBackward: config.speedLevelBackward || 5,
          decelStartEnabled: false,
          decelEndEnabled: true,
          decelZoneMM: 20,
          decelEffectPercent: 50,
          decelMode: 1,
          oscCenterPositionMM: config.centerPositionMM || center,
          oscAmplitudeMM: config.amplitudeMM || 50,
          oscWaveform: 0,
          oscFrequencyHz: 0.5,
          oscEnableRampIn: false,
          oscEnableRampOut: false,
          oscRampInDurationMs: 1000,
          oscRampOutDurationMs: 1000,
          chaosCenterPositionMM: config.centerPositionMM || center,
          chaosAmplitudeMM: config.amplitudeMM || 50,
          chaosMaxSpeedLevel: 10,
          chaosCrazinessPercent: 50,
          chaosDurationSeconds: 30,
          chaosSeed: 0,
          chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true],
          vaetCyclePauseEnabled: false,
          vaetCyclePauseIsRandom: false,
          vaetCyclePauseDurationSec: 0.0,
          vaetCyclePauseMinSec: 0.5,
          vaetCyclePauseMaxSec: 3.0,
          oscCyclePauseEnabled: false,
          oscCyclePauseIsRandom: false,
          oscCyclePauseDurationSec: 0.0,
          oscCyclePauseMinSec: 0.5,
          oscCyclePauseMaxSec: 3.0
        };
      }
      
      // Validate before sending
      const errors = validateSequencerLine(newLine, newLine.movementType);
      if (errors.length > 0) {
        showNotification('❌ Preset invalide pour séquenceur:\n' + errors.join('\n'), 'error', 5000);
        return;
      }
      
      // Send to backend
      sendCommand(WS_CMD.ADD_SEQUENCE_LINE, newLine);
      showNotification('✅ Ligne ajoutée au séquenceur: ' + preset.name, 'success', 3000);
      
      console.log('✅ Quick Add to sequencer:', preset.name, newLine);
    }
    
    /**
     * Load a preset from playlist into sequencer edit modal
     */
    function loadPresetIntoSequencerModal(mode) {
      const selectId = 'edit' + mode.charAt(0).toUpperCase() + mode.slice(1) + 'PresetSelect';
      const select = document.getElementById(selectId);
      const presetId = parseInt(select.value);
      
      if (!presetId) {
        showNotification('⚠️ Veuillez sélectionner un preset', 'error', 2000);
        return;
      }
      
      const preset = PlaylistState[mode].find(p => p.id === presetId);
      if (!preset) {
        showNotification('❌ Preset introuvable', 'error', 2000);
        return;
      }
      
      const config = preset.config;
      
      // Fill sequencer modal fields based on mode
      if (mode === 'simple') {
        document.getElementById('editStartPos').value = config.startPositionMM || 0;
        document.getElementById('editDistance').value = config.distanceMM || 50;
        document.getElementById('editSpeedFwd').value = config.speedLevelForward || 5;
        document.getElementById('editSpeedBack').value = config.speedLevelBackward || 5;
      } else if (mode === 'oscillation') {
        document.getElementById('editOscCenter').value = config.centerPositionMM || 100;
        document.getElementById('editOscAmplitude').value = config.amplitudeMM || 20;
        document.getElementById('editOscWaveform').value = config.waveform || 0;
        document.getElementById('editOscFrequency').value = config.frequencyHz || 1.0;
        document.getElementById('editOscRampIn').checked = config.enableRampIn || false;
        document.getElementById('editOscRampInDur').value = config.rampInDurationMs || 2000;
        document.getElementById('editOscRampOut').checked = config.enableRampOut || false;
        document.getElementById('editOscRampOutDur').value = config.rampOutDurationMs || 2000;
        
        // Cycles (0 = infinite not allowed in sequencer, use stored value or default to 10)
        const cycleValue = config.cycleCount === 0 ? 10 : config.cycleCount;
        document.getElementById('editCycles').value = cycleValue;
      } else if (mode === 'chaos') {
        document.getElementById('editChaosCenter').value = config.centerPositionMM || 100;
        document.getElementById('editChaosAmplitude').value = config.amplitudeMM || 40;
        document.getElementById('editChaosSpeed').value = config.maxSpeedLevel || 15;
        document.getElementById('editChaosCraziness').value = config.crazinessPercent || 50;
        document.getElementById('editChaosDuration').value = config.durationSeconds || 30;
        document.getElementById('editChaosSeed').value = 0; // Default seed
        
        // Set pattern checkboxes
        if (config.patternsEnabled && Array.isArray(config.patternsEnabled)) {
          for (let i = 0; i < config.patternsEnabled.length && i < 11; i++) {
            const checkbox = document.querySelector(`#chaosFields input[name="chaosPattern${i}"]`);
            if (checkbox) {
              checkbox.checked = config.patternsEnabled[i];
            }
          }
        }
      }
      
      // Trigger validation to update UI state
      validateEditForm();
      
      showNotification('✅ Valeurs chargées depuis: ' + preset.name, 'success', 2000);
    }
    
    function moveSequenceLine(lineId, direction) {
      sendCommand(WS_CMD.MOVE_SEQUENCE_LINE, { lineId: lineId, direction: direction });
    }
    
    function duplicateSequenceLine(lineId) {
      sendCommand(WS_CMD.DUPLICATE_SEQUENCE_LINE, { lineId: lineId });
    }
    
    /**
     * Test a single line by disabling others (replaceSequence doesn't exist in backend)
     */
    function testSequenceLine(lineId) {
      const line = sequenceLines.find(l => l.lineId === lineId);
      if (!line) {
        showNotification('❌ Ligne introuvable', 'error', 2000);
        return;
      }
      
      // Check if sequence is currently running
      if (AppState.sequencer && AppState.sequencer.isRunning) {
        showNotification('⚠️ Arrêtez la séquence en cours avant de tester', 'error', 3000);
        return;
      }
      
      // Backup current sequence state (enabled flags + cycle counts)
      window.sequenceBackup = sequenceLines.map(l => ({ 
        lineId: l.lineId, 
        enabled: l.enabled, 
        cycleCount: l.cycleCount 
      }));
      window.testedLineId = lineId;
      window.isTestingLine = true;
      
      console.log('🧪 Testing line #' + lineId + ' - Temporarily disabling other lines');
      
      // Disable all lines except the one we want to test
      sequenceLines.forEach(l => {
        if (l.lineId === lineId) {
          // Target line: ensure enabled (keep original cycle count - test full line!)
          if (!l.enabled) {
            sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: l.lineId, enabled: true });
            l.enabled = true;
          }
          // Don't modify cycleCount - we want to test the line as configured
        } else {
          // Other lines: temporarily disable
          if (l.enabled) {
            sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: l.lineId, enabled: false });
            l.enabled = false;
          }
        }
      });
      
      // Update visual state of lines without full re-render (to preserve selection)
      sequenceLines.forEach((l, idx) => {
        const row = document.querySelector(`tr[data-line-id="${l.lineId}"]`);
        if (row) {
          row.style.background = l.enabled ? 'white' : '#f5f5f5';
          row.style.opacity = l.enabled ? '1' : '0.6';
        }
      });
      
      // Disable ALL test buttons during test
      document.querySelectorAll('[id^="btnTestLine_"]').forEach(btn => {
        btn.disabled = true;
        btn.style.opacity = '0.5';
        btn.style.cursor = 'not-allowed';
      });
      
      // Start sequence after commands are sent
      setTimeout(() => {
        // Disable Unique and Boucle buttons JUST BEFORE starting
        // Use setButtonState to ensure it overrides any other state management
        console.log('🔍 About to disable buttons before startSequence, isTestingLine=', window.isTestingLine);
        if (DOM.btnStartSequence) {
          setButtonState(DOM.btnStartSequence, false);
          console.log('✅ btnStartSequence disabled via setButtonState');
        }
        if (DOM.btnLoopSequence) {
          setButtonState(DOM.btnLoopSequence, false);
          console.log('✅ btnLoopSequence disabled via setButtonState');
        }
        
        sendCommand(WS_CMD.START_SEQUENCE, {});
        const testedLine = sequenceLines.find(l => l.lineId === lineId);
        const cycleText = testedLine ? testedLine.cycleCount + ' cycle(s)' : '';
        showNotification('🧪 Test ligne #' + lineId + ' (' + cycleText + ')', 'info', 3000);
      }, 500);
    }
    
    /**
     * Restore original sequence after test
     */
    function restoreSequenceAfterTest() {
      if (!window.isTestingLine || !window.sequenceBackup) return;
      
      console.log('🔄 Restoring sequence original state');
      
      // Restore enabled flags only (cycleCount was not modified)
      window.sequenceBackup.forEach(backup => {
        const line = sequenceLines.find(l => l.lineId === backup.lineId);
        if (line && line.enabled !== backup.enabled) {
          sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: backup.lineId, enabled: backup.enabled });
          line.enabled = backup.enabled;
        }
      });
      
      // Clear test state FIRST (before re-render)
      window.isTestingLine = false;
      
      // Refresh UI (this will NOT re-disable buttons now that isTestingLine=false)
      renderSequenceTable();
      
      // Re-enable Unique and Boucle buttons immediately using setButtonState
      // No setTimeout needed since isTestingLine is already false
      if (DOM.btnStartSequence) {
        setButtonState(DOM.btnStartSequence, true);
        console.log('✅ btnStartSequence re-enabled after test');
      }
      if (DOM.btnLoopSequence) {
        setButtonState(DOM.btnLoopSequence, true);
        console.log('✅ btnLoopSequence re-enabled after test');
      }
      
      // Re-enable ALL test buttons
      document.querySelectorAll('[id^="btnTestLine_"]').forEach(btn => {
        btn.disabled = false;
        btn.style.opacity = '1';
        btn.style.cursor = 'pointer';
      });
      window.sequenceBackup = null;
      window.testedLineId = null;
      
      showNotification('✅ Séquence restaurée', 'success', 2000);
    }
    
    function toggleSequenceLine(lineId, enabled) {
      sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: lineId, enabled: enabled });
    }
    
    function clearSequence() {
      if (confirm('Effacer toutes les lignes du tableau?')) {
        sendCommand(WS_CMD.CLEAR_SEQUENCE, {});
      }
    }
    
    function exportSequence() {
      sendCommand(WS_CMD.EXPORT_SEQUENCE, {});
    }
    
    function importSequence() {
      const input = document.createElement('input');
      input.type = 'file';
      input.accept = '.json';
      
      input.onchange = function(e) {
        const file = e.target.files[0];
        if (!file) return;
        
        const reader = new FileReader();
        reader.onload = function(event) {
          try {
            let jsonText = event.target.result;
            
            // Remove /* */ comments (JSON doesn't support comments)
            jsonText = jsonText.replace(/\/\*[\s\S]*?\*\//g, '');
            
            // Remove // comments
            jsonText = jsonText.replace(/\/\/.*/g, '');
            
            // Validate JSON before sending
            const parsed = JSON.parse(jsonText);
            
            console.log('📤 Sending import via HTTP:', parsed.lineCount, 'lines,', jsonText.length, 'bytes');
            
            // Use HTTP POST instead of WebSocket to avoid size limits
            fetch('/api/sequence/import', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: jsonText
            })
            .then(response => response.json())
            .then(data => {
              if (data.success) {
                console.log('✅ Import successful:', data.message);
                alert('✅ Séquence importée avec succès!');
                
                // Refresh sequence table without reloading page
                sendCommand(WS_CMD.GET_SEQUENCE_TABLE, {});
              } else {
                console.error('❌ Import failed:', data.error);
                alert('❌ Erreur import: ' + (data.error || 'Unknown error'));
              }
            })
            .catch(error => {
              console.error('❌ HTTP request failed:', error);
              alert('❌ Erreur réseau: ' + error.message);
            });
            
          } catch (error) {
            console.error('❌ JSON parse error:', error);
            alert('Erreur JSON: ' + error.message + '\n\nVérifiez que le fichier est un JSON valide (pas de commentaires, virgules correctes, etc.)');
          }
        };
        reader.readAsText(file);
      };
      
      input.click();
    }
    
    /**
     * Download a JSON template with examples and documentation
     * Helps users create their own sequence files
     */
    function downloadTemplate() {
      // Use pure function from sequencer.js if available
      let fullDoc;
      if (typeof getSequenceTemplateDocPure === 'function') {
        fullDoc = getSequenceTemplateDocPure();
      } else {
        // Fallback - minimal template
        fullDoc = {
          TEMPLATE: {
            version: "2.0",
            lineCount: 1,
            lines: [{
              lineId: 1,
              enabled: true,
              movementType: 4,  // CALIBRATION
              cycleCount: 1,
              pauseAfterMs: 1000,
              startPositionMM: 0,
              distanceMM: 100,
              speedForward: 5.0,
              speedBackward: 5.0,
              decelStartEnabled: false,
              decelEndEnabled: false,
              decelZoneMM: 50.0,
              decelEffectPercent: 50.0,
              decelMode: 0,
              oscCenterPositionMM: 100.0,
              oscAmplitudeMM: 50.0,
              oscWaveform: 0,
              oscFrequencyHz: 0.1,
              oscEnableRampIn: false,
              oscEnableRampOut: false,
              oscRampInDurationMs: 1000.0,
              oscRampOutDurationMs: 1000.0,
              chaosCenterPositionMM: 110.0,
              chaosAmplitudeMM: 50.0,
              chaosMaxSpeedLevel: 10.0,
              chaosCrazinessPercent: 50.0,
              chaosDurationSeconds: 30,
              chaosSeed: 0,
              chaosPatternsEnabled: [true, true, true, true, true, true, true, true, true, true, true]
            }]
          },
          DOCUMENTATION: {
            "Note": "Template minimal - Voir sequencer.js pour la version complète"
          }
        };
      }
      
      // Create downloadable file
      const jsonStr = JSON.stringify(fullDoc, null, 2);
      const blob = new Blob([jsonStr], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'sequence_template_avec_aide.json';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      
      showNotification('📄 Template téléchargé avec documentation complète !', 'success', 3000);
    }
    
    function renderSequenceTable(data) {
      // If called without data, use existing sequenceLines (e.g., after restore)
      if (data && data.lines) {
        sequenceLines = data.lines;
      } else if (!sequenceLines || sequenceLines.length === 0) {
        console.error('Invalid sequence data');
        return;
      }
      
      // 🔄 MIGRATION: Convert old microsecond values to speedLevel (0-20)
      // If speed > 20, it's likely an old microsecond value (50-500µs)
      sequenceLines.forEach(line => {
        if (line.speedForward > 20) {
          // Old microsecond value: convert to speedLevel
          // 500µs = 1, 50µs = 20 (linear: speedLevel = 21 - delay/25)
          line.speedForward = Math.max(1, Math.min(20, 21 - line.speedForward / 25));
          console.log('⚠️ Converted old speedForward from µs to speedLevel:', line.speedForward.toFixed(1));
        }
        if (line.speedBackward > 20) {
          line.speedBackward = Math.max(1, Math.min(20, 21 - line.speedBackward / 25));
          console.log('⚠️ Converted old speedBackward from µs to speedLevel:', line.speedBackward.toFixed(1));
        }
      });
      
      const tbody = document.getElementById('sequenceTableBody');
      tbody.innerHTML = '';
      
      if (sequenceLines.length === 0) {
        tbody.innerHTML = `
          <tr>
            <td colspan="9" style="padding: 40px; text-align: center; color: #999;">
              <div style="font-size: 48px; margin-bottom: 10px;">📋</div>
              <div style="font-size: 16px;">Aucune ligne - Cliquez sur "➕ Ajouter ligne" pour commencer</div>
            </td>
          </tr>
        `;
        return;
      }
      
      sequenceLines.forEach((line, index) => {
        const row = document.createElement('tr');
        row.style.background = line.enabled ? 'white' : '#f5f5f5';
        row.style.opacity = line.enabled ? '1' : '0.6';
        row.style.borderBottom = '1px solid #ddd';
        row.style.transition = 'all 0.2s';
        
        // Phase 2: Drag & Drop attributes
        row.draggable = true;
        row.classList.add('sequence-line-draggable');
        row.setAttribute('data-line-id', line.lineId);
        row.setAttribute('data-line-index', index);
        
        // Phase 2: Multi-select - check if selected
        if (selectedLineIds.has(line.lineId)) {
          row.classList.add('sequence-line-selected');
        }
        
        // Add tooltip data
        const tooltipContent = generateSequenceLineTooltip(line);
        row.setAttribute('data-tooltip', tooltipContent.replace(/"/g, '&quot;'));
        row.setAttribute('data-line-number', index + 1);
        
        // Movement type icon and info
        const movementType = line.movementType !== undefined ? line.movementType : 0;
        // Movement type display - delegate to pure function if available
        let typeIcon = '';
        let typeInfo = '';
        let typeName = '';
        
        if (typeof getMovementTypeDisplayPure === 'function') {
          const typeDisplay = getMovementTypeDisplayPure(movementType, line);
          typeIcon = typeDisplay.icon;
          typeName = typeDisplay.name;
          // Format info as HTML for table display
          if (movementType === 0) {
            typeInfo = `
              <div style="font-size: 10px; line-height: 1.2;">
                <div>${line.startPositionMM.toFixed(1)}mm</div>
                <div>±${line.distanceMM.toFixed(1)}mm</div>
              </div>
            `;
          } else if (movementType === 1) {
            const waveformNames = ['SIN', 'TRI', 'SQR'];
            const waveformName = waveformNames[line.oscWaveform] || '?';
            typeInfo = `
              <div style="font-size: 10px; line-height: 1.2;">
                <div>C:${line.oscCenterPositionMM ? line.oscCenterPositionMM.toFixed(0) : '100'}mm</div>
                <div>A:±${line.oscAmplitudeMM ? line.oscAmplitudeMM.toFixed(0) : '50'}mm</div>
                <div>${waveformName} ${line.oscFrequencyHz ? line.oscFrequencyHz.toFixed(2) : '0.5'}Hz</div>
              </div>
            `;
          } else if (movementType === 2) {
            typeInfo = `
              <div style="font-size: 10px; line-height: 1.2;">
                <div>⏱️${line.chaosDurationSeconds || 30}s</div>
                <div>🎲${line.chaosCrazinessPercent ? line.chaosCrazinessPercent.toFixed(0) : '50'}%</div>
              </div>
            `;
          } else if (movementType === 4) {
            typeInfo = `
              <div style="font-size: 10px; line-height: 1.2;">
                <div>Calibration</div>
                <div>complète</div>
              </div>
            `;
          }
        } else {
          // Fallback - inline logic
          if (movementType === 0) {
            typeIcon = '�';
            typeName = 'Va-et-vient';
            typeInfo = `<div style="font-size: 10px;">${line.startPositionMM.toFixed(1)}mm ±${line.distanceMM.toFixed(1)}mm</div>`;
          } else if (movementType === 1) {
            typeIcon = '〰️';
            typeName = 'Oscillation';
            typeInfo = `<div style="font-size: 10px;">Osc</div>`;
          } else if (movementType === 2) {
            typeIcon = '🌀';
            typeName = 'Chaos';
            typeInfo = `<div style="font-size: 10px;">Chaos</div>`;
          } else if (movementType === 4) {
            typeIcon = '📏';
            typeName = 'Calibration';
            typeInfo = `<div style="font-size: 10px;">Calib</div>`;
          }
        }
        
        // Deceleration summary - delegate to pure function if available
        let decelSummary = '';
        if (typeof getDecelSummaryPure === 'function') {
          const decel = getDecelSummaryPure(line, movementType);
          if (decel.enabled) {
            decelSummary = `
              <div style="font-size: 10px; line-height: 1.3;">
                <div style="color: #4CAF50; font-weight: bold;">${decel.partsText}</div>
                <div>${decel.zoneMM}mm ${decel.effectPercent}%</div>
                <div>${decel.modeName}</div>
              </div>
            `;
          } else {
            decelSummary = '<span style="color: #999; font-size: 10px;">--</span>';
          }
        } else if (movementType === 0 && (line.decelStartEnabled || line.decelEndEnabled)) {
          // Fallback
          const parts = [];
          if (line.decelStartEnabled) parts.push('D');
          if (line.decelEndEnabled) parts.push('F');
          const modeLabels = ['Lin', 'Sin', 'Tri⁻¹', 'Sin⁻¹'];
          decelSummary = `
            <div style="font-size: 10px; line-height: 1.3;">
              <div style="color: #4CAF50; font-weight: bold;">${parts.join('/')}</div>
              <div>${line.decelZoneMM}mm ${line.decelEffectPercent}%</div>
              <div>${modeLabels[line.decelMode] || '?'}</div>
            </div>
          `;
        } else {
          decelSummary = '<span style="color: #999; font-size: 10px;">--</span>';
        }
        
        // Speeds - delegate to pure function if available
        let speedsDisplay = '';
        if (typeof getLineSpeedsDisplayPure === 'function') {
          const speeds = getLineSpeedsDisplayPure(line, movementType);
          if (speeds.type === 'bidirectional') {
            speedsDisplay = `
              <div style="font-size: 11px; line-height: 1.3;">
                <div style="color: #2196F3; font-weight: bold;">↗${speeds.forward}</div>
                <div style="color: #FF9800; font-weight: bold;">↙${speeds.backward}</div>
              </div>
            `;
          } else if (speeds.type === 'peak') {
            speedsDisplay = `
              <div style="font-size: 11px; font-weight: bold; color: #9C27B0;">
                ${speeds.peakSpeedDisplay}
              </div>
            `;
          } else if (speeds.type === 'max') {
            speedsDisplay = `
              <div style="font-size: 11px; font-weight: bold; color: #E91E63;">
                ${speeds.maxSpeedDisplay}
              </div>
            `;
          } else if (speeds.type === 'fixed') {
            speedsDisplay = `<div style="font-size: 11px; color: #999;">${speeds.display}</div>`;
          }
        } else {
          // Fallback - inline logic
          if (movementType === 0) {
            speedsDisplay = `
              <div style="font-size: 11px; line-height: 1.3;">
                <div style="color: #2196F3; font-weight: bold;">↗${line.speedForward.toFixed(1)}</div>
                <div style="color: #FF9800; font-weight: bold;">↙${line.speedBackward.toFixed(1)}</div>
              </div>
            `;
          } else if (movementType === 1) {
            const peakSpeedMMPerSec = 2 * Math.PI * line.oscFrequencyHz * line.oscAmplitudeMM;
            speedsDisplay = `
              <div style="font-size: 11px; font-weight: bold; color: #9C27B0;">
                ${peakSpeedMMPerSec.toFixed(0)} mm/s
              </div>
            `;
          } else {
            speedsDisplay = `
              <div style="font-size: 11px; font-weight: bold; color: #E91E63;">
                ${line.chaosMaxSpeedLevel ? line.chaosMaxSpeedLevel.toFixed(1) : '10.0'}
              </div>
            `;
          }
        }
        
        // Cycles and pause - delegate to pure function if available
        let cyclesDisplay = line.cycleCount;
        let pauseDisplay = line.pauseAfterMs > 0 ? (line.pauseAfterMs / 1000).toFixed(1) + 's' : '--';
        let pauseColor = line.pauseAfterMs > 0 ? '#9C27B0' : '#999';
        let pauseWeight = line.pauseAfterMs > 0 ? 'bold' : 'normal';
        
        if (typeof getLineCyclesPausePure === 'function') {
          const cyclesPause = getLineCyclesPausePure(line, movementType);
          cyclesDisplay = cyclesPause.cyclesDisplay;
          pauseDisplay = cyclesPause.pauseDisplay;
          pauseColor = cyclesPause.pauseMs > 0 ? '#9C27B0' : '#999';
          pauseWeight = cyclesPause.pauseMs > 0 ? 'bold' : 'normal';
        }
        
        row.innerHTML = `
          <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
            <input type="checkbox" ${line.enabled ? 'checked' : ''} 
              onchange="toggleSequenceLine(${line.lineId}, this.checked)"
              style="width: 16px; height: 16px; cursor: pointer;">
          </td>
          <td style="padding: 4px 2px; text-align: center; font-weight: bold; color: #667eea; border-right: 1px solid #eee; font-size: 12px;">
            ${index + 1}
          </td>
          <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;" title="${movementType === 0 ? 'Va-et-vient' : (movementType === 1 ? 'Oscillation' : (movementType === 2 ? 'Chaos' : 'Calibration'))}">
            <div style="font-size: 16px;">${typeIcon}</div>
          </td>
          <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
            ${typeInfo}
          </td>
          <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
            <span style="color: #999; font-size: 10px;">--</span>
          </td>
          <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
            ${speedsDisplay}
          </td>
          <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
            ${decelSummary}
          </td>
          <td style="padding: 4px 2px; text-align: center; font-weight: bold; color: #FF9800; border-right: 1px solid #eee;">
            ${cyclesDisplay}
          </td>
          <td style="padding: 4px 2px; text-align: center; color: ${pauseColor}; font-weight: ${pauseWeight}; border-right: 1px solid #eee; font-size: 10px;">
            ${pauseDisplay}
          </td>
          <td style="padding: 4px 2px; text-align: center; white-space: nowrap;">
            <button onclick="testSequenceLine(${line.lineId})" 
              id="btnTestLine_${line.lineId}"
              style="background: #9C27B0; color: white; border: none; padding: 4px 6px; border-radius: 3px; cursor: pointer; margin: 1px; font-size: 12px;"
              title="Tester cette ligne">▶️</button>
            <button onclick="editSequenceLine(${line.lineId})" 
              style="background: #2196F3; color: white; border: none; padding: 4px 6px; border-radius: 3px; cursor: pointer; margin: 1px; font-size: 12px;"
              title="Éditer">✏️</button>
            <button onclick="duplicateSequenceLine(${line.lineId})"
              style="background: #4CAF50; color: white; border: none; padding: 4px 6px; border-radius: 3px; cursor: pointer; margin: 1px; font-size: 12px;"
              title="Dupliquer">📋</button>
            <span id="tooltipEye_${line.lineId}" class="sequence-tooltip-eye" data-line-id="${line.lineId}"
              style="display: inline-block; padding: 4px 6px; cursor: pointer; margin: 1px; font-size: 14px;"
              title="Voir détails">👁️</span>
          </td>
        `;
        
        // Add line type attribute
        row.setAttribute('data-line-type', typeName);
        
        // Hover effect (no tooltip on row hover)
        row.onmouseenter = function() {
          if (line.enabled && !this.classList.contains('sequence-line-selected')) {
            this.style.background = '#f0f4ff';
          }
        };
        row.onmouseleave = function() {
          if (!this.classList.contains('sequence-line-selected')) {
            this.style.background = line.enabled ? 'white' : '#f5f5f5';
          }
        };
        
        // Phase 2: Drag events
        row.ondragstart = function(e) {
          draggedLineId = line.lineId;
          draggedLineIndex = index;
          this.classList.add('sequence-line-dragging');
          e.dataTransfer.effectAllowed = 'move';
          e.dataTransfer.setData('text/plain', line.lineId);
          
          // Highlight permanent trash zone
          const trashDropZone = document.getElementById('sequenceTrashDropZone');
          if (trashDropZone) {
            trashDropZone.classList.add('drag-active');
          }
        };
        
        row.ondragend = function(e) {
          this.classList.remove('sequence-line-dragging');
          
          // Remove spacer row
          const spacer = document.querySelector('.sequence-drop-spacer');
          if (spacer) {
            spacer.remove();
          }
          
          // Reset permanent trash zone
          const trashDropZone = document.getElementById('sequenceTrashDropZone');
          if (trashDropZone) {
            trashDropZone.classList.remove('drag-active');
          }
        };
        
        row.ondragover = function(e) {
          if (draggedLineId === line.lineId) return;
          e.preventDefault();
          e.dataTransfer.dropEffect = 'move';
          
          // Smart spacer positioning based on mouse Y position within row
          if (draggedLineId !== line.lineId) {
            // Get row bounds
            const rect = this.getBoundingClientRect();
            const mouseY = e.clientY;
            const rowMiddle = rect.top + (rect.height / 2);
            
            // Determine if we should insert BEFORE or AFTER based on mouse position
            let insertAfter;
            if (draggedLineIndex < index) {
              // Dragging DOWN: only show spacer if mouse is in bottom half of row
              insertAfter = (mouseY > rowMiddle);
            } else {
              // Dragging UP: show spacer based on mouse position
              insertAfter = (mouseY > rowMiddle);
            }
            
            // Calculate final target position after insert
            let finalTargetIndex;
            if (insertAfter) {
              finalTargetIndex = index;
            } else {
              finalTargetIndex = index - 1;
            }
            
            // Don't show spacer if it would result in same position (no movement)
            // Also block if trying to insert on the dragged line itself
            if (finalTargetIndex === draggedLineIndex || index === draggedLineIndex) {
              const existingSpacer = document.querySelector('.sequence-drop-spacer');
              if (existingSpacer) existingSpacer.remove();
              return false;
            }
            
            // Throttle: only update if 200ms elapsed since last update (reduce flicker)
            const now = Date.now();
            if (now - lastDragEnterTime < 200) return false;
            lastDragEnterTime = now;
            
            // Check if spacer already exists at correct position
            const existingSpacer = document.querySelector('.sequence-drop-spacer');
            if (existingSpacer) {
              const spacerParent = existingSpacer.parentNode;
              const spacerNextSibling = existingSpacer.nextSibling;
              const spacerPrevSibling = existingSpacer.previousSibling;
              
              // Check if spacer is already at correct position
              if (insertAfter && spacerPrevSibling === this) {
                // Already after this row, no need to recreate
                return false;
              }
              if (!insertAfter && spacerNextSibling === this) {
                // Already before this row, no need to recreate
                return false;
              }
              
              // Remove if position changed
              existingSpacer.remove();
            }
            
            // Create spacer row
            const spacer = document.createElement('tr');
            spacer.className = 'sequence-drop-spacer';
            spacer.innerHTML = '<td colspan="10" style="height: 50px; padding: 0; border: none; background: transparent;"><div class="sequence-drop-placeholder-inner">⬇ Insérer ici ⬇</div></td>';
            
            // Store target for drop handling
            spacer.dataset.targetLineId = line.lineId;
            spacer.dataset.targetIndex = index;
            
            // Make spacer droppable
            spacer.ondragover = function(e) {
              e.preventDefault();
              e.dataTransfer.dropEffect = 'move';
              return false;
            };
            
            spacer.ondrop = function(e) {
              e.preventDefault();
              e.stopPropagation();
              const targetRow = document.querySelector(`[data-line-id="${this.dataset.targetLineId}"]`);
              if (targetRow && targetRow.ondrop) {
                targetRow.ondrop(e);
              }
              return false;
            };
            
            // Insert spacer
            if (insertAfter) {
              this.parentNode.insertBefore(spacer, this.nextSibling);
            } else {
              this.parentNode.insertBefore(spacer, this);
            }
          }
          
          return false;
        };
        
        row.ondragenter = function(e) {
          // Just prevent default, ondragover handles everything
          if (draggedLineId === line.lineId) return;
          e.preventDefault();
        };
        
        row.ondragleave = function(e) {
          // Don't remove placeholder on leave - keep it visible
        };
        
        row.ondrop = function(e) {
          e.stopPropagation();
          e.preventDefault();
          
          // Remove spacer row
          const spacer = document.querySelector('.sequence-drop-spacer');
          if (spacer) {
            spacer.remove();
          }
          
          if (draggedLineId && draggedLineId !== line.lineId) {
            // Calculate direction: drag FROM draggedLineIndex TO index
            const targetIndex = index;
            let direction;
            
            if (draggedLineIndex < targetIndex) {
              // Moving down: need (targetIndex - draggedLineIndex) moves down
              direction = targetIndex - draggedLineIndex;
            } else {
              // Moving up: need -(draggedLineIndex - targetIndex) moves up
              direction = -(draggedLineIndex - targetIndex);
            }
            
            // Send multiple move commands to backend
            console.log(`📦 Drag: line ${draggedLineId} from index ${draggedLineIndex} to ${targetIndex} (${direction} moves)`);
            
            // Execute moves sequentially
            let movesRemaining = Math.abs(direction);
            const moveDirection = direction > 0 ? 1 : -1;
            
            const executeMove = () => {
              if (movesRemaining > 0) {
                sendCommand(WS_CMD.MOVE_SEQUENCE_LINE, { lineId: draggedLineId, direction: moveDirection });
                movesRemaining--;
                setTimeout(executeMove, 100); // Wait 100ms between moves
              } else {
                // After move complete: clear selection if dragged line was selected
                if (selectedLineIds.has(draggedLineId)) {
                  selectedLineIds.delete(draggedLineId);
                  updateBatchToolbar();
                  renderSequenceTable({ lines: sequenceLines });
                }
              }
            };
            
            executeMove();
          }
          
          return false;
        };
        
        // Phase 2: Multi-select click handler
        row.onclick = function(e) {
          // Don't trigger if clicking on buttons or checkbox
          if (e.target.tagName === 'BUTTON' || e.target.tagName === 'INPUT') {
            return;
          }
          
          if (e.shiftKey && lastSelectedIndex !== null) {
            // Shift+Click: select range
            const startIdx = Math.min(lastSelectedIndex, index);
            const endIdx = Math.max(lastSelectedIndex, index);
            
            for (let i = startIdx; i <= endIdx; i++) {
              if (i < sequenceLines.length) {
                selectedLineIds.add(sequenceLines[i].lineId);
              }
            }
          } else if (e.ctrlKey || e.metaKey) {
            // Ctrl+Click: toggle selection
            if (selectedLineIds.has(line.lineId)) {
              selectedLineIds.delete(line.lineId);
            } else {
              selectedLineIds.add(line.lineId);
            }
          } else {
            // Regular click: select only this line
            selectedLineIds.clear();
            selectedLineIds.add(line.lineId);
          }
          
          lastSelectedIndex = index;
          updateBatchToolbar();
          renderSequenceTable({ lines: sequenceLines }); // Re-render to show selection
        };
        
        tbody.appendChild(row);
        
        // NOW attach eye icon tooltip handler (after DOM insertion)
        setTimeout(() => {
          const eyeIcon = document.getElementById('tooltipEye_' + line.lineId);
          if (eyeIcon) {
            eyeIcon.onmouseenter = function(e) {
              showSequenceTooltip(row);
            };
            eyeIcon.onmouseleave = function() {
              hidePlaylistTooltip();
            };
          }
        }, 0);
      });
      
      // Add drag leave handler to tbody to cleanup spacer if leaving table
      tbody.ondragleave = function(e) {
        // Only remove if leaving tbody completely (not entering child element)
        if (!this.contains(e.relatedTarget)) {
          const spacer = document.querySelector('.sequence-drop-spacer');
          if (spacer) {
            spacer.remove();
          }
        }
      };
      
      // Update batch toolbar visibility
      updateBatchToolbar();
      
      // Initialize trash zones drag handlers (only once)
      initializeTrashZones();
      
      // Re-disable test buttons if testing (in case of re-render during test)
      // But do it synchronously to avoid timing issues
      if (window.isTestingLine) {
        document.querySelectorAll('[id^="btnTestLine_"]').forEach(btn => {
          btn.disabled = true;
          btn.style.opacity = '0.5';
          btn.style.cursor = 'not-allowed';
        });
        // Also re-disable start buttons using setButtonState
        if (DOM.btnStartSequence) setButtonState(DOM.btnStartSequence, false);
        if (DOM.btnLoopSequence) setButtonState(DOM.btnLoopSequence, false);
      }
    }
    
    /**
     * Phase 2: Initialize trash zones drag & drop handlers
     */
    function initializeTrashZones() {
      // Toolbar trash zone (batch operations)
      const trashZone = document.getElementById('sequenceTrashZone');
      if (trashZone && !trashZone.hasAttribute('data-initialized')) {
        trashZone.setAttribute('data-initialized', 'true');
      
      trashZone.ondragover = function(e) {
        e.preventDefault();
        e.dataTransfer.dropEffect = 'move';
        this.classList.add('drag-over');
        return false;
      };
      
      trashZone.ondragleave = function(e) {
        if (!this.contains(e.relatedTarget)) {
          this.classList.remove('drag-over');
        }
      };
      
      trashZone.ondrop = function(e) {
        e.preventDefault();
        e.stopPropagation();
        this.classList.remove('drag-over');
        
        // Get dragged line(s)
        const linesToDelete = selectedLineIds.size > 0 ? 
          Array.from(selectedLineIds) : 
          [draggedLineId];
        
        if (linesToDelete.length === 0) return;
        
        // Confirm deletion
        const count = linesToDelete.length;
        const message = count === 1 ? 
          `⚠️ Supprimer la ligne sélectionnée ?\n\nCette action est irréversible.` :
          `⚠️ Supprimer ${count} lignes sélectionnées ?\n\nCette action est irréversible.`;
        
        if (!confirm(message)) return;
        
        console.log(`🗑️ Trash zone drop: deleting ${count} line(s)`);
        
        // Sort descending to delete from end
        const sortedIds = linesToDelete.sort((a, b) => b - a);
        
        sortedIds.forEach(lineId => {
          sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
        });
        
        showNotification(`✅ ${count} ligne(s) supprimée(s)`, 'success', 2000);
        
        // Clear selection
        clearSelection();
        
        return false;
      };
      }
      
      // Permanent trash drop zone (always visible)
      const trashDropZone = document.getElementById('sequenceTrashDropZone');
      if (trashDropZone && !trashDropZone.hasAttribute('data-initialized')) {
        trashDropZone.setAttribute('data-initialized', 'true');
        
        trashDropZone.ondragover = function(e) {
          e.preventDefault();
          e.dataTransfer.dropEffect = 'move';
          return false;
        };
        
        trashDropZone.ondrop = function(e) {
          e.preventDefault();
          e.stopPropagation();
          
          // Get dragged line(s)
          const linesToDelete = selectedLineIds.size > 0 ? 
            Array.from(selectedLineIds) : 
            [draggedLineId];
          
          if (linesToDelete.length === 0) return;
          
          // Confirm deletion
          const count = linesToDelete.length;
          const message = count === 1 ? 
            `⚠️ Supprimer la ligne sélectionnée ?\n\nCette action est irréversible.` :
            `⚠️ Supprimer ${count} lignes sélectionnées ?\n\nCette action est irréversible.`;
          
          if (!confirm(message)) return;
          
          console.log(`🗑️ Permanent trash zone drop: deleting ${count} line(s)`);
          
          // Sort descending to delete from end
          const sortedIds = linesToDelete.sort((a, b) => b - a);
          
          sortedIds.forEach(lineId => {
            sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
          });
          
          showNotification(`✅ ${count} ligne(s) supprimée(s)`, 'success', 2000);
          
          // Clear selection
          clearSelection();
          
          return false;
        };
      }
    }
    
    /**
     * Phase 2: Update batch toolbar visibility and count
     */
    function updateBatchToolbar() {
      const toolbar = document.getElementById('sequenceBatchToolbar');
      const countDisplay = document.getElementById('batchSelectionCount');
      
      if (selectedLineIds.size > 0) {
        toolbar.classList.add('visible');
        countDisplay.textContent = `${selectedLineIds.size} ligne(s) sélectionnée(s)`;
      } else {
        toolbar.classList.remove('visible');
      }
    }
    
    /**
     * Phase 2: Clear all selections
     */
    function clearSelection() {
      selectedLineIds.clear();
      lastSelectedIndex = null;
      updateBatchToolbar();
      renderSequenceTable({ lines: sequenceLines });
    }
    
    /**
     * Phase 2: Batch enable/disable lines
     */
    function batchEnableLines(enabled) {
      if (selectedLineIds.size === 0) return;
      
      console.log(`📦 Batch ${enabled ? 'enable' : 'disable'} ${selectedLineIds.size} lines`);
      
      selectedLineIds.forEach(lineId => {
        sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: lineId, enabled: enabled });
        
        // Update local state
        const line = sequenceLines.find(l => l.lineId === lineId);
        if (line) {
          line.enabled = enabled;
        }
      });
      
      showNotification(`✅ ${selectedLineIds.size} ligne(s) ${enabled ? 'activée(s)' : 'désactivée(s)'}`, 'success', 2000);
      
      // Clear selection after operation
      clearSelection();
    }
    
    /**
     * Phase 2: Batch delete lines
     */
    function batchDeleteLines() {
      if (selectedLineIds.size === 0) return;
      
      const count = selectedLineIds.size;
      if (!confirm(`⚠️ Supprimer ${count} ligne(s) sélectionnée(s) ?\n\nCette action est irréversible.`)) {
        return;
      }
      
      console.log(`📦 Batch delete ${count} lines`);
      
      // Convert to array and sort by lineId descending (delete from end to avoid index shifts)
      const lineIdsArray = Array.from(selectedLineIds).sort((a, b) => b - a);
      
      lineIdsArray.forEach(lineId => {
        sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
      });
      
      showNotification(`✅ ${count} ligne(s) supprimée(s)`, 'success', 2000);
      
      // Clear selection
      clearSelection();
    }
    
    function updateSequenceStatus(status) {
      if (!status) return;
      
      // Update mode
      const modeText = status.isRunning 
        ? (status.isLoopMode ? '🔁 BOUCLE INFINIE' : '▶️ LECTURE UNIQUE')
        : '⏹️ Arrêté';
      DOM.seqMode.textContent = modeText;
      DOM.seqMode.style.color = status.isRunning ? '#4CAF50' : '#999';
      
      // Update current line
      const lineText = status.isRunning 
        ? `${status.currentLineNumber} / ${status.totalLines}`
        : '-- / --';
      DOM.seqCurrentLine.textContent = lineText;
      
      // Update cycle
      DOM.seqLineCycle.textContent = 
        status.isRunning ? status.currentCycle : '--';
      
      // Update loop count
      DOM.seqLoopCount.textContent = status.loopCount || 0;
      
      // Update pause remaining
      const pauseText = status.pauseRemaining > 0 
        ? `${status.pauseRemaining} ms`
        : '-- ms';
      DOM.seqPauseRemaining.textContent = pauseText;
      
      // Button states: ONLY controlled by backend sequenceStatus
      // Backend says "running" = buttons disabled, "stopped" = buttons enabled
      const isRunning = status.isRunning;
      
      // Auto-restore sequence after test
      if (!isRunning && window.isTestingLine) {
        console.log('🔄 Test finished, scheduling restore...');
        setTimeout(() => {
          restoreSequenceAfterTest();
        }, 500);
      }
      
      // Start buttons: enable ONLY when backend confirms sequence stopped
      // BUT: Keep disabled during line test (will be re-enabled by restoreSequenceAfterTest)
      console.log('🔍 updateSequenceStatus: isRunning=', isRunning, 'isTestingLine=', window.isTestingLine);
      if (!isRunning && !window.isTestingLine) {
        const canStart = canStartOperation();
        console.log('🟢 Enabling start buttons, canStart=', canStart);
        setButtonState(DOM.btnStartSequence, canStart);
        setButtonState(DOM.btnLoopSequence, canStart);
      } else {
        console.log('🔴 Keeping start buttons disabled');
        // Force disable to override any other updates
        setButtonState(DOM.btnStartSequence, false);
        setButtonState(DOM.btnLoopSequence, false);
      }
      // If running or testing, keep disabled (already disabled on click or by testSequenceLine)
      
      // Control buttons: available while running
      setButtonState(DOM.btnPauseSequence, isRunning);
      setButtonState(DOM.btnStopSequence, isRunning);
      setButtonState(DOM.btnSkipLine, isRunning);
      
      // Update pause button text
      if (isRunning && status.isPaused) {
        DOM.btnPauseSequence.innerHTML = '▶️ Reprendre';
      } else {
        DOM.btnPauseSequence.innerHTML = '⏸️ Pause';
      }
      
      // Highlight active line using currentLineIndex (not currentLineId)
      const tbody = document.getElementById('sequenceTableBody');
      if (tbody) {
        const rows = tbody.querySelectorAll('tr');
        rows.forEach(row => {
          row.classList.remove('sequence-line-active');
        });
        
        if (isRunning && status.currentLineIndex !== undefined) {
          // Backend sends 0-based currentLineIndex
          const activeIndex = status.currentLineIndex;
          
          if (activeIndex >= 0 && activeIndex < rows.length) {
            rows[activeIndex].classList.add('sequence-line-active');
            
            // Scroll to active line
            rows[activeIndex].scrollIntoView({ behavior: 'smooth', block: 'nearest' });
          }
        }
      }
    }
    
    // ========================================================================
    // WEBSOCKET CONNECTION - Moved to /js/websocket.js
    // ========================================================================
    // connectWebSocket() is now loaded from external module
    // Handles: onopen, onmessage, onclose, onerror
    // Message routing: handleWebSocketMessage() dispatches to appropriate handlers
    
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
      
      if (data.totalTraveled !== undefined) {
        DOM.totalTraveled.textContent = (data.totalTraveled / 1000.0).toFixed(3) + " m";

        
        // Update milestone icon
        const milestoneInfo = getMilestoneInfo(data.totalTraveled / 1000.0); // Convert mm to m
        
        if (milestoneInfo.current) {
          // Build tooltip with progress info
          let tooltip = `${milestoneInfo.current.emoji} ${milestoneInfo.current.name} (${milestoneInfo.current.threshold}m)`;
          if (milestoneInfo.current.location !== "-") {
            tooltip += ` - ${milestoneInfo.current.location}`;
          }
          
          if (milestoneInfo.next) {
            tooltip += `\n\n⏭️ Prochain: ${milestoneInfo.next.emoji} ${milestoneInfo.next.name} (${milestoneInfo.next.threshold}m)`;
            tooltip += `\n📊 Progression: ${milestoneInfo.progressPercent}%`;
          } else {
            tooltip += `\n\n🎉 Dernier jalon atteint!`;
          }
          
          DOM.milestoneIcon.textContent = milestoneInfo.current.emoji;
          DOM.milestoneIcon.title = tooltip;
          
          // Milestone tracking logic
          const newThreshold = milestoneInfo.current.threshold;
          const totalTraveledM = data.totalTraveled / 1000.0;
          
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
                message += `\n⏭️ Prochain: ${milestoneInfo.next.emoji} (${milestoneInfo.next.threshold}m) - ${milestoneInfo.progressPercent}%`;
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
            const tooltip = `⏭️ Prochain: ${milestoneInfo.next.emoji} ${milestoneInfo.next.name} (${milestoneInfo.next.threshold}m)\n📊 Progression: ${milestoneInfo.progressPercent}%`;
            DOM.milestoneIcon.textContent = '🐜';
            DOM.milestoneIcon.title = tooltip;
          } else {
            DOM.milestoneIcon.textContent = '';
            DOM.milestoneIcon.title = '';
          }
        }
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
      
      // Update deceleration zone configuration from server
      if (data.decelZone && AppState.editing.input !== 'decelZone') {
        const section = document.getElementById('decelSection');
        const headerText = document.getElementById('decelHeaderText');
        
        // Defense: Only update full decelZone fields if enabled (Phase 1 optimization)
        // When disabled, backend sends only {enabled: false} to save bandwidth
        if (data.decelZone.enabled && data.decelZone.zoneMM !== undefined) {
          // Update section collapsed state and header text based on enabled
          if (section && headerText) {
            section.classList.remove('collapsed');
            headerText.textContent = '🎯 Décélération - activée';
          }
          
          // Safe access to optional fields
          if (data.decelZone.enableStart !== undefined) {
            const startCheckbox = document.getElementById('decelZoneStart');
            if (startCheckbox) startCheckbox.checked = data.decelZone.enableStart;
          }
          if (data.decelZone.enableEnd !== undefined) {
            const endCheckbox = document.getElementById('decelZoneEnd');
            if (endCheckbox) endCheckbox.checked = data.decelZone.enableEnd;
          }
          
          // Check if zone value was adapted by ESP32 (only if we just sent a request)
          const decelZoneInput = document.getElementById('decelZoneMM');
          const requestedZone = AppState.lastDecelZoneRequest;
          const receivedZone = data.decelZone.zoneMM;
          
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
          if (data.decelZone.effectPercent !== undefined) {
            const effectPercentInput = document.getElementById('decelEffectPercent');
            const effectValueSpan = document.getElementById('effectValue');
            if (effectPercentInput) {
              effectPercentInput.value = data.decelZone.effectPercent;
            }
            if (effectValueSpan) {
              effectValueSpan.textContent = data.decelZone.effectPercent.toFixed(0) + '%';
            }
          }
          
          // Update select dropdown for mode
          if (data.decelZone.mode !== undefined) {
            const decelModeSelect = document.getElementById('decelModeSelect');
            if (decelModeSelect) {
              decelModeSelect.value = data.decelZone.mode.toString();
            }
          }
          
          // Update zone preset active state
          document.querySelectorAll('[data-decel-zone]').forEach(btn => {
            const btnValue = parseInt(btn.getAttribute('data-decel-zone'));
            if (btnValue === data.decelZone.zoneMM) {
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
      
      // Update oscillation state display
      if (data.oscillation && data.oscillationState) {
        DOM.oscCurrentAmplitude.textContent = 
          data.oscillationState.currentAmplitude.toFixed(2);
        DOM.oscCompletedCycles.textContent = 
          data.oscillationState.completedCycles;
        
        let rampStatus = 'Aucune';
        if (data.oscillationState.isTransitioning) {
          rampStatus = '🔄 Transition fréquence...';
        } else if (data.oscillationState.isRampingIn) {
          rampStatus = '📈 Rampe entrée';
        } else if (data.oscillationState.isRampingOut) {
          rampStatus = '📉 Rampe sortie';
        } else if (data.operationMode === 3 && data.state === SystemState.RUNNING) {  // MODE_OSCILLATION + RUNNING
          rampStatus = '✅ Stabilisé';
        }
        DOM.oscRampStatus.textContent = rampStatus;
        
        // 🔒 DISABLE frequency controls during transition (500ms smooth change)
        const isTransitioning = data.oscillationState.isTransitioning || false;
        DOM.oscFrequency.disabled = isTransitioning;
        
        // Apply visual feedback during transition
        if (isTransitioning) {
          DOM.oscFrequency.style.backgroundColor = '#fff3cd';
          DOM.oscFrequency.style.cursor = 'not-allowed';
        } else {
          DOM.oscFrequency.style.backgroundColor = '';
          DOM.oscFrequency.style.cursor = '';
        }
        
        // Also disable preset buttons during transition
        document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
          btn.disabled = isTransitioning;
          btn.style.opacity = isTransitioning ? '0.5' : '1';
          btn.style.cursor = isTransitioning ? 'not-allowed' : 'pointer';
        });
        
        // Sync oscillation config to UI (skip fields being edited OR having focus)
        if (AppState.editing.oscField !== 'oscCenter' && document.activeElement !== DOM.oscCenter) {
          DOM.oscCenter.value = data.oscillation.centerPositionMM.toFixed(1);
        }
        
        if (AppState.editing.oscField !== 'oscAmplitude' && document.activeElement !== DOM.oscAmplitude) {
          DOM.oscAmplitude.value = data.oscillation.amplitudeMM.toFixed(1);
        }
        
        if (AppState.editing.oscField !== 'oscWaveform' && document.activeElement !== DOM.oscWaveform) {
          DOM.oscWaveform.value = data.oscillation.waveform;
        }
        
        if (AppState.editing.oscField !== 'oscFrequency' && document.activeElement !== DOM.oscFrequency && !isTransitioning) {
          // Use effective frequency if available (accounts for speed limiting)
          const displayFreq = data.oscillation.effectiveFrequencyHz || data.oscillation.frequencyHz;
          const isFreqLimited = data.oscillation.effectiveFrequencyHz && 
                                Math.abs(data.oscillation.effectiveFrequencyHz - data.oscillation.frequencyHz) > 0.001;
          
          DOM.oscFrequency.value = displayFreq.toFixed(3);
          
          // Visual feedback if frequency is limited
          if (isFreqLimited) {
            DOM.oscFrequency.style.backgroundColor = '#ffe8e8';
            DOM.oscFrequency.style.fontWeight = 'bold';
            DOM.oscFrequency.style.color = '#d32f2f';
            DOM.oscFrequency.title = `⚠️ Fréquence limitée de ${data.oscillation.frequencyHz.toFixed(2)} Hz à ${displayFreq.toFixed(2)} Hz (vitesse max: 300 mm/s)`;
          } else {
            DOM.oscFrequency.style.backgroundColor = '';
            DOM.oscFrequency.style.fontWeight = '';
            DOM.oscFrequency.style.color = '';
            DOM.oscFrequency.title = '';
          }
        }
        
        if (AppState.editing.oscField !== 'oscRampInDuration' && document.activeElement !== DOM.oscRampInDuration) {
          DOM.oscRampInDuration.value = data.oscillation.rampInDurationMs;
        }
        
        if (AppState.editing.oscField !== 'oscRampOutDuration' && document.activeElement !== DOM.oscRampOutDuration) {
          DOM.oscRampOutDuration.value = data.oscillation.rampOutDurationMs;
        }
        
        if (AppState.editing.oscField !== 'oscCycleCount' && document.activeElement !== DOM.oscCycleCount) {
          DOM.oscCycleCount.value = data.oscillation.cycleCount;
        }
        
        // Checkboxes (not edited via focus)
        DOM.oscRampInEnable.checked = data.oscillation.enableRampIn;
        DOM.oscRampOutEnable.checked = data.oscillation.enableRampOut;
        DOM.oscReturnCenter.checked = data.oscillation.returnToCenter;
        
        // Update ramp visibility (removed in compact mode - always visible inline)
        // DOM.oscRampInConfig.style.display = 
        //   data.oscillation.enableRampIn ? 'block' : 'none';
        // DOM.oscRampOutConfig.style.display = 
        //   data.oscillation.enableRampOut ? 'block' : 'none';
        
        // Validate limits (only if not editing center or amplitude)
        if (AppState.editing.oscField !== 'oscCenter' && AppState.editing.oscField !== 'oscAmplitude') {
          validateOscillationLimits();
        }
        
        // Update preset buttons visual state
        updateOscillationPresets();
      }
      
      // ===== UPDATE CYCLE PAUSE DISPLAY (MODE SIMPLE) =====
      if (data.motion && data.motion.cyclePause) {
        const pauseStatus = document.getElementById('cyclePauseStatus');
        const pauseRemaining = document.getElementById('cyclePauseRemaining');
        
        if (data.motion.cyclePause.isPausing && pauseStatus && pauseRemaining) {
          // Use server-calculated remaining time
          const remainingSec = (data.motion.cyclePause.remainingMs / 1000).toFixed(1);
          
          pauseStatus.style.display = 'block';
          pauseRemaining.textContent = remainingSec + 's';
        } else if (pauseStatus) {
          pauseStatus.style.display = 'none';
        }
        
        // Sync UI to backend state (only if section is expanded)
        const section = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
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
      
      // ===== UPDATE CYCLE PAUSE DISPLAY (MODE OSCILLATION) =====
      if (data.oscillation && data.oscillation.cyclePause) {
        const pauseStatusOsc = document.getElementById('cyclePauseStatusOsc');
        const pauseRemainingOsc = document.getElementById('cyclePauseRemainingOsc');
        
        if (data.oscillation.cyclePause.isPausing && pauseStatusOsc && pauseRemainingOsc) {
          // Use server-calculated remaining time
          const remainingSec = (data.oscillation.cyclePause.remainingMs / 1000).toFixed(1);
          
          pauseStatusOsc.style.display = 'block';
          pauseRemainingOsc.textContent = remainingSec + 's';
        } else if (pauseStatusOsc) {
          pauseStatusOsc.style.display = 'none';
        }
        
        // Sync UI to backend state (only if section is expanded)
        const sectionOsc = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
        const headerTextOsc = document.getElementById('cyclePauseOscHeaderText');
        if (sectionOsc && headerTextOsc) {
          const isEnabled = data.oscillation.cyclePause.enabled;
          const isCollapsed = sectionOsc.classList.contains('collapsed');
          
          // Sync collapsed state with backend enabled state
          if (isEnabled && isCollapsed) {
            sectionOsc.classList.remove('collapsed');
            headerTextOsc.textContent = '⏸️ Pause entre cycles - activée';
          } else if (!isEnabled && !isCollapsed) {
            sectionOsc.classList.add('collapsed');
            headerTextOsc.textContent = '⏸️ Pause entre cycles - désactivée';
          }
          
          // Sync radio buttons
          if (data.oscillation.cyclePause.isRandom) {
            document.getElementById('pauseModeRandomOsc').checked = true;
            document.getElementById('pauseFixedControlsOsc').style.display = 'none';
            document.getElementById('pauseRandomControlsOsc').style.display = 'block';
          } else {
            document.getElementById('pauseModeFixedOsc').checked = true;
            document.getElementById('pauseFixedControlsOsc').style.display = 'flex';
            document.getElementById('pauseRandomControlsOsc').style.display = 'none';
          }
          
          // Sync input values (avoid overwriting if user is editing)
          if (document.activeElement !== document.getElementById('cyclePauseDurationOsc')) {
            document.getElementById('cyclePauseDurationOsc').value = data.oscillation.cyclePause.pauseDurationSec.toFixed(1);
          }
          if (document.activeElement !== document.getElementById('cyclePauseMinOsc')) {
            document.getElementById('cyclePauseMinOsc').value = data.oscillation.cyclePause.minPauseSec.toFixed(1);
          }
          if (document.activeElement !== document.getElementById('cyclePauseMaxOsc')) {
            document.getElementById('cyclePauseMaxOsc').value = data.oscillation.cyclePause.maxPauseSec.toFixed(1);
          }
        }
      }
      
      // Update chaos UI
      updateChaosUI(data);
      
      // Update Pause/Resume buttons for Simple, Oscillation, and Chaos modes
      const isPaused = (data.state === SystemState.PAUSED);
      const isRunningOrPaused = (isRunning || isPaused);
      const isError = (data.state === SystemState.ERROR);
      
      // Simple mode Pause button
      const btnPause = document.getElementById('btnPause');
      if (btnPause) {
        btnPause.disabled = !isRunningOrPaused;
        if (isPaused) {
          btnPause.innerHTML = '▶ Reprendre';
        } else {
          btnPause.innerHTML = '⏸ Pause';
        }
      }
      
      // Simple mode Stop button - ALSO enabled in ERROR state for recovery
      const btnStop = document.getElementById('btnStop');
      if (btnStop) {
        btnStop.disabled = !(isRunningOrPaused || isError);
      }
      
      // Oscillation mode Pause button
      const btnPauseOsc = document.getElementById('btnPauseOscillation');
      if (btnPauseOsc) {
        btnPauseOsc.disabled = !isRunningOrPaused;
        if (isPaused) {
          btnPauseOsc.innerHTML = '▶ Reprendre';
        } else {
          btnPauseOsc.innerHTML = '⏸ Pause';
        }
      }
      
      // Oscillation mode Stop button - ALSO enabled in ERROR state for recovery
      const btnStopOsc = document.getElementById('btnStopOscillation');
      if (btnStopOsc) {
        btnStopOsc.disabled = !(isRunningOrPaused || isError);
      }
      
      // Chaos mode Pause button
      const btnPauseChaos = document.getElementById('btnPauseChaos');
      if (btnPauseChaos) {
        btnPauseChaos.disabled = !isRunningOrPaused;
        if (isPaused) {
          btnPauseChaos.innerHTML = '▶ Reprendre';
        } else {
          btnPauseChaos.innerHTML = '⏸ Pause';
        }
      }
      
      // Chaos mode Stop button - ALSO enabled in ERROR state for recovery
      const btnStopChaos = document.getElementById('btnStopChaos');
      if (btnStopChaos) {
        btnStopChaos.disabled = !(isRunningOrPaused || isError);
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
    // PLAYLIST MANAGEMENT FUNCTIONS
    // ============================================================================
    
    function generatePresetName(mode, config) {
      // Delegate to pure function if available (from presets.js)
      if (typeof generatePresetNamePure === 'function') {
        return generatePresetNamePure(mode, config);
      }
      
      // Fallback
      if (mode === 'simple') {
        return `${config.startPositionMM}→${config.startPositionMM + config.distanceMM}mm v:${config.speedLevelForward}/${config.speedLevelBackward}`;
      } else if (mode === 'oscillation') {
        const waveNames = ['Sine', 'Triangle', 'Square'];
        return `${waveNames[config.waveform] || 'Sine'} ${config.frequencyHz}Hz ±${config.amplitudeMM}mm`;
      } else if (mode === 'chaos') {
        return `Chaos ${config.durationSeconds}s (${config.crazinessPercent}%)`;
      }
      return 'Preset';
    }
    
    function generatePresetTooltip(mode, config) {
      // Delegate to pure function if available (from presets.js)
      if (typeof generatePresetTooltipPure === 'function') {
        return generatePresetTooltipPure(mode, config);
      }
      
      // Fallback - simplified version
      if (mode === 'simple') {
        return `📍 Départ: ${config.startPositionMM || 0}mm\n📏 Distance: ${config.distanceMM || 50}mm`;
      } else if (mode === 'oscillation') {
        return `📍 Centre: ${config.centerPositionMM || 100}mm\n↔️ Amplitude: ±${config.amplitudeMM || 20}mm`;
      } else if (mode === 'chaos') {
        return `📍 Centre: ${config.centerPositionMM}mm\n🎲 Folie: ${config.crazinessPercent}%`;
      }
      return 'Preset';
    }
    
    /**
     * Generate tooltip content for sequence line - delegates to pure function
     */
    function generateSequenceLineTooltip(line) {
      // Delegate to pure function if available (from sequencer.js)
      if (typeof generateSequenceLineTooltipPure === 'function') {
        return generateSequenceLineTooltipPure(line);
      }
      
      // Fallback if modules not loaded
      console.warn('generateSequenceLineTooltipPure not available');
      return 'Ligne de séquence';
    }
    
    function getCurrentModeConfig(mode) {
      if (mode === 'simple') {
        // Check if cycle pause section is expanded (enabled)
        const cyclePauseSection = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
        const cyclePauseEnabled = cyclePauseSection && !cyclePauseSection.classList.contains('collapsed');
        
        // 🆕 Check if deceleration section is expanded (enabled)
        const decelSection = document.getElementById('decelSection');
        const decelSectionEnabled = decelSection && !decelSection.classList.contains('collapsed');
        
        // Determine if random mode is selected
        const isRandom = document.getElementById('pauseModeRandom')?.checked || false;
        
        return {
          startPositionMM: parseFloat(document.getElementById('startPosition').value) || 0,
          distanceMM: parseFloat(document.getElementById('distance').value) || 50,
          speedLevelForward: parseFloat(document.getElementById('speedForward')?.value || document.getElementById('speedUnified').value) || 5,
          speedLevelBackward: parseFloat(document.getElementById('speedBackward')?.value || document.getElementById('speedUnified').value) || 5,
          // 🆕 Deceleration parameters - ONLY if section is expanded
          decelStartEnabled: decelSectionEnabled ? (document.getElementById('decelZoneStart')?.checked || false) : false,
          decelEndEnabled: decelSectionEnabled ? (document.getElementById('decelZoneEnd')?.checked || false) : false,
          decelZoneMM: parseFloat(document.getElementById('decelZoneMM')?.value) || 20,
          decelEffectPercent: parseFloat(document.getElementById('decelEffectPercent')?.value) || 50,
          decelMode: parseInt(document.getElementById('decelModeSelect')?.value) || 1,
          // Cycle pause parameters
          cyclePauseEnabled: cyclePauseEnabled,
          cyclePauseIsRandom: isRandom,
          cyclePauseDurationSec: parseFloat(document.getElementById('cyclePauseDuration')?.value) || 0.0,
          cyclePauseMinSec: parseFloat(document.getElementById('cyclePauseMin')?.value) || 0.5,
          cyclePauseMaxSec: parseFloat(document.getElementById('cyclePauseMax')?.value) || 3.0
        };
      } else if (mode === 'oscillation') {
        // Check if cycle pause section is expanded (enabled)
        const cyclePauseOscSection = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
        const cyclePauseEnabled = cyclePauseOscSection && !cyclePauseOscSection.classList.contains('collapsed');
        
        // Determine if random mode is selected
        const isRandom = document.getElementById('pauseModeRandomOsc')?.checked || false;
        
        return {
          centerPositionMM: parseFloat(document.getElementById('oscCenter').value) || 100,
          amplitudeMM: parseFloat(document.getElementById('oscAmplitude').value) || 20,
          waveform: parseInt(document.getElementById('oscWaveform').value) || 0,
          frequencyHz: parseFloat(document.getElementById('oscFrequency').value) || 1.0,
          cycleCount: parseInt(document.getElementById('oscCycleCount').value) || 10,
          enableRampIn: document.getElementById('oscRampInEnable').checked,
          rampInDurationMs: parseInt(document.getElementById('oscRampInDuration').value) || 2000,
          enableRampOut: document.getElementById('oscRampOutEnable').checked,
          rampOutDurationMs: parseInt(document.getElementById('oscRampOutDuration').value) || 2000,
          returnToCenter: document.getElementById('oscReturnCenter').checked,
          // Cycle pause parameters
          cyclePauseEnabled: cyclePauseEnabled,
          cyclePauseIsRandom: isRandom,
          cyclePauseDurationSec: parseFloat(document.getElementById('cyclePauseDurationOsc')?.value) || 0.0,
          cyclePauseMinSec: parseFloat(document.getElementById('cyclePauseMinOsc')?.value) || 0.5,
          cyclePauseMaxSec: parseFloat(document.getElementById('cyclePauseMaxOsc')?.value) || 3.0
        };
      } else if (mode === 'chaos') {
        const patterns = [
          document.getElementById('patternZigzag').checked,
          document.getElementById('patternSweep').checked,
          document.getElementById('patternPulse').checked,
          document.getElementById('patternDrift').checked,
          document.getElementById('patternBurst').checked,
          document.getElementById('patternWave').checked,
          document.getElementById('patternPendulum').checked,
          document.getElementById('patternSpiral').checked,
          document.getElementById('patternCalm').checked,
          document.getElementById('patternBruteForce').checked,
          document.getElementById('patternLiberator').checked
        ];
        return {
          centerPositionMM: parseFloat(document.getElementById('chaosCenterPos').value) || 100,
          amplitudeMM: parseFloat(document.getElementById('chaosAmplitude').value) || 40,
          maxSpeedLevel: parseFloat(document.getElementById('chaosMaxSpeed').value) || 15,
          crazinessPercent: parseInt(document.getElementById('chaosCraziness').value) || 50,
          durationSeconds: parseInt(document.getElementById('chaosDuration').value) || 30,
          patternsEnabled: patterns
        };
      }
      return {};
    }
    
    function openPlaylistModal(mode) {
      const modal = document.getElementById('playlistModal');
      const titleEl = document.getElementById('playlistModalTitle');
      const configEl = document.getElementById('playlistCurrentConfigContent');
      
      modal.dataset.mode = mode;
      
      // Set title - delegate to pure function if available
      if (typeof getPlaylistModalTitlePure === 'function') {
        titleEl.textContent = getPlaylistModalTitlePure(mode);
      } else {
        // Fallback
        const titles = { 'simple': 'Mode Simple', 'oscillation': 'Mode Oscillation', 'chaos': 'Mode Chaos' };
        titleEl.textContent = titles[mode] || mode;
      }
      
      // Display current config - delegate to pure function if available
      const config = getCurrentModeConfig(mode);
      if (typeof generateConfigPreviewHTMLPure === 'function') {
        configEl.innerHTML = generateConfigPreviewHTMLPure(mode, config);
      } else {
        // Fallback - simplified
        configEl.innerHTML = `Mode: ${mode}`;
      }
      
      // Refresh presets list
      refreshPlaylistPresets(mode);
      
      modal.classList.add('active');
    }
    
    function closePlaylistModal() {
      document.getElementById('playlistModal').classList.remove('active');
    }
    
    function closePlaylistModalOnOverlayClick(event) {
      // Only close if clicking on the overlay itself (not the content)
      if (event.target.id === 'playlistModal') {
        closePlaylistModal();
      }
    }
    
    function refreshPlaylistPresets(mode) {
      const listEl = document.getElementById('playlistPresetsList');
      const countEl = document.getElementById('playlistCount');
      const presets = PlaylistState[mode] || [];
      
      console.log('🔄 refreshPlaylistPresets called for mode:', mode, 'presets count:', presets.length);
      
      countEl.textContent = presets.length;
      
      if (presets.length === 0) {
        console.log('⚠️ No presets found, displaying empty message');
        listEl.innerHTML = '<div style="color: #999; font-style: italic; padding: 20px; text-align: center;">Aucun preset sauvegardé</div>';
        return;
      }
      
      // Sort by timestamp desc (most recent first)
      const sortedPresets = [...presets].sort((a, b) => b.timestamp - a.timestamp);
      
      console.log('✅ Building HTML for', sortedPresets.length, 'presets');
      
      let html = '';
      sortedPresets.forEach(preset => {
        const tooltipContent = generatePresetTooltip(mode, preset.config);
        html += `
          <div class="preset-item" 
               data-tooltip="${tooltipContent.replace(/"/g, '&quot;')}"
               style="background: #f9f9f9; padding: 8px 10px; border-radius: 4px; margin-bottom: 6px; border: 1px solid #ddd;">
            <div style="display: flex; justify-content: space-between; align-items: center; gap: 6px;">
              <div style="flex: 1; min-width: 0;">
                <div style="font-weight: 500; font-size: 12px; margin-bottom: 2px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">${preset.name}</div>
                <div style="font-size: 10px; color: #888;">${new Date(preset.timestamp * 1000).toLocaleString('fr-FR', {dateStyle: 'short', timeStyle: 'short'})}</div>
              </div>
              <div style="display: flex; gap: 2px; flex-shrink: 0;">
                <button onclick="loadPresetInMode('${mode}', ${preset.id})" style="padding: 4px 6px; font-size: 11px; min-width: unset;" title="Charger dans le mode actuel">
                  ⬇️
                </button>
                <button onclick="quickAddToSequencer('${mode}', ${preset.id})" style="padding: 4px 6px; font-size: 11px; min-width: unset;" title="Ajouter direct au séquenceur">
                  ➕📋
                </button>
                <button onclick="renamePlaylistPreset('${mode}', ${preset.id})" style="padding: 4px 6px; font-size: 11px; min-width: unset;" title="Renommer">
                  ✏️
                </button>
                <button onclick="deleteFromPlaylist('${mode}', ${preset.id})" style="padding: 4px 6px; font-size: 11px; min-width: unset;" title="Supprimer">
                  🗑️
                </button>
                <button class="preset-tooltip-eye" data-preset-id="${preset.id}"
                  style="display: inline-block; padding: 4px 6px; cursor: pointer; font-size: 14px;"
                  title="Voir détails">👁️</button>
              </div>
            </div>
          </div>
        `;
      });
      
      console.log('✅ Setting HTML, length:', html.length);
      listEl.innerHTML = html;
      
      // Attach eye icon tooltip handlers after DOM insertion
      setTimeout(() => {
        document.querySelectorAll('.preset-tooltip-eye').forEach(eyeIcon => {
          eyeIcon.onmouseenter = function(e) {
            const presetItem = this.closest('.preset-item');
            if (presetItem) {
              showPlaylistTooltip(presetItem);
            }
          };
          eyeIcon.onmouseleave = function() {
            hidePlaylistTooltip();
          };
        });
      }, 0);
    }
    
    function showPlaylistTooltip(element) {
      const tooltipContent = element.getAttribute('data-tooltip');
      const overlay = document.getElementById('playlistTooltipOverlay');
      if (overlay && tooltipContent) {
        overlay.innerHTML = tooltipContent;
        overlay.classList.add('visible');
      }
    }
    
    function hidePlaylistTooltip() {
      const overlay = document.getElementById('playlistTooltipOverlay');
      if (overlay) {
        overlay.classList.remove('visible');
      }
    }
    
    /**
     * Show sequence line tooltip
     */
    function showSequenceTooltip(element) {
      const tooltipContent = element.getAttribute('data-tooltip');
      const lineNumber = element.getAttribute('data-line-number');
      const lineType = element.getAttribute('data-line-type');
      
      const overlay = document.getElementById('playlistTooltipOverlay');
      if (overlay && tooltipContent) {
        const header = `<div style="font-weight: 600; margin-bottom: 8px; font-size: 14px; border-bottom: 2px solid rgba(255,255,255,0.3); padding-bottom: 6px;">#${lineNumber} - ${lineType}</div>`;
        overlay.innerHTML = header + tooltipContent;
        overlay.classList.add('visible');
      }
    }
    
    function filterPlaylistPresets(searchTerm) {
      const items = document.querySelectorAll('.preset-item');
      const term = (searchTerm || '').toLowerCase().trim();
      let visibleCount = 0;
      
      items.forEach(item => {
        // Search in preset name
        const nameEl = item.querySelector('div > div:first-child');
        const name = nameEl ? nameEl.textContent.toLowerCase() : '';
        const visible = term === '' || name.includes(term);
        
        item.style.display = visible ? 'block' : 'none';
        if (visible) visibleCount++;
      });
      
      // Update count display
      const countEl = document.getElementById('playlistCount');
      if (countEl) {
        countEl.textContent = visibleCount;
      }
      
      console.log('🔍 Search:', term, '→', visibleCount, 'results');
    }
    
    function addToPlaylist(mode) {
      const config = getCurrentModeConfig(mode);
      
      // Validation: refuse infinite durations
      if (mode === 'oscillation' && config.cycleCount === 0) {
        showNotification('❌ Impossible d\'ajouter: cycles infinis non supportés dans la playlist', 'error', 5000);
        return;
      }
      if (mode === 'chaos' && config.durationSeconds === 0) {
        showNotification('❌ Impossible d\'ajouter: durée infinie non supportée dans la playlist', 'error', 5000);
        return;
      }
      
      // Check limit
      if (PlaylistState[mode].length >= 20) {
        showNotification('❌ Limite atteinte: maximum 20 presets par mode', 'error', 4000);
        return;
      }
      
      // Generate default name
      const defaultName = generatePresetName(mode, config);
      const name = prompt('Nom du preset:', defaultName);
      if (!name) return;
      
      // Send to backend
      fetch('/api/playlists/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          mode: mode,
          name: name,
          config: config
        })
      })
      .then(r => r.json())
      .then(data => {
        if (data.success) {
          showNotification('✅ Preset ajouté à la playlist', 'success', 3000);
          console.log('✅ Preset added, reloading playlists...');
          // Reload playlists, then refresh modal display
          loadPlaylists(() => {
            console.log('✅ Playlists reloaded, refreshing modal for mode:', mode);
            refreshPlaylistPresets(mode);
          });
        } else {
          showNotification('❌ Erreur: ' + (data.error || 'Unknown'), 'error');
        }
      })
      .catch(error => {
        showNotification('❌ Erreur réseau: ' + error, 'error');
      });
    }
    
    function deleteFromPlaylist(mode, id) {
      if (!confirm('Supprimer ce preset de la playlist?')) return;
      
      fetch('/api/playlists/delete', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          mode: mode,
          id: id
        })
      })
      .then(r => r.json())
      .then(data => {
        if (data.success) {
          showNotification('✅ Preset supprimé', 'success', 2000);
          // Reload playlists, then refresh modal display
          loadPlaylists(() => refreshPlaylistPresets(mode));
        } else {
          showNotification('❌ Erreur: ' + (data.error || 'Unknown'), 'error');
        }
      })
      .catch(error => {
        showNotification('❌ Erreur réseau: ' + error, 'error');
      });
    }
    
    function renamePlaylistPreset(mode, id) {
      const preset = PlaylistState[mode].find(p => p.id === id);
      if (!preset) return;
      
      const newName = prompt('Nouveau nom:', preset.name);
      if (!newName || newName === preset.name) return;
      
      fetch('/api/playlists/update', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          mode: mode,
          id: id,
          name: newName
        })
      })
      .then(r => r.json())
      .then(data => {
        if (data.success) {
          showNotification('✅ Preset renommé', 'success', 2000);
          // Reload playlists, then refresh modal display
          loadPlaylists(() => refreshPlaylistPresets(mode));
        } else {
          showNotification('❌ Erreur: ' + (data.error || 'Unknown'), 'error');
        }
      })
      .catch(error => {
        showNotification('❌ Erreur réseau: ' + error, 'error');
      });
    }
    
    function loadPresetInMode(mode, id) {
      const preset = PlaylistState[mode].find(p => p.id === id);
      if (!preset) {
        console.error('❌ Preset not found:', mode, id);
        return;
      }
      
      console.log('📥 Loading preset:', preset.name, '| Config:', preset.config);
      
      const config = preset.config;
      
      if (mode === 'simple') {
        // Load basic parameters
        document.getElementById('startPosition').value = config.startPositionMM || 0;
        document.getElementById('distance').value = config.distanceMM || 50;
        
        // Check if unified or separate speed mode
        const isSeparate = document.getElementById('speedModeSeparate').checked;
        if (isSeparate) {
          document.getElementById('speedForward').value = config.speedLevelForward || 5;
          document.getElementById('speedBackward').value = config.speedLevelBackward || 5;
        } else {
          document.getElementById('speedUnified').value = config.speedLevelForward || 5;
        }
        
        // 🆕 Load DECELERATION parameters (if present)
        if (config.decelStartEnabled !== undefined) {
          const decelStartEl = document.getElementById('decelZoneStart');
          if (decelStartEl) decelStartEl.checked = config.decelStartEnabled;
        }
        if (config.decelEndEnabled !== undefined) {
          const decelEndEl = document.getElementById('decelZoneEnd');
          if (decelEndEl) decelEndEl.checked = config.decelEndEnabled;
        }
        if (config.decelZoneMM !== undefined) {
          const decelZoneEl = document.getElementById('decelZoneMM');
          if (decelZoneEl) decelZoneEl.value = config.decelZoneMM;
        }
        if (config.decelEffectPercent !== undefined) {
          const decelEffectEl = document.getElementById('decelEffectPercent');
          const decelValueEl = document.getElementById('effectValue');
          if (decelEffectEl) decelEffectEl.value = config.decelEffectPercent;
          if (decelValueEl) decelValueEl.textContent = config.decelEffectPercent + '%';
        }
        if (config.decelMode !== undefined) {
          const decelModeEl = document.getElementById('decelModeSelect');
          if (decelModeEl) decelModeEl.value = config.decelMode;
        }
        
        // Auto-expand deceleration section if enabled
        if (config.decelStartEnabled || config.decelEndEnabled) {
          const decelSection = document.getElementById('decelSection');
          if (decelSection?.classList.contains('collapsed')) {
            decelSection.classList.remove('collapsed');
            const chevron = decelSection.querySelector('.section-chevron');
            if (chevron) chevron.textContent = '▼';
          }
        }
        
        // Load cycle pause parameters
        const pauseEnabled = config.cyclePauseEnabled || false;
        const pauseIsRandom = config.cyclePauseIsRandom || false;
        
        // Set radio buttons for pause mode
        const pauseModeFixedEl = document.getElementById('pauseModeFixed');
        const pauseModeRandomEl = document.getElementById('pauseModeRandom');
        if (pauseModeFixedEl && pauseModeRandomEl) {
          if (pauseIsRandom) {
            pauseModeRandomEl.checked = true;
            pauseModeFixedEl.checked = false;
          } else {
            pauseModeFixedEl.checked = true;
            pauseModeRandomEl.checked = false;
          }
        }
        
        // Set pause duration values
        const cyclePauseDurationEl = document.getElementById('cyclePauseDuration');
        const cyclePauseMinEl = document.getElementById('cyclePauseMin');
        const cyclePauseMaxEl = document.getElementById('cyclePauseMax');
        
        if (cyclePauseDurationEl) {
          cyclePauseDurationEl.value = config.cyclePauseDurationSec || 0.0;
        }
        if (cyclePauseMinEl) {
          cyclePauseMinEl.value = config.cyclePauseMinSec || 0.5;
        }
        if (cyclePauseMaxEl) {
          cyclePauseMaxEl.value = config.cyclePauseMaxSec || 3.0;
        }
        
        // Force toggle visibility (fixed/random)
        const fixedDiv = document.getElementById('pauseFixedControls');
        const randomDiv = document.getElementById('pauseRandomControls');
        if (fixedDiv && randomDiv) {
          if (pauseIsRandom) {
            fixedDiv.style.display = 'none';
            randomDiv.style.display = 'block';
          } else {
            fixedDiv.style.display = 'flex';
            randomDiv.style.display = 'none';
          }
        }
        
        // Auto-expand pause section if enabled
        if (pauseEnabled) {
          const pauseSection = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
          const pauseHeaderText = document.getElementById('cyclePauseHeaderText');
          if (pauseSection && pauseHeaderText) {
            if (pauseSection.classList.contains('collapsed')) {
              pauseSection.classList.remove('collapsed');
              const chevron = pauseSection.querySelector('.collapse-icon');
              if (chevron) chevron.textContent = '▼';
            }
            pauseHeaderText.textContent = '⏸️ Pause entre cycles - activée';
          }
        }
        
        // Send commands to backend
        sendCommand(WS_CMD.SET_START_POSITION, {startPosition: config.startPositionMM || 0});
        sendCommand(WS_CMD.SET_DISTANCE, {distance: config.distanceMM || 50});
        sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: config.speedLevelForward || 5});
        sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: config.speedLevelBackward || 5});
        
        // Send deceleration config to backend
        const decelCmd = {
          enabled: config.decelStartEnabled || config.decelEndEnabled,
          enableStart: config.decelStartEnabled || false,
          enableEnd: config.decelEndEnabled || false,
          zoneMM: config.decelZoneMM || 20,
          effectPercent: config.decelEffectPercent || 50,
          mode: config.decelMode || 1
        };
        console.log('🔧 Sending setDecelZone:', decelCmd);
        sendCommand(WS_CMD.SET_DECEL_ZONE, decelCmd);
        
        // Send cycle pause config to backend
        const pauseCmd = {
          mode: 'simple',
          enabled: pauseEnabled,
          isRandom: pauseIsRandom,
          durationSec: config.cyclePauseDurationSec || 0.0,
          minSec: config.cyclePauseMinSec || 0.5,
          maxSec: config.cyclePauseMaxSec || 3.0
        };
        console.log('🔧 Sending setCyclePause:', pauseCmd);
        sendCommand(WS_CMD.SET_CYCLE_PAUSE, pauseCmd);
        
        console.log('✅ Simple preset loaded | Pause enabled:', pauseEnabled, '| Random:', pauseIsRandom);
        
      } else if (mode === 'oscillation') {
        document.getElementById('oscCenter').value = config.centerPositionMM || 100;
        document.getElementById('oscAmplitude').value = config.amplitudeMM || 20;
        document.getElementById('oscWaveform').value = config.waveform || 0;
        document.getElementById('oscFrequency').value = config.frequencyHz || 1.0;
        document.getElementById('oscCycleCount').value = config.cycleCount || 10;
        document.getElementById('oscRampInEnable').checked = config.enableRampIn || false;
        document.getElementById('oscRampInDuration').value = config.rampInDurationMs || 2000;
        document.getElementById('oscRampOutEnable').checked = config.enableRampOut || false;
        document.getElementById('oscRampOutDuration').value = config.rampOutDurationMs || 2000;
        document.getElementById('oscReturnCenter').checked = config.returnToCenter || false;
        
        // Load cycle pause parameters
        const pauseEnabled = config.cyclePauseEnabled || false;
        const pauseIsRandom = config.cyclePauseIsRandom || false;
        
        // Set radio buttons for pause mode
        const pauseModeFixedOscEl = document.getElementById('pauseModeFixedOsc');
        const pauseModeRandomOscEl = document.getElementById('pauseModeRandomOsc');
        if (pauseModeFixedOscEl && pauseModeRandomOscEl) {
          if (pauseIsRandom) {
            pauseModeRandomOscEl.checked = true;
            pauseModeFixedOscEl.checked = false;
          } else {
            pauseModeFixedOscEl.checked = true;
            pauseModeRandomOscEl.checked = false;
          }
        }
        
        // Set pause duration values
        const cyclePauseDurationOscEl = document.getElementById('cyclePauseDurationOsc');
        const cyclePauseMinOscEl = document.getElementById('cyclePauseMinOsc');
        const cyclePauseMaxOscEl = document.getElementById('cyclePauseMaxOsc');
        
        if (cyclePauseDurationOscEl) {
          cyclePauseDurationOscEl.value = config.cyclePauseDurationSec || 0.0;
        }
        if (cyclePauseMinOscEl) {
          cyclePauseMinOscEl.value = config.cyclePauseMinSec || 0.5;
        }
        if (cyclePauseMaxOscEl) {
          cyclePauseMaxOscEl.value = config.cyclePauseMaxSec || 3.0;
        }
        
        // Force toggle visibility (fixed/random)
        const fixedDiv = document.getElementById('pauseFixedControlsOsc');
        const randomDiv = document.getElementById('pauseRandomControlsOsc');
        if (fixedDiv && randomDiv) {
          if (pauseIsRandom) {
            fixedDiv.style.display = 'none';
            randomDiv.style.display = 'block';
          } else {
            fixedDiv.style.display = 'flex';
            randomDiv.style.display = 'none';
          }
        }
        
        // Auto-expand pause section if enabled
        if (pauseEnabled) {
          const pauseSection = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
          const pauseHeaderText = document.getElementById('cyclePauseOscHeaderText');
          if (pauseSection && pauseHeaderText) {
            if (pauseSection.classList.contains('collapsed')) {
              pauseSection.classList.remove('collapsed');
              const chevron = pauseSection.querySelector('.collapse-icon');
              if (chevron) chevron.textContent = '▼';
            }
            pauseHeaderText.textContent = '⏸️ Pause entre cycles - activée';
          }
        }
        
        // Send command to backend
        sendCommand(WS_CMD.SET_OSCILLATION, {
          centerPositionMM: config.centerPositionMM || 100,
          amplitudeMM: config.amplitudeMM || 20,
          waveform: config.waveform || 0,
          frequencyHz: config.frequencyHz || 1.0,
          cycleCount: config.cycleCount || 10,
          enableRampIn: config.enableRampIn || false,
          rampInDurationMs: config.rampInDurationMs || 2000,
          enableRampOut: config.enableRampOut || false,
          rampOutDurationMs: config.rampOutDurationMs || 2000,
          returnToCenter: config.returnToCenter || false,
          // Cycle pause parameters
          cyclePauseEnabled: pauseEnabled,
          cyclePauseIsRandom: pauseIsRandom,
          cyclePauseDurationSec: config.cyclePauseDurationSec || 0.0,
          cyclePauseMinSec: config.cyclePauseMinSec || 0.5,
          cyclePauseMaxSec: config.cyclePauseMaxSec || 3.0
        });
        
        console.log('✅ OSC preset loaded | Pause enabled:', pauseEnabled, '| Random:', pauseIsRandom);
        
      } else if (mode === 'chaos') {
        document.getElementById('chaosCenterPos').value = config.centerPositionMM || 100;
        document.getElementById('chaosAmplitude').value = config.amplitudeMM || 40;
        document.getElementById('chaosMaxSpeed').value = config.maxSpeedLevel || 15;
        document.getElementById('chaosCraziness').value = config.crazinessPercent || 50;
        document.getElementById('chaosDuration').value = config.durationSeconds || 30;
        document.getElementById('crazinessValue').textContent = config.crazinessPercent || 50;

        // Set pattern checkboxes (correct IDs)
        if (config.patternsEnabled && Array.isArray(config.patternsEnabled) && config.patternsEnabled.length >= 11) {
          document.getElementById('patternZigzag').checked = config.patternsEnabled[0];
          document.getElementById('patternSweep').checked = config.patternsEnabled[1];
          document.getElementById('patternPulse').checked = config.patternsEnabled[2];
          document.getElementById('patternDrift').checked = config.patternsEnabled[3];
          document.getElementById('patternBurst').checked = config.patternsEnabled[4];
          document.getElementById('patternWave').checked = config.patternsEnabled[5];
          document.getElementById('patternPendulum').checked = config.patternsEnabled[6];
          document.getElementById('patternSpiral').checked = config.patternsEnabled[7];
          document.getElementById('patternCalm').checked = config.patternsEnabled[8];
          document.getElementById('patternBruteForce').checked = config.patternsEnabled[9];
          document.getElementById('patternLiberator').checked = config.patternsEnabled[10];
        }
        
        // Send command to backend
        sendCommand(WS_CMD.SET_CHAOS_CONFIG, {
          centerPositionMM: config.centerPositionMM || 100,
          amplitudeMM: config.amplitudeMM || 40,
          maxSpeedLevel: config.maxSpeedLevel || 15,
          crazinessPercent: config.crazinessPercent || 50,
          durationSeconds: config.durationSeconds || 30,
          seed: 0,  // Use default seed
          patternsEnabled: config.patternsEnabled || []
        });
      }
      
      closePlaylistModal();
      showNotification('✅ Preset chargé dans le mode ' + mode, 'info', 2000);
    }
    
    function updateStartPresets(maxDist) {
      // Use cached NodeList for performance (called ~50 times/second via updateUI)
      DOM.presetStartButtons.forEach(btn => {
        const startPos = parseFloat(btn.getAttribute('data-start'));
        const isValid = startPos <= maxDist;
        btn.disabled = !isValid;
        btn.style.opacity = isValid ? '1' : '0.3';
        btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      });
    }
    
    function updateDistancePresets(maxAvailable) {
      // Use cached NodeList for performance (called ~50 times/second via updateUI)
      DOM.presetDistanceButtons.forEach(btn => {
        const distance = parseFloat(btn.getAttribute('data-distance'));
        const isValid = distance <= maxAvailable;
        btn.disabled = !isValid;
        btn.style.opacity = isValid ? '1' : '0.3';
        btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      });
    }
    
    // Note: canStartOperation, setButtonState, setupEditableInput, setupPresetButtons, sendCommand loaded from utils.js
    
    document.getElementById('btnCalibrateCommon').addEventListener('click', function() {
      sendCommand(WS_CMD.CALIBRATE);
    });
    
    // ============================================================================
    // MAX DISTANCE LIMIT CONFIGURATION
    // ============================================================================
    
    // Max Distance Limit: Helper function to update UI state
    function updateMaxDistLimitUI() {
      const isReady = AppState.system.currentState === SystemState.READY;
      const totalMM = AppState.pursuit.totalDistanceMM || 0;
      
      // Get current limit percent from AppState (or default to 100)
      const currentPercent = AppState.pursuit.maxDistLimitPercent || 100;
      
      // Only update slider value if user is NOT currently editing it
      if (!isEditingMaxDistLimit) {
        DOM.maxDistLimitSlider.value = currentPercent;
      }
      
      // Enable/disable controls based on state
      DOM.maxDistLimitSlider.disabled = !isReady;
      DOM.btnApplyMaxDistLimit.disabled = !isReady;
      
      // Show/hide warning
      DOM.maxDistLimitWarning.style.display = isReady ? 'none' : 'block';
      
      // Update slider value and display (only if not editing)
      if (!isEditingMaxDistLimit) {
        const effectiveMM = (totalMM * currentPercent / 100).toFixed(1);
        DOM.maxDistLimitValue.textContent = currentPercent + '%';
        DOM.maxDistLimitMM.textContent = '(' + effectiveMM + ' mm)';
      }
    }
    
    // Max Distance Limit: Initialize event listeners (called after initDOMCache)
    function initMaxDistLimitListeners() {
      // Toggle configuration panel
      DOM.btnConfigMaxDist.addEventListener('click', function() {
        const isVisible = DOM.maxDistConfigPanel.style.display !== 'none';
        DOM.maxDistConfigPanel.style.display = isVisible ? 'none' : 'block';
        
        if (!isVisible) {
          // Panel just opened - load current value and START blocking updates
          isEditingMaxDistLimit = true; // Block updates while panel is open
          updateMaxDistLimitUI();
        } else {
          // Panel closed - stop blocking updates
          isEditingMaxDistLimit = false;
        }
      });
      
      // Update slider display while dragging
      DOM.maxDistLimitSlider.addEventListener('input', function() {
        const percent = parseFloat(this.value);
        const totalMM = AppState.pursuit.totalDistanceMM || 0;
        const effectiveMM = (totalMM * percent / 100).toFixed(1);
        
        DOM.maxDistLimitValue.textContent = percent + '%';
        DOM.maxDistLimitMM.textContent = '(' + effectiveMM + ' mm)';
      });
      
      // Apply limit
      DOM.btnApplyMaxDistLimit.addEventListener('click', function() {
        const percent = parseFloat(DOM.maxDistLimitSlider.value);
        
        // Reset input fields to safe defaults when applying limit
        document.getElementById('startPosition').value = 0;
        document.getElementById('distance').value = 0;
        
        // Update oscillation and chaos centers with new effective max
        const totalMM = AppState.pursuit.totalDistanceMM || 0;
        const effectiveMax = totalMM * percent / 100;
        
        const oscCenterField = document.getElementById('oscCenter');
        if (oscCenterField && effectiveMax > 0) {
          oscCenterField.value = (effectiveMax / 2).toFixed(1);
          // Send to backend to update oscillation config
          sendCommand(WS_CMD.SET_OSCILLATION_CONFIG, {
            centerPositionMM: effectiveMax / 2,
            amplitudeMM: parseFloat(document.getElementById('oscAmplitude').value) || 50,
            frequencyHz: parseFloat(document.getElementById('oscFrequency').value) || 1,
            waveform: parseInt(document.getElementById('oscWaveform').value) || 0
          });
        }
        
        const chaosCenterField = document.getElementById('chaosCenterPos');
        if (chaosCenterField && effectiveMax > 0) {
          chaosCenterField.value = (effectiveMax / 2).toFixed(1);
        }
        
        sendCommand(WS_CMD.SET_MAX_DISTANCE_LIMIT, {percent: percent});
        DOM.maxDistConfigPanel.style.display = 'none';
        isEditingMaxDistLimit = false; // Allow updates after applying
      });
      
      // Cancel
      DOM.btnCancelMaxDistLimit.addEventListener('click', function() {
        DOM.maxDistConfigPanel.style.display = 'none';
        isEditingMaxDistLimit = false; // Allow updates after cancelling
      });
    }
    
    document.getElementById('btnStart').addEventListener('click', function() {
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
    });
    
    document.getElementById('btnPause').addEventListener('click', function() {
      sendCommand(WS_CMD.PAUSE);
    });
    
    document.getElementById('btnStop').addEventListener('click', function() {
      // Only show modal if motor has moved (currentStep > 0)
      if (currentPositionMM > 0.5) {
        showStopModal();
      } else {
        // Direct stop if at position 0
        sendCommand(WS_CMD.STOP);
      }
    });
    
    document.getElementById('btnResetDistanceCommon').addEventListener('click', function() {
      if (confirm('Réinitialiser le compteur de distance parcourue ?')) {
        sendCommand(WS_CMD.RESET_TOTAL_DISTANCE);
      }
    });
    
    // ========================================================================
    // PLAYLIST BUTTON EVENT LISTENERS
    // ========================================================================
    
    document.getElementById('btnManagePlaylistSimple').addEventListener('click', function() {
      openPlaylistModal('simple');
    });
    
    document.getElementById('btnManagePlaylistOscillation').addEventListener('click', function() {
      openPlaylistModal('oscillation');
    });
    
    document.getElementById('btnManagePlaylistChaos').addEventListener('click', function() {
      openPlaylistModal('chaos');
    });
    
    document.getElementById('btnAddCurrentToPlaylist').addEventListener('click', function() {
      const mode = document.getElementById('playlistModal').dataset.mode;
      if (mode) {
        addToPlaylist(mode);
      }
    });
    
    // Track when user starts editing an input
    // Use mousedown to catch spinner clicks BEFORE focus + force focus immediately
    document.getElementById('startPosition').addEventListener('mousedown', function() {
      AppState.editing.input = 'startPosition';
      this.focus();
    });
    document.getElementById('startPosition').addEventListener('focus', function() {
      AppState.editing.input = 'startPosition';
    });
    document.getElementById('startPosition').addEventListener('blur', function() {
      AppState.editing.input = null;
    });
    document.getElementById('startPosition').addEventListener('change', function() {
      const startPos = parseFloat(this.value);
      sendCommand(WS_CMD.SET_START_POSITION, {startPosition: startPos});
      AppState.editing.input = null;
    });
    
    document.getElementById('distance').addEventListener('mousedown', function() {
      AppState.editing.input = 'distance';
      this.focus();
    });
    document.getElementById('distance').addEventListener('focus', function() {
      AppState.editing.input = 'distance';
    });
    document.getElementById('distance').addEventListener('blur', function() {
      AppState.editing.input = null;
    });
    document.getElementById('distance').addEventListener('change', function() {
      const distance = parseFloat(this.value);
      sendCommand(WS_CMD.SET_DISTANCE, {distance: distance});
      AppState.editing.input = null;
    });
    
    // Unified speed control
    document.getElementById('speedUnified').addEventListener('mousedown', function() {
      AppState.editing.input = 'speedUnified';
      this.focus();
    });
    document.getElementById('speedUnified').addEventListener('focus', function() {
      AppState.editing.input = 'speedUnified';
    });
    document.getElementById('speedUnified').addEventListener('blur', function() {
      AppState.editing.input = null;
    });
    document.getElementById('speedUnified').addEventListener('change', function() {
      const speed = parseFloat(this.value);
      
      // Update hidden separate fields for consistency
      document.getElementById('speedForward').value = speed;
      document.getElementById('speedBackward').value = speed;
      
      // Send both commands to ESP32
      sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
      sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
      
      AppState.editing.input = null;
    });
    
    // Separate speed controls
    document.getElementById('speedForward').addEventListener('mousedown', function() {
      AppState.editing.input = 'speedForward';
      this.focus();
    });
    document.getElementById('speedForward').addEventListener('focus', function() {
      AppState.editing.input = 'speedForward';
    });
    document.getElementById('speedForward').addEventListener('blur', function() {
      AppState.editing.input = null;
    });
    document.getElementById('speedForward').addEventListener('change', function() {
      const speed = parseFloat(this.value);
      sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
      AppState.editing.input = null;
    });
    
    document.getElementById('speedBackward').addEventListener('mousedown', function() {
      AppState.editing.input = 'speedBackward';
      this.focus();
    });
    document.getElementById('speedBackward').addEventListener('focus', function() {
      AppState.editing.input = 'speedBackward';
    });
    document.getElementById('speedBackward').addEventListener('blur', function() {
      AppState.editing.input = null;
    });
    document.getElementById('speedBackward').addEventListener('change', function() {
      const speed = parseFloat(this.value);
      sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
      AppState.editing.input = null;
    });
    
    // Cycle Pause listeners (Simple mode)
    function sendSimpleCyclePauseConfig() {
      const enabled = document.getElementById('cyclePauseEnabled')?.checked || false;
      const isRandom = document.getElementById('cyclePauseRandom')?.checked || false;
      const durationSec = parseFloat(document.getElementById('cyclePauseDuration')?.value || 0);
      let minSec = parseFloat(document.getElementById('cyclePauseMin')?.value || 0.5);
      let maxSec = parseFloat(document.getElementById('cyclePauseMax')?.value || 3.0);
      
      // 🆕 VALIDATION: Min doit être ≤ Max (seulement si random activé)
      if (isRandom && minSec > maxSec) {
        showNotification('⚠️ Pause Min (' + minSec.toFixed(1) + 's) doit être ≤ Max (' + maxSec.toFixed(1) + 's)', 'warning');
        // Auto-correction: ajuster Max = Min
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
    
    // Unified speed presets
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
    
    // Separate speed presets
    document.querySelectorAll('[data-speed-forward]').forEach(btn => {
      btn.addEventListener('click', function() {
        const speed = parseFloat(this.getAttribute('data-speed-forward'));
        document.getElementById('speedForward').value = speed;
        sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: speed});
        
        document.querySelectorAll('[data-speed-forward]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
      });
    });
    
    document.querySelectorAll('[data-speed-backward]').forEach(btn => {
      btn.addEventListener('click', function() {
        const speed = parseFloat(this.getAttribute('data-speed-backward'));
        document.getElementById('speedBackward').value = speed;
        sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: speed});
        
        document.querySelectorAll('[data-speed-backward]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
      });
    });
    
    // Toggle between unified and separate speed controls (RADIO BUTTONS)
    document.querySelectorAll('input[name="speedMode"]').forEach(radio => {
      radio.addEventListener('change', function() {
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
      });
    });

    // ============================================================================
    // PURSUIT MODE - Interactive Gauge Control
    // ============================================================================
    
    let pursuitActive = false;
    // Pursuit mode constants and local state
    // Note: pursuitTargetMM migrated to AppState.pursuit.targetMM
    let pursuitMaxSpeedLevel = 10;
    const PURSUIT_COMMAND_INTERVAL = 20;  // Send command max every 20ms (50Hz)
    
    // Flag to prevent WebSocket updates while user is editing max distance limit
    let isEditingMaxDistLimit = false;
    
    function updateGaugePosition(positionMM) {
      if (AppState.pursuit.totalDistanceMM <= 0) return;
      
      const containerHeight = DOM.gaugeContainer.offsetHeight;
      
      // Calculate position (0mm = bottom, totalDistanceMM = top)
      const percent = positionMM / AppState.pursuit.totalDistanceMM;
      const pixelPosition = containerHeight - (percent * containerHeight);
      
      DOM.gaugePosition.style.top = pixelPosition + 'px';
      DOM.currentPositionMM.textContent = positionMM.toFixed(1);
      
      // Update error
      const error = Math.abs(AppState.pursuit.targetMM - positionMM);
      DOM.positionError.textContent = error.toFixed(1);
    }
    
    function setGaugeTarget(positionMM) {
      if (AppState.pursuit.totalDistanceMM <= 0) return;
      
      // Clamp to valid range
      if (positionMM < 0) positionMM = 0;
      if (positionMM > AppState.pursuit.totalDistanceMM) positionMM = AppState.pursuit.totalDistanceMM;
      
      AppState.pursuit.targetMM = positionMM;
      
      const containerHeight = DOM.gaugeContainer.offsetHeight;
      
      // Calculate position (0mm = bottom, totalDistanceMM = top)
      const percent = positionMM / AppState.pursuit.totalDistanceMM;
      const pixelPosition = containerHeight - (percent * containerHeight);
      
      DOM.gaugeCursor.style.top = pixelPosition + 'px';
      DOM.targetPositionMM.textContent = positionMM.toFixed(1);
      
      // Update error
      const error = Math.abs(AppState.pursuit.targetMM - AppState.pursuit.currentPositionMM);
      DOM.positionError.textContent = error.toFixed(1);
      
      // Send pursuit command if active and enough time has passed
      if (pursuitActive) {
        const now = Date.now();
        if (now - AppState.pursuit.lastCommandTime > PURSUIT_COMMAND_INTERVAL) {
          sendPursuitCommand();
          AppState.pursuit.lastCommandTime = now;
        }
      }
    }
    
    function sendPursuitCommand() {
      // Always send command - the ESP32 will handle when to stop (errorSteps == 0)
      sendCommand(WS_CMD.PURSUIT_MOVE, {
        targetPosition: AppState.pursuit.targetMM,
        maxSpeed: pursuitMaxSpeedLevel
      });
    }
    
    // Gauge mouse interaction
    document.getElementById('gaugeContainer').addEventListener('mousedown', function(e) {
      AppState.pursuit.isDragging = true;
      updateGaugeFromMouse(e);
    });
    
    document.addEventListener('mousemove', function(e) {
      if (AppState.pursuit.isDragging) {
        updateGaugeFromMouse(e);
      }
    });
    
    document.addEventListener('mouseup', function() {
      AppState.pursuit.isDragging = false;
    });
    
    function updateGaugeFromMouse(e) {
      const container = document.getElementById('gaugeContainer');
      const rect = container.getBoundingClientRect();
      const y = e.clientY - rect.top;
      const containerHeight = rect.height;
      
      // Convert y position to percentage (top = 100%, bottom = 0%)
      let percent = 1 - (y / containerHeight);
      if (percent < 0) percent = 0;
      if (percent > 1) percent = 1;
      
      // Convert to position in mm
      const positionMM = percent * AppState.pursuit.totalDistanceMM;
      setGaugeTarget(positionMM);
    }
    
    // Pursuit mode controls
    document.getElementById('pursuitActiveCheckbox').addEventListener('change', function() {
      pursuitActive = this.checked;
      
      if (pursuitActive) {
        // Check if system is calibrating
        if (AppState.system.currentState === SystemState.CALIBRATING) {
          this.checked = false;
          pursuitActive = false;
          alert('Veuillez attendre la fin de la calibration');
          return;
        }
        
        document.getElementById('btnActivatePursuit').textContent = '⏸ Pause Poursuite';
        document.getElementById('btnActivatePursuit').classList.remove('btn-success');
        document.getElementById('btnActivatePursuit').classList.add('btn-warning');
        
        // Enable gauge interaction
        document.getElementById('gaugeContainer').style.opacity = '1';
        document.getElementById('gaugeContainer').style.cursor = 'crosshair';
        document.getElementById('gaugeContainer').style.pointerEvents = 'auto';
        
        // Use already set target position (from gauge clicks or current position)
        // Don't reset to current position - keep user's target choice
        if (AppState.pursuit.targetMM === undefined || isNaN(AppState.pursuit.targetMM)) {
          // Only initialize if never set before
          AppState.pursuit.targetMM = AppState.pursuit.currentPositionMM;
          setGaugeTarget(AppState.pursuit.currentPositionMM);
        }
        // else: Keep the target position that was set (even if 0 mm)
        
        // Enable pursuit mode on ESP32
        sendCommand(WS_CMD.ENABLE_PURSUIT_MODE, {});
        
        // Send initial position command after ESP32 mode switch completes
        setTimeout(function() {
          sendPursuitCommand();
          setTimeout(startPursuitLoop, PURSUIT_COMMAND_INTERVAL);
        }, 200);
      } else {
        document.getElementById('btnActivatePursuit').textContent = '▶ Démarrer';
        document.getElementById('btnActivatePursuit').classList.remove('btn-warning');
        document.getElementById('btnActivatePursuit').classList.add('btn-success');
        
        // Disable gauge interaction visually
        document.getElementById('gaugeContainer').style.opacity = '0.5';
        document.getElementById('gaugeContainer').style.cursor = 'not-allowed';
        document.getElementById('gaugeContainer').style.pointerEvents = 'none';
        
        // Disable pursuit mode on ESP32
        sendCommand(WS_CMD.DISABLE_PURSUIT_MODE, {});
        
        // NOTE: AppState.pursuit.targetMM is preserved for when user re-enables pursuit mode
      }
    });
    
    document.getElementById('btnActivatePursuit').addEventListener('click', function() {
      const checkbox = document.getElementById('pursuitActiveCheckbox');
      checkbox.checked = !checkbox.checked;
      checkbox.dispatchEvent(new Event('change'));
    });
    
    document.getElementById('pursuitMaxSpeed').addEventListener('change', function() {
      pursuitMaxSpeedLevel = parseFloat(this.value);
    });
    
    // Pursuit speed presets
    document.querySelectorAll('[data-pursuit-speed]').forEach(btn => {
      btn.addEventListener('click', function() {
        const speed = parseFloat(this.getAttribute('data-pursuit-speed'));
        document.getElementById('pursuitMaxSpeed').value = speed;
        pursuitMaxSpeedLevel = speed;
        
        document.querySelectorAll('[data-pursuit-speed]').forEach(b => b.classList.remove('active'));
        this.classList.add('active');
      });
    });
    
    // Pursuit loop - sends commands periodically
    function startPursuitLoop() {
      if (!pursuitActive) return;
      
      sendPursuitCommand();
      
      // Continue loop
      setTimeout(startPursuitLoop, PURSUIT_COMMAND_INTERVAL);
    }
    
    document.getElementById('btnStopPursuit').addEventListener('click', function() {
      // Disable pursuit mode
      document.getElementById('pursuitActiveCheckbox').checked = false;
      document.getElementById('pursuitActiveCheckbox').dispatchEvent(new Event('change'));
      
      // Return to start position to verify contact
      setTimeout(function() {
        console.log('Stopping pursuit - returning to start for contact verification');
        sendCommand(WS_CMD.RETURN_TO_START, {});
      }, 200);  // Small delay to let pursuit mode disable first
    });

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
        if (pursuitActive) {
          document.getElementById('pursuitActiveCheckbox').checked = false;
          document.getElementById('pursuitActiveCheckbox').dispatchEvent(new Event('change'));
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
      // Also check if pursuit mode is active
      return AppState.system.currentState === SystemState.RUNNING || 
             AppState.system.currentState === SystemState.PAUSED || 
             pursuitActive;
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
      if (pursuitActive) {
        console.log('Mode change: Disabling pursuit mode first');
        document.getElementById('pursuitActiveCheckbox').checked = false;
        document.getElementById('pursuitActiveCheckbox').dispatchEvent(new Event('change'));
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
          if (pursuitActive) {
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
      
      // Initialize max distance limit event listeners
      initMaxDistLimitListeners();
      
      // Ensure pursuit checkbox is unchecked on fresh load
      DOM.pursuitActiveCheckbox.checked = false;
      pursuitActive = false;
      
      // Reset button text
      DOM.btnActivatePursuit.textContent = '▶ Démarrer';
      DOM.btnActivatePursuit.classList.remove('btn-warning');
      DOM.btnActivatePursuit.classList.add('btn-success');
      
      // Disable gauge interaction on page load (pursuit mode inactive)
      if (DOM.gaugeContainer) {
        DOM.gaugeContainer.style.opacity = '0.5';
        DOM.gaugeContainer.style.cursor = 'not-allowed';
        DOM.gaugeContainer.style.pointerEvents = 'none';
      }
      
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
    // LOGS PANEL MANAGEMENT
    // ============================================================================
    
    document.getElementById('btnShowLogs').addEventListener('click', function() {
      const panel = document.getElementById('logsPanel');
      if (panel.style.display === 'none') {
        panel.style.display = 'block';
        this.innerHTML = '📋 Logs';
        this.style.background = '#e74c3c';
        this.style.color = 'white';
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
        panel.style.display = 'none';
        this.innerHTML = '📋 Logs';
        this.style.background = '#17a2b8';
        this.style.color = 'white';
      }
    });

    document.getElementById('btnCloseLogs').addEventListener('click', function() {
      document.getElementById('logsPanel').style.display = 'none';
      document.getElementById('btnShowLogs').innerHTML = '📋 Logs';
      document.getElementById('btnShowLogs').style.background = '#17a2b8';
      document.getElementById('btnShowLogs').style.color = 'white';
    });

    document.getElementById('btnClearLogsPanel').addEventListener('click', function() {
      const logEl = document.getElementById('logConsolePanel');
      if (logEl) logEl.textContent = '(logs effacés)';
    });

    document.getElementById('debugLevelCheckbox').addEventListener('change', function() {
      // Save debug level preference to EEPROM via API
      const preferences = {
        loggingEnabled: true,  // Keep logging enabled
        logLevel: this.checked ? 3 : 2  // 3=DEBUG, 2=INFO
      };
      
      fetch('/api/system/logging/preferences', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(preferences)
      })
        .then(response => response.json())
        .then(data => {
          console.log('💾 Log level saved:', this.checked ? 'DEBUG' : 'INFO');
          
          // Also update the checkbox in Sys panel
          const chkDebug = document.getElementById('chkDebugLevel');
          if (chkDebug) {
            chkDebug.checked = this.checked;
          }
        })
        .catch(error => {
          console.error('❌ Error saving log level:', error);
        });
    });

    document.getElementById('btnClearAllLogFiles').addEventListener('click', function() {
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
    });

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
    // STATISTICS PANEL MANAGEMENT
    // ============================================================================
    // Note: statsChart, dayIcons, dayNames, loadStatsData, displayStatsTable, 
    //       displayStatsChart all loaded from stats.js
    
    // Note: getISOWeek() loaded from utils.js
    
    document.getElementById('btnShowStats').addEventListener('click', function() {
      const panel = document.getElementById('statsPanel');
      const wasVisible = (panel.style.display !== 'none');
      
      if (!wasVisible) {
        // Opening panel
        panel.style.display = 'block';
        this.innerHTML = '📊 Stats';
        this.style.background = '#e74c3c';
        this.style.color = 'white';
        
        // Update state and signal backend
        AppState.statsPanel.isVisible = true;
        AppState.statsPanel.lastToggle = Date.now();
        
        // Send WebSocket command to backend (enable stats sending)
        if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
          AppState.ws.send(JSON.stringify({
            cmd: 'requestStats',
            enable: true
          }));
          console.log('📊 Stats requested from backend');
        }
        
        // Load stats data
        loadStatsData();
      } else {
        // Closing panel
        panel.style.display = 'none';
        this.innerHTML = '📊 Stats';
        this.style.background = '#4CAF50';
        this.style.color = 'white';
        
        // Update state and signal backend
        AppState.statsPanel.isVisible = false;
        AppState.statsPanel.lastToggle = Date.now();
        
        // Send WebSocket command to backend (disable stats sending)
        if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
          AppState.ws.send(JSON.stringify({
            cmd: 'requestStats',
            enable: false
          }));
          console.log('📊 Stats no longer needed');
        }
      }
    });

    document.getElementById('btnCloseStats').addEventListener('click', function() {
      document.getElementById('statsPanel').style.display = 'none';
      document.getElementById('btnShowStats').innerHTML = '📊 Stats';
      document.getElementById('btnShowStats').style.background = '#4CAF50';
      document.getElementById('btnShowStats').style.color = 'white';
      
      // Update state and signal backend
      AppState.statsPanel.isVisible = false;
      AppState.statsPanel.lastToggle = Date.now();
      
      // Send WebSocket command to backend (disable stats sending)
      if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
        AppState.ws.send(JSON.stringify({
          cmd: 'requestStats',
          enable: false
        }));
        console.log('📊 Stats panel closed');
      }
    });

    document.getElementById('btnShowSystem').addEventListener('click', function() {
      const panel = document.getElementById('systemPanel');
      if (panel.style.display === 'none') {
        panel.style.display = 'block';
        this.innerHTML = '⚙️ Sys';
        this.style.background = '#e74c3c';
        this.style.color = 'white';
        
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
        panel.style.display = 'none';
        this.innerHTML = '⚙️ Sys';
        this.style.background = '#2196F3';
        this.style.color = 'white';
        
        // Disable system stats when closing (optional optimization)
        if (AppState.ws && AppState.ws.readyState === WebSocket.OPEN) {
          AppState.ws.send(JSON.stringify({
            cmd: 'requestStats',
            enable: false
          }));
        }
      }
    });

    document.getElementById('btnCloseSystem').addEventListener('click', function() {
      document.getElementById('systemPanel').style.display = 'none';
      document.getElementById('btnShowSystem').innerHTML = '⚙️ Sys';
      document.getElementById('btnShowSystem').style.background = '#2196F3';
      document.getElementById('btnShowSystem').style.color = 'white';
    });

    document.getElementById('btnRefreshWifi').addEventListener('click', function() {
      if (confirm('📶 Reconnecter le WiFi?\n\nCela peut interrompre brièvement la connexion (~2-3 secondes).\n\nVoulez-vous continuer?')) {
        const btn = this;
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
    });

    document.getElementById('btnReboot').addEventListener('click', function() {
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
    });

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

    // ========================================================================
    // LOGGING PREFERENCES - EEPROM
    // ========================================================================
    
    // Load logging preferences from ESP32
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
    
    // Save logging preferences to ESP32 EEPROM
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
    
    // Attach event handlers for checkboxes
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
    
    // Load preferences on startup
    loadLoggingPreferences();

    document.getElementById('btnClearStats').addEventListener('click', function() {
      if (confirm('⚠️ Supprimer TOUTES les statistiques?\n\nCette action est irréversible et ne supprime PAS le compteur de distance (RAZ).')) {
        fetch('/api/stats/clear', { method: 'POST' })
          .then(response => response.json())
          .then(data => {
            if (data.success) {
              alert('✅ Statistiques effacées');
              loadStatsData();  // Refresh display
            } else {
              alert('❌ Erreur: ' + (data.error || 'Unknown'));
            }
          })
          .catch(error => {
            alert('❌ Erreur réseau: ' + error);
          });
      }
    });

    // Export Stats Button
    document.getElementById('btnExportStats').addEventListener('click', function() {
      fetch('/api/stats/export')
        .then(response => {
          if (!response.ok) throw new Error('Export failed');
          return response.json();
        })
        .then(data => {
          // Create JSON file and download
          const jsonStr = JSON.stringify(data, null, 2);
          const blob = new Blob([jsonStr], { type: 'application/json' });
          const url = URL.createObjectURL(blob);
          
          // Generate filename with current date
          const now = new Date();
          const dateStr = now.toISOString().split('T')[0]; // YYYY-MM-DD
          const filename = `stepper_stats_${dateStr}.json`;
          
          // Trigger download
          const a = document.createElement('a');
          a.href = url;
          a.download = filename;
          a.click();
          
          URL.revokeObjectURL(url);
          console.log(`✅ Stats exported: ${filename}`);
        })
        .catch(error => {
          alert('❌ Erreur export: ' + error.message);
          console.error('Export error:', error);
        });
    });

    // Import Stats Button
    document.getElementById('btnImportStats').addEventListener('click', function() {
      document.getElementById('statsFileInput').click();
    });

    // File input handler
    document.getElementById('statsFileInput').addEventListener('change', function(e) {
      const file = e.target.files[0];
      if (!file) return;
      
      if (!file.name.endsWith('.json')) {
        alert('❌ Fichier invalide. Utilisez un fichier JSON exporté.');
        return;
      }
      
      const reader = new FileReader();
      reader.onload = function(event) {
        try {
          const importData = JSON.parse(event.target.result);
          
          // Validate structure
          if (!importData.stats || !Array.isArray(importData.stats)) {
            throw new Error('Format JSON invalide (manque "stats" array)');
          }
          
          // Confirm import (show preview)
          const entryCount = importData.stats.length;
          const exportDate = importData.exportDate || 'inconnu';
          const totalKm = importData.totalDistanceMM ? (importData.totalDistanceMM / 1000000).toFixed(3) : '?';
          
          const confirmMsg = `📤 Importer les statistiques?\n\n` +
                           `📅 Date export: ${exportDate}\n` +
                           `📊 Entrées: ${entryCount}\n` +
                           `📏 Distance totale: ${totalKm} km\n\n` +
                           `⚠️ Ceci va ÉCRASER les statistiques actuelles!`;
          
          if (!confirm(confirmMsg)) {
            e.target.value = ''; // Reset file input
            return;
          }
          
          // Send to backend
          fetch('/api/stats/import', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(importData)
          })
          .then(response => response.json())
          .then(data => {
            if (data.success) {
              alert(`✅ Import réussi!\n\n📊 ${data.entriesImported} entrées importées\n📏 Total: ${(data.totalDistanceMM / 1000000).toFixed(3)} km`);
              loadStatsData();  // Refresh display
            } else {
              alert('❌ Erreur import: ' + (data.error || 'Unknown'));
            }
            e.target.value = ''; // Reset file input
          })
          .catch(error => {
            alert('❌ Erreur réseau: ' + error.message);
            console.error('Import error:', error);
            e.target.value = ''; // Reset file input
          });
          
        } catch (error) {
          alert('❌ Erreur parsing JSON: ' + error.message);
          console.error('JSON parse error:', error);
          e.target.value = ''; // Reset file input
        }
      };
      
      reader.readAsText(file);
    });

    // Note: loadStatsData, displayStatsTable, displayStatsChart moved to stats.js
    
    document.getElementById('btnStartSequence').addEventListener('click', function() {
      // Disable both start buttons immediately (instant feedback)
      setButtonState(DOM.btnStartSequence, false);
      setButtonState(DOM.btnLoopSequence, false);
      sendCommand(WS_CMD.START_SEQUENCE, {});
    });
    
    document.getElementById('btnLoopSequence').addEventListener('click', function() {
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
    // ============================================================================
    // MODE OSCILLATION - Event Listeners
    // ============================================================================
    
    // Toggle help section
    function toggleOscHelp() {
      const helpSection = document.getElementById('oscHelpSection');
      helpSection.style.display = helpSection.style.display === 'none' ? 'block' : 'none';
    }
    
    function toggleChaosHelp() {
      const helpSection = document.getElementById('chaosHelpSection');
      helpSection.style.display = helpSection.style.display === 'none' ? 'block' : 'none';
    }
    
    // Debounce timer for validation
    let validationDebounceTimer = null;
    
    // Update limit validation on input change - delegates to pure function
    function validateOscillationLimits() {
      const center = parseFloat(DOM.oscCenter.value) || 0;
      const amplitude = parseFloat(DOM.oscAmplitude.value) || 0;
      const totalDistMM = AppState.pursuit.totalDistanceMM || 0;
      
      const warning = document.getElementById('oscLimitWarning');
      const statusSpan = document.getElementById('oscLimitStatus');
      const btnStart = DOM.btnStartOscillation;
      
      // If not calibrated yet, show waiting message
      if (!AppState.system.canStart || !totalDistMM || totalDistMM === 0) {
        warning.style.display = 'none';
        statusSpan.textContent = '⏳ En attente calibration';
        statusSpan.style.color = '#ff9800';
        btnStart.disabled = true;
        btnStart.style.opacity = '0.5';
        btnStart.style.cursor = 'not-allowed';
        return false;
      }
      
      // Use pure function if available (from oscillation.js)
      let isValid = true;
      if (typeof validateOscillationLimitsPure === 'function') {
        const result = validateOscillationLimitsPure(center, amplitude, totalDistMM);
        isValid = result.valid;
      } else {
        // Fallback to inline logic
        const minPos = center - amplitude;
        const maxPos = center + amplitude;
        isValid = minPos >= 0 && maxPos <= totalDistMM;
      }
      
      if (!isValid) {
        warning.style.display = 'block';
        statusSpan.textContent = '❌ Invalide';
        statusSpan.style.color = '#e74c3c';
        btnStart.disabled = true;
        btnStart.style.opacity = '0.5';
        btnStart.style.cursor = 'not-allowed';
        return false;
      } else {
        warning.style.display = 'none';
        statusSpan.textContent = '✅ Valide';
        statusSpan.style.color = '#27ae60';
        btnStart.disabled = false;
        btnStart.style.opacity = '1';
        btnStart.style.cursor = 'pointer';
        updateOscillationPresets();  // Update preset buttons state
        return true;
      }
    }
    
    // Helper function to send oscillation config in real-time
    function sendOscillationConfig() {
      const amplitude = parseFloat(document.getElementById('oscAmplitude').value) || 0;
      const frequency = parseFloat(document.getElementById('oscFrequency').value) || 0.5;
      
      // 🚀 SAFETY: Check if frequency would exceed speed limit
      const MAX_SPEED_MM_S = maxSpeedLevel * 20.0; // 300 mm/s by default
      
      // Use pure function if available (from oscillation.js)
      if (typeof calculateOscillationPeakSpeedPure === 'function') {
        const theoreticalSpeed = calculateOscillationPeakSpeedPure(frequency, amplitude);
        
        if (amplitude > 0 && theoreticalSpeed > MAX_SPEED_MM_S) {
          const maxAllowedFreq = MAX_SPEED_MM_S / (2.0 * Math.PI * amplitude);
          showNotification(
            `⚠️ Fréquence limitée: ${frequency.toFixed(2)} Hz → ${maxAllowedFreq.toFixed(2)} Hz (vitesse max: ${MAX_SPEED_MM_S.toFixed(0)} mm/s)`,
            'error',
            4000
          );
        }
      }
      
      // Collect form values and delegate to pure function
      const formValues = {
        centerPos: document.getElementById('oscCenter').value,
        amplitude: document.getElementById('oscAmplitude').value,
        waveform: document.getElementById('oscWaveform').value,
        frequency: document.getElementById('oscFrequency').value,
        cycleCount: document.getElementById('oscCycleCount').value,
        enableRampIn: document.getElementById('oscRampInEnable').checked,
        rampInDuration: document.getElementById('oscRampInDuration').value,
        enableRampOut: document.getElementById('oscRampOutEnable').checked,
        rampOutDuration: document.getElementById('oscRampOutDuration').value,
        returnToCenter: document.getElementById('oscReturnCenter').checked
      };
      
      // Delegate to pure function if available
      let config;
      if (typeof buildOscillationConfigPure === 'function') {
        config = buildOscillationConfigPure(formValues);
      } else {
        // Fallback
        config = {
          centerPositionMM: parseFloat(formValues.centerPos) || 0,
          amplitudeMM: parseFloat(formValues.amplitude) || 0,
          waveform: parseInt(formValues.waveform) || 0,
          frequencyHz: parseFloat(formValues.frequency) || 0.5,
          cycleCount: parseInt(formValues.cycleCount) || 0,
          enableRampIn: formValues.enableRampIn,
          rampInDurationMs: parseFloat(formValues.rampInDuration) || 2000,
          enableRampOut: formValues.enableRampOut,
          rampOutDurationMs: parseFloat(formValues.rampOutDuration) || 2000,
          returnToCenter: formValues.returnToCenter
        };
      }
      
      sendCommand(WS_CMD.SET_OSCILLATION, config);
    }
    
    // Oscillation input editing protection + real-time update
    // Use mousedown to catch spinner clicks BEFORE focus + force focus immediately
    document.getElementById('oscCenter').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscCenter';
      this.focus();
    });
    document.getElementById('oscCenter').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscCenter';
    });
    document.getElementById('oscCenter').addEventListener('blur', function() {
      AppState.editing.oscField = null;
      validateOscillationLimits();
      sendOscillationConfig();  // Send on blur
    });
    document.getElementById('oscCenter').addEventListener('input', function() {
      // Debounce validation on input (300ms)
      clearTimeout(validationDebounceTimer);
      validationDebounceTimer = setTimeout(validateOscillationLimits, 300);
    });
    
    document.getElementById('oscAmplitude').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscAmplitude';
      this.focus();
    });
    document.getElementById('oscAmplitude').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscAmplitude';
    });
    document.getElementById('oscAmplitude').addEventListener('blur', function() {
      AppState.editing.oscField = null;
      validateOscillationLimits();
      sendOscillationConfig();  // Send on blur
    });
    document.getElementById('oscAmplitude').addEventListener('input', function() {
      // Debounce validation on input (300ms)
      clearTimeout(validationDebounceTimer);
      validationDebounceTimer = setTimeout(validateOscillationLimits, 300);
    });
    
    document.getElementById('oscWaveform').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscWaveform';
      this.focus();
    });
    document.getElementById('oscWaveform').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscWaveform';
    });
    document.getElementById('oscWaveform').addEventListener('change', function() {
      AppState.editing.oscField = null;
      sendOscillationConfig();  // Send immediately on change
    });
    
    document.getElementById('oscFrequency').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscFrequency';
      this.focus();
    });
    document.getElementById('oscFrequency').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscFrequency';
    });
    document.getElementById('oscFrequency').addEventListener('blur', function() {
      AppState.editing.oscField = null;
      validateOscillationLimits();  // Validate on blur
      sendOscillationConfig();  // Send on blur
    });
    document.getElementById('oscFrequency').addEventListener('input', function() {
      // Debounce validation on input (300ms)
      clearTimeout(validationDebounceTimer);
      validationDebounceTimer = setTimeout(() => {
        validateOscillationLimits();
        updateOscillationPresets(); // Update presets to gray out invalid ones
      }, 300);
    });
    
    document.getElementById('oscRampInDuration').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscRampInDuration';
      this.focus();
    });
    document.getElementById('oscRampInDuration').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscRampInDuration';
    });
    document.getElementById('oscRampInDuration').addEventListener('blur', function() {
      AppState.editing.oscField = null;
      sendOscillationConfig();  // Send on blur
    });
    
    document.getElementById('oscRampOutDuration').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscRampOutDuration';
      this.focus();
    });
    document.getElementById('oscRampOutDuration').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscRampOutDuration';
    });
    document.getElementById('oscRampOutDuration').addEventListener('blur', function() {
      AppState.editing.oscField = null;
      sendOscillationConfig();  // Send on blur
    });
    
    document.getElementById('oscCycleCount').addEventListener('mousedown', function() {
      AppState.editing.oscField = 'oscCycleCount';
      this.focus();
    });
    document.getElementById('oscCycleCount').addEventListener('focus', function() {
      AppState.editing.oscField = 'oscCycleCount';
    });
    document.getElementById('oscCycleCount').addEventListener('blur', function() {
      AppState.editing.oscField = null;
      sendOscillationConfig();  // Send on blur
    });
    
    // Toggle ramp config visibility + send config (compact mode: always visible)
    document.getElementById('oscRampInEnable').addEventListener('change', function() {
      // document.getElementById('oscRampInConfig').style.display = this.checked ? 'block' : 'none'; // Removed in compact mode
      sendOscillationConfig();  // Send on change
    });
    
    document.getElementById('oscRampOutEnable').addEventListener('change', function() {
      // document.getElementById('oscRampOutConfig').style.display = this.checked ? 'block' : 'none'; // Removed in compact mode
      sendOscillationConfig();  // Send on change
    });
    
    document.getElementById('oscReturnCenter').addEventListener('change', function() {
      sendOscillationConfig();  // Send on change
    });
    
    // Cycle Pause listeners (Oscillation mode)
    function sendOscCyclePauseConfig() {
      const enabled = document.getElementById('oscCyclePauseEnabled')?.checked || false;
      const isRandom = document.getElementById('oscCyclePauseRandom')?.checked || false;
      const durationSec = parseFloat(document.getElementById('oscCyclePauseDuration')?.value || 0);
      let minSec = parseFloat(document.getElementById('oscCyclePauseMin')?.value || 0.5);
      let maxSec = parseFloat(document.getElementById('oscCyclePauseMax')?.value || 3.0);
      
      // 🆕 VALIDATION: Min doit être ≤ Max (seulement si random activé)
      if (isRandom && minSec > maxSec) {
        showNotification('⚠️ Pause Min (' + minSec.toFixed(1) + 's) doit être ≤ Max (' + maxSec.toFixed(1) + 's)', 'warning');
        // Auto-correction: ajuster Max = Min
        maxSec = minSec;
        document.getElementById('oscCyclePauseMax').value = maxSec;
      }
      
      sendCommand(WS_CMD.SET_CYCLE_PAUSE, {
        mode: 'oscillation',
        enabled: enabled,
        isRandom: isRandom,
        durationSec: durationSec,
        minSec: minSec,
        maxSec: maxSec
      });
    }
    
    if (document.getElementById('oscCyclePauseEnabled')) {
      document.getElementById('oscCyclePauseEnabled').addEventListener('change', sendOscCyclePauseConfig);
    }
    if (document.getElementById('oscCyclePauseRandom')) {
      document.getElementById('oscCyclePauseRandom').addEventListener('change', sendOscCyclePauseConfig);
    }
    if (document.getElementById('oscCyclePauseDuration')) {
      document.getElementById('oscCyclePauseDuration').addEventListener('change', sendOscCyclePauseConfig);
    }
    if (document.getElementById('oscCyclePauseMin')) {
      document.getElementById('oscCyclePauseMin').addEventListener('change', sendOscCyclePauseConfig);
    }
    if (document.getElementById('oscCyclePauseMax')) {
      document.getElementById('oscCyclePauseMax').addEventListener('change', sendOscCyclePauseConfig);
    }
    
    // Start oscillation
    document.getElementById('btnStartOscillation').addEventListener('click', function() {
      if (!validateOscillationLimits()) {
        showNotification('Limites invalides: ajustez le centre ou l\'amplitude', 'error');
        return;
      }
      
      // Send final config + start (config already sent in real-time, but ensure it's current)
      sendOscillationConfig();
      
      // Wait a bit then start
      setTimeout(function() {
        sendCommand(WS_CMD.START_OSCILLATION, {});
      }, 50);
    });
    
    // Stop oscillation
    document.getElementById('btnStopOscillation').addEventListener('click', function() {
      // Only show modal if motor has moved (currentStep > 0)
      if (currentPositionMM > 0.5) {
        showStopModal();
      } else {
        // Direct stop if at position 0
        sendCommand(WS_CMD.STOP_OSCILLATION, {});
      }
    });
    
    // Pause oscillation
    document.getElementById('btnPauseOscillation').addEventListener('click', function() {
      sendCommand(WS_CMD.PAUSE);
    });
    
    // Oscillation preset buttons handlers
    document.querySelectorAll('[data-osc-center]').forEach(btn => {
      btn.addEventListener('click', function() {
        if (!this.disabled) {
          document.getElementById('oscCenter').value = this.getAttribute('data-osc-center');
          sendOscillationConfig();
          validateOscillationLimits();
          updateOscillationPresets();
        }
      });
    });
    
    document.querySelectorAll('[data-osc-amplitude]').forEach(btn => {
      btn.addEventListener('click', function() {
        if (!this.disabled) {
          const newAmplitude = this.getAttribute('data-osc-amplitude');
          console.log('🎯 Preset amplitude clicked: ' + newAmplitude + 'mm');
          document.getElementById('oscAmplitude').value = newAmplitude;
          console.log('📤 Sending oscillation config with amplitude=' + newAmplitude);
          sendOscillationConfig();
          validateOscillationLimits();
          updateOscillationPresets();
        }
      });
    });
    
    document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
      btn.addEventListener('click', function() {
        if (!this.disabled) {
          document.getElementById('oscFrequency').value = this.getAttribute('data-osc-frequency');
          sendOscillationConfig();
        }
      });
    });
    
    // Function to update visual state of oscillation preset buttons
    function updateOscillationPresets() {
      const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
      if (effectiveMax === 0) return;
      
      const currentCenter = parseFloat(document.getElementById('oscCenter').value) || 0;
      const currentAmplitude = parseFloat(document.getElementById('oscAmplitude').value) || 0;
      
      // 🚀 MAX_SPEED_LEVEL constant (must match backend)
      const MAX_SPEED_MM_S = maxSpeedLevel * 20.0; // 300 mm/s by default
      
      // Validate center presets (must allow current amplitude)
      document.querySelectorAll('[data-osc-center]').forEach(btn => {
        const centerValue = parseFloat(btn.getAttribute('data-osc-center'));
        const minPos = centerValue - currentAmplitude;
        const maxPos = centerValue + currentAmplitude;
        const isValid = minPos >= 0 && maxPos <= effectiveMax;
        
        btn.disabled = !isValid;
        btn.style.opacity = isValid ? '1' : '0.3';
        btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      });
      
      // Validate amplitude presets (must respect current center)
      document.querySelectorAll('[data-osc-amplitude]').forEach(btn => {
        const amplitudeValue = parseFloat(btn.getAttribute('data-osc-amplitude'));
        const minPos = currentCenter - amplitudeValue;
        const maxPos = currentCenter + amplitudeValue;
        const isValid = minPos >= 0 && maxPos <= effectiveMax;
        
        btn.disabled = !isValid;
        btn.style.opacity = isValid ? '1' : '0.3';
        btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      });
      
      // 🚀 Validate frequency presets (must not exceed speed limit)
      document.querySelectorAll('[data-osc-frequency]').forEach(btn => {
        const frequencyValue = parseFloat(btn.getAttribute('data-osc-frequency'));
        
        // Calculate theoretical speed for this frequency
        if (currentAmplitude > 0) {
          const theoreticalSpeed = 2 * Math.PI * frequencyValue * currentAmplitude;
          const isValid = theoreticalSpeed <= MAX_SPEED_MM_S;
          
          btn.disabled = !isValid;
          btn.style.opacity = isValid ? '1' : '0.3';
          btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
          btn.title = isValid 
            ? `${frequencyValue} Hz (${theoreticalSpeed.toFixed(0)} mm/s)` 
            : `⚠️ ${frequencyValue} Hz dépasserait ${MAX_SPEED_MM_S} mm/s (${theoreticalSpeed.toFixed(0)} mm/s calculé)`;
        }
      });
    }
    
    // ============================================================================
    // CHAOS MODE HANDLERS
    // ============================================================================
    
    // Send chaos configuration
    // Send chaos configuration
    function sendChaosConfig() {
      // Collect form values
      const formValues = {
        centerPos: document.getElementById('chaosCenterPos').value,
        amplitude: document.getElementById('chaosAmplitude').value,
        maxSpeed: document.getElementById('chaosMaxSpeed').value,
        craziness: document.getElementById('chaosCraziness').value,
        duration: document.getElementById('chaosDuration').value,
        seed: document.getElementById('chaosSeed').value,
        patternsEnabled: [
          document.getElementById('patternZigzag').checked,
          document.getElementById('patternSweep').checked,
          document.getElementById('patternPulse').checked,
          document.getElementById('patternDrift').checked,
          document.getElementById('patternBurst').checked,
          document.getElementById('patternWave').checked,
          document.getElementById('patternPendulum').checked,
          document.getElementById('patternSpiral').checked,
          document.getElementById('patternCalm').checked,
          document.getElementById('patternBruteForce').checked,
          document.getElementById('patternLiberator').checked
        ]
      };
      
      // Delegate to pure function if available (from chaos.js)
      let config;
      if (typeof buildChaosConfigPure === 'function') {
        config = buildChaosConfigPure(formValues);
      } else {
        // Fallback
        config = {
          centerPositionMM: parseFloat(formValues.centerPos) || 0,
          amplitudeMM: parseFloat(formValues.amplitude) || 0,
          maxSpeedLevel: parseFloat(formValues.maxSpeed) || 10,
          crazinessPercent: parseInt(formValues.craziness) || 50,
          durationSeconds: parseInt(formValues.duration) || 30,
          seed: parseInt(formValues.seed) || 0,
          patternsEnabled: formValues.patternsEnabled
        };
      }
      
      sendCommand(WS_CMD.SET_CHAOS_CONFIG, config);
    }
    
    // Validate chaos limits - delegates to pure function from context.js
    function validateChaosLimits() {
      const centerPos = parseFloat(document.getElementById('chaosCenterPos').value);
      const amplitude = parseFloat(document.getElementById('chaosAmplitude').value);
      const totalDistMM = AppState.pursuit.totalDistanceMM || 0;
      
      // Use pure function if available (from context.js)
      if (typeof validateChaosLimitsPure === 'function') {
        const result = validateChaosLimitsPure(centerPos, amplitude, totalDistMM);
        return result.valid;
      }
      
      // Fallback to inline logic
      if (centerPos - amplitude < 0 || centerPos + amplitude > totalDistMM) {
        return false;
      }
      return true;
    }
    
    // Function to update visual state of chaos preset buttons
    function updateChaosPresets() {
      const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
      if (effectiveMax === 0) return;
      
      const currentCenter = parseFloat(document.getElementById('chaosCenterPos').value) || 0;
      const currentAmplitude = parseFloat(document.getElementById('chaosAmplitude').value) || 0;
      
      // Validate center presets (must allow current amplitude)
      document.querySelectorAll('[data-chaos-center]').forEach(btn => {
        const centerValue = parseFloat(btn.getAttribute('data-chaos-center'));
        const minPos = centerValue - currentAmplitude;
        const maxPos = centerValue + currentAmplitude;
        const isValid = minPos >= 0 && maxPos <= effectiveMax;
        
        btn.disabled = !isValid;
        btn.style.opacity = isValid ? '1' : '0.3';
        btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      });
      
      // Validate amplitude presets (must respect current center)
      document.querySelectorAll('[data-chaos-amplitude]').forEach(btn => {
        const amplitudeValue = parseFloat(btn.getAttribute('data-chaos-amplitude'));
        const minPos = currentCenter - amplitudeValue;
        const maxPos = currentCenter + amplitudeValue;
        const isValid = minPos >= 0 && maxPos <= effectiveMax;
        
        btn.disabled = !isValid;
        btn.style.opacity = isValid ? '1' : '0.3';
        btn.style.cursor = isValid ? 'pointer' : 'not-allowed';
      });
    }
    
    // Function to update pattern toggle button text (Tout/Aucun)
    function updatePatternToggleButton() {
      const patterns = [
        'patternZigzag', 'patternSweep', 'patternPulse', 'patternDrift',
        'patternBurst', 'patternWave', 'patternPendulum', 'patternSpiral',
        'patternCalm', 'patternBruteForce', 'patternLiberator'
      ];
      
      const checkedCount = patterns.filter(id => document.getElementById(id).checked).length;
      const btn = document.getElementById('btnEnableAllPatterns');
      
      if (checkedCount === patterns.length) {
        // All checked → show "Aucun" (next action will uncheck all)
        btn.textContent = '❌ Aucun';
      } else {
        // Some or none checked → show "Tout" (next action will check all)
        btn.textContent = '✅ Tout';
      }
    }
    
    // Update chaos UI with live data
    function updateChaosUI(data) {
      if (!data.chaosState) return;
      
      const isRunning = data.chaosState.isRunning;
      const wasRunning = DOM.chaosStats.style.display === 'block';  // Track previous state
      const isCalibrating = data.state === SystemState.CALIBRATING;
      
      // Show/hide stats panel
      DOM.chaosStats.style.display = isRunning ? 'block' : 'none';
      
      // CRITICAL FIX: Reset patterns flag when chaos stops
      // This allows patterns to be re-synced from backend after each run
      if (wasRunning && !isRunning) {
        console.log('🔄 Chaos stopped - resetting patterns flag for next sync');
        AppState.flags.patternsInitialized = false;
      }
      
      // Update button states (disable if not calibrated or calibrating)
      const canStart = canStartOperation() && !isRunning;
      setButtonState(DOM.btnStartChaos, canStart);
      DOM.btnStopChaos.disabled = !isRunning;
      
      // Allow live config changes while running (except seed)
      // Config inputs remain enabled for real-time adjustments
      document.getElementById('chaosCenterPos').disabled = false;
      document.getElementById('chaosAmplitude').disabled = false;
      document.getElementById('chaosMaxSpeed').disabled = false;
      document.getElementById('chaosCraziness').disabled = false;
      document.getElementById('chaosDuration').disabled = false;
      document.getElementById('chaosSeed').disabled = isRunning;  // Only disable seed while running
      
      // Enable/disable pattern checkboxes and preset buttons based on running state
      // Checkboxes: ALWAYS disabled while running, ALWAYS enabled when stopped
      document.getElementById('patternZigzag').disabled = isRunning || isCalibrating;
      document.getElementById('patternSweep').disabled = isRunning || isCalibrating;
      document.getElementById('patternPulse').disabled = isRunning || isCalibrating;
      document.getElementById('patternDrift').disabled = isRunning || isCalibrating;
      document.getElementById('patternBurst').disabled = isRunning || isCalibrating;
      document.getElementById('patternWave').disabled = isRunning || isCalibrating;
      document.getElementById('patternPendulum').disabled = isRunning || isCalibrating;
      document.getElementById('patternSpiral').disabled = isRunning || isCalibrating;
      document.getElementById('btnEnableAllPatterns').disabled = isRunning || isCalibrating;
      document.getElementById('btnEnableSoftPatterns').disabled = isRunning || isCalibrating;
      document.getElementById('btnEnableDynamicPatterns').disabled = isRunning || isCalibrating;
      
      // Restore pattern states from backend ONLY on first load (not on every status update)
      // This prevents user's checkbox changes from being overwritten during runtime config changes
      if (!AppState.flags.patternsInitialized && !isRunning && data.chaos && data.chaos.patternsEnabled) {
        document.getElementById('patternZigzag').checked = data.chaos.patternsEnabled[0];
        document.getElementById('patternSweep').checked = data.chaos.patternsEnabled[1];
        document.getElementById('patternPulse').checked = data.chaos.patternsEnabled[2];
        document.getElementById('patternDrift').checked = data.chaos.patternsEnabled[3];
        document.getElementById('patternBurst').checked = data.chaos.patternsEnabled[4];
        document.getElementById('patternWave').checked = data.chaos.patternsEnabled[5];
        document.getElementById('patternPendulum').checked = data.chaos.patternsEnabled[6];
        document.getElementById('patternSpiral').checked = data.chaos.patternsEnabled[7];
        AppState.flags.patternsInitialized = true;  // Mark as initialized
      }
      
      if (isRunning) {
        // Update stats
        document.getElementById('statPattern').textContent = data.chaosState.patternName;
        document.getElementById('statPosition').textContent = data.positionMM.toFixed(2) + ' mm';
        
        const range = data.chaosState.maxReachedMM - data.chaosState.minReachedMM;
        document.getElementById('statRange').textContent = 
          data.chaosState.minReachedMM.toFixed(1) + ' - ' + 
          data.chaosState.maxReachedMM.toFixed(1) + ' mm (' + 
          range.toFixed(1) + ' mm)';
        
        document.getElementById('statCount').textContent = data.chaosState.patternsExecuted;
        
        // Update timer
        if (data.chaos.durationSeconds > 0 && data.chaosState.elapsedSeconds !== undefined) {
          document.getElementById('statTimer').style.display = 'block';
          document.getElementById('statElapsed').textContent = 
            data.chaosState.elapsedSeconds + ' / ' + data.chaos.durationSeconds;
        } else if (data.chaosState.elapsedSeconds !== undefined) {
          document.getElementById('statTimer').style.display = 'block';
          document.getElementById('statElapsed').textContent = data.chaosState.elapsedSeconds;
        } else {
          document.getElementById('statTimer').style.display = 'none';
        }
      }
      
      // Update preset buttons visual state
      updateChaosPresets();
    }
    
    // Start chaos
    document.getElementById('btnStartChaos').addEventListener('click', function() {
      if (!validateChaosLimits()) {
        showNotification('Limites invalides: la zone chaos dépasse les limites calibrées', 'error');
        return;
      }
      
      const centerPos = parseFloat(document.getElementById('chaosCenterPos').value);
      const amplitude = parseFloat(document.getElementById('chaosAmplitude').value);
      const maxSpeed = parseFloat(document.getElementById('chaosMaxSpeed').value);
      const craziness = parseFloat(document.getElementById('chaosCraziness').value);
      const duration = parseInt(document.getElementById('chaosDuration').value);
      const seed = parseInt(document.getElementById('chaosSeed').value);
      
      // Collect pattern selections (11 patterns: ZIGZAG, SWEEP, PULSE, DRIFT, BURST, WAVE, PENDULUM, SPIRAL, BREATHING, BRUTE_FORCE, LIBERATOR)
      const patternsEnabled = [
        document.getElementById('patternZigzag').checked,
        document.getElementById('patternSweep').checked,
        document.getElementById('patternPulse').checked,
        document.getElementById('patternDrift').checked,
        document.getElementById('patternBurst').checked,
        document.getElementById('patternWave').checked,
        document.getElementById('patternPendulum').checked,
        document.getElementById('patternSpiral').checked,
        document.getElementById('patternCalm').checked,
        document.getElementById('patternBruteForce').checked,
        document.getElementById('patternLiberator').checked
      ];
      
      // Validate at least one pattern selected
      if (!patternsEnabled.some(p => p)) {
        showNotification('⚠️ Au moins un pattern doit être activé', 'error');
        return;
      }
      
      sendCommand(WS_CMD.START_CHAOS, {
        centerPositionMM: centerPos,
        amplitudeMM: amplitude,
        maxSpeedLevel: maxSpeed,
        crazinessPercent: craziness,
        durationSeconds: duration,
        seed: seed,
        patternsEnabled: patternsEnabled
      });
    });
    
    // Stop chaos
    document.getElementById('btnStopChaos').addEventListener('click', function() {
      // Only show modal if motor has moved (currentStep > 0)
      if (currentPositionMM > 0.5) {
        showStopModal();
      } else {
        // Direct stop if at position 0
        sendCommand(WS_CMD.STOP_CHAOS, {});
      }
    });
    
    // Pause chaos
    document.getElementById('btnPauseChaos').addEventListener('click', function() {
      sendCommand(WS_CMD.PAUSE);
    });
    
    // Chaos preset buttons
    document.querySelectorAll('[data-chaos-center]').forEach(btn => {
      btn.addEventListener('click', function() {
        if (!this.disabled) {
          document.getElementById('chaosCenterPos').value = this.dataset.chaosCenter;
          sendChaosConfig();  // Send on preset click
          updateChaosPresets();  // Update visual state
        }
      });
    });
    
    document.querySelectorAll('[data-chaos-amplitude]').forEach(btn => {
      btn.addEventListener('click', function() {
        if (!this.disabled) {
          document.getElementById('chaosAmplitude').value = this.dataset.chaosAmplitude;
          sendChaosConfig();  // Send on preset click
          updateChaosPresets();  // Update visual state
        }
      });
    });
    
    document.querySelectorAll('[data-chaos-speed]').forEach(btn => {
      btn.addEventListener('click', function() {
        document.getElementById('chaosMaxSpeed').value = this.dataset.chaosSpeed;
        sendChaosConfig();  // Send on preset click
      });
    });
    
    document.querySelectorAll('[data-chaos-duration]').forEach(btn => {
      btn.addEventListener('click', function() {
        document.getElementById('chaosDuration').value = this.dataset.chaosDuration;
        sendChaosConfig();  // Send on preset click
      });
    });
    
    document.querySelectorAll('[data-chaos-craziness]').forEach(btn => {
      btn.addEventListener('click', function() {
        document.getElementById('chaosCraziness').value = this.dataset.chaosCraziness;
        document.getElementById('crazinessValue').textContent = this.dataset.chaosCraziness;
        sendChaosConfig();  // Send on preset click
      });
    });
    
    // Update craziness value display and send config
    document.getElementById('chaosCraziness').addEventListener('input', function() {
      document.getElementById('crazinessValue').textContent = this.value;
    });
    
    // Send config on blur (when user finishes editing)
    document.getElementById('chaosCenterPos').addEventListener('blur', function() {
      sendChaosConfig();
      updateChaosPresets();  // Update visual state after manual edit
    });
    document.getElementById('chaosAmplitude').addEventListener('blur', function() {
      sendChaosConfig();
      updateChaosPresets();  // Update visual state after manual edit
    });
    document.getElementById('chaosMaxSpeed').addEventListener('blur', function() {
      sendChaosConfig();
    });
    document.getElementById('chaosCraziness').addEventListener('blur', function() {
      sendChaosConfig();
    });
    document.getElementById('chaosDuration').addEventListener('blur', function() {
      sendChaosConfig();
    });
    document.getElementById('chaosSeed').addEventListener('blur', function() {
      sendChaosConfig();
    });
    
    // Pattern preset buttons
    document.getElementById('btnEnableAllPatterns').addEventListener('click', function() {
      // Get all pattern checkboxes
      const patterns = [
        'patternZigzag', 'patternSweep', 'patternPulse', 'patternDrift',
        'patternBurst', 'patternWave', 'patternPendulum', 'patternSpiral',
        'patternCalm', 'patternBruteForce', 'patternLiberator'
      ];
      
      // Check if all are currently checked
      const allChecked = patterns.every(id => document.getElementById(id).checked);
      
      // Toggle: if all checked → uncheck all, otherwise check all
      const newState = !allChecked;
      patterns.forEach(id => {
        document.getElementById(id).checked = newState;
      });
      
      // Update button text immediately
      updatePatternToggleButton();
    });
    
    document.getElementById('btnEnableSoftPatterns').addEventListener('click', function() {
      // Doux (45%): WAVE, PENDULUM, SPIRAL, BREATHING
      document.getElementById('patternZigzag').checked = false;
      document.getElementById('patternSweep').checked = false;
      document.getElementById('patternPulse').checked = false;
      document.getElementById('patternDrift').checked = false;
      document.getElementById('patternBurst').checked = false;
      document.getElementById('patternWave').checked = true;
      document.getElementById('patternPendulum').checked = true;
      document.getElementById('patternSpiral').checked = true;
      document.getElementById('patternCalm').checked = true;
      document.getElementById('patternBruteForce').checked = false;
      document.getElementById('patternLiberator').checked = false;
      updatePatternToggleButton();
    });
    
    document.getElementById('btnEnableDynamicPatterns').addEventListener('click', function() {
      // Dynamiques (55%): ZIGZAG, SWEEP, PULSE, DRIFT, BURST, BRUTE_FORCE, LIBERATOR
      document.getElementById('patternZigzag').checked = true;
      document.getElementById('patternSweep').checked = true;
      document.getElementById('patternPulse').checked = true;
      document.getElementById('patternDrift').checked = true;
      document.getElementById('patternBurst').checked = true;
      document.getElementById('patternWave').checked = false;
      document.getElementById('patternPendulum').checked = false;
      document.getElementById('patternSpiral').checked = false;
      document.getElementById('patternCalm').checked = false;
      document.getElementById('patternBruteForce').checked = true;
      document.getElementById('patternLiberator').checked = true;
      updatePatternToggleButton();
    });
    
    // Add listeners to individual pattern checkboxes to update toggle button
    const patternIds = [
      'patternZigzag', 'patternSweep', 'patternPulse', 'patternDrift',
      'patternBurst', 'patternWave', 'patternPendulum', 'patternSpiral',
      'patternCalm', 'patternBruteForce', 'patternLiberator'
    ];
    patternIds.forEach(id => {
      document.getElementById(id).addEventListener('change', updatePatternToggleButton);
    });
    
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
      const section = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
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
        const section = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
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
          const section = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
          if (!section.classList.contains('collapsed')) {
            sendCyclePauseConfig();
          }
        });
      }
    });
    
    // Send cycle pause config (Mode Simple)
    function sendCyclePauseConfig() {
      const section = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
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
      const section = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
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
        const section = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
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
          const section = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
          if (!section.classList.contains('collapsed')) {
            sendCyclePauseConfigOsc();
          }
        });
      }
    });
    
    // Send cycle pause config (Mode Oscillation)
    function sendCyclePauseConfigOsc() {
      const section = document.querySelector('.section-collapsible:has(#cyclePauseOscHeaderText)');
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