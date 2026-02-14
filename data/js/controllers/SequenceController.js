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
 * 
 * State: Centralized in AppState.sequence:
 *   - lines: Array of sequence line objects
 *   - editingLineId: Currently editing line ID
 *   - isLoadingEditForm: Loading edit form flag
 *   - selectedIds: Set of selected line IDs
 *   - lastSelectedIndex: Last selected index for shift-click
 *   - drag.lineId, drag.lineIndex, drag.lastEnterTime: Drag state
 */

// ========================================================================
// STATE ALIASES (for cleaner code, points to AppState.sequence)
// ========================================================================
// These provide shorthand access to AppState.sequence properties
const seqState = AppState.sequence;

// Helper getters for commonly accessed properties
function getSequenceLines() { return seqState.lines; }
function setSequenceLines(lines) { seqState.lines = lines; }

// Backward compatibility aliases (will be removed in future refactoring)
// Using Object.defineProperty to create live references
Object.defineProperty(window, 'sequenceLines', {
  get: function() { return seqState.lines; },
  set: function(val) { seqState.lines = val; }
});
Object.defineProperty(window, 'editingLineId', {
  get: function() { return seqState.editingLineId; },
  set: function(val) { seqState.editingLineId = val; }
});
Object.defineProperty(window, 'isLoadingEditForm', {
  get: function() { return seqState.isLoadingEditForm; },
  set: function(val) { seqState.isLoadingEditForm = val; }
});
Object.defineProperty(window, 'selectedLineIds', {
  get: function() { return seqState.selectedIds; },
  set: function(val) { seqState.selectedIds = val; }
});
Object.defineProperty(window, 'lastSelectedIndex', {
  get: function() { return seqState.lastSelectedIndex; },
  set: function(val) { seqState.lastSelectedIndex = val; }
});
Object.defineProperty(window, 'draggedLineId', {
  get: function() { return seqState.drag.lineId; },
  set: function(val) { seqState.drag.lineId = val; }
});
Object.defineProperty(window, 'draggedLineIndex', {
  get: function() { return seqState.drag.lineIndex; },
  set: function(val) { seqState.drag.lineIndex = val; }
});
Object.defineProperty(window, 'lastDragEnterTime', {
  get: function() { return seqState.drag.lastEnterTime; },
  set: function(val) { seqState.drag.lastEnterTime = val; }
});

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
      vaetZoneEffect: {
        enabled: false,
        enableStart: true,
        enableEnd: true,
        mirrorOnReturn: false,
        zoneMM: 50,
        speedEffect: 1,
        speedCurve: 1,
        speedIntensity: 75,
        randomTurnbackEnabled: false,
        turnbackChance: 30,
        endPauseEnabled: false,
        endPauseIsRandom: false,
        endPauseDurationSec: 1.0,
        endPauseMinSec: 0.5,
        endPauseMaxSec: 2.0
      },
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
    showAlert(t('sequencer.addValidationFailed') + '\n\n' + errors.join('\n'), { type: 'error', title: 'Validation' });
    return;
  }
  
  sendCommand(WS_CMD.ADD_SEQUENCE_LINE, newLine);
}

async function deleteSequenceLine(lineId) {
  const confirmed = await showConfirm(t('sequencer.deleteLineConfirm'), {
    title: t('sequencer.deleteLineTitle'),
    type: 'danger',
    confirmText: '🗑️ ' + t('common.delete'),
    dangerous: true
  });
  if (confirmed) {
    sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
  }
}

function moveSequenceLine(lineId, direction) {
  sendCommand(WS_CMD.MOVE_SEQUENCE_LINE, { lineId: lineId, direction: direction });
}

function reorderSequenceLine(lineId, newIndex) {
  sendCommand(WS_CMD.REORDER_SEQUENCE_LINE, { lineId: lineId, newIndex: newIndex });
}

