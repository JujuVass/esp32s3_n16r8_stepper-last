/**
 * SequenceController.js - Sequence Table Management Module
 * 
 * Handles all sequence/playlist table operations:
 * - CRUD operations (add, edit, delete, duplicate lines)
 * - Drag & drop reordering
 * - Multi-select batch operations
 * - Import/Export sequences
 * - Line testing
 * - Real-time validation
 * 
 * Dependencies: app.js (AppState, WS_CMD), utils.js (sendCommand, showNotification)
 *               PlaylistController.js (PlaylistState, generatePresetTooltip, hidePlaylistTooltip)
 *               DOMManager.js (DOM, setButtonState)
 */

// ========================================================================
// SEQUENCE GLOBAL STATE
// ========================================================================
let sequenceLines = [];
let editingLineId = null;
let isLoadingEditForm = false;

// Drag & Drop state
let draggedLineId = null;
let draggedLineIndex = null;
let lastDragEnterTime = 0;

// Multi-select state
let selectedLineIds = new Set();
let lastSelectedIndex = null;

// ========================================================================
// VALIDATION FUNCTIONS
// ========================================================================

function validateSequencerLine(line, movementType) {
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  
  if (typeof validateSequencerLinePure === 'function') {
    return validateSequencerLinePure(line, movementType, effectiveMax);
  }
  
  console.warn('validateSequencerLinePure not available, skipping validation');
  return [];
}

// ========================================================================
// CRUD OPERATIONS
// ========================================================================

function addSequenceLine() {
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  
  let newLine;
  if (typeof buildSequenceLineDefaultsPure === 'function') {
    newLine = buildSequenceLineDefaultsPure(effectiveMax);
  } else {
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

function moveSequenceLine(lineId, direction) {
  sendCommand(WS_CMD.MOVE_SEQUENCE_LINE, { lineId: lineId, direction: direction });
}

function duplicateSequenceLine(lineId) {
  sendCommand(WS_CMD.DUPLICATE_SEQUENCE_LINE, { lineId: lineId });
}

function toggleSequenceLine(lineId, enabled) {
  sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: lineId, enabled: enabled });
}

function clearSequence() {
  if (confirm('Effacer toutes les lignes du tableau?')) {
    sendCommand(WS_CMD.CLEAR_SEQUENCE, {});
  }
}

// ========================================================================
// IMPORT / EXPORT
// ========================================================================

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
        jsonText = jsonText.replace(/\/\*[\s\S]*?\*\//g, '');
        jsonText = jsonText.replace(/\/\/.*/g, '');
        const parsed = JSON.parse(jsonText);
        
        console.log('📤 Sending import via HTTP:', parsed.lineCount, 'lines,', jsonText.length, 'bytes');
        
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
        alert('Erreur JSON: ' + error.message);
      }
    };
    reader.readAsText(file);
  };
  
  input.click();
}

function downloadTemplate() {
  let fullDoc;
  if (typeof getSequenceTemplateDocPure === 'function') {
    fullDoc = getSequenceTemplateDocPure();
  } else {
    fullDoc = {
      TEMPLATE: {
        version: "2.0",
        lineCount: 1,
        lines: [{
          lineId: 1, enabled: true, movementType: 4, cycleCount: 1, pauseAfterMs: 1000,
          startPositionMM: 0, distanceMM: 100, speedForward: 5.0, speedBackward: 5.0,
          decelStartEnabled: false, decelEndEnabled: false, decelZoneMM: 50.0, decelEffectPercent: 50.0, decelMode: 0,
          oscCenterPositionMM: 100.0, oscAmplitudeMM: 50.0, oscWaveform: 0, oscFrequencyHz: 0.1,
          oscEnableRampIn: false, oscEnableRampOut: false, oscRampInDurationMs: 1000.0, oscRampOutDurationMs: 1000.0,
          chaosCenterPositionMM: 110.0, chaosAmplitudeMM: 50.0, chaosMaxSpeedLevel: 10.0, chaosCrazinessPercent: 50.0,
          chaosDurationSeconds: 30, chaosSeed: 0, chaosPatternsEnabled: [true,true,true,true,true,true,true,true,true,true,true]
        }]
      },
      DOCUMENTATION: { "Note": "Template minimal - Voir sequencer.js pour la version complète" }
    };
  }
  
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

// ========================================================================
// LINE TESTING
// ========================================================================

function testSequenceLine(lineId) {
  const line = sequenceLines.find(l => l.lineId === lineId);
  if (!line) {
    showNotification('❌ Ligne introuvable', 'error', 2000);
    return;
  }
  
  if (AppState.sequencer && AppState.sequencer.isRunning) {
    showNotification('⚠️ Arrêtez la séquence en cours avant de tester', 'error', 3000);
    return;
  }
  
  window.sequenceBackup = sequenceLines.map(l => ({ 
    lineId: l.lineId, enabled: l.enabled, cycleCount: l.cycleCount 
  }));
  window.testedLineId = lineId;
  window.isTestingLine = true;
  
  console.log('🧪 Testing line #' + lineId + ' - Temporarily disabling other lines');
  
  sequenceLines.forEach(l => {
    if (l.lineId === lineId) {
      if (!l.enabled) {
        sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: l.lineId, enabled: true });
        l.enabled = true;
      }
    } else {
      if (l.enabled) {
        sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: l.lineId, enabled: false });
        l.enabled = false;
      }
    }
  });
  
  sequenceLines.forEach((l, idx) => {
    const row = document.querySelector(`tr[data-line-id="${l.lineId}"]`);
    if (row) {
      row.style.background = l.enabled ? 'white' : '#f5f5f5';
      row.style.opacity = l.enabled ? '1' : '0.6';
    }
  });
  
  document.querySelectorAll('[id^="btnTestLine_"]').forEach(btn => {
    btn.disabled = true;
    btn.style.opacity = '0.5';
    btn.style.cursor = 'not-allowed';
  });
  
  setTimeout(() => {
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

function restoreSequenceAfterTest() {
  console.log('🔄 restoreSequenceAfterTest called, isTestingLine=', window.isTestingLine, 'hasBackup=', !!window.sequenceBackup);
  
  // Always reset the flag, even if restore fails
  const wasTestingLine = window.isTestingLine;
  window.isTestingLine = false;
  
  if (!wasTestingLine || !window.sequenceBackup) {
    console.log('⚠️ No restore needed or no backup available');
    // Still enable buttons since test mode is now off
    if (DOM.btnStartSequence) setButtonState(DOM.btnStartSequence, canStartOperation());
    if (DOM.btnLoopSequence) setButtonState(DOM.btnLoopSequence, canStartOperation());
    return;
  }
  
  console.log('🔄 Restoring sequence original state');
  
  window.sequenceBackup.forEach(backup => {
    const line = sequenceLines.find(l => l.lineId === backup.lineId);
    if (line && line.enabled !== backup.enabled) {
      sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: backup.lineId, enabled: backup.enabled });
      line.enabled = backup.enabled;
    }
  });
  
  renderSequenceTable();
  
  if (DOM.btnStartSequence) {
    setButtonState(DOM.btnStartSequence, true);
    console.log('✅ btnStartSequence re-enabled after test');
  }
  if (DOM.btnLoopSequence) {
    setButtonState(DOM.btnLoopSequence, true);
    console.log('✅ btnLoopSequence re-enabled after test');
  }
  
  document.querySelectorAll('[id^="btnTestLine_"]').forEach(btn => {
    btn.disabled = false;
    btn.style.opacity = '1';
    btn.style.cursor = 'pointer';
  });
  window.sequenceBackup = null;
  window.testedLineId = null;
  
  showNotification('✅ Séquence restaurée', 'success', 2000);
}

