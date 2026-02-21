/**
 * SimpleUtils.js - Pure Utility Functions for Simple Mode
 * 
 * Contains pure/helper functions for Simple mode:
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
 * @param {number} zoneProgress - Progress through zone (0 = at boundary/contact, 1 = exiting zone)
 * @param {number} maxSlowdown - Maximum slowdown factor (e.g., 10 = 10× slower)
 * @param {number} mode - Deceleration curve mode (0=linear, 1=sine, 2=triangle_inv, 3=sine_inv)
 * @returns {number} Slowdown factor (1 = normal speed, >1 = slower)
 */
function calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode) {
  // Clamp progress to [0, 1]
  const progress = Math.max(0, Math.min(1, zoneProgress));
  
  let factor;
  
  switch (mode) {
    case 0: // DECEL_LINEAR - constant deceleration rate
      // C++: factor = 1 + (1 - zoneProgress) * (maxSlowdown - 1)
      factor = 1 + (1 - progress) * (maxSlowdown - 1);
      break;
      
    case 1: // DECEL_SINE - sinusoidal curve (smooth, max slowdown at contact)
      // C++: smoothProgress = (1 - cos(zoneProgress * PI)) / 2
      //      factor = 1 + (1 - smoothProgress) * (maxSlowdown - 1)
      {
        const smoothProgress = (1 - Math.cos(progress * Math.PI)) / 2;
        factor = 1 + (1 - smoothProgress) * (maxSlowdown - 1);
      }
      break;
      
    case 2: // DECEL_TRIANGLE_INV - weak at start, strong at end (quadratic)
      // C++: invProgress = 1 - zoneProgress
      //      curved = invProgress * invProgress
      //      factor = 1 + curved * (maxSlowdown - 1)
      {
        const invProgress = 1 - progress;
        const curved = invProgress * invProgress;
        factor = 1 + curved * (maxSlowdown - 1);
      }
      break;
      
    case 3: // DECEL_SINE_INV - weak at start, strong at end (sine curve)
      // C++: invProgress = 1 - zoneProgress
      //      curved = sin(invProgress * PI / 2)
      //      factor = 1 + curved * (maxSlowdown - 1)
      {
        const invProgress = 1 - progress;
        const curved = Math.sin(invProgress * Math.PI / 2);
        factor = 1 + curved * (maxSlowdown - 1);
      }
      break;
      
    default:
      factor = 1;
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
  
  ctx.clearRect(0, 0, width, height);
  
  const {
    enableStart = true, enableEnd = true, zoneMM = 50,
    speedEffect = 1,
    randomTurnbackEnabled = false, turnbackChance = 30,
    endPauseEnabled = false, movementAmplitude = 150
  } = config;
  
  const hasSpeedEffect = speedEffect !== 0;
  const hasAnyEffect = hasSpeedEffect || randomTurnbackEnabled || endPauseEnabled;
  
  if (!hasAnyEffect) {
    ctx.font = '14px Arial';
    ctx.fillStyle = '#999';
    ctx.textAlign = 'center';
    ctx.fillText(t('utils.noActiveEffect'), width / 2, height / 2);
    return;
  }
  
  const plotCtx = { ctx, width, height, padding, plotWidth, plotHeight };
  
  drawPreviewAxes(plotCtx);
  if (hasSpeedEffect) drawPreviewSpeedCurve(plotCtx, config);
  drawPreviewZoneBoundaries(plotCtx, enableStart, enableEnd, zoneMM, movementAmplitude);
  if (randomTurnbackEnabled) drawPreviewTurnback(plotCtx, enableStart, enableEnd, zoneMM, movementAmplitude, turnbackChance);
  if (endPauseEnabled) drawPreviewEndPause(plotCtx, enableStart, enableEnd);
  drawPreviewLabels(plotCtx, hasSpeedEffect, speedEffect);
}

/** Draw axes and horizontal baseline */
function drawPreviewAxes(p) {
  p.ctx.strokeStyle = '#ccc';
  p.ctx.lineWidth = 1;
  p.ctx.beginPath();
  p.ctx.moveTo(p.padding, p.padding);
  p.ctx.lineTo(p.padding, p.height - p.padding);
  p.ctx.lineTo(p.width - p.padding, p.height - p.padding);
  p.ctx.stroke();
  
  p.ctx.strokeStyle = '#ddd';
  p.ctx.setLineDash([2, 2]);
  p.ctx.beginPath();
  const normalY = p.height - p.padding - (0.5 * p.plotHeight);
  p.ctx.moveTo(p.padding, normalY);
  p.ctx.lineTo(p.width - p.padding, normalY);
  p.ctx.stroke();
  p.ctx.setLineDash([]);
}

/** Draw the speed effect curve */
function drawPreviewSpeedCurve(p, config) {
  const { enableStart, enableEnd, zoneMM, speedEffect, speedCurve, speedIntensity, movementAmplitude = 150 } = config;
  const isAccel = speedEffect === 2;
  const maxFactor = 1 + (speedIntensity / 100) * 9;
  
  p.ctx.strokeStyle = isAccel ? '#2196F3' : '#4CAF50';
  p.ctx.lineWidth = 2;
  p.ctx.beginPath();
  
  for (let x = 0; x <= p.plotWidth; x++) {
    const positionMM = (x / p.plotWidth) * movementAmplitude;
    const speedFactor = computeZoneSpeedFactor(positionMM, { enableStart, enableEnd, zoneMM, movementAmplitude, maxFactor, speedCurve, isAccel });
    
    const normalizedSpeed = isAccel
      ? 0.5 + (1 - speedFactor) * 0.5
      : 0.5 / speedFactor;
    const y = p.height - p.padding - (normalizedSpeed * p.plotHeight);
    
    if (x === 0) p.ctx.moveTo(p.padding + x, y);
    else p.ctx.lineTo(p.padding + x, y);
  }
  p.ctx.stroke();
}

/** Compute speed factor at a given position considering start/end zones */
function computeZoneSpeedFactor(positionMM, cfg) {
  const { enableStart, enableEnd, zoneMM, movementAmplitude, maxFactor, speedCurve, isAccel } = cfg;
  let factor = 1;
  if (enableStart && positionMM <= zoneMM) {
    factor = calculateSlowdownFactorPure(positionMM / zoneMM, maxFactor, speedCurve);
    if (isAccel) factor = 1 / factor;
  }
  if (enableEnd && positionMM >= (movementAmplitude - zoneMM)) {
    factor = calculateSlowdownFactorPure((movementAmplitude - positionMM) / zoneMM, maxFactor, speedCurve);
    if (isAccel) factor = 1 / factor;
  }
  return factor;
}

/** Draw dashed zone boundary lines */
function drawPreviewZoneBoundaries(p, enableStart, enableEnd, zoneMM, movementAmplitude) {
  if (!enableStart && !enableEnd) return;
  p.ctx.setLineDash([5, 3]);
  p.ctx.strokeStyle = '#FF9800';
  p.ctx.lineWidth = 1;
  
  if (enableStart) {
    const startX = p.padding + (zoneMM / movementAmplitude) * p.plotWidth;
    p.ctx.beginPath();
    p.ctx.moveTo(startX, p.padding);
    p.ctx.lineTo(startX, p.height - p.padding);
    p.ctx.stroke();
  }
  if (enableEnd) {
    const endX = p.padding + ((movementAmplitude - zoneMM) / movementAmplitude) * p.plotWidth;
    p.ctx.beginPath();
    p.ctx.moveTo(endX, p.padding);
    p.ctx.lineTo(endX, p.height - p.padding);
    p.ctx.stroke();
  }
  p.ctx.setLineDash([]);
}

/** Draw turnback overlay and percentage labels */
function drawPreviewTurnback(p, enableStart, enableEnd, zoneMM, movementAmplitude, turnbackChance) {
  const zoneWidth = (zoneMM / movementAmplitude) * p.plotWidth;
  p.ctx.fillStyle = 'rgba(156, 39, 176, 0.2)';
  
  if (enableStart) p.ctx.fillRect(p.padding, p.padding, zoneWidth, p.plotHeight);
  if (enableEnd) {
    const endStartX = p.padding + ((movementAmplitude - zoneMM) / movementAmplitude) * p.plotWidth;
    p.ctx.fillRect(endStartX, p.padding, zoneWidth, p.plotHeight);
  }
  
  p.ctx.fillStyle = '#9C27B0';
  p.ctx.font = '12px Arial';
  p.ctx.textAlign = 'center';
  if (enableStart) p.ctx.fillText('🔄 ' + turnbackChance + '%', p.padding + 20, p.padding + 15);
  if (enableEnd) p.ctx.fillText('🔄 ' + turnbackChance + '%', p.width - p.padding - 20, p.padding + 15);
}

/** Draw end pause icons */
function drawPreviewEndPause(p, enableStart, enableEnd) {
  p.ctx.fillStyle = '#FFC107';
  p.ctx.font = 'bold 14px Arial';
  p.ctx.textAlign = 'center';
  if (enableStart) p.ctx.fillText('⏸', p.padding + 8, p.height - p.padding - 5);
  if (enableEnd) p.ctx.fillText('⏸', p.width - p.padding - 8, p.height - p.padding - 5);
}

/** Draw axis labels and speed indicators */
function drawPreviewLabels(p, hasSpeedEffect, speedEffect) {
  p.ctx.font = '10px Arial';
  p.ctx.fillStyle = '#666';
  p.ctx.textAlign = 'center';
  p.ctx.fillText(t('utils.startLabel'), p.padding, p.height - 5);
  p.ctx.fillText(t('utils.endLabel'), p.width - p.padding, p.height - 5);
  
  if (hasSpeedEffect) {
    const isAccel = speedEffect === 2;
    p.ctx.textAlign = 'left';
    p.ctx.fillText(isAccel ? t('utils.fast') : t('utils.normal'), p.padding + 5, p.padding + 10);
    p.ctx.fillText(isAccel ? t('utils.normal') : t('utils.slow'), p.padding + 5, p.height - p.padding - 5);
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

console.debug('✅ SimpleUtils.js loaded - Zone effects utilities and helpers');
