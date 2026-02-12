/**
 * Speed Milestones - Real speed achievement icons
 * Similar pattern to milestones.js (DRY approach)
 * 
 * Speed is measured in cm/s (calculated client-side from totalTraveled delta)
 * Thresholds are cumulative - highest matching threshold wins
 */

// Speed milestone definitions (sorted by threshold ascending)
const SPEED_MILESTONES = [
  { threshold: 0,    emoji: '⏸️',  name: 'Arrêté' },
  { threshold: 0.1,  emoji: '🐌',  name: 'Escargot' },
  { threshold: 0.5,  emoji: '🐢',  name: 'Tortue' },
  { threshold: 2,    emoji: '🚶',  name: 'Marche lente' },
  { threshold: 5,    emoji: '🐕',  name: 'Chien au trot' },
  { threshold: 10,   emoji: '🚶‍♂️', name: 'Marche rapide' },
  { threshold: 20,   emoji: '🏃',  name: 'Jogging' },
  { threshold: 35,   emoji: '🚴',  name: 'Vélo' },
  { threshold: 50,   emoji: '🐎',  name: 'Cheval au galop' },
  { threshold: 70,   emoji: '🏎️',  name: 'Vitesse max !' }
];

/**
 * Get speed milestone info for a given speed in cm/s
 * @param {number} speedCmPerSec - Current speed in cm/s
 * @returns {object} { current: {threshold, emoji, name}, next: {threshold, emoji, name}|null }
 */
function getSpeedMilestoneInfo(speedCmPerSec) {
  let current = SPEED_MILESTONES[0]; // Default: Arrêté
  let next = SPEED_MILESTONES.length > 1 ? SPEED_MILESTONES[1] : null;

  for (let i = SPEED_MILESTONES.length - 1; i >= 0; i--) {
    if (speedCmPerSec >= SPEED_MILESTONES[i].threshold) {
      current = SPEED_MILESTONES[i];
      next = (i + 1 < SPEED_MILESTONES.length) ? SPEED_MILESTONES[i + 1] : null;
      break;
    }
  }

  return { current, next };
}