function duplicateSequenceLine(lineId) {
  sendCommand(WS_CMD.DUPLICATE_SEQUENCE_LINE, { lineId: lineId });
}

function toggleSequenceLine(lineId, enabled) {
  sendCommand(WS_CMD.TOGGLE_SEQUENCE_LINE, { lineId: lineId, enabled: enabled });
}

async function clearSequence() {
  const confirmed = await showConfirm(t('sequencer.clearConfirm'), {
    title: t('sequencer.clearTitle'),
    type: 'danger',
    confirmText: '🗑️ ' + t('sequencer.clearAll'),
    dangerous: true
  });
  if (confirmed) {
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
        
        postWithRetry('/api/sequence/import', parsed)
        .then(data => {
          if (data.success) {
            console.log('✅ Import successful:', data.message);
            showAlert(t('sequencer.importSuccess'), { type: 'success' });
            sendCommand(WS_CMD.GET_SEQUENCE_TABLE, {});
          } else {
            console.error('❌ Import failed:', data.error);
            showAlert(t('common.error') + ' import: ' + (data.error || 'Unknown error'), { type: 'error' });
          }
        })
        .catch(error => {
          console.error('❌ HTTP request failed:', error);
          showAlert(t('sequencer.networkError', {msg: error.message}), { type: 'error' });
        });
        
      } catch (error) {
        console.error('❌ JSON parse error:', error);
        showAlert(t('common.error') + ' JSON: ' + error.message, { type: 'error' });
      }
    };
    reader.readAsText(file);
  };
  
  input.click();
}

function downloadTemplate() {
  // Use helper from SequenceUtils.js
  const fullDoc = getSequenceTemplateDoc();
  
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
  
  showNotification('📄 ' + t('sequencer.templateDownloaded'), 'success', 3000);
}

// ========================================================================
// LINE TESTING
// ========================================================================

