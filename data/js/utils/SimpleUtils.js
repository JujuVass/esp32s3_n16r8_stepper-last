/**
 * SimpleUtils.js - Pure Utility Functions for Simple Mode
 * 
 * Contains pure/helper functions extracted from SimpleController.js:
 * - Deceleration factor calculation
 * - Deceleration curve preview drawing
 * - Constants for deceleration modes
 * 
 * Dependencies: None (pure functions)
 */

// ============================================================================
// DECELERATION CALCULATION (Pure Function)
// ============================================================================

/**
 * Calculate slowdown factor based on position in deceleration zone
 * Pure JavaScript implementation EXACTLY matching C++ calculateSlowdownFactor()
 * in BaseMovementController.cpp
 * 
 * @param {number} zoneProgress - Progress through zone (0.0 = at boundary/contact, 1.0 = exiting zone)
 * @param {number} maxSlowdown - Maximum slowdown factor (e.g., 10.0 = 10× slower)
 * @param {number} mode - Deceleration curve mode (0=linear, 1=sine, 2=triangle_inv, 3=sine_inv)
 * @returns {number} Slowdown factor (1.0 = normal speed, >1.0 = slower)
 */
function calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode) {
  // Clamp progress to [0, 1]
  const progress = Math.max(0, Math.min(1, zoneProgress));
  
  let factor;
  
  switch (mode) {
    case 0: // DECEL_LINEAR - constant deceleration rate
      // C++: factor = 1.0 + (1.0 - zoneProgress) * (maxSlowdown - 1.0)
      factor = 1.0 + (1.0 - progress) * (maxSlowdown - 1.0);
      break;
      
    case 1: // DECEL_SINE - sinusoidal curve (smooth, max slowdown at contact)
      // C++: smoothProgress = (1.0 - cos(zoneProgress * PI)) / 2.0
      //      factor = 1.0 + (1.0 - smoothProgress) * (maxSlowdown - 1.0)
      {
        const smoothProgress = (1.0 - Math.cos(progress * Math.PI)) / 2.0;
        factor = 1.0 + (1.0 - smoothProgress) * (maxSlowdown - 1.0);
      }
      break;
      
    case 2: // DECEL_TRIANGLE_INV - weak at start, strong at end (quadratic)
      // C++: invProgress = 1.0 - zoneProgress
      //      curved = invProgress * invProgress
      //      factor = 1.0 + curved * (maxSlowdown - 1.0)
      {
        const invProgress = 1.0 - progress;
        const curved = invProgress * invProgress;
        factor = 1.0 + curved * (maxSlowdown - 1.0);
      }
      break;
      
    case 3: // DECEL_SINE_INV - weak at start, strong at end (sine curve)
      // C++: invProgress = 1.0 - zoneProgress
      //      curved = sin(invProgress * PI / 2.0)
      //      factor = 1.0 + curved * (maxSlowdown - 1.0)
      {
        const invProgress = 1.0 - progress;
        const curved = Math.sin(invProgress * Math.PI / 2.0);
        factor = 1.0 + curved * (maxSlowdown - 1.0);
      }
      break;
      
    default:
      factor = 1.0;
      break;
  }
  
  return factor;
}

// ============================================================================
// ZONE EFFECTS PREVIEW DRAWING
// ============================================================================

/**
 * Draw zone effects preview on a canvas
 * Shows: speed effects (decel/accel), random turnback zones, and end pause indicators
 * 
 * @param {HTMLCanvasElement} canvas - Target canvas element
 * @param {Object} config - Zone effect configuration
 * @param {boolean} config.enableStart - Whether start zone is active
 * @param {boolean} config.enableEnd - Whether end zone is active
 * @param {number} config.zoneMM - Zone size in millimeters
 * @param {number} config.speedEffect - Speed effect type (0=none, 1=decel, 2=accel)
 * @param {number} config.speedCurve - Speed curve type (0-3)
 * @param {number} config.speedIntensity - Speed effect intensity (0-100)
 * @param {boolean} config.randomTurnbackEnabled - Whether random turnback is enabled
 * @param {number} config.turnbackChance - Turnback chance percentage
 * @param {boolean} config.endPauseEnabled - Whether end pause is enabled
 * @param {number} [config.movementAmplitude=150] - Total movement range for preview
 */