// ========================================================================
// EDIT MODAL
// ========================================================================

function editSequenceLine(lineId) {
  console.log('🔍 editSequenceLine called with lineId:', lineId);
  const line = sequenceLines.find(l => l.lineId === lineId);
  if (!line) {
    console.error('❌ Line not found! lineId:', lineId);
    return;
  }
  
  console.log('✅ Found line:', line);
  editingLineId = lineId;
  isLoadingEditForm = true;
  
  clearErrorFields();
  const errorContainer = document.getElementById('editValidationErrors');
  if (errorContainer) errorContainer.style.display = 'none';
  
  document.getElementById('editLineNumber').textContent = sequenceLines.indexOf(line) + 1;
  
  const movementType = line.movementType !== undefined ? line.movementType : 0;
  if (movementType === 0) document.getElementById('editTypeVaet').checked = true;
  else if (movementType === 1) document.getElementById('editTypeOsc').checked = true;
  else if (movementType === 2) document.getElementById('editTypeChaos').checked = true;
  else if (movementType === 4) document.getElementById('editTypeCalibration').checked = true;
  updateMovementTypeFields();
  
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
  
  // VA-ET-VIENT Cycle Pause
  document.getElementById('editVaetPauseEnabled').checked = line.vaetCyclePauseEnabled || false;
  document.getElementById('editVaetPauseRandom').checked = line.vaetCyclePauseIsRandom || false;
  document.getElementById('editVaetPauseDuration').value = line.vaetCyclePauseDurationSec || 0.0;
  document.getElementById('editVaetPauseMin').value = line.vaetCyclePauseMinSec || 0.5;
  document.getElementById('editVaetPauseMax').value = line.vaetCyclePauseMaxSec || 3.0;
  
  // OSCILLATION Cycle Pause
  document.getElementById('editOscPauseEnabled').checked = line.oscCyclePauseEnabled || false;
  document.getElementById('editOscPauseRandom').checked = line.oscCyclePauseIsRandom || false;
  document.getElementById('editOscPauseDuration').value = line.oscCyclePauseDurationSec || 0.0;
  document.getElementById('editOscPauseMin').value = line.oscCyclePauseMinSec || 0.5;
  document.getElementById('editOscPauseMax').value = line.oscCyclePauseMaxSec || 3.0;
  
  document.getElementById('editVaetPauseEnabled').dispatchEvent(new Event('change'));
  document.getElementById('editOscPauseEnabled').dispatchEvent(new Event('change'));
  
  // CHAOS fields
  document.getElementById('editChaosCenter').value = line.chaosCenterPositionMM || 110;
  document.getElementById('editChaosAmplitude').value = line.chaosAmplitudeMM || 50;
  document.getElementById('editChaosSpeed').value = line.chaosMaxSpeedLevel || 10;
  document.getElementById('editChaosCraziness').value = line.chaosCrazinessPercent || 50;
  document.getElementById('editChaosDuration').value = line.chaosDurationSeconds || 30;
  document.getElementById('editChaosSeed').value = line.chaosSeed || 0;
  
  if (line.chaosPatternsEnabled && line.chaosPatternsEnabled.length === 11) {
    for (let i = 0; i < 11; i++) {
      const checkbox = document.querySelector(`input[name="chaosPattern${i}"]`);
      if (checkbox) checkbox.checked = line.chaosPatternsEnabled[i];
    }
  }
  
  // COMMON fields
  document.getElementById('editCycles').value = line.cycleCount || 1;
  document.getElementById('editPause').value = ((line.pauseAfterMs || 0) / 1000).toFixed(1);
  
  if (PlaylistState.loaded) {
    populateSequencerDropdown('simple');
    populateSequencerDropdown('oscillation');
    populateSequencerDropdown('chaos');
  }
  
  document.getElementById('editLineModal').style.display = 'block';
  isLoadingEditForm = false;
}

