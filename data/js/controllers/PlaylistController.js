/**
 * PlaylistController.js - Playlist Management Module
 * 
 * Handles all playlist functionality:
 * - Load playlists from backend
 * - Playlist modal (open/close, display presets)
 * - Add, delete, rename presets
 * - Load preset into mode configuration
 * - Quick add preset to sequencer
 * - Filter/search presets
 * 
 * Dependencies: 
 * - AppState, PlaylistState, WS_CMD from app.js
 * - sendCommand, showNotification from utils.js
 * - DOM cache from DOMManager.js
 * - PlaylistUtils.js (name generation, tooltips, preset button updates)
 */

// ============================================================================
// PLAYLIST LOAD FROM BACKEND
// ============================================================================

/**
 * Load playlists from backend API
 * @param {Function} callback - Optional callback after loading
 */
function loadPlaylists(callback) {
  getWithRetry('/api/playlists', { silent: true })
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

/**
 * Update playlist button counters in the UI
 */
function updatePlaylistButtonCounters() {
  const btnSimple = document.getElementById('btnManagePlaylistSimple');
  const btnOsc = document.getElementById('btnManagePlaylistOscillation');
  const btnChaos = document.getElementById('btnManagePlaylistChaos');
  
  if (btnSimple) {
    btnSimple.innerHTML = '📋 ' + t('simple.playlist') + ' (' + PlaylistState.simple.length + '/20)';
  }
  if (btnOsc) {
    btnOsc.innerHTML = '📋 ' + t('simple.playlist') + ' (' + PlaylistState.oscillation.length + '/20)';
  }
  if (btnChaos) {
    btnChaos.innerHTML = '📋 ' + t('simple.playlist') + ' (' + PlaylistState.chaos.length + '/20)';
  }
}

// ============================================================================
// PRESET NAME & TOOLTIP GENERATION (delegates to PlaylistUtils.js)
// ============================================================================

/**
 * Generate default preset name based on mode and config
 * Delegates to PlaylistUtils.js pure function
 */
function generatePresetName(mode, config) {
  return generatePresetNamePure(mode, config);
}

/**
 * Generate tooltip content for a preset
 * Delegates to PlaylistUtils.js pure function
 */
function generatePresetTooltip(mode, config) {
  return generatePresetTooltipPure(mode, config);
}

/**
 * Generate tooltip content for sequence line
 * Delegates to PlaylistUtils.js pure function
 */
function generateSequenceLineTooltip(line) {
  return generateSequenceLineTooltipPure(line);
}

// ============================================================================
// GET CURRENT MODE CONFIGURATION
// ============================================================================

/**
 * Get current mode configuration from UI inputs
 * @param {string} mode - 'simple', 'oscillation', or 'chaos'
 * @returns {Object} Configuration object
 */
function getCurrentModeConfig(mode) {
  if (mode === 'simple') {
    // Check if cycle pause section is expanded (enabled)
    const cyclePauseSection = document.querySelector('.section-collapsible:has(#cyclePauseHeaderText)');
    const cyclePauseEnabled = cyclePauseSection && !cyclePauseSection.classList.contains('collapsed');
    
    // Check if Zone Effects section is expanded (enabled)
    const zoneEffectSection = document.getElementById('zoneEffectSection');
    const zoneEffectEnabled = zoneEffectSection && !zoneEffectSection.classList.contains('collapsed');
    
    // Determine if random mode is selected (cycle pause)
    const isRandom = document.getElementById('pauseModeRandom')?.checked || false;
    
    // Determine if random mode is selected (end pause)
    const endPauseIsRandom = document.getElementById('endPauseModeRandom')?.checked || false;
    
    return {
      startPositionMM: parseFloat(document.getElementById('startPosition').value) || 0,
      distanceMM: parseFloat(document.getElementById('distance').value) || 50,
      speedLevelForward: parseFloat(document.getElementById('speedForward')?.value || document.getElementById('speedUnified').value) || 5,
      speedLevelBackward: parseFloat(document.getElementById('speedBackward')?.value || document.getElementById('speedUnified').value) || 5,
      
      // Zone Effects (new unified structure)
      vaetZoneEffect: {
        enabled: zoneEffectEnabled,
        enableStart: document.getElementById('zoneEffectStart')?.checked ?? true,
        enableEnd: document.getElementById('zoneEffectEnd')?.checked ?? true,
        mirrorOnReturn: document.getElementById('zoneEffectMirror')?.checked ?? false,
        zoneMM: parseFloat(document.getElementById('zoneEffectMM')?.value) || 50,
        speedEffect: parseInt(document.getElementById('speedEffectType')?.value) || 1,
        speedCurve: parseInt(document.getElementById('speedCurveSelect')?.value) || 1,
        speedIntensity: parseFloat(document.getElementById('speedIntensity')?.value) || 75,
        randomTurnbackEnabled: document.getElementById('randomTurnbackEnabled')?.checked || false,
        turnbackChance: parseInt(document.getElementById('turnbackChance')?.value) || 30,
        endPauseEnabled: document.getElementById('endPauseEnabled')?.checked || false,
        endPauseIsRandom: endPauseIsRandom,
        endPauseDurationSec: parseFloat(document.getElementById('endPauseDuration')?.value) || 1.0,
        endPauseMinSec: parseFloat(document.getElementById('endPauseMin')?.value) || 0.5,
        endPauseMaxSec: parseFloat(document.getElementById('endPauseMax')?.value) || 2.0
      },
      
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
    return {
      centerPositionMM: parseFloat(document.getElementById('chaosCenterPos').value) || 100,
      amplitudeMM: parseFloat(document.getElementById('chaosAmplitude').value) || 40,
      maxSpeedLevel: parseFloat(document.getElementById('chaosMaxSpeed').value) || 15,
      crazinessPercent: parseInt(document.getElementById('chaosCraziness').value) || 50,
      durationSeconds: parseInt(document.getElementById('chaosDuration').value) || 30,
      patternsEnabled: getPatternStates()
    };
  }
  return {};
}

// ============================================================================
// PLAYLIST MODAL MANAGEMENT
// ============================================================================

/**
 * Open playlist modal for a specific mode
 */
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

/**
 * Close playlist modal
 */
function closePlaylistModal() {
  document.getElementById('playlistModal').classList.remove('active');
}

/**
 * Close playlist modal on overlay click
 */
function closePlaylistModalOnOverlayClick(event) {
  // Only close if clicking on the overlay itself (not the content)
  if (event.target.id === 'playlistModal') {
    closePlaylistModal();
  }
}

/**
 * Refresh presets list in modal
 */
function refreshPlaylistPresets(mode) {
  const listEl = document.getElementById('playlistPresetsList');
  const countEl = document.getElementById('playlistCount');
  const presets = PlaylistState[mode] || [];
  
  console.log('🔄 refreshPlaylistPresets called for mode:', mode, 'presets count:', presets.length);
  
  countEl.textContent = presets.length;
  
  if (presets.length === 0) {
    console.log('⚠️ No presets found, displaying empty message');
    listEl.innerHTML = '<div class="empty-state">' + t('playlist.noPresets') + '</div>';
    return;
  }
  
  // Sort by timestamp desc (most recent first)
  const sortedPresets = [...presets].sort((a, b) => b.timestamp - a.timestamp);
  
  console.log('✅ Building HTML for', sortedPresets.length, 'presets');
  
  let html = '';
  sortedPresets.forEach(preset => {
    const tooltipContent = generatePresetTooltip(mode, preset.config);
    html += `
      <div class="preset-item preset-item-box" 
           data-tooltip="${tooltipContent.replace(/"/g, '&quot;')}">
        <div class="flex-between" style="gap: 6px;">
          <div style="flex: 1; min-width: 0;">
            <div class="text-500 text-md mb-4" style="white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">${preset.name}</div>
            <div class="text-xs" style="color: #888;">${new Date(preset.timestamp * 1000).toLocaleString('fr-FR', {dateStyle: 'short', timeStyle: 'short'})}</div>
          </div>
          <div class="flex-gap-6" style="flex-shrink: 0;">
            <button onclick="loadPresetInMode('${mode}', ${preset.id})" class="btn-mini" title="${t('playlist.loadInMode')}">
              ⬇️
            </button>
            <button onclick="quickAddToSequencer('${mode}', ${preset.id})" class="btn-mini" title="${t('playlist.addDirectToSeq')}">
              ➕📋
            </button>
            <button onclick="renamePlaylistPreset('${mode}', ${preset.id})" class="btn-mini" title="${t('playlist.rename')}">
              ✏️
            </button>
            <button onclick="deleteFromPlaylist('${mode}', ${preset.id})" class="btn-mini" title="${t('common.delete')}">
              🗑️
            </button>
            <button class="preset-tooltip-eye" data-preset-id="${preset.id}"
              style="display: inline-block; padding: 4px 6px; cursor: pointer; font-size: 14px;"
              title="${t('sequencer.viewDetails')}">👁️</button>
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

// ============================================================================
// TOOLTIP FUNCTIONS (delegated to PlaylistUtils.js)
// showPlaylistTooltip(), hidePlaylistTooltip(), showSequenceTooltip()
// are now in PlaylistUtils.js
// ============================================================================

// ============================================================================
// FILTER/SEARCH PRESETS
// ============================================================================

/**
 * Filter presets by search term
 */
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

// ============================================================================
// PLAYLIST CRUD OPERATIONS
// ============================================================================

/**
 * Add current configuration to playlist
 */
function addToPlaylist(mode) {
  const config = getCurrentModeConfig(mode);
  
  // Validation: refuse infinite durations
  if (mode === 'oscillation' && config.cycleCount === 0) {
    showNotification('❌ ' + t('playlist.infiniteCycles'), 'error', 5000);
    return;
  }
  if (mode === 'chaos' && config.durationSeconds === 0) {
    showNotification('❌ ' + t('playlist.infiniteDuration'), 'error', 5000);
    return;
  }
  
  // Check limit
  if (PlaylistState[mode].length >= 20) {
    showNotification('❌ ' + t('playlist.limitReached'), 'error', 4000);
    return;
  }
  
  // Generate default name
  const defaultName = generatePresetName(mode, config);
  const name = prompt(t('playlist.promptName'), defaultName);
  if (!name) return;
  
  // Send to backend
  postWithRetry('/api/playlists/add', {
    mode: mode,
    name: name,
    config: config
  })
  .then(data => {
    if (data.success) {
      showNotification('✅ ' + t('playlist.addedToPlaylist'), 'success', 3000);
      console.log('✅ Preset added, reloading playlists...');
      // Reload playlists, then refresh modal display
      loadPlaylists(() => {
        console.log('✅ Playlists reloaded, refreshing modal for mode:', mode);
        refreshPlaylistPresets(mode);
      });
    } else {
      showNotification('❌ ' + t('common.error') + ': ' + (data.error || 'Unknown'), 'error');
    }
  })
  .catch(error => {
    showNotification('❌ ' + t('playlist.networkError', {msg: error}), 'error');
  });
}

/**
 * Delete preset from playlist
 */
async function deleteFromPlaylist(mode, id) {
  const confirmed = await showConfirm(t('playlist.deleteConfirm'), {
    title: t('playlist.deleteTitle'),
    type: 'danger',
    confirmText: '🗑️ ' + t('common.delete'),
    dangerous: true
  });
  
  if (!confirmed) return;
  
  postWithRetry('/api/playlists/delete', {
    mode: mode,
    id: id
  })
  .then(data => {
    if (data.success) {
      showNotification('✅ ' + t('playlist.presetDeleted'), 'success', 2000);
      // Reload playlists, then refresh modal display
      loadPlaylists(() => refreshPlaylistPresets(mode));
    } else {
      showNotification('❌ ' + t('common.error') + ': ' + (data.error || 'Unknown'), 'error');
    }
  })
  .catch(error => {
    showNotification('❌ ' + t('playlist.networkError', {msg: error}), 'error');
  });
}
/**
 * Rename preset in playlist
 */
function renamePlaylistPreset(mode, id) {
  const preset = PlaylistState[mode].find(p => p.id === id);
  if (!preset) return;
  
  const newName = prompt(t('playlist.promptRename'), preset.name);
  if (!newName || newName === preset.name) return;
  
  postWithRetry('/api/playlists/update', {
    mode: mode,
    id: id,
    name: newName
  })
  .then(data => {
    if (data.success) {
      showNotification('✅ ' + t('playlist.presetRenamed'), 'success', 2000);
      // Reload playlists, then refresh modal display
      loadPlaylists(() => refreshPlaylistPresets(mode));
    } else {
      showNotification('❌ ' + t('common.error') + ': ' + (data.error || 'Unknown'), 'error');
    }
  })
  .catch(error => {
    showNotification('❌ ' + t('playlist.networkError', {msg: error}), 'error');
  });
}

// ============================================================================
// LOAD PRESET INTO MODE
// ============================================================================

/**
 * Load preset configuration into the mode's UI controls
 */
function loadPresetInMode(mode, id) {
  const preset = PlaylistState[mode].find(p => p.id === id);
  if (!preset) {
    console.error('❌ Preset not found:', mode, id);
    return;
  }
  
  console.log('📥 Loading preset:', preset.name, '| Config:', preset.config);
  
  const config = preset.config;
  
  if (mode === 'simple') {
    loadSimplePreset(config);
  } else if (mode === 'oscillation') {
    loadOscillationPreset(config);
  } else if (mode === 'chaos') {
    loadChaosPreset(config);
  }
  
  closePlaylistModal();
  showNotification('✅ ' + t('playlist.presetLoaded', {mode: mode}), 'info', 2000);
}

/**
 * Load simple mode preset
 */
function loadSimplePreset(config) {
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
  
  // Load Zone Effects parameters (new format: vaetZoneEffect, or legacy: decel*)
  let ze = config.vaetZoneEffect;
  if (!ze) {
    // Convert legacy format to new format
    ze = {
      enabled: config.decelStartEnabled || config.decelEndEnabled || false,
      enableStart: config.decelStartEnabled ?? true,
      enableEnd: config.decelEndEnabled ?? true,
      zoneMM: config.decelZoneMM || 50,
      speedEffect: 1,  // DECEL
      speedCurve: config.decelMode || 1,
      speedIntensity: config.decelEffectPercent || 75,
      randomTurnbackEnabled: false,
      turnbackChance: 30,
      endPauseEnabled: false,
      endPauseIsRandom: false,
      endPauseDurationSec: 1.0,
      endPauseMinSec: 0.5,
      endPauseMaxSec: 2.0
    };
  }
  
  // Apply Zone Effects to UI
  const zoneEffectSection = document.getElementById('zoneEffectSection');
  const zoneEffectHeaderText = document.getElementById('zoneEffectHeaderText');
  
  // Zone settings
  const zoneMMEl = document.getElementById('zoneEffectMM');
  const zoneStartEl = document.getElementById('zoneEffectStart');
  const zoneEndEl = document.getElementById('zoneEffectEnd');
  if (zoneMMEl) zoneMMEl.value = ze.zoneMM || 50;
  if (zoneStartEl) zoneStartEl.checked = ze.enableStart ?? true;
  if (zoneEndEl) zoneEndEl.checked = ze.enableEnd ?? true;
  const zoneMirrorEl = document.getElementById('zoneEffectMirror');
  if (zoneMirrorEl) zoneMirrorEl.checked = ze.mirrorOnReturn || false;
  
  // Speed effect
  const speedEffectEl = document.getElementById('speedEffectType');
  const speedCurveEl = document.getElementById('speedCurveSelect');
  const speedIntensityEl = document.getElementById('speedIntensity');
  const speedIntensityValueEl = document.getElementById('speedIntensityValue');
  if (speedEffectEl) speedEffectEl.value = ze.speedEffect || 1;
  if (speedCurveEl) speedCurveEl.value = ze.speedCurve || 1;
  if (speedIntensityEl) speedIntensityEl.value = ze.speedIntensity || 75;
  if (speedIntensityValueEl) speedIntensityValueEl.textContent = (ze.speedIntensity || 75) + '%';
  
  // Random turnback
  const turnbackEnabledEl = document.getElementById('randomTurnbackEnabled');
  const turnbackChanceEl = document.getElementById('turnbackChance');
  const turnbackChanceValueEl = document.getElementById('turnbackChanceValue');
  if (turnbackEnabledEl) turnbackEnabledEl.checked = ze.randomTurnbackEnabled || false;
  if (turnbackChanceEl) turnbackChanceEl.value = ze.turnbackChance || 30;
  if (turnbackChanceValueEl) turnbackChanceValueEl.textContent = (ze.turnbackChance || 30) + '%';
  
  // End pause
  const endPauseEnabledEl = document.getElementById('endPauseEnabled');
  const endPauseModeFixedEl = document.getElementById('endPauseModeFixed');
  const endPauseModeRandomEl = document.getElementById('endPauseModeRandom');
  const endPauseDurationEl = document.getElementById('endPauseDuration');
  const endPauseMinEl = document.getElementById('endPauseMin');
  const endPauseMaxEl = document.getElementById('endPauseMax');
  
  if (endPauseEnabledEl) endPauseEnabledEl.checked = ze.endPauseEnabled || false;
  if (endPauseModeFixedEl && endPauseModeRandomEl) {
    if (ze.endPauseIsRandom) {
      endPauseModeRandomEl.checked = true;
      endPauseModeFixedEl.checked = false;
    } else {
      endPauseModeFixedEl.checked = true;
      endPauseModeRandomEl.checked = false;
    }
  }
  if (endPauseDurationEl) endPauseDurationEl.value = ze.endPauseDurationSec || 1.0;
  if (endPauseMinEl) endPauseMinEl.value = ze.endPauseMinSec || 0.5;
  if (endPauseMaxEl) endPauseMaxEl.value = ze.endPauseMaxSec || 2.0;
  
  // Toggle end pause fixed/random visibility
  const endPauseFixedControls = document.getElementById('endPauseFixedControls');
  const endPauseRandomControls = document.getElementById('endPauseRandomControls');
  if (endPauseFixedControls && endPauseRandomControls) {
    if (ze.endPauseIsRandom) {
      endPauseFixedControls.classList.add('hidden');
      endPauseRandomControls.classList.remove('hidden');
    } else {
      endPauseFixedControls.classList.remove('hidden');
      endPauseRandomControls.classList.add('hidden');
    }
  }
  
  // Auto-expand Zone Effects section if enabled
  if (ze.enabled && zoneEffectSection && zoneEffectHeaderText) {
    if (zoneEffectSection.classList.contains('collapsed')) {
      zoneEffectSection.classList.remove('collapsed');
      const chevron = zoneEffectSection.querySelector('.collapse-icon');
      if (chevron) chevron.textContent = '▼';
    }
    zoneEffectHeaderText.textContent = '🎯 ' + t('simple.zoneEffectsEnabled').replace('🎯 ', '');
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
  
  if (cyclePauseDurationEl) cyclePauseDurationEl.value = config.cyclePauseDurationSec || 0.0;
  if (cyclePauseMinEl) cyclePauseMinEl.value = config.cyclePauseMinSec || 0.5;
  if (cyclePauseMaxEl) cyclePauseMaxEl.value = config.cyclePauseMaxSec || 3.0;
  
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
      pauseHeaderText.textContent = t('simple.cyclePauseEnabled');
    }
  }
  
  // Send commands to backend
  sendCommand(WS_CMD.SET_START_POSITION, {startPosition: config.startPositionMM || 0});
  sendCommand(WS_CMD.SET_DISTANCE, {distance: config.distanceMM || 50});
  sendCommand(WS_CMD.SET_SPEED_FORWARD, {speed: config.speedLevelForward || 5});
  sendCommand(WS_CMD.SET_SPEED_BACKWARD, {speed: config.speedLevelBackward || 5});
  
  // Send Zone Effects config to backend (use new format)
  const zoneEffectCmd = ze;  // ze is already built above
  console.log('🔧 Sending setZoneEffect:', zoneEffectCmd);
  if (WS_CMD.SET_ZONE_EFFECT) {
    sendCommand(WS_CMD.SET_ZONE_EFFECT, zoneEffectCmd);
  } else {
    // Fallback to old command if new one not available
    sendCommand(WS_CMD.SET_DECEL_ZONE, {
      enabled: ze.enabled,
      enableStart: ze.enableStart,
      enableEnd: ze.enableEnd,
      zoneMM: ze.zoneMM,
      effectPercent: ze.speedIntensity,
      mode: ze.speedCurve
    });
  }
  
  // Send cycle pause config to backend (unified API)
  const pauseCmd = {
    enabled: pauseEnabled,
    isRandom: pauseIsRandom,
    pauseDurationSec: config.cyclePauseDurationSec || 0.0,
    minPauseSec: config.cyclePauseMinSec || 0.5,
    maxPauseSec: config.cyclePauseMaxSec || 3.0
  };
  console.log('🔧 Sending updateCyclePause:', pauseCmd);
  sendCommand(WS_CMD.UPDATE_CYCLE_PAUSE, pauseCmd);
  
  console.log('✅ Simple preset loaded | Zone Effects:', ze.enabled);
}

/**
 * Load oscillation mode preset
 */
function loadOscillationPreset(config) {
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
  
  if (cyclePauseDurationOscEl) cyclePauseDurationOscEl.value = config.cyclePauseDurationSec || 0.0;
  if (cyclePauseMinOscEl) cyclePauseMinOscEl.value = config.cyclePauseMinSec || 0.5;
  if (cyclePauseMaxOscEl) cyclePauseMaxOscEl.value = config.cyclePauseMaxSec || 3.0;
  
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
      pauseHeaderText.textContent = t('oscillation.cyclePauseEnabled');
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
}

/**
 * Load chaos mode preset
 */
function loadChaosPreset(config) {
  document.getElementById('chaosCenterPos').value = config.centerPositionMM || 100;
  document.getElementById('chaosAmplitude').value = config.amplitudeMM || 40;
  document.getElementById('chaosMaxSpeed').value = config.maxSpeedLevel || 15;
  document.getElementById('chaosCraziness').value = config.crazinessPercent || 50;
  document.getElementById('chaosDuration').value = config.durationSeconds || 30;
  document.getElementById('crazinessValue').textContent = config.crazinessPercent || 50;

  // Set pattern checkboxes using shared helper
  if (config.patternsEnabled && Array.isArray(config.patternsEnabled)) {
    setPatternStates(config.patternsEnabled);
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
  
  console.log('✅ Chaos preset loaded');
}

// ============================================================================
// QUICK ADD TO SEQUENCER
// ============================================================================

/**
 * Quick Add preset directly to sequencer (from playlist modal)
 */
function quickAddToSequencer(mode, presetId) {
  const preset = PlaylistState[mode].find(p => p.id === presetId);
  if (!preset) {
    showNotification('❌ ' + t('playlist.presetNotFound'), 'error', 2000);
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
    
    // Build vaetZoneEffect from config (new format) or convert from legacy format
    let vaetZoneEffect;
    if (config.vaetZoneEffect) {
      // New format
      vaetZoneEffect = config.vaetZoneEffect;
    } else {
      // Legacy format - convert decel* fields
      vaetZoneEffect = {
        enabled: config.decelStartEnabled || config.decelEndEnabled,
        enableStart: config.decelStartEnabled ?? true,
        enableEnd: config.decelEndEnabled ?? true,
        zoneMM: config.decelZoneMM || 50,
        speedEffect: 1,  // DECEL
        speedCurve: config.decelMode || 1,
        speedIntensity: config.decelEffectPercent || 75,
        randomTurnbackEnabled: false,
        turnbackChance: 30,
        endPauseEnabled: false,
        endPauseIsRandom: false,
        endPauseDurationSec: 1.0,
        endPauseMinSec: 0.5,
        endPauseMaxSec: 2.0
      };
    }
    
    newLine = {
      enabled: true,
      movementType: mode === 'simple' ? 0 : mode === 'oscillation' ? 1 : 2,
      cycleCount: 1,
      pauseAfterMs: 0,
      startPositionMM: config.startPositionMM || 0,
      distanceMM: config.distanceMM || 50,
      speedForward: config.speedLevelForward || 5,
      speedBackward: config.speedLevelBackward || 5,
      vaetZoneEffect: vaetZoneEffect,
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
      vaetCyclePauseEnabled: config.cyclePauseEnabled || false,
      vaetCyclePauseIsRandom: config.cyclePauseIsRandom || false,
      vaetCyclePauseDurationSec: config.cyclePauseDurationSec || 0.0,
      vaetCyclePauseMinSec: config.cyclePauseMinSec || 0.5,
      vaetCyclePauseMaxSec: config.cyclePauseMaxSec || 3.0,
      oscCyclePauseEnabled: false,
      oscCyclePauseIsRandom: false,
      oscCyclePauseDurationSec: 0.0,
      oscCyclePauseMinSec: 0.5,
      oscCyclePauseMaxSec: 3.0
    };
  }
  
  // Validate before sending (validateSequencerLine from main.js)
  if (typeof validateSequencerLine === 'function') {
    const errors = validateSequencerLine(newLine, newLine.movementType);
    if (errors.length > 0) {
      showNotification('❌ ' + t('playlist.invalidPreset') + '\n' + errors.join('\n'), 'error', 5000);
      return;
    }
  }
  
  // Send to backend
  sendCommand(WS_CMD.ADD_SEQUENCE_LINE, newLine);
  showNotification('✅ ' + t('playlist.lineAddedToSeq') + ' ' + preset.name, 'success', 3000);
  
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
    showNotification('⚠️ ' + t('playlist.selectPreset'), 'error', 2000);
    return;
  }
  
  const preset = PlaylistState[mode].find(p => p.id === presetId);
  if (!preset) {
    showNotification('❌ ' + t('playlist.presetNotFound'), 'error', 2000);
    return;
  }
  
  const config = preset.config;
  
  // Fill sequencer modal fields based on mode
  if (mode === 'simple') {
    document.getElementById('editStartPos').value = config.startPositionMM || 0;
    document.getElementById('editDistance').value = config.distanceMM || 50;
    document.getElementById('editSpeedFwd').value = config.speedLevelForward || 5;
    document.getElementById('editSpeedBack').value = config.speedLevelBackward || 5;
    
    // Load Zone Effects into sequencer modal
    const ze = config.vaetZoneEffect;
    if (ze) {
      document.getElementById('editZoneEnableStart').checked = ze.enableStart ?? true;
      document.getElementById('editZoneEnableEnd').checked = ze.enableEnd ?? true;
      document.getElementById('editZoneMirror').checked = ze.mirrorOnReturn || false;
      document.getElementById('editZoneMM').value = ze.zoneMM || 50;
      document.getElementById('editSpeedEffect').value = ze.speedEffect ?? 1;
      document.getElementById('editSpeedCurve').value = ze.speedCurve ?? 1;
      document.getElementById('editSpeedIntensity').value = ze.speedIntensity || 75;
      const intensityVal = document.getElementById('editSpeedIntensityValue');
      if (intensityVal) intensityVal.textContent = (ze.speedIntensity || 75) + '%';
      document.getElementById('editRandomTurnback').checked = ze.randomTurnbackEnabled || false;
      document.getElementById('editTurnbackChance').value = ze.turnbackChance || 30;
      const turnbackVal = document.getElementById('editTurnbackChanceValue');
      if (turnbackVal) turnbackVal.textContent = (ze.turnbackChance || 30) + '%';
      document.getElementById('editEndPauseEnabled').checked = ze.endPauseEnabled || false;
      const endPauseFixedEl = document.getElementById('editEndPauseModeFixed');
      const endPauseRandomEl = document.getElementById('editEndPauseModeRandom');
      if (endPauseFixedEl && endPauseRandomEl) {
        endPauseFixedEl.checked = !ze.endPauseIsRandom;
        endPauseRandomEl.checked = ze.endPauseIsRandom || false;
      }
      document.getElementById('editEndPauseDuration').value = ze.endPauseDurationSec || 1.0;
      document.getElementById('editEndPauseMin').value = ze.endPauseMinSec || 0.5;
      document.getElementById('editEndPauseMax').value = ze.endPauseMaxSec || 2.0;
    }
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
  
  // Trigger validation to update UI state (if available)
  if (typeof validateEditForm === 'function') {
    validateEditForm();
  }
  
  showNotification('✅ ' + t('playlist.valuesLoaded') + ' ' + preset.name, 'success', 2000);
}

// ============================================================================
// PRESET BUTTON STATE UPDATES (delegated to PlaylistUtils.js)
// updateStartPresets(), updateDistancePresets() are now in PlaylistUtils.js
// ============================================================================

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize playlist event listeners
 * Called once on page load
 */
function initPlaylistListeners() {
  console.log('📋 Initializing Playlist listeners...');
  
  // Playlist modal buttons
  document.getElementById('btnManagePlaylistSimple').addEventListener('click', function() {
    openPlaylistModal('simple');
  });
  
  document.getElementById('btnManagePlaylistOscillation').addEventListener('click', function() {
    openPlaylistModal('oscillation');
  });
  
  document.getElementById('btnManagePlaylistChaos').addEventListener('click', function() {
    openPlaylistModal('chaos');
  });
  
  // Add current config to playlist button
  document.getElementById('btnAddCurrentToPlaylist').addEventListener('click', function() {
    const mode = document.getElementById('playlistModal').dataset.mode;
    if (mode) {
      addToPlaylist(mode);
    }
  });
  
  // Modal overlay click to close
  const modal = document.getElementById('playlistModal');
  if (modal) {
    modal.addEventListener('click', closePlaylistModalOnOverlayClick);
  }
  
  // Search input (if exists)
  const searchInput = document.getElementById('playlistSearchInput');
  if (searchInput) {
    searchInput.addEventListener('input', function() {
      filterPlaylistPresets(this.value);
    });
  }
  
  // Load playlists from backend on startup
  loadPlaylists();
  
  console.log('✅ Playlist listeners initialized');
}

// Log initialization
console.log('✅ PlaylistController.js loaded');
