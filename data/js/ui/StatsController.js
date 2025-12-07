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
 * 
 * Created: December 2024 (extracted from index.html)
 * Refactored: December 2025 (merged stats.js + stats from ToolsController.js)
 * ============================================================================
 */

// ============================================================================
// CONSTANTS & STATE
// ============================================================================

/**
 * Stats state is centralized in AppState.stats:
 * - chart: Chart.js instance reference
 */

// Day names for display (constants - not moved to AppState)
const dayNames = ['Lun', 'Mar', 'Mer', 'Jeu', 'Ven', 'Sam', 'Dim'];
const dayIcons = ['🚴', '🏃', '🏁', '🏀', '🔴', '🏓', '⚪'];

// ============================================================================
// STATS DATA LOADING
// ============================================================================

/**
 * Load stats from backend API
 */
function loadStatsData() {
  console.log('Stats requested from backend');
  
  fetch('/api/stats')
    .then(response => response.json())
    .then(data => {
      displayStatsTable(data);
      displayStatsChart(data);
    })
    .catch(error => {
      console.error('Error loading stats:', error);
      const tbody = document.getElementById('statsTableBody');
      if (tbody) {
        tbody.innerHTML = 
          '<tr><td colspan="4" style="padding: 20px; text-align: center; color: #f44336;">❌ Erreur de chargement</td></tr>';
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
  const tbody = document.getElementById('statsTableBody');
  const totalEl = document.getElementById('statsTotalDistance');
  
  if (!tbody || !totalEl) {
    console.warn('Stats table elements not found');
    return;
  }
  
  if (!stats || stats.length === 0) {
    tbody.innerHTML = '<tr><td colspan="4" style="padding: 20px; text-align: center; color: #999; font-style: italic;">Aucune statistique disponible</td></tr>';
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
    const milestoneName = milestoneInfo.current ? milestoneInfo.current.name : 'Démarrage';
    
    // Build tooltip with milestone info
    let milestoneTooltip = milestoneInfo.current 
      ? `${milestoneInfo.current.emoji} ${milestoneInfo.current.name} (${milestoneInfo.current.threshold}m)` 
      : '🏁 Démarrage';
    if (milestoneInfo.current && milestoneInfo.current.location !== "-") {
      milestoneTooltip += ` - ${milestoneInfo.current.location}`;
    }
    
    const row = document.createElement('tr');
    row.style.borderBottom = '1px solid #f0f0f0';
    row.innerHTML = `
      <td style="padding: 8px;">${entry.date}</td>
      <td style="padding: 8px; text-align: center; font-size: 12px;">${dayNames[dayIndex]}</td>
      <td style="padding: 8px; text-align: center; font-size: 18px; cursor: help;" title="${milestoneTooltip}">${milestoneIcon}</td>
      <td style="padding: 8px; text-align: right; font-family: monospace;">${displayDistance}</td>
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
  const totalMilestoneEl = document.getElementById('statsTotalMilestone');
  if (!totalMilestoneEl || totalMM <= 0) return;
  
  const totalMeters = totalMM / 1000;
  const totalMilestoneInfo = getMilestoneInfo(totalMeters);
  const totalMilestoneIcon = totalMilestoneInfo.current ? totalMilestoneInfo.current.emoji : '🏁';
  const totalMilestoneName = totalMilestoneInfo.current ? totalMilestoneInfo.current.name : 'Démarrage';
  
  // Build tooltip with progress to next milestone
  let totalTooltip = totalMilestoneInfo.current 
    ? `${totalMilestoneIcon} ${totalMilestoneName} (${totalMilestoneInfo.current.threshold}m)`
    : `${totalMilestoneIcon} ${totalMilestoneName}`;
  if (totalMilestoneInfo.current && totalMilestoneInfo.current.location !== "-") {
    totalTooltip += ` - ${totalMilestoneInfo.current.location}`;
  }
  
  if (totalMilestoneInfo.next) {
    totalTooltip += `\n\n⏳️ Prochain: ${totalMilestoneInfo.next.emoji} ${totalMilestoneInfo.next.name} (${totalMilestoneInfo.next.threshold}m)`;
    totalTooltip += `\n📊 Progression: ${totalMilestoneInfo.progress.toFixed(0)}%`;
  } else {
    totalTooltip += '\n\n🏆 Objectif final atteint!';
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
    const month = week.startDate.toLocaleDateString('fr-FR', { month: 'short' });
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
  const canvas = document.getElementById('statsChart');
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
        label: 'Distance hebdomadaire (m)',
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
              return `Semaine ${weekKey.split('-')[1]} (${labels[context[0].dataIndex]})`;
            },
            label: function(context) {
              const value = parseFloat(context.parsed.y);
              return value >= 1000 ? `${(value / 1000).toFixed(2)} km` : `${value.toFixed(1)} m`;
            },
            afterLabel: function(context) {
              const weekKey = sortedWeeks[context.dataIndex];
              const week = weeklyData[weekKey];
              if (!week) return '';
              
              const lines = [`${week.days.length} jour(s) actif(s)`];
              
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
            text: 'Distance (m)'
          }
        },
        x: {
          title: {
            display: true,
            text: 'Semaines (90 derniers jours)'
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
  const panel = document.getElementById('statsPanel');
  const btn = document.getElementById('btnShowStats');
  const wasVisible = (panel.style.display !== 'none');
  
  if (!wasVisible) {
    // Opening panel
    panel.style.display = 'block';
    btn.innerHTML = '📊 Stats';
    btn.style.background = '#e74c3c';
    btn.style.color = 'white';
    
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
    closeStatsPanel();
  }
}

/**
 * Close stats panel
 */
function closeStatsPanel() {
  const panel = document.getElementById('statsPanel');
  const btn = document.getElementById('btnShowStats');
  
  panel.style.display = 'none';
  btn.innerHTML = '📊 Stats';
  btn.style.background = '#4CAF50';
  btn.style.color = 'white';
  
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
}

/**
 * Clear all statistics
 */
async function clearAllStats() {
  const confirmed = await showConfirm('Supprimer TOUTES les statistiques ?\n\nCette action est irréversible.\nLe compteur de distance (RAZ) n\'est PAS supprimé.', {
    title: 'Effacer Statistiques',
    type: 'danger',
    confirmText: '🗑️ Tout effacer',
    dangerous: true
  });
  
  if (confirmed) {
    fetch('/api/stats/clear', { method: 'POST' })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          showAlert('Statistiques effacées', { type: 'success' });
          loadStatsData();  // Refresh display
        } else {
          showAlert('Erreur: ' + (data.error || 'Unknown'), { type: 'error' });
        }
      })
      .catch(error => {
        showAlert('Erreur réseau: ' + error, { type: 'error' });
      });
  }
}

/**
 * Export statistics to JSON file
 */
function exportStats() {
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
      const filename = 'stepper_stats_' + dateStr + '.json';
      
      // Create download link and click it
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      
      console.log('📊 Stats exported to:', filename);
    })
    .catch(error => {
      console.error('❌ Export error:', error);
      showAlert('Erreur export: ' + error.message, { type: 'error' });
    });
}

/**
 * Trigger stats file import dialog
 */
function triggerStatsImport() {
  document.getElementById('statsFileInput').click();
}

/**
 * Handle stats file import
 * @param {Event} e - File input change event
 */
function handleStatsFileImport(e) {
  const file = e.target.files[0];
  if (!file) return;
  
  if (!file.name.endsWith('.json')) {
    showAlert('Fichier invalide. Utilisez un fichier JSON exporté.', { type: 'error' });
    e.target.value = ''; // Reset file input
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
      
      const confirmMsg = `Importer les statistiques ?\n\n` +
                       `📅 Date export: ${exportDate}\n` +
                       `📊 Entrées: ${entryCount}\n` +
                       `📏 Distance totale: ${totalKm} km\n\n` +
                       `⚠️ Ceci va ÉCRASER les statistiques actuelles !`;
      
      showConfirm(confirmMsg, {
        title: '📤 Import Statistiques',
        type: 'warning',
        confirmText: 'Importer',
        dangerous: true
      }).then(confirmed => {
        if (!confirmed) {
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
            showAlert(`Import réussi !\n\n📊 ${data.entriesImported} entrées importées\n📏 Total: ${(data.totalDistanceMM / 1000000).toFixed(3)} km`, { type: 'success', title: 'Import OK' });
            loadStatsData();  // Refresh display
          } else {
            showAlert('Erreur import: ' + (data.error || 'Unknown'), { type: 'error' });
          }
          e.target.value = ''; // Reset file input
        })
        .catch(error => {
          showAlert('Erreur réseau: ' + error.message, { type: 'error' });
          console.error('Import error:', error);
          e.target.value = ''; // Reset file input
        });
      });
      
    } catch (error) {
      showAlert('Erreur parsing JSON: ' + error.message, { type: 'error' });
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
 * Extracted from main.js for better organization
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

// ============================================================================
// INITIALIZE STATS LISTENERS
// ============================================================================

/**
 * Initialize all Stats event listeners
 * Called once on page load
 */
function initStatsListeners() {
  console.log('📊 Initializing Stats listeners...');
  
  document.getElementById('btnShowStats').addEventListener('click', toggleStatsPanel);
  document.getElementById('btnCloseStats').addEventListener('click', closeStatsPanel);
  document.getElementById('btnClearStats').addEventListener('click', clearAllStats);
  document.getElementById('btnExportStats').addEventListener('click', exportStats);
  document.getElementById('btnImportStats').addEventListener('click', triggerStatsImport);
  document.getElementById('statsFileInput').addEventListener('change', handleStatsFileImport);
  
  console.log('✅ Stats listeners initialized');
}

// Log initialization
console.log('✅ StatsController.js loaded - Statistics control ready');