function saveLineEdit(event) {
  event.preventDefault();
  
  const form = document.getElementById('editLineForm');
  const movementType = parseInt(form.movementType.value);
  
  const updatedLine = {
    lineId: editingLineId,
    enabled: true,
    movementType: movementType,
    startPositionMM: parseFloat(form.startPositionMM.value),
    distanceMM: parseFloat(form.distanceMM.value),
    speedForward: parseFloat(form.speedForward.value),
    speedBackward: parseFloat(form.speedBackward.value),
    decelStartEnabled: form.decelStartEnabled.checked,
    decelEndEnabled: form.decelEndEnabled.checked,
    decelZoneMM: parseFloat(form.decelZoneMM.value),
    decelEffectPercent: parseFloat(form.decelEffectPercent.value),
    decelMode: parseInt(form.decelMode.value),
    oscCenterPositionMM: parseFloat(form.oscCenterPositionMM.value),
    oscAmplitudeMM: parseFloat(form.oscAmplitudeMM.value),
    oscWaveform: parseInt(form.oscWaveform.value),
    oscFrequencyHz: parseFloat(form.oscFrequencyHz.value),
    oscEnableRampIn: form.oscEnableRampIn.checked,
    oscEnableRampOut: form.oscEnableRampOut.checked,
    oscRampInDurationMs: parseFloat(form.oscRampInDurationMs.value),
    oscRampOutDurationMs: parseFloat(form.oscRampOutDurationMs.value),
    vaetCyclePauseEnabled: form.vaetCyclePauseEnabled.checked,
    vaetCyclePauseIsRandom: form.vaetCyclePauseIsRandom.checked,
    vaetCyclePauseDurationSec: parseFloat(form.vaetCyclePauseDurationSec.value),
    vaetCyclePauseMinSec: parseFloat(form.vaetCyclePauseMinSec.value),
    vaetCyclePauseMaxSec: parseFloat(form.vaetCyclePauseMaxSec.value),
    oscCyclePauseEnabled: form.oscCyclePauseEnabled.checked,
    oscCyclePauseIsRandom: form.oscCyclePauseIsRandom.checked,
    oscCyclePauseDurationSec: parseFloat(form.oscCyclePauseDurationSec.value),
    oscCyclePauseMinSec: parseFloat(form.oscCyclePauseMinSec.value),
    oscCyclePauseMaxSec: parseFloat(form.oscCyclePauseMaxSec.value),
    chaosCenterPositionMM: parseFloat(form.chaosCenterPositionMM.value),
    chaosAmplitudeMM: parseFloat(form.chaosAmplitudeMM.value),
    chaosMaxSpeedLevel: parseFloat(form.chaosMaxSpeedLevel.value),
    chaosCrazinessPercent: parseFloat(form.chaosCrazinessPercent.value),
    chaosDurationSeconds: parseInt(form.chaosDurationSeconds.value),
    chaosSeed: parseInt(form.chaosSeed.value),
    chaosPatternsEnabled: [
      form.chaosPattern0.checked, form.chaosPattern1.checked, form.chaosPattern2.checked,
      form.chaosPattern3.checked, form.chaosPattern4.checked, form.chaosPattern5.checked,
      form.chaosPattern6.checked, form.chaosPattern7.checked, form.chaosPattern8.checked,
      form.chaosPattern9.checked, form.chaosPattern10.checked
    ],
    cycleCount: parseInt(form.cycleCount.value),
    pauseAfterMs: Math.round(parseFloat(form.pauseAfterSec.value) * 1000)
  };
  
  const errors = validateSequencerLine(updatedLine, movementType);
  if (errors.length > 0) {
    console.error('Validation errors on save:', errors);
    validateEditForm();
    return;
  }
  
  sendCommand(WS_CMD.UPDATE_SEQUENCE_LINE, updatedLine);
  closeEditModal();
}

function validateEditForm() {
  if (isLoadingEditForm) return;
  
  const form = document.getElementById('editLineForm');
  const movementType = parseInt(form.movementType.value);
  const emptyFieldErrors = [];
  
  if (movementType === 0) {
    if (form.startPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ Position de départ est incorrect');
    if (form.distanceMM.value.trim() === '') emptyFieldErrors.push('⚠️ Distance est incorrect');
    if (form.speedForward.value.trim() === '') emptyFieldErrors.push('⚠️ Vitesse aller est incorrect');
    if (form.speedBackward.value.trim() === '') emptyFieldErrors.push('⚠️ Vitesse retour est incorrect');
    if (form.decelZoneMM.value.trim() === '') emptyFieldErrors.push('⚠️ Zone décélération est incorrect');
  }
  
  if (movementType === 1) {
    if (form.oscCenterPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ Centre oscillation est incorrect');
    if (form.oscAmplitudeMM.value.trim() === '') emptyFieldErrors.push('⚠️ Amplitude oscillation est incorrect');
    if (form.oscFrequencyHz.value.trim() === '') emptyFieldErrors.push('⚠️ Fréquence est incorrect');
    if (form.oscRampInDurationMs.value.trim() === '') emptyFieldErrors.push('⚠️ Durée rampe IN est incorrect');
    if (form.oscRampOutDurationMs.value.trim() === '') emptyFieldErrors.push('⚠️ Durée rampe OUT est incorrect');
  }
  
  if (movementType === 2) {
    if (form.chaosCenterPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ Centre chaos est incorrect');
    if (form.chaosAmplitudeMM.value.trim() === '') emptyFieldErrors.push('⚠️ Amplitude chaos est incorrect');
    if (form.chaosMaxSpeedLevel.value.trim() === '') emptyFieldErrors.push('⚠️ Vitesse max chaos est incorrect');
    if (form.chaosCrazinessPercent.value.trim() === '') emptyFieldErrors.push('⚠️ Degré de folie est incorrect');
    if (form.chaosDurationSeconds.value.trim() === '') emptyFieldErrors.push('⚠️ Durée chaos est incorrect');
    if (form.chaosSeed.value.trim() === '') emptyFieldErrors.push('⚠️ Seed est incorrect');
  }
  
  if (movementType !== 4) {
    if (movementType !== 2 && form.cycleCount.value.trim() === '') emptyFieldErrors.push('⚠️ Nombre de cycles est incorrect');
    if (form.pauseAfterSec.value.trim() === '') emptyFieldErrors.push('⚠️ Pause est incorrect');
  }
  
  const line = {
    movementType: movementType,
    startPositionMM: parseFloat(form.startPositionMM.value) || 0,
    distanceMM: parseFloat(form.distanceMM.value) || 0,
    speedForward: parseFloat(form.speedForward.value) || 0,
    speedBackward: parseFloat(form.speedBackward.value) || 0,
    decelZoneMM: parseFloat(form.decelZoneMM.value) || 0,
    oscCenterPositionMM: parseFloat(form.oscCenterPositionMM.value) || 0,
    oscAmplitudeMM: parseFloat(form.oscAmplitudeMM.value) || 0,
    oscFrequencyHz: parseFloat(form.oscFrequencyHz.value) || 0,
    oscRampInDurationMs: parseFloat(form.oscRampInDurationMs.value) || 0,
    oscRampOutDurationMs: parseFloat(form.oscRampOutDurationMs.value) || 0,
    chaosCenterPositionMM: parseFloat(form.chaosCenterPositionMM.value) || 0,
    chaosAmplitudeMM: parseFloat(form.chaosAmplitudeMM.value) || 0,
    chaosMaxSpeedLevel: parseFloat(form.chaosMaxSpeedLevel.value) || 0,
    chaosCrazinessPercent: parseFloat(form.chaosCrazinessPercent.value) || 0,
    chaosDurationSeconds: parseInt(form.chaosDurationSeconds.value) || 0,
    chaosSeed: parseInt(form.chaosSeed.value) || 0,
    cycleCount: parseInt(form.cycleCount.value) || 0,
    pauseAfterMs: Math.round(parseFloat(form.pauseAfterSec.value) * 1000) || 0
  };
  
  const validationErrors = validateSequencerLine(line, movementType);
  const errors = emptyFieldErrors.concat(validationErrors);
  
  const errorContainer = document.getElementById('editValidationErrors');
  const errorList = document.getElementById('editValidationErrorsList');
  const saveButton = document.getElementById('btnSaveEdit');
  
  if (errors.length > 0) {
    errorContainer.style.display = 'block';
    errorList.innerHTML = errors.map(err => '<li>' + err + '</li>').join('');
    saveButton.disabled = true;
    saveButton.style.opacity = '0.5';
    saveButton.style.cursor = 'not-allowed';
    highlightErrorFields(movementType, line, emptyFieldErrors);
  } else {
    errorContainer.style.display = 'none';
    errorList.innerHTML = '';
    saveButton.disabled = false;
    saveButton.style.opacity = '1';
    saveButton.style.cursor = 'pointer';
    clearErrorFields();
  }
}

function highlightErrorFields(movementType, line, emptyFieldErrors) {
  clearErrorFields();
  const effectiveMax = AppState.pursuit.effectiveMaxDistMM || AppState.pursuit.totalDistanceMM || 0;
  
  let invalidFieldIds = [];
  if (typeof getAllInvalidFieldsPure === 'function') {
    invalidFieldIds = getAllInvalidFieldsPure(line, movementType, effectiveMax, emptyFieldErrors);
  } else if (typeof getErrorFieldIdsPure === 'function') {
    invalidFieldIds = getErrorFieldIdsPure(emptyFieldErrors);
  }
  
  const errorStyle = '2px solid #f44336';
  invalidFieldIds.forEach(fieldId => {
    const field = document.getElementById(fieldId);
    if (field) field.style.border = errorStyle;
  });
}

function clearErrorFields() {
  const fields = (typeof ALL_EDIT_FIELDS !== 'undefined') ? ALL_EDIT_FIELDS : [
    'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack', 'editDecelZone',
    'editOscCenter', 'editOscAmplitude', 'editOscFrequency', 'editOscRampInDur', 'editOscRampOutDur',
    'editChaosCenter', 'editChaosAmplitude', 'editChaosSpeed', 'editChaosCraziness',
    'editChaosDuration', 'editChaosSeed', 'editCycles', 'editPause'
  ];
  fields.forEach(fieldId => {
    const field = document.getElementById(fieldId);
    if (field) field.style.border = '2px solid #ddd';
  });
}

function updateMovementTypeFields() {
  const isVaet = document.getElementById('editTypeVaet').checked;
  const isOsc = document.getElementById('editTypeOsc').checked;
  const isChaos = document.getElementById('editTypeChaos').checked;
  const isCalibration = document.getElementById('editTypeCalibration').checked;
  
  document.getElementById('vaetFields').style.display = isVaet ? 'block' : 'none';
  document.getElementById('oscFields').style.display = isOsc ? 'block' : 'none';
  document.getElementById('chaosFields').style.display = isChaos ? 'block' : 'none';
  
  document.getElementById('playlistLoaderSimple').style.display = isVaet ? 'block' : 'none';
  document.getElementById('playlistLoaderOscillation').style.display = isOsc ? 'block' : 'none';
  document.getElementById('playlistLoaderChaos').style.display = isChaos ? 'block' : 'none';
  
  document.getElementById('cyclesFieldDiv').style.display = (isChaos || isCalibration) ? 'none' : 'block';
  document.getElementById('pauseFieldDiv').style.display = 'block';
  
  validateEditForm();
}

function closeEditModal() {
  document.getElementById('editLineModal').style.display = 'none';
  editingLineId = null;
  clearErrorFields();
  const errorContainer = document.getElementById('editValidationErrors');
  if (errorContainer) errorContainer.style.display = 'none';
  
  const saveButton = document.getElementById('btnSaveEdit');
  if (saveButton) {
    saveButton.disabled = false;
    saveButton.style.opacity = '1';
    saveButton.style.cursor = 'pointer';
  }
}

function closeEditLineModalOnOverlayClick(event) {
  if (event.target.id === 'editLineModal') {
    closeEditModal();
  }
}

// ========================================================================
// PLAYLIST INTEGRATION IN SEQUENCER
// ========================================================================

function populateSequencerDropdown(mode) {
  const selectId = 'edit' + mode.charAt(0).toUpperCase() + mode.slice(1) + 'PresetSelect';
  const select = document.getElementById(selectId);
  if (!select) return;
  
  const presets = PlaylistState[mode] || [];
  select.innerHTML = '<option value="">-- Sélectionner un preset --</option>';
  
  const sortedPresets = [...presets].sort((a, b) => b.timestamp - a.timestamp);
  sortedPresets.forEach(preset => {
    const option = document.createElement('option');
    option.value = preset.id;
    option.textContent = preset.name;
    select.appendChild(option);
  });
}

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
    setTimeout(() => { overlay.classList.remove('visible'); }, 5000);
  }
}

// ========================================================================
// BATCH OPERATIONS & SELECTION
// ========================================================================

function clearSelection() {
  selectedLineIds.clear();
  lastSelectedIndex = null;
  updateBatchToolbar();
  renderSequenceTable({ lines: sequenceLines });
}

function batchEnableLines(enabled) {
  if (selectedLineIds.size === 0) return;
  
  console.log(`📦 Batch ${enabled ? 'enable' : 'disable'} ${selectedLineIds.size} lines`);
  
  selectedLineIds.forEach(lineId => {
    sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: lineId, enabled: enabled });
    const line = sequenceLines.find(l => l.lineId === lineId);
    if (line) line.enabled = enabled;
  });
  
  showNotification(`✅ ${selectedLineIds.size} ligne(s) ${enabled ? 'activée(s)' : 'désactivée(s)'}`, 'success', 2000);
  clearSelection();
}