function drawZoneEffectPreviewPure(canvas, config) {
  if (!canvas) return;
  
  const ctx = canvas.getContext('2d');
  const width = canvas.width;
  const height = canvas.height;
  const padding = 20;
  const plotWidth = width - 2 * padding;
  const plotHeight = height - 2 * padding;
  
  // Clear canvas
  ctx.clearRect(0, 0, width, height);
  
  // Destructure config with defaults
  const {
    enableStart = true,
    enableEnd = true,
    zoneMM = 50,
    speedEffect = 1,        // 0=none, 1=decel, 2=accel
    speedCurve = 1,         // 0=linear, 1=sine, 2=tri_inv, 3=sine_inv
    speedIntensity = 75,
    randomTurnbackEnabled = false,
    turnbackChance = 30,
    endPauseEnabled = false,
    movementAmplitude = 150
  } = config;
  
  // Check if any effect is active
  const hasSpeedEffect = speedEffect !== 0;
  const hasAnyEffect = hasSpeedEffect || randomTurnbackEnabled || endPauseEnabled;
  
  // Show disabled message if nothing is enabled
  if (!hasAnyEffect) {
    ctx.font = '14px Arial';
    ctx.fillStyle = '#999';
    ctx.textAlign = 'center';
    ctx.fillText('Aucun effet actif', width / 2, height / 2);
    return;
  }
  
  // Calculate max slowdown/speedup from intensity
  const maxFactor = 1.0 + (speedIntensity / 100.0) * 9.0;  // 1× to 10×
  
  // Draw axes
  ctx.strokeStyle = '#ccc';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(padding, padding);
  ctx.lineTo(padding, height - padding);
  ctx.lineTo(width - padding, height - padding);
  ctx.stroke();
  
  // Draw horizontal line for normal speed (y = 0.5 * plotHeight)
  ctx.strokeStyle = '#ddd';
  ctx.setLineDash([2, 2]);
  ctx.beginPath();
  const normalY = height - padding - (0.5 * plotHeight);
  ctx.moveTo(padding, normalY);
  ctx.lineTo(width - padding, normalY);
  ctx.stroke();
  ctx.setLineDash([]);
  
  // Draw speed curve if speed effect is active
  if (hasSpeedEffect) {
    const isAccel = speedEffect === 2;
    ctx.strokeStyle = isAccel ? '#2196F3' : '#4CAF50';  // Blue for accel, green for decel
    ctx.lineWidth = 2;
    ctx.beginPath();
    
    for (let x = 0; x <= plotWidth; x++) {
      const positionMM = (x / plotWidth) * movementAmplitude;
      let speedFactor = 1.0;  // Normal speed
      
      // Check START zone
      if (enableStart && positionMM <= zoneMM) {
        const zoneProgress = positionMM / zoneMM;
        speedFactor = calculateSlowdownFactorPure(zoneProgress, maxFactor, speedCurve);
        if (isAccel) {
          // For acceleration: invert the slowdown to speedup
          speedFactor = 1.0 / speedFactor;
        }
      }
      // Check END zone
      if (enableEnd && positionMM >= (movementAmplitude - zoneMM)) {
        const distanceFromEnd = movementAmplitude - positionMM;
        const zoneProgress = distanceFromEnd / zoneMM;
        speedFactor = calculateSlowdownFactorPure(zoneProgress, maxFactor, speedCurve);
        if (isAccel) {
          speedFactor = 1.0 / speedFactor;
        }
      }
      
      // Convert speed factor to Y coordinate
      // For decel: slower = higher value = lower on graph (towards bottom)
      // For accel: faster = lower value (< 1) = higher on graph
      let normalizedSpeed;
      if (isAccel) {
        // Accel: values range from 1 (normal) to < 1 (faster)
        // Map to 0.5 (normal) to 1.0 (fast) for display
        normalizedSpeed = 0.5 + (1.0 - speedFactor) * 0.5;
      } else {
        // Decel: values range from 1 (normal) to > 1 (slower)
        // Map to 0.5 (normal) to 0 (slow) for display
        normalizedSpeed = 0.5 / speedFactor;
      }
      
      const y = height - padding - (normalizedSpeed * plotHeight);
      
      if (x === 0) {
        ctx.moveTo(padding + x, y);
      } else {
        ctx.lineTo(padding + x, y);
      }
    }
    
    ctx.stroke();
  }
  
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
  
  // Draw random turnback indicator
  if (randomTurnbackEnabled) {
    ctx.fillStyle = 'rgba(156, 39, 176, 0.2)';  // Purple with transparency
    
    if (enableStart) {
      ctx.fillRect(padding, padding, (zoneMM / movementAmplitude) * plotWidth, plotHeight);
    }
    if (enableEnd) {
      const endStartX = padding + ((movementAmplitude - zoneMM) / movementAmplitude) * plotWidth;
      ctx.fillRect(endStartX, padding, (zoneMM / movementAmplitude) * plotWidth, plotHeight);
    }
    
    // Draw turnback symbol
    ctx.fillStyle = '#9C27B0';
    ctx.font = '12px Arial';
    ctx.textAlign = 'center';
    if (enableStart) {
      ctx.fillText('🔄 ' + turnbackChance + '%', padding + 20, padding + 15);
    }
    if (enableEnd) {
      ctx.fillText('🔄 ' + turnbackChance + '%', width - padding - 20, padding + 15);
    }
  }
  
  // Draw end pause indicator
  if (endPauseEnabled) {
    ctx.fillStyle = '#FFC107';  // Amber
    ctx.font = 'bold 14px Arial';
    ctx.textAlign = 'center';
    
    if (enableStart) {
      ctx.fillText('⏸', padding + 8, height - padding - 5);
    }
    if (enableEnd) {
      ctx.fillText('⏸', width - padding - 8, height - padding - 5);
    }
  }
  
  // Draw labels
  ctx.font = '10px Arial';
  ctx.fillStyle = '#666';
  ctx.textAlign = 'center';
  ctx.fillText(t('utils.startLabel'), padding, height - 5);
  ctx.fillText(t('utils.endLabel'), width - padding, height - 5);
  
  // Draw speed indicators
  ctx.textAlign = 'left';
  if (hasSpeedEffect) {
    const isAccel = speedEffect === 2;
    ctx.fillText(isAccel ? t('utils.fast') : t('utils.normal'), padding + 5, padding + 10);
    ctx.fillText(isAccel ? t('utils.normal') : t('utils.slow'), padding + 5, height - padding - 5);
  }
}

// ============================================================================
// CYCLE PAUSE SECTION HELPERS
// ============================================================================

/**
 * Get Cycle Pause section element for Simple mode
 * @returns {HTMLElement|null} Section element or null
 */
function getCyclePauseSection() {
  const header = document.getElementById('cyclePauseHeaderText');
  return header ? header.closest('.section-collapsible') : null;
}

/**
 * Get Cycle Pause section element for Oscillation mode
 * @returns {HTMLElement|null} Section element or null
 */
function getCyclePauseOscSection() {
  const header = document.getElementById('cyclePauseOscHeaderText');
  return header ? header.closest('.section-collapsible') : null;
}

console.log('✅ SimpleUtils.js loaded - Zone effects utilities and helpers');
