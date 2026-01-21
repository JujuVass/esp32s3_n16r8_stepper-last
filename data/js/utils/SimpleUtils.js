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
// CONSTANTS
// ============================================================================

/**
 * Deceleration mode names for UI display
 * MUST match C++ enum DecelMode in Types.h:
 *   DECEL_LINEAR = 0, DECEL_SINE = 1, DECEL_TRIANGLE_INV = 2, DECEL_SINE_INV = 3
 */
const DECEL_MODE_NAMES = {
  0: 'Linéaire',
  1: 'Sinusoïdal',
  2: 'Triangle Inv.',
  3: 'Sinus Inv.'
};

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
// DECELERATION PREVIEW DRAWING
// ============================================================================

/**
 * Draw deceleration curve preview on canvas
 * Visualizes how speed changes across the movement range
 * 
 * @param {HTMLCanvasElement} canvas - Canvas element to draw on
 * @param {Object} config - Configuration object
 * @param {boolean} config.enabled - Whether deceleration is enabled
 * @param {boolean} config.enableStart - Whether start zone is active
 * @param {boolean} config.enableEnd - Whether end zone is active
 * @param {number} config.zoneMM - Zone size in millimeters
 * @param {number} config.effectPercent - Effect intensity (0-100)
 * @param {number} config.mode - Deceleration curve mode
 * @param {number} [config.movementAmplitude=150] - Total movement range for preview
 */
function drawDecelPreviewPure(canvas, config) {
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
    enabled = false,
    enableStart = true,
    enableEnd = true,
    zoneMM = 50,
    effectPercent = 75,
    mode = 1,
    movementAmplitude = 150
  } = config;
  
  // Show disabled message if not enabled
  if (!enabled) {
    ctx.font = '14px Arial';
    ctx.fillStyle = '#999';
    ctx.textAlign = 'center';
    ctx.fillText('Décélération désactivée', width / 2, height / 2);
    return;
  }
  
  // Calculate max slowdown from effect percent
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
      speedFactor = calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode);
    }
    // Check END zone
    if (enableEnd && positionMM >= (movementAmplitude - zoneMM)) {
      const distanceFromEnd = movementAmplitude - positionMM;
      const zoneProgress = distanceFromEnd / zoneMM;
      speedFactor = calculateSlowdownFactorPure(zoneProgress, maxSlowdown, mode);
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

/**
 * Get deceleration config from DOM elements
 * Helper to gather config for drawDecelPreviewPure
 * 
 * @returns {Object} Configuration object for drawDecelPreviewPure
 */
function getDecelConfigFromDOM() {
  const section = document.getElementById('decelSection');
  return {
    enabled: section ? !section.classList.contains('collapsed') : false,
    enableStart: document.getElementById('decelZoneStart')?.checked ?? true,
    enableEnd: document.getElementById('decelZoneEnd')?.checked ?? true,
    zoneMM: parseFloat(document.getElementById('decelZoneMM')?.value) || 50,
    effectPercent: parseFloat(document.getElementById('decelEffectPercent')?.value) || 75,
    mode: parseInt(document.getElementById('decelModeSelect')?.value) || 1,
    movementAmplitude: 150  // Default preview range
  };
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

console.log('✅ SimpleUtils.js loaded - Deceleration utilities and helpers');