function batchDeleteLines() {
  if (selectedLineIds.size === 0) return;
  
  const count = selectedLineIds.size;
  if (!confirm(`⚠️ Supprimer ${count} ligne(s) sélectionnée(s) ?\n\nCette action est irréversible.`)) return;
  
  console.log(`📦 Batch delete ${count} lines`);
  
  const lineIdsArray = Array.from(selectedLineIds).sort((a, b) => b - a);
  lineIdsArray.forEach(lineId => {
    sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
  });
  
  showNotification(`✅ ${count} ligne(s) supprimée(s)`, 'success', 2000);
  clearSelection();
}

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

// ========================================================================
// TRASH ZONES
// ========================================================================

function initializeTrashZones() {
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
      if (!this.contains(e.relatedTarget)) this.classList.remove('drag-over');
    };
    
    trashZone.ondrop = function(e) {
      e.preventDefault();
      e.stopPropagation();
      this.classList.remove('drag-over');
      
      const linesToDelete = selectedLineIds.size > 0 ? Array.from(selectedLineIds) : [draggedLineId];
      if (linesToDelete.length === 0) return;
      
      const count = linesToDelete.length;
      const message = count === 1 ? 
        `⚠️ Supprimer la ligne sélectionnée ?` :
        `⚠️ Supprimer ${count} lignes sélectionnées ?`;
      
      if (!confirm(message)) return;
      
      console.log(`🗑️ Trash zone drop: deleting ${count} line(s)`);
      const sortedIds = linesToDelete.sort((a, b) => b - a);
      sortedIds.forEach(lineId => sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId }));
      
      showNotification(`✅ ${count} ligne(s) supprimée(s)`, 'success', 2000);
      clearSelection();
      return false;
    };
  }
  
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
      
      const linesToDelete = selectedLineIds.size > 0 ? Array.from(selectedLineIds) : [draggedLineId];
      if (linesToDelete.length === 0) return;
      
      const count = linesToDelete.length;
      const message = count === 1 ? 
        `⚠️ Supprimer la ligne sélectionnée ?` :
        `⚠️ Supprimer ${count} lignes sélectionnées ?`;
      
      if (!confirm(message)) return;
      
      console.log(`🗑️ Permanent trash zone drop: deleting ${count} line(s)`);
      const sortedIds = linesToDelete.sort((a, b) => b - a);
      sortedIds.forEach(lineId => sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId }));
      
      showNotification(`✅ ${count} ligne(s) supprimée(s)`, 'success', 2000);
      clearSelection();
      return false;
    };
  }
}

// ========================================================================
// STATUS UPDATE
// ========================================================================

function updateSequenceStatus(status) {
  if (!status) return;
  
  const modeText = status.isRunning 
    ? (status.isLoopMode ? '🔁 BOUCLE INFINIE' : '▶️ LECTURE UNIQUE')
    : '⏹️ Arrêté';
  DOM.seqMode.textContent = modeText;
  DOM.seqMode.style.color = status.isRunning ? '#4CAF50' : '#999';
  
  const lineText = status.isRunning 
    ? `${status.currentLineNumber} / ${status.totalLines}`
    : '-- / --';
  DOM.seqCurrentLine.textContent = lineText;
  
  DOM.seqLineCycle.textContent = status.isRunning ? status.currentCycle : '--';
  DOM.seqLoopCount.textContent = status.loopCount || 0;
  
  const pauseText = status.pauseRemaining > 0 ? `${status.pauseRemaining} ms` : '-- ms';
  DOM.seqPauseRemaining.textContent = pauseText;
  
  const isRunning = status.isRunning;
  
  if (!isRunning && window.isTestingLine) {
    setTimeout(() => { restoreSequenceAfterTest(); }, 500);
  }
  
  if (!isRunning && !window.isTestingLine) {
    // When sequence ends normally, force canStart to true (backend confirmed sequence is done)
    AppState.system.canStart = true;
    const canStart = canStartOperation();
    setButtonState(DOM.btnStartSequence, canStart);
    setButtonState(DOM.btnLoopSequence, canStart);
  } else {
    setButtonState(DOM.btnStartSequence, false);
    setButtonState(DOM.btnLoopSequence, false);
  }
  
  setButtonState(DOM.btnPauseSequence, isRunning);
  setButtonState(DOM.btnStopSequence, isRunning);
  setButtonState(DOM.btnSkipLine, isRunning);
  
  if (isRunning && status.isPaused) {
    DOM.btnPauseSequence.innerHTML = '▶️ Reprendre';
  } else {
    DOM.btnPauseSequence.innerHTML = '⏸️ Pause';
  }
  
  const tbody = document.getElementById('sequenceTableBody');
  if (tbody) {
    const rows = tbody.querySelectorAll('tr');
    rows.forEach(row => row.classList.remove('sequence-line-active'));
    
    if (isRunning && status.currentLineIndex !== undefined) {
      const activeIndex = status.currentLineIndex;
      if (activeIndex >= 0 && activeIndex < rows.length) {
        rows[activeIndex].classList.add('sequence-line-active');
        rows[activeIndex].scrollIntoView({ behavior: 'smooth', block: 'nearest' });
      }
    }
  }
}

// ========================================================================
// RENDER SEQUENCE TABLE
// ========================================================================

function renderSequenceTable(data) {
  if (data && data.lines) {
    sequenceLines = data.lines;
  } else if (!sequenceLines || sequenceLines.length === 0) {
    console.error('Invalid sequence data');
    return;
  }
  
  // Migration: convert old microsecond values to speedLevel
  sequenceLines.forEach(line => {
    if (line.speedForward > 20) {
      line.speedForward = Math.max(1, Math.min(20, 21 - line.speedForward / 25));
    }
    if (line.speedBackward > 20) {
      line.speedBackward = Math.max(1, Math.min(20, 21 - line.speedBackward / 25));
    }
  });
  
  const tbody = document.getElementById('sequenceTableBody');
  tbody.innerHTML = '';
  
  if (sequenceLines.length === 0) {
    tbody.innerHTML = `
      <tr><td colspan="9" style="padding: 40px; text-align: center; color: #999;">
        <div style="font-size: 48px; margin-bottom: 10px;">📋</div>
        <div style="font-size: 16px;">Aucune ligne - Cliquez sur "➕ Ajouter ligne" pour commencer</div>
      </td></tr>
    `;
    return;
  }
  
  sequenceLines.forEach((line, index) => {
    const row = createSequenceRow(line, index);
    tbody.appendChild(row);
    
    setTimeout(() => {
      const eyeIcon = document.getElementById('tooltipEye_' + line.lineId);
      if (eyeIcon) {
        eyeIcon.onmouseenter = function(e) { showSequenceTooltip(row); };
        eyeIcon.onmouseleave = function() { hidePlaylistTooltip(); };
      }
    }, 0);
  });
  
  tbody.ondragleave = function(e) {
    if (!this.contains(e.relatedTarget)) {
      const spacer = document.querySelector('.sequence-drop-spacer');
      if (spacer) spacer.remove();
    }
  };
  
  updateBatchToolbar();
  initializeTrashZones();
  
  if (window.isTestingLine) {
    document.querySelectorAll('[id^="btnTestLine_"]').forEach(btn => {
      btn.disabled = true;
      btn.style.opacity = '0.5';
      btn.style.cursor = 'not-allowed';
    });
    if (DOM.btnStartSequence) setButtonState(DOM.btnStartSequence, false);
    if (DOM.btnLoopSequence) setButtonState(DOM.btnLoopSequence, false);
  }
}

