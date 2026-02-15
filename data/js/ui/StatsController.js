/**
 * ============================================================================
 * StatsController.js - Statistics Control Module
 * ============================================================================
 * Handles all statistics functionality:
 * - Stats panel (show/hide, clear, export, import)
 * - Loading and displaying distance statistics
 * - Weekly charts (Chart.js)
 * - Milestone tracking display
 * 
 * Dependencies:
 * - app.js (AppState, DOM)
 * - milestones.js (MILESTONES, getMilestoneInfo)
 * - utils.js (getISOWeek)
 * - Chart.js (external library loaded in HTML)
 * ============================================================================
 */

// ============================================================================
// CONSTANTS & STATE
// ============================================================================

/**
 * Stats state is centralized in AppState.stats:
 * - chart: Chart.js instance reference
 */

// Day names for display - functions using i18n
function getDayNames() { return t('stats.dayNames') || ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']; }
const dayIcons = ['🚴', '🏃', '🏁', '🏀', '🔴', '🏓', '⚪'];

// ============================================================================
// STATS DATA LOADING
// ============================================================================

/**
 * Load stats from backend API
 */
function loadStatsData() {
  console.debug('Stats requested from backend');
  
  getWithRetry('/api/stats', { silent: true })
    .then(data => {
      displayStatsTable(data);
      displayStatsChart(data);
    })
    .catch(error => {
      console.error('Error loading stats:', error);
      const tbody = DOM.statsTableBody;
      if (tbody) {
        tbody.innerHTML = 
          '<tr><td colspan="4" class="empty-state text-error">❌ ' + t('stats.loadingError') + '</td></tr>';
      }
    });
}

// ============================================================================
// STATS TABLE DISPLAY
// ============================================================================

/**
 * Display stats in a table format with milestone icons
 * @param {Array} stats - Array of {date, distanceMM} objects
 */
function displayStatsTable(stats) {
  const tbody = DOM.statsTableBody;
  const totalEl = DOM.statsTotalDistance;
  
  if (!tbody || !totalEl) {
    console.warn('Stats table elements not found');
    return;
  }
  
  if (!stats || stats.length === 0) {
    tbody.innerHTML = '<tr><td colspan="4" class="empty-state">' + t('stats.noStats') + '</td></tr>';
    totalEl.textContent = '0 km';
    return;
  }
  
  // Sort by date (newest first)
  stats.sort((a, b) => new Date(b.date) - new Date(a.date));
  
  let totalMM = 0;
  tbody.innerHTML = '';
  
  stats.forEach(entry => {
    const distanceMM = entry.distanceMM || 0;
    totalMM += distanceMM;
    
    const date = new Date(entry.date);
    const dayOfWeek = date.getDay();  // 0=Sunday, 1=Monday, ...
    const dayIndex = dayOfWeek === 0 ? 6 : dayOfWeek - 1;  // Convert to Monday=0
    const dayIcon = dayIcons[dayIndex];
    
    const distanceKM = (distanceMM / 1000000).toFixed(3);
    const distanceM = (distanceMM / 1000).toFixed(1);
    const displayDistance = distanceMM >= 1000000 ? `${distanceKM} km` : `${distanceM} m`;
    
    // Get milestone icon for this day's distance
    const distanceMeters = distanceMM / 1000;
    const milestoneInfo = getMilestoneInfo(distanceMeters);
    const milestoneIcon = milestoneInfo.current ? milestoneInfo.current.emoji : '🏁';
    const milestoneName = milestoneInfo.current ? milestoneInfo.current.name : t('stats.startup');
    
    // Build tooltip with milestone info
    let milestoneTooltip = milestoneInfo.current 
      ? `${milestoneInfo.current.emoji} ${milestoneInfo.current.name} (${milestoneInfo.current.threshold}m)` 
      : '🏁 ' + t('stats.startup');
    if (milestoneInfo.current && milestoneInfo.current.location !== "-") {
      milestoneTooltip += ` - ${milestoneInfo.current.location}`;
    }
    
    const row = document.createElement('tr');
    row.style.borderBottom = '1px solid #f0f0f0';
    row.innerHTML = `
      <td class="stats-cell">${entry.date}</td>
      <td class="stats-cell text-md">${getDayNames()[dayIndex]}</td>
      <td class="stats-cell" style="font-size: 18px; cursor: help;" title="${milestoneTooltip}">${milestoneIcon}</td>
      <td class="stats-cell text-right font-mono">${displayDistance}</td>
    `;
    tbody.appendChild(row);
  });
  
  // Calculate total display
  const totalKM = (totalMM / 1000000).toFixed(3);
  const totalM = (totalMM / 1000).toFixed(1);
  const displayTotal = totalMM >= 1000000 ? `${totalKM} km` : `${totalM} m`;
  
  // Update total milestone icon with progress
  updateTotalMilestone(totalMM);
  
  // Add progress percentage to total distance display
  const totalMilestoneInfo = getMilestoneInfo(totalMM / 1000);
  if (totalMilestoneInfo.next) {
    totalEl.textContent = `${displayTotal} (${totalMilestoneInfo.progress.toFixed(0)}%)`;
  } else {
    totalEl.textContent = displayTotal;
  }
}

/**
 * Update total milestone icon and tooltip
 * @param {number} totalMM - Total distance in millimeters
 */
function updateTotalMilestone(totalMM) {
  const totalMilestoneEl = DOM.statsTotalMilestone;
  if (!totalMilestoneEl || totalMM <= 0) return;
  
  const totalMeters = totalMM / 1000;
  const totalMilestoneInfo = getMilestoneInfo(totalMeters);
  const totalMilestoneIcon = totalMilestoneInfo.current ? totalMilestoneInfo.current.emoji : '🏁';
  const totalMilestoneName = totalMilestoneInfo.current ? totalMilestoneInfo.current.name : t('stats.startup');
  
  // Build tooltip with progress to next milestone
  let totalTooltip = totalMilestoneInfo.current 
    ? `${totalMilestoneIcon} ${totalMilestoneName} (${totalMilestoneInfo.current.threshold}m)`
    : `${totalMilestoneIcon} ${totalMilestoneName}`;
  if (totalMilestoneInfo.current && totalMilestoneInfo.current.location !== "-") {
    totalTooltip += ` - ${totalMilestoneInfo.current.location}`;
  }
  
  if (totalMilestoneInfo.next) {
    totalTooltip += `\n\n⏳️ ${t('stats.next')}: ${totalMilestoneInfo.next.emoji} ${totalMilestoneInfo.next.name} (${totalMilestoneInfo.next.threshold}m)`;
    totalTooltip += `\n📊 ${t('stats.progression')}: ${totalMilestoneInfo.progress.toFixed(0)}%`;
  } else {
    totalTooltip += '\n\n🏆 ' + t('stats.finalGoalReached');
  }
  
  totalMilestoneEl.textContent = totalMilestoneIcon;
  totalMilestoneEl.setAttribute('title', totalTooltip);
  totalMilestoneEl.style.cursor = 'help';
}

// ============================================================================
// STATS CHART (Chart.js)
// ============================================================================

/**
 * Display weekly stats chart using Chart.js
 * @param {Array} stats - Array of {date, distanceMM} objects
 */
function displayStatsChart(stats) {
  if (!stats || stats.length === 0) {
    if (AppState.stats.chart) {
      AppState.stats.chart.destroy();
      AppState.stats.chart = null;
    }
    return;
  }
  
  // Filter last 90 days (3 months sliding window)
  const today = new Date();
  const threeMonthsAgo = new Date(today);
  threeMonthsAgo.setDate(today.getDate() - 90);
  
  const filteredStats = stats.filter(entry => {
    const entryDate = new Date(entry.date);
    return entryDate >= threeMonthsAgo;
  });
  
  // Sort by date (oldest first for chart)
  filteredStats.sort((a, b) => new Date(a.date) - new Date(b.date));
  
  // Aggregate by week (1 bar = 1 week)
  const weeklyData = aggregateByWeek(filteredStats);
  
  // Find milestone for each week's distance
  const sortedWeeks = Object.keys(weeklyData).sort();
  sortedWeeks.forEach(weekKey => {
    const week = weeklyData[weekKey];
    const weekMeters = week.totalMM / 1000;
    
    week.currentMilestone = null;
    for (let i = MILESTONES.length - 1; i >= 0; i--) {
      if (weekMeters >= MILESTONES[i].threshold) {
        week.currentMilestone = MILESTONES[i];
        break;
      }
    }
  });
  
  // Prepare data for Chart.js
  const labels = sortedWeeks.map(weekKey => {
    const week = weeklyData[weekKey];
    const start = week.startDate.getDate();
    const end = week.endDate.getDate();
    const locale = (typeof I18n !== 'undefined' && I18n.getLang() === 'en') ? 'en-US' : 'fr-FR';
    const month = week.startDate.toLocaleDateString(locale, { month: 'short' });
    return `${start}-${end} ${month}`;
  });
  
  const distances = sortedWeeks.map(weekKey => {
    const totalMM = weeklyData[weekKey].totalMM;
    return (totalMM / 1000).toFixed(1);  // mm → m
  });
  
  // Render chart
  renderStatsChart(labels, distances, sortedWeeks, weeklyData);
}

/**
 * Aggregate daily stats into weekly buckets
 * @param {Array} stats - Sorted array of daily stats
 * @returns {Object} Weekly data keyed by ISO week (e.g., "2025-W43")
 */
function aggregateByWeek(stats) {
  const weeklyData = {};
  
  stats.forEach(entry => {
    const date = new Date(entry.date);
    
    // Calculate ISO 8601 week number
    const weekNumber = getISOWeek(date);
    const year = date.getFullYear();
    const weekKey = `${year}-W${String(weekNumber).padStart(2, '0')}`;
    
    if (!weeklyData[weekKey]) {
      weeklyData[weekKey] = {
        totalMM: 0,
        startDate: null,
        endDate: null,
        days: []
      };
    }
    
    weeklyData[weekKey].totalMM += entry.distanceMM;
    weeklyData[weekKey].days.push(entry.date);
    
    if (!weeklyData[weekKey].startDate || date < weeklyData[weekKey].startDate) {
      weeklyData[weekKey].startDate = date;
    }
    if (!weeklyData[weekKey].endDate || date > weeklyData[weekKey].endDate) {
      weeklyData[weekKey].endDate = date;
    }
  });
  
  return weeklyData;
}

/**
 * Render Chart.js bar chart
 * @param {Array} labels - Week labels
 * @param {Array} distances - Distance values in meters
 * @param {Array} sortedWeeks - Sorted week keys
 * @param {Object} weeklyData - Weekly data for tooltips
 */
function renderStatsChart(labels, distances, sortedWeeks, weeklyData) {
  const canvas = DOM.statsChartCanvas;
  if (!canvas) {
    console.warn('Stats chart canvas not found');
    return;
  }
  
  const ctx = canvas.getContext('2d');
  
  if (AppState.stats.chart) {
    AppState.stats.chart.destroy();
  }
  
  AppState.stats.chart = new Chart(ctx, {
    type: 'bar',
    data: {
      labels: labels,
      datasets: [{
        label: t('stats.weeklyDistance'),
        data: distances,
        backgroundColor: 'rgba(76, 175, 80, 0.6)',
        borderColor: 'rgba(76, 175, 80, 1)',
        borderWidth: 1
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: true,
      plugins: {
        legend: {
          display: false
        },
        tooltip: {
          callbacks: {
            title: function(context) {
              const weekKey = sortedWeeks[context[0].dataIndex];
              return `${t('stats.week')} ${weekKey.split('-')[1]} (${labels[context[0].dataIndex]})`;
            },
            label: function(context) {
              const value = parseFloat(context.parsed.y);
              return value >= 1000 ? `${(value / 1000).toFixed(2)} km` : `${value.toFixed(1)} m`;
            },
            afterLabel: function(context) {
              const weekKey = sortedWeeks[context.dataIndex];
              const week = weeklyData[weekKey];
              if (!week) return '';
              
              const lines = [`${week.days.length} ${t('stats.activeDays')}`];
              
              if (week.currentMilestone) {
                lines.push('');
                lines.push(`${week.currentMilestone.emoji} ${week.currentMilestone.name}`);
              }
              
              return lines;
            }
          }
        }
      },
      scales: {
        y: {
          beginAtZero: true,
          title: {
            display: true,
            text: t('stats.distanceM')
          }
        },
        x: {
          title: {
            display: true,
            text: t('stats.weeks90days')
          },
          ticks: {
            maxRotation: 45,
            minRotation: 45
          }
        }
      }
    }
  });
}

// ============================================================================
// STATS PANEL MANAGEMENT
// ============================================================================

/**
 * Toggle stats panel visibility
 */
function toggleStatsPanel() {
  const panel = DOM.statsPanel;
  const btn = DOM.btnShowStats;
  const wasVisible = (panel.style.display !== 'none');
  
  if (!wasVisible) {
    // Opening panel
    panel.style.display = 'block';
    btn.innerHTML = '📊 ' + t('status.stats');
    btn.style.background = '#e74c3c';
    btn.style.color = 'white';
    
    // Update state and signal backend
    AppState.statsPanel.isVisible = true;
    AppState.statsPanel.lastToggle = Date.now();
    
    // Send WebSocket command to backend (enable stats sending)
    sendCommand(WS_CMD.REQUEST_STATS, { enable: true });
    console.debug('📊 Stats requested from backend');
    
    // Load stats data
    loadStatsData();
  } else {
    closeStatsPanel();
  }
}

/**
 * Close stats panel
 */
function closeStatsPanel() {
  const panel = DOM.statsPanel;
  const btn = DOM.btnShowStats;
  
  panel.style.display = 'none';
  btn.innerHTML = '📊 ' + t('status.stats');
  btn.style.background = '#4CAF50';
  btn.style.color = 'white';
  
  // Update state and signal backend
  AppState.statsPanel.isVisible = false;
  AppState.statsPanel.lastToggle = Date.now();
  
  // Send WebSocket command to backend (disable stats sending)
  sendCommand(WS_CMD.REQUEST_STATS, { enable: false });
  console.debug('📊 Stats panel closed');
}

/**
 * Clear all statistics
 */
async function clearAllStats() {
  const confirmed = await showConfirm(t('stats.clearConfirm'), {
    title: t('stats.clearTitle'),
    type: 'danger',
    confirmText: '🗑️ ' + t('stats.clearAll'),
    dangerous: true
  });
  
  if (confirmed) {
    postWithRetry('/api/stats/clear', {})
      .then(data => {
        if (data.success) {
          showAlert(t('stats.statsCleared'), { type: 'success' });
          loadStatsData();  // Refresh display
        } else {
          showAlert(t('common.error') + ': ' + (data.error || 'Unknown'), { type: 'error' });
        }
      })
      .catch(error => {
        showAlert(t('stats.networkError') + ': ' + error, { type: 'error' });
      });
  }
}

/**
 * Export statistics to JSON file
 */
function exportStats() {
  getWithRetry('/api/stats/export')
    .then(data => {
      // Create JSON file and download
      const jsonStr = JSON.stringify(data, null, 2);
      const blob = new Blob([jsonStr], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      
      // Generate filename with current date
      const now = new Date();
      const dateStr = now.toISOString().split('T')[0]; // YYYY-MM-DD
      const filename = 'stepper_stats_' + dateStr + '.json';
      
      // Create download link and click it
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      
      console.debug('📊 Stats exported to:', filename);
    })
    .catch(error => {
      console.error('❌ Export error:', error);
      showAlert(t('stats.exportError') + ': ' + error.message, { type: 'error' });
    });
}

/**
 * Trigger stats file import dialog
 */
function triggerStatsImport() {
  DOM.statsFileInput.click();
}

/**
 * Handle stats file import
 * @param {Event} e - File input change event
 */
function handleStatsFileImport(e) {
  const file = e.target.files[0];
  if (!file) return;
  
  if (!file.name.endsWith('.json')) {
    showAlert(t('stats.invalidFile'), { type: 'error' });
    e.target.value = ''; // Reset file input
    return;
  }
  
  const reader = new FileReader();
  reader.onload = function(event) {
    try {
      const importData = JSON.parse(event.target.result);
      
      // Validate structure
      if (!importData.stats || !Array.isArray(importData.stats)) {
        throw new Error(t('stats.invalidJsonFormat'));
      }
      
      // Confirm import (show preview)
      const entryCount = importData.stats.length;
      const exportDate = importData.exportDate || t('stats.unknown');
      const totalKm = importData.totalDistanceMM ? (importData.totalDistanceMM / 1000000).toFixed(3) : '?';
      
      const confirmMsg = t('stats.importConfirm') + `\n\n` +
                       `📅 ${t('stats.exportDate')}: ${exportDate}\n` +
                       `📊 ${t('stats.entries')}: ${entryCount}\n` +
                       `📏 ${t('stats.totalDistance')}: ${totalKm} km\n\n` +
                       `⚠️ ${t('stats.importWarning')}`;
      
      showConfirm(confirmMsg, {
        title: '📤 ' + t('stats.importTitle'),
        type: 'warning',
        confirmText: t('stats.import'),
        dangerous: true
      }).then(confirmed => {
        if (!confirmed) {
          e.target.value = ''; // Reset file input
          return;
        }
        
        // Send to backend
        postWithRetry('/api/stats/import', importData)
        .then(data => {
          if (data.success) {
            showAlert(t('stats.importSuccess', {count: data.entriesImported, total: (data.totalDistanceMM / 1000000).toFixed(3)}), { type: 'success', title: t('stats.importOk') });
            loadStatsData();  // Refresh display
          } else {
            showAlert(t('stats.importError') + ': ' + (data.error || 'Unknown'), { type: 'error' });
          }
          e.target.value = ''; // Reset file input
        })
        .catch(error => {
          showAlert(t('stats.networkError') + ': ' + error.message, { type: 'error' });
          console.error('Import error:', error);
          e.target.value = ''; // Reset file input
        });
      });
      
    } catch (error) {
      showAlert(t('stats.jsonParseError') + ': ' + error.message, { type: 'error' });
      console.error('JSON parse error:', error);
      e.target.value = ''; // Reset file input
    }
  };
  
  reader.readAsText(file);
}

// ============================================================================
// MILESTONE UI UPDATE
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
      tooltip += `\n\n⏭️ ${t('stats.next')}: ${milestoneInfo.next.emoji} ${milestoneInfo.next.name} (${milestoneInfo.next.threshold}m)`;
      tooltip += `\n📊 ${t('stats.progression')}: ${milestoneInfo.progress.toFixed(0)}%`;
    } else {
      tooltip += `\n\n🎉 ${t('stats.lastMilestoneReached')}`;
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
        let message = `🎉 ${t('stats.milestoneReached')}: ${milestoneInfo.current.emoji} (${newThreshold}m)`;
        if (milestoneInfo.next) {
          message += `\n⏭️ ${t('stats.next')}: ${milestoneInfo.next.emoji} (${milestoneInfo.next.threshold}m) - ${milestoneInfo.progress.toFixed(0)}%`;
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
      const tooltip = `⏭️ ${t('stats.next')}: ${milestoneInfo.next.emoji} ${milestoneInfo.next.name} (${milestoneInfo.next.threshold}m)\n📊 ${t('stats.progression')}: ${milestoneInfo.progress.toFixed(0)}%`;
      DOM.milestoneIcon.textContent = '🐜';
      DOM.milestoneIcon.title = tooltip;
    } else {
      DOM.milestoneIcon.textContent = '';
      DOM.milestoneIcon.title = '';
    }
  }
}

// ============================================================================
// STATS RECORDING TOGGLE
// ============================================================================

/**
 * Toggle stats recording on/off (persisted to EEPROM)
 */
function toggleStatsRecording() {
  const checkbox = DOM.statsRecordingEnabled;
  if (!checkbox) return;
  
  const enabled = checkbox.checked;
  console.debug(`📊 Stats recording toggle: ${enabled ? 'ENABLED' : 'DISABLED'}`);
  
  // Set flag to prevent status updates from reverting the checkbox
  AppState.stats.isEditingRecording = true;
  
  // Send command via WebSocket
  sendCommand(WS_CMD.SET_STATS_RECORDING, { enabled: enabled });
  
  // Clear flag after server has time to process and send updated status
  setTimeout(() => {
    AppState.stats.isEditingRecording = false;
  }, 500);
}

/**
 * Update stats recording UI from status
 * @param {boolean} enabled - Current recording state from ESP32
 */
function updateStatsRecordingUI(enabled) {
  // Don't update if user is currently editing
  if (AppState.stats.isEditingRecording) return;
  
  const checkbox = DOM.statsRecordingEnabled;
  const warning = DOM.statsRecordingWarning;
  
  if (checkbox) {
    checkbox.checked = enabled;
  }
  
  if (warning) {
    warning.style.display = enabled ? 'none' : 'inline';
  }
}

// ============================================================================
// INITIALIZE STATS LISTENERS
// ============================================================================

/**
 * Initialize all Stats event listeners
 * Called once on page load
 */
function initStatsListeners() {
  console.debug('📊 Initializing Stats listeners...');
  
  DOM.btnShowStats.addEventListener('click', toggleStatsPanel);
  DOM.btnCloseStats.addEventListener('click', closeStatsPanel);
  DOM.btnClearStats.addEventListener('click', clearAllStats);
  DOM.btnExportStats.addEventListener('click', exportStats);
  DOM.btnImportStats.addEventListener('click', triggerStatsImport);
  DOM.statsFileInput.addEventListener('change', handleStatsFileImport);
  
  // Stats recording toggle
  if (DOM.statsRecordingEnabled) {
    DOM.statsRecordingEnabled.addEventListener('change', toggleStatsRecording);
  }
  
  console.debug('✅ Stats listeners initialized');
}

// Log initialization
console.debug('✅ StatsController.js loaded - Statistics control ready');