function testSequenceLine(lineId) {
  const line = sequenceLines.find(l => l.lineId === lineId);
  if (!line) {
    showNotification('❌ ' + t('sequencer.lineNotFound'), 'error', 2000);
    return;
  }
  
  if (AppState.sequencer && AppState.sequencer.isRunning) {
    showNotification('⚠️ ' + t('sequencer.stopSequenceFirst'), 'error', 3000);
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
  
  showNotification('✅ ' + t('sequencer.sequenceRestored'), 'success', 2000);
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
  
  // Zone Effects - Map from vaetZoneEffect or legacy fields
  let ze = line.vaetZoneEffect;
  if (!ze) {
    // Legacy format - convert
    ze = {
      enabled: line.decelStartEnabled || line.decelEndEnabled,
      enableStart: line.decelStartEnabled ?? false,
      enableEnd: line.decelEndEnabled ?? true,
      zoneMM: line.decelZoneMM || 50,
      speedEffect: 1,
      speedCurve: line.decelMode ?? 1,
      speedIntensity: line.decelEffectPercent || 75,
      randomTurnbackEnabled: false,
      turnbackChance: 30,
      endPauseEnabled: false,
      endPauseIsRandom: false,
      endPauseDurationSec: 1.0,
      endPauseMinSec: 0.5,
      endPauseMaxSec: 2.0
    };
  }
  
  // Apply ALL Zone Effects to edit modal
  document.getElementById('editZoneEnableStart').checked = ze.enableStart || false;
  document.getElementById('editZoneEnableEnd').checked = ze.enableEnd ?? true;
  document.getElementById('editZoneMirror').checked = ze.mirrorOnReturn || false;
  document.getElementById('editZoneMM').value = ze.zoneMM || 50;
  document.getElementById('editSpeedEffect').value = ze.speedEffect ?? 1;
  document.getElementById('editSpeedCurve').value = ze.speedCurve ?? 1;
  document.getElementById('editSpeedIntensity').value = ze.speedIntensity || 75;
  document.getElementById('editSpeedIntensityValue').textContent = (ze.speedIntensity || 75) + '%';
  document.getElementById('editRandomTurnback').checked = ze.randomTurnbackEnabled || false;
  document.getElementById('editTurnbackChance').value = ze.turnbackChance || 30;
  document.getElementById('editTurnbackChanceValue').textContent = (ze.turnbackChance || 30) + '%';
  document.getElementById('editEndPauseEnabled').checked = ze.endPauseEnabled || false;
  document.getElementById('editEndPauseModeFixed').checked = !ze.endPauseIsRandom;
  document.getElementById('editEndPauseModeRandom').checked = ze.endPauseIsRandom || false;
  document.getElementById('editEndPauseDuration').value = ze.endPauseDurationSec || 1.0;
  document.getElementById('editEndPauseMin').value = ze.endPauseMinSec || 0.5;
  document.getElementById('editEndPauseMax').value = ze.endPauseMaxSec || 2.0;
  // Toggle visibility of fixed/random pause divs
  document.getElementById('editEndPauseFixedDiv').style.display = ze.endPauseIsRandom ? 'none' : 'flex';
  document.getElementById('editEndPauseRandomDiv').style.display = ze.endPauseIsRandom ? 'flex' : 'none';
  
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
  
  // Build FULL vaetZoneEffect from modal fields
  const vaetZoneEffect = {
    enabled: form.zoneEnableStart.checked || form.zoneEnableEnd.checked,
    enableStart: form.zoneEnableStart.checked,
    enableEnd: form.zoneEnableEnd.checked,
    mirrorOnReturn: form.zoneMirrorOnReturn.checked,
    zoneMM: parseFloat(form.zoneMM.value),
    speedEffect: parseInt(form.speedEffect.value),
    speedCurve: parseInt(form.speedCurve.value),
    speedIntensity: parseFloat(form.speedIntensity.value),
    randomTurnbackEnabled: form.randomTurnbackEnabled.checked,
    turnbackChance: parseInt(form.turnbackChance.value),
    endPauseEnabled: form.endPauseEnabled.checked,
    endPauseIsRandom: form.elements['editEndPauseMode'].value === 'random',
    endPauseDurationSec: parseFloat(form.endPauseDurationSec.value),
    endPauseMinSec: parseFloat(form.endPauseMinSec.value),
    endPauseMaxSec: parseFloat(form.endPauseMaxSec.value)
  };
  
  const updatedLine = {
    lineId: editingLineId,
    enabled: true,
    movementType: movementType,
    startPositionMM: parseFloat(form.startPositionMM.value),
    distanceMM: parseFloat(form.distanceMM.value),
    speedForward: parseFloat(form.speedForward.value),
    speedBackward: parseFloat(form.speedBackward.value),
    vaetZoneEffect: vaetZoneEffect,
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
    if (form.startPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.startPosIncorrect'));
    if (form.distanceMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.distanceIncorrect'));
    if (form.speedForward.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.speedFwdIncorrect'));
    if (form.speedBackward.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.speedBwdIncorrect'));
    if (form.zoneMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.zoneIncorrect'));
  }
  
  if (movementType === 1) {
    if (form.oscCenterPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.oscCenterIncorrect'));
    if (form.oscAmplitudeMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.oscAmplitudeIncorrect'));
    if (form.oscFrequencyHz.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.freqIncorrect'));
    if (form.oscRampInDurationMs.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.rampInIncorrect'));
    if (form.oscRampOutDurationMs.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.rampOutIncorrect'));
  }
  
  if (movementType === 2) {
    if (form.chaosCenterPositionMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.chaosCenterIncorrect'));
    if (form.chaosAmplitudeMM.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.chaosAmplitudeIncorrect'));
    if (form.chaosMaxSpeedLevel.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.chaosSpeedIncorrect'));
    if (form.chaosCrazinessPercent.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.crazinessIncorrect'));
    if (form.chaosDurationSeconds.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.durationIncorrect'));
    if (form.chaosSeed.value.trim() === '') emptyFieldErrors.push('⚠️ Seed incorrect');
  }
  
  if (movementType !== 4) {
    if (movementType !== 2 && form.cycleCount.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.cyclesIncorrect'));
    if (form.pauseAfterSec.value.trim() === '') emptyFieldErrors.push('⚠️ ' + t('sequencer.pauseIncorrect'));
  }
  
  const line = {
    movementType: movementType,
    startPositionMM: parseFloat(form.startPositionMM.value) || 0,
    distanceMM: parseFloat(form.distanceMM.value) || 0,
    speedForward: parseFloat(form.speedForward.value) || 0,
    speedBackward: parseFloat(form.speedBackward.value) || 0,
    zoneMM: parseFloat(form.zoneMM.value) || 50,
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
    'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack', 'editZoneMM',
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
  select.innerHTML = '<option value="">' + t('sequencer.selectPreset') + '</option>';
  
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
  
  showNotification('✅ ' + t('sequencer.linesEnabled', {count: selectedLineIds.size, state: enabled ? t('sequencer.enable') : t('sequencer.disable')}), 'success', 2000);
  clearSelection();
}

async function batchDeleteLines() {
  if (selectedLineIds.size === 0) return;
  
  const count = selectedLineIds.size;
  const confirmed = await showConfirm(t('sequencer.deleteSelection', {count: count}), {
    title: t('sequencer.deleteSelectionTitle'),
    type: 'danger',
    confirmText: '🗑️ ' + t('sequencer.linesDeleted', {count: count}),
    dangerous: true
  });
  
  if (!confirmed) return;
  
  console.log(`📦 Batch delete ${count} lines`);
  
  const lineIdsArray = Array.from(selectedLineIds).sort((a, b) => b - a);
  lineIdsArray.forEach(lineId => {
    sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId });
  });
  
  showNotification('✅ ' + t('sequencer.linesDeleted', {count: count}), 'success', 2000);
  clearSelection();
}

function updateBatchToolbar() {
  const toolbar = document.getElementById('sequenceBatchToolbar');
  const countDisplay = document.getElementById('batchSelectionCount');
  
  if (selectedLineIds.size > 0) {
    toolbar.classList.add('visible');
    countDisplay.textContent = t('sequencer.linesSelected', {count: selectedLineIds.size});
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
        t('sequencer.deleteSingle') :
        t('sequencer.deleteMultiple', {count: count});
      
      showConfirm(message, {
        title: t('common.delete'),
        type: 'danger',
        confirmText: '🗑️ ' + t('common.delete'),
        dangerous: true
      }).then(confirmed => {
        if (!confirmed) return;
        
        console.log(`🗑️ Trash zone drop: deleting ${count} line(s)`);
        const sortedIds = linesToDelete.sort((a, b) => b - a);
        sortedIds.forEach(lineId => sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId }));
        
        showNotification('✅ ' + t('sequencer.linesDeleted', {count: count}), 'success', 2000);
        clearSelection();
      });
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
        t('sequencer.deleteSingle') :
        t('sequencer.deleteMultiple', {count: count});
      
      showConfirm(message, {
        title: t('common.delete'),
        type: 'danger',
        confirmText: '🗑️ ' + t('common.delete'),
        dangerous: true
      }).then(confirmed => {
        if (!confirmed) return;
        
        console.log(`🗑️ Permanent trash zone drop: deleting ${count} line(s)`);
        const sortedIds = linesToDelete.sort((a, b) => b - a);
        sortedIds.forEach(lineId => sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: lineId }));
        
        showNotification('✅ ' + t('sequencer.linesDeleted', {count: count}), 'success', 2000);
        clearSelection();
      });
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
    ? (status.isLoopMode ? '🔁 ' + t('sequencer.loopInfinite') : '▶️ ' + t('sequencer.singlePlay'))
    : '⏹️ ' + t('sequencer.stopped');
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
    DOM.btnPauseSequence.innerHTML = '▶️ ' + t('common.resume');
  } else {
    DOM.btnPauseSequence.innerHTML = '⏸️ ' + t('common.pause');
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
        <div style="font-size: 16px;">${t('sequencer.emptyTable')}</div>
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
  
  updateBatchToolbar();
  initializeTrashZones();
  initSequenceSortable();  // Initialize SortableJS drag & drop
  
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
    <td class="seq-cell">
      <input type="checkbox" ${line.enabled ? 'checked' : ''} 
        onchange="toggleSequenceLine(${line.lineId}, this.checked)"
        class="icon-md" style="cursor: pointer;">
    </td>
    <td class="seq-cell text-bold text-primary text-md" style="cursor: grab;">
      ${index + 1}
    </td>
    <td class="seq-cell" title="${typeDisplay.typeName}">
      <div style="font-size: 16px;">${typeDisplay.typeIcon}</div>
    </td>
    <td class="seq-cell">
      ${typeDisplay.typeInfo}
    </td>
    <td class="seq-cell">
      <span class="text-light text-xs">--</span>
    </td>
    <td class="seq-cell">
      ${speedsDisplay}
    </td>
    <td class="seq-cell">
      ${decelSummary}
    </td>
    <td class="seq-cell text-bold text-warning">
      ${cyclesDisplay}
    </td>
    <td class="seq-cell text-xs" style="color: ${pauseColor}; font-weight: ${pauseWeight};">
      ${pauseDisplay}
    </td>
    <td class="seq-cell-last">
      <button onclick="testSequenceLine(${line.lineId})" 
        id="btnTestLine_${line.lineId}"
        class="btn-action btn-action-test"
        title="${t('sequencer.testThisLine')}">▶️</button>
      <button onclick="editSequenceLine(${line.lineId})" 
        class="btn-action btn-action-edit"
        title="${t('sequencer.editBtn')}">✏️</button>
      <button onclick="duplicateSequenceLine(${line.lineId})"
        class="btn-action btn-action-copy"
        title="${t('sequencer.duplicate')}">📋</button>
      <span id="tooltipEye_${line.lineId}" class="sequence-tooltip-eye" data-line-id="${line.lineId}"
        style="display: inline-block; padding: 4px 6px; cursor: pointer; margin: 1px; font-size: 14px;"
        title="${t('sequencer.viewDetails')}">👁️</span>
    </td>
  `;
  
  row.setAttribute('data-line-type', typeDisplay.typeName);
  
  // Event handlers
  attachRowEventHandlers(row, line, index);
  
  return row;
}

// ============================================================================
// DISPLAY HELPERS - Loaded from SequenceUtils.js
// ============================================================================
// Note: The following functions are now in SequenceUtils.js:
// - getTypeDisplay(movementType, line)
// - getDecelSummary(line, movementType)
// - getSpeedsDisplay(line, movementType)
// - getCyclesPause(line, movementType)
// - getSequenceTemplateDoc()
// - MOVEMENT_TYPE constants

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

// ============================================================================
// SORTABLEJS INITIALIZATION
// ============================================================================

let sequenceSortable = null;

/**
 * Initialize SortableJS for the sequence table
 * Called after table render
 */
function initSequenceSortable() {
  const tbody = document.getElementById('sequenceTableBody');
  if (!tbody || typeof Sortable === 'undefined') return;
  
  // Destroy previous instance if exists
  if (sequenceSortable) {
    sequenceSortable.destroy();
    sequenceSortable = null;
  }
  
  sequenceSortable = new Sortable(tbody, {
    animation: 150,
    ghostClass: 'sortable-ghost',
    chosenClass: 'sortable-chosen',
    dragClass: 'sortable-drag',
    filter: 'input, button',  // Ignore drag on inputs and buttons
    preventOnFilter: false,   // Allow click events on filtered elements
    onStart: function(evt) {
      // Store dragged line ID for trash zone detection
      draggedLineId = parseInt(evt.item.dataset.lineId);
      
      // Track last known pointer position (dragend may report 0,0 on some browsers)
      seqState.drag.lastPointerX = 0;
      seqState.drag.lastPointerY = 0;
      const trackPointer = function(e) {
        const touch = e.touches ? e.touches[0] : e;
        seqState.drag.lastPointerX = touch.clientX;
        seqState.drag.lastPointerY = touch.clientY;
      };
      seqState.drag.trackPointer = trackPointer;
      document.addEventListener('mousemove', trackPointer, { passive: true });
      document.addEventListener('touchmove', trackPointer, { passive: true });
      document.addEventListener('dragover', trackPointer, { passive: true });
      
      // Show trash zone
      const trashZone = document.getElementById('sequenceTrashDropZone');
      if (trashZone) trashZone.classList.add('drag-active');
    },
    onEnd: function(evt) {
      // Remove pointer tracking listeners
      if (seqState.drag.trackPointer) {
        document.removeEventListener('mousemove', seqState.drag.trackPointer);
        document.removeEventListener('touchmove', seqState.drag.trackPointer);
        document.removeEventListener('dragover', seqState.drag.trackPointer);
        seqState.drag.trackPointer = null;
      }
      
      // Hide trash zone
      const trashZone = document.getElementById('sequenceTrashDropZone');
      if (trashZone) trashZone.classList.remove('drag-active');
      
      // Get drop position: prefer originalEvent, fallback to tracked position
      const origEvt = evt.originalEvent;
      let dropX = 0, dropY = 0;
      if (origEvt && (origEvt.clientX || origEvt.clientY)) {
        dropX = origEvt.clientX;
        dropY = origEvt.clientY;
      } else if (origEvt && origEvt.changedTouches && origEvt.changedTouches.length) {
        dropX = origEvt.changedTouches[0].clientX;
        dropY = origEvt.changedTouches[0].clientY;
      } else {
        // Fallback: use last tracked pointer position
        dropX = seqState.drag.lastPointerX || 0;
        dropY = seqState.drag.lastPointerY || 0;
      }
      const dropTarget = (dropX || dropY) ? document.elementFromPoint(dropX, dropY) : null;
      
      const trashDrop = document.getElementById('sequenceTrashDropZone');
      const trashBatch = document.getElementById('sequenceTrashZone');
      const isOnTrash = (trashDrop && trashDrop.contains(dropTarget)) || 
                        (trashBatch && trashBatch.contains(dropTarget));
      
      if (isOnTrash) {
        // Dropped on trash → delete the line(s)
        const lineId = parseInt(evt.item.dataset.lineId);
        const linesToDelete = selectedLineIds.size > 0 ? Array.from(selectedLineIds) : [lineId];
        const count = linesToDelete.length;
        const message = count === 1 ? 
          t('sequencer.deleteSingle') :
          t('sequencer.deleteMultiple', {count: count});
        
        showConfirm(message, {
          title: t('common.delete'),
          type: 'danger',
          confirmText: '🗑️ ' + t('common.delete'),
          dangerous: true
        }).then(confirmed => {
          if (!confirmed) return;
          
          console.log(`🗑️ SortableJS trash drop: deleting ${count} line(s)`);
          const sortedIds = linesToDelete.sort((a, b) => b - a);
          sortedIds.forEach(id => sendCommand(WS_CMD.DELETE_SEQUENCE_LINE, { lineId: id }));
          
          showNotification('✅ ' + t('sequencer.linesDeleted', {count: count}), 'success', 2000);
          clearSelection();
        });
      } else if (evt.oldIndex !== evt.newIndex) {
        // Normal reorder within table
        const lineId = parseInt(evt.item.dataset.lineId);
        console.log(`🔄 SortableJS: line ${lineId} moved from ${evt.oldIndex} to ${evt.newIndex}`);
        reorderSequenceLine(lineId, evt.newIndex);
      }
      
      // Clear drag state
      draggedLineId = null;
    }
  });
  
  console.log('✅ SortableJS initialized for sequence table');
}

// ============================================================================
// SEQUENCER EVENT LISTENERS INITIALIZATION
// ============================================================================

/**
 * Initialize all sequencer-related event listeners
 * Called from main.js on window.load
 */
function initSequenceListeners() {
  console.log('📋 Initializing Sequence listeners...');
  
  // ===== TABLE ACTION BUTTONS =====
  document.getElementById('btnAddLine').addEventListener('click', addSequenceLine);
  document.getElementById('btnClearAll').addEventListener('click', clearSequence);
  document.getElementById('btnImportSeq').addEventListener('click', importSequence);
  document.getElementById('btnExportSeq').addEventListener('click', exportSequence);
  document.getElementById('btnDownloadTemplate').addEventListener('click', downloadTemplate);
  
  // ===== PLAYBACK CONTROLS =====
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
  
  document.getElementById('btnSkipLine').addEventListener('click', function() {
    sendCommand(WS_CMD.SKIP_SEQUENCE_LINE, {});
  });
  
  // ===== KEYBOARD SHORTCUTS (multi-select) =====
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
  
  // ===== EDIT MODAL HANDLERS =====
  document.getElementById('editLineForm').addEventListener('submit', saveLineEdit);
  document.getElementById('btnCancelEdit').addEventListener('click', closeEditModal);
  document.getElementById('btnCloseModal').addEventListener('click', closeEditModal);
  
  // Close modal on outside click
  document.getElementById('editLineModal').addEventListener('click', function(e) {
    if (e.target === this) {
      closeEditModal();
    }
  });
  
  // ===== MOVEMENT TYPE RADIO BUTTONS =====
  document.getElementById('editTypeVaet').addEventListener('change', updateMovementTypeFields);
  document.getElementById('editTypeOsc').addEventListener('change', updateMovementTypeFields);
  document.getElementById('editTypeChaos').addEventListener('change', updateMovementTypeFields);
  document.getElementById('editTypeCalibration').addEventListener('change', updateMovementTypeFields);
  
  // ===== CYCLE PAUSE TOGGLES (VA-ET-VIENT in modal) =====
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
  
  // ===== CYCLE PAUSE TOGGLES (OSCILLATION in modal) =====
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
  
  // ===== ZONE EFFECTS SLIDERS (modal) =====
  document.getElementById('editSpeedIntensity').addEventListener('input', function() {
    document.getElementById('editSpeedIntensityValue').textContent = this.value + '%';
  });
  document.getElementById('editTurnbackChance').addEventListener('input', function() {
    document.getElementById('editTurnbackChanceValue').textContent = this.value + '%';
  });
  
  // ===== END PAUSE MODE TOGGLE (modal) =====
  document.getElementById('editEndPauseModeFixed').addEventListener('change', function() {
    if (this.checked) {
      document.getElementById('editEndPauseFixedDiv').style.display = 'flex';
      document.getElementById('editEndPauseRandomDiv').style.display = 'none';
    }
  });
  document.getElementById('editEndPauseModeRandom').addEventListener('change', function() {
    if (this.checked) {
      document.getElementById('editEndPauseFixedDiv').style.display = 'none';
      document.getElementById('editEndPauseRandomDiv').style.display = 'flex';
    }
  });
  
  // ===== NUMERIC CONSTRAINTS (modal inputs) =====
  const numericInputs = [
    'editStartPos', 'editDistance', 'editSpeedFwd', 'editSpeedBack',
    'editZoneMM', 'editEndPauseDuration', 'editEndPauseMin', 'editEndPauseMax',
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
  
  console.log('✅ Sequence listeners initialized');
}