function createSequenceRow(line, index) {
  const row = document.createElement('tr');
  row.style.background = line.enabled ? 'white' : '#f5f5f5';
  row.style.opacity = line.enabled ? '1' : '0.6';
  row.style.borderBottom = '1px solid #ddd';
  row.style.transition = 'all 0.2s';
  
  row.draggable = true;
  row.classList.add('sequence-line-draggable');
  row.setAttribute('data-line-id', line.lineId);
  row.setAttribute('data-line-index', index);
  
  if (selectedLineIds.has(line.lineId)) {
    row.classList.add('sequence-line-selected');
  }
  
  const tooltipContent = generateSequenceLineTooltip(line);
  row.setAttribute('data-tooltip', tooltipContent.replace(/"/g, '&quot;'));
  row.setAttribute('data-line-number', index + 1);
  
  const movementType = line.movementType !== undefined ? line.movementType : 0;
  const typeDisplay = getTypeDisplay(movementType, line);
  const decelSummary = getDecelSummary(line, movementType);
  const speedsDisplay = getSpeedsDisplay(line, movementType);
  const { cyclesDisplay, pauseDisplay, pauseColor, pauseWeight } = getCyclesPause(line, movementType);
  
  row.innerHTML = `
    <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
      <input type="checkbox" ${line.enabled ? 'checked' : ''} 
        onchange="toggleSequenceLine(${line.lineId}, this.checked)"
        style="width: 16px; height: 16px; cursor: pointer;">
    </td>
    <td style="padding: 4px 2px; text-align: center; font-weight: bold; color: #667eea; border-right: 1px solid #eee; font-size: 12px;">
      ${index + 1}
    </td>
    <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;" title="${typeDisplay.typeName}">
      <div style="font-size: 16px;">${typeDisplay.typeIcon}</div>
    </td>
    <td style="padding: 4px 2px; text-align: center; border-right: 1px solid #eee;">
      ${typeDisplay.typeInfo}
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
  
  row.setAttribute('data-line-type', typeDisplay.typeName);
  
  // Event handlers
  attachRowEventHandlers(row, line, index);
  
  return row;
}

function getTypeDisplay(movementType, line) {
  let typeIcon = '', typeInfo = '', typeName = '';
  
  if (movementType === 0) {
    typeIcon = '↔️';
    typeName = 'Va-et-vient';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>${line.startPositionMM.toFixed(1)}mm</div>
      <div>±${line.distanceMM.toFixed(1)}mm</div>
    </div>`;
  } else if (movementType === 1) {
    typeIcon = '〰️';
    typeName = 'Oscillation';
    const waveformNames = ['SIN', 'TRI', 'SQR'];
    const waveformName = waveformNames[line.oscWaveform] || '?';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>C:${line.oscCenterPositionMM ? line.oscCenterPositionMM.toFixed(0) : '100'}mm</div>
      <div>A:±${line.oscAmplitudeMM ? line.oscAmplitudeMM.toFixed(0) : '50'}mm</div>
      <div>${waveformName} ${line.oscFrequencyHz ? line.oscFrequencyHz.toFixed(2) : '0.5'}Hz</div>
    </div>`;
  } else if (movementType === 2) {
    typeIcon = '🌀';
    typeName = 'Chaos';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>⏱️${line.chaosDurationSeconds || 30}s</div>
      <div>🎲${line.chaosCrazinessPercent ? line.chaosCrazinessPercent.toFixed(0) : '50'}%</div>
    </div>`;
  } else if (movementType === 4) {
    typeIcon = '📏';
    typeName = 'Calibration';
    typeInfo = `<div style="font-size: 10px; line-height: 1.2;">
      <div>Calibration</div>
      <div>complète</div>
    </div>`;
  }
  
  return { typeIcon, typeInfo, typeName };
}

function getDecelSummary(line, movementType) {
  if (movementType === 0 && (line.decelStartEnabled || line.decelEndEnabled)) {
    const parts = [];
    if (line.decelStartEnabled) parts.push('D');
    if (line.decelEndEnabled) parts.push('F');
    const modeLabels = ['Lin', 'Sin', 'Tri⁻¹', 'Sin⁻¹'];
    return `<div style="font-size: 10px; line-height: 1.3;">
      <div style="color: #4CAF50; font-weight: bold;">${parts.join('/')}</div>
      <div>${line.decelZoneMM}mm ${line.decelEffectPercent}%</div>
      <div>${modeLabels[line.decelMode] || '?'}</div>
    </div>`;
  }
  return '<span style="color: #999; font-size: 10px;">--</span>';
}

function getSpeedsDisplay(line, movementType) {
  if (movementType === 0) {
    return `<div style="font-size: 11px; line-height: 1.3;">
      <div style="color: #2196F3; font-weight: bold;">↗${line.speedForward.toFixed(1)}</div>
      <div style="color: #FF9800; font-weight: bold;">↙${line.speedBackward.toFixed(1)}</div>
    </div>`;
  } else if (movementType === 1) {
    const peakSpeedMMPerSec = 2 * Math.PI * line.oscFrequencyHz * line.oscAmplitudeMM;
    return `<div style="font-size: 11px; font-weight: bold; color: #9C27B0;">${peakSpeedMMPerSec.toFixed(0)} mm/s</div>`;
  } else if (movementType === 2) {
    return `<div style="font-size: 11px; font-weight: bold; color: #E91E63;">${line.chaosMaxSpeedLevel ? line.chaosMaxSpeedLevel.toFixed(1) : '10.0'}</div>`;
  }
  return '<span style="color: #999;">--</span>';
}

function getCyclesPause(line, movementType) {
  let cyclesDisplay = line.cycleCount;
  if (movementType === 2) cyclesDisplay = '--';
  
  const pauseMs = line.pauseAfterMs || 0;
  const pauseDisplay = pauseMs > 0 ? (pauseMs / 1000).toFixed(1) + 's' : '--';
  const pauseColor = pauseMs > 0 ? '#9C27B0' : '#999';
  const pauseWeight = pauseMs > 0 ? 'bold' : 'normal';
  
  return { cyclesDisplay, pauseDisplay, pauseColor, pauseWeight };
}

function attachRowEventHandlers(row, line, index) {
  // Hover effect
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
  
  // Drag start
  row.ondragstart = function(e) {
    draggedLineId = line.lineId;
    draggedLineIndex = index;
    this.classList.add('sequence-line-dragging');
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', line.lineId);
    
    const trashDropZone = document.getElementById('sequenceTrashDropZone');
    if (trashDropZone) trashDropZone.classList.add('drag-active');
  };
  
  // Drag end
  row.ondragend = function(e) {
    this.classList.remove('sequence-line-dragging');
    const spacer = document.querySelector('.sequence-drop-spacer');
    if (spacer) spacer.remove();
    
    const trashDropZone = document.getElementById('sequenceTrashDropZone');
    if (trashDropZone) trashDropZone.classList.remove('drag-active');
  };
  
  // Drag over
  row.ondragover = function(e) {
    if (draggedLineId === line.lineId) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    
    const rect = this.getBoundingClientRect();
    const mouseY = e.clientY;
    const rowMiddle = rect.top + (rect.height / 2);
    
    let insertAfter = (mouseY > rowMiddle);
    let finalTargetIndex = insertAfter ? index : index - 1;
    
    if (finalTargetIndex === draggedLineIndex || index === draggedLineIndex) {
      const existingSpacer = document.querySelector('.sequence-drop-spacer');
      if (existingSpacer) existingSpacer.remove();
      return false;
    }
    
    const now = Date.now();
    if (now - lastDragEnterTime < 200) return false;
    lastDragEnterTime = now;
    
    const existingSpacer = document.querySelector('.sequence-drop-spacer');
    if (existingSpacer) existingSpacer.remove();
    
    const spacer = document.createElement('tr');
    spacer.className = 'sequence-drop-spacer';
    spacer.innerHTML = '<td colspan="10" style="height: 50px; padding: 0; border: none; background: transparent;"><div class="sequence-drop-placeholder-inner">⬇ Insérer ici ⬇</div></td>';
    spacer.dataset.targetLineId = line.lineId;
    spacer.dataset.targetIndex = index;
    
    spacer.ondragover = function(e) {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'move';
      return false;
    };
    
    spacer.ondrop = function(e) {
      e.preventDefault();
      e.stopPropagation();
      const targetRow = document.querySelector(`[data-line-id="${this.dataset.targetLineId}"]`);
      if (targetRow && targetRow.ondrop) targetRow.ondrop(e);
      return false;
    };
    
    if (insertAfter) {
      this.parentNode.insertBefore(spacer, this.nextSibling);
    } else {
      this.parentNode.insertBefore(spacer, this);
    }
    
    return false;
  };
  
  row.ondragenter = function(e) {
    if (draggedLineId === line.lineId) return;
    e.preventDefault();
  };
  
  row.ondragleave = function(e) { /* Keep spacer visible */ };
  
  // Drop
  row.ondrop = function(e) {
    e.stopPropagation();
    e.preventDefault();
    
    const spacer = document.querySelector('.sequence-drop-spacer');
    if (spacer) spacer.remove();
    
    if (draggedLineId && draggedLineId !== line.lineId) {
      const targetIndex = index;
      let direction;
      
      if (draggedLineIndex < targetIndex) {
        direction = targetIndex - draggedLineIndex;
      } else {
        direction = -(draggedLineIndex - targetIndex);
      }
      
      console.log(`📦 Drag: line ${draggedLineId} from index ${draggedLineIndex} to ${targetIndex} (${direction} moves)`);
      
      let movesRemaining = Math.abs(direction);
      const moveDirection = direction > 0 ? 1 : -1;
      
      const executeMove = () => {
        if (movesRemaining > 0) {
          sendCommand(WS_CMD.MOVE_SEQUENCE_LINE, { lineId: draggedLineId, direction: moveDirection });
          movesRemaining--;
          setTimeout(executeMove, 100);
        } else {
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
  
  // Multi-select click
  row.onclick = function(e) {
    if (e.target.tagName === 'BUTTON' || e.target.tagName === 'INPUT') return;
    
    if (e.shiftKey && lastSelectedIndex !== null) {
      const startIdx = Math.min(lastSelectedIndex, index);
      const endIdx = Math.max(lastSelectedIndex, index);
      for (let i = startIdx; i <= endIdx; i++) {
        if (i < sequenceLines.length) {
          selectedLineIds.add(sequenceLines[i].lineId);
        }
      }
    } else if (e.ctrlKey || e.metaKey) {
      if (selectedLineIds.has(line.lineId)) {
        selectedLineIds.delete(line.lineId);
      } else {
        selectedLineIds.add(line.lineId);
      }
    } else {
      selectedLineIds.clear();
      selectedLineIds.add(line.lineId);
    }
    
    lastSelectedIndex = index;
    updateBatchToolbar();
    renderSequenceTable({ lines: sequenceLines });
  };
}

