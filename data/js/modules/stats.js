/**
 * ============================================================================
 * stats.js - Statistics Display & Charts
 * ============================================================================
 * Handles loading, displaying and charting distance statistics
 * 
 * Dependencies:
 * - app.js (AppState)
 * - milestones.js (MILESTONES, getMilestoneInfo)
 * - utils.js (getISOWeek)
 * - Chart.js (external library loaded in HTML)
 * 
 * Created: December 2024 (extracted from index.html)
 * ============================================================================
 */

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Chart.js instance reference
let statsChart = null;

// Day names for display
const dayNames = ['Lun', 'Mar', 'Mer', 'Jeu', 'Ven', 'Sam', 'Dim'];
const dayIcons = ['🔵', '🟢', '🟡', '🟠', '🔴', '🟣', '⚪'];

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
    totalTooltip += `\n\n⏭️ Prochain: ${totalMilestoneInfo.next.emoji} ${totalMilestoneInfo.next.name} (${totalMilestoneInfo.next.threshold}m)`;
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
    if (statsChart) {
      statsChart.destroy();
      statsChart = null;
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
  
  if (statsChart) {
    statsChart.destroy();
  }
  
  statsChart = new Chart(ctx, {
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
// STATS PANEL TOGGLE
// ============================================================================

/**
 * Toggle stats panel visibility
 */
function toggleStatsPanel() {
  const panel = document.getElementById('statsPanel');
  if (!panel) return;
  
  if (panel.style.display === 'none') {
    panel.style.display = 'block';
    loadStatsData();
    AppState.statsPanel.isVisible = true;
  } else {
    panel.style.display = 'none';
    AppState.statsPanel.isVisible = false;
  }
  
  AppState.statsPanel.lastToggle = Date.now();
}

/**
 * Hide stats panel
 */
function hideStatsPanel() {
  const panel = document.getElementById('statsPanel');
  if (panel) {
    panel.style.display = 'none';
  }
  AppState.statsPanel.isVisible = false;
  AppState.statsPanel.lastToggle = Date.now();
}

// Log initialization
console.log('✅ stats.js loaded - Statistics display ready');
