# 🚀 Plan d'Optimisation - ESP32 Stepper Controller

> **Date**: 4 décembre 2025  
> **Backup commit**: `f2c9d37` - "BACKUP: Pre-optimization state"  
> **État actuel**: Fonctionnel, 6660 lignes backend + 10699 lignes frontend
> 
> **Mise à jour**: Session optimisation - 5 déc 2025
> **Commits optimisations**: 
> - `405eac0` - "Phase 1 Optimizations"
> - `268b038` - "Phase 2.4 COMPLETE: All sendCommand migrated to WS_CMD constants"

---

## ✅ Tâches Complétées

### Phase 1.1 - Backend Constantes ✅
- [x] Config.h: Ajout WS_PORT (81), HTTP_PORT (80)
- [x] Config.h: Ajout JSON_DOC_SIZE_SMALL (256), JSON_DOC_SIZE_MEDIUM (512), JSON_DOC_SIZE_LARGE (1024)
- [x] Config.h: Ajout RETRY_MAX_ATTEMPTS (3), RETRY_DELAY_MS (1000)
- [x] Documentation "Why?" pour chaque constante

### Phase 1.2 - Extraction CSS ✅
- [x] Créé `data/css/styles.css` (988 lignes extraites)
- [x] Modifié `index.html` avec `<link rel="stylesheet">`
- [x] Ajouté route `/css/styles.css` dans APIRoutes.h avec cache 24h

### Phase 1.3 - Constantes WebSocket ✅
- [x] Créé objet `WS_CMD` avec 50+ commandes
- [x] Organisé par catégorie (Movement, Simple, Oscillation, Chaos, Sequence, etc.)
- [x] Object.freeze() pour immutabilité

### Phase 2.2 - Helper Functions ✅
- [x] `setupEditableInput()` - Gestion edit state standardisée
- [x] `setupPresetButtons()` - Boutons preset génériques
- [x] `validateNumericInput()` - Validation avec min/max/default
- [x] `validateMinMaxPair()` - Validation paires min/max

### Phase 2.4 - Remplacement sendCommand strings → WS_CMD ✅
- [x] Migration de 75+ appels sendCommand('string') → sendCommand(WS_CMD.XXX)
- [x] Commandes Core: START, CALIBRATE, STOP, PAUSE, RETURN_TO_START, GET_STATUS, SAVE_STATS
- [x] Commandes Simple: SET_START_POSITION, SET_DISTANCE, SET_SPEED_FORWARD/BACKWARD
- [x] Commandes Oscillation: SET_OSCILLATION, SET_OSCILLATION_CONFIG, START/STOP_OSCILLATION
- [x] Commandes Chaos: SET_CHAOS_CONFIG, START/STOP_CHAOS
- [x] Commandes Sequence: ADD/DELETE/UPDATE/MOVE/DUPLICATE/TOGGLE/CLEAR/EXPORT/GET_SEQUENCE_*
- [x] Commandes Pursuit: PURSUIT_MOVE, ENABLE/DISABLE_PURSUIT_MODE
- [x] Commandes Config: SET_DECEL_ZONE, SET_CYCLE_PAUSE, UPDATE_CYCLE_PAUSE*, SET_MAX_DISTANCE_LIMIT

---

## 📊 Résumé de l'Analyse

### Backend (stepper_controller_restructured.ino)
| Critère | Score | Notes |
|---------|-------|-------|
| Architecture | ⭐⭐⭐ | Headers bien extraits |
| Performance | ⭐⭐⭐⭐ | Debouncing, timing OK |
| Maintenabilité | ⭐⭐ | Fichier principal trop gros |
| Testabilité | ⭐ | Aucun test unitaire |

### Frontend (index.html)
| Critère | Score | Notes |
|---------|-------|-------|
| Fonctionnalités | ⭐⭐⭐⭐⭐ | Complet |
| UX | ⭐⭐⭐⭐ | Gamification, tooltips |
| Maintenabilité | ⭐⭐ | Monolithe 10K lignes |
| Performance | ⭐⭐⭐⭐ | DOM cache optimisé |

---

## 🎯 PHASE 1 - Quick Wins (Risque faible, Impact immédiat) ✅ COMPLÉTÉE

### 1.1 Backend - Extraction Constantes Magiques ✅
**Fichier**: `include/Config.h`  
**Effort**: 2h | **Impact**: Maintenabilité +20%  
**Status**: ✅ COMPLÉTÉ - Commit `405eac0`

```cpp
// À AJOUTER dans Config.h
// ========== TIMING CONSTANTS ==========
constexpr uint32_t STATUS_BROADCAST_INTERVAL_MS = 20;
constexpr uint32_t CONTACT_DEBOUNCE_MS = 50;
constexpr uint32_t OTA_CHECK_INTERVAL_MS = 1000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t STATS_SAVE_INTERVAL_MS = 60000;

// ========== MOTION LIMITS ==========
constexpr float MAX_SPEED_MM_PER_SEC = 300.0f;
constexpr float MIN_STEP_INTERVAL_US = 20.0f;
constexpr uint8_t MAX_SPEED_LEVEL = 20;
constexpr uint8_t DEFAULT_SPEED_LEVEL = 5;

// ========== CHAOS PATTERNS ==========
constexpr uint8_t CHAOS_PATTERN_COUNT = 11;
constexpr float CHAOS_MIN_AMPLITUDE_MM = 5.0f;
constexpr float CHAOS_MAX_AMPLITUDE_MM = 200.0f;

// ========== SEQUENCER ==========
constexpr uint8_t MAX_SEQUENCE_LINES = 20;
constexpr uint16_t MAX_CYCLES_PER_LINE = 9999;
```

**Fichiers à modifier**:
- [x] `Config.h` - Constantes réseau et JSON ajoutées
- [ ] `stepper_controller_restructured.ino` - Remplacer magic numbers restants (future)
- [ ] `ChaosPatterns.h` - Utiliser constantes (future)
- [ ] `Types.h` - Valeurs par défaut des structs (future)

---

### 1.2 Frontend - Extraction CSS ✅
**Effort**: 2h | **Impact**: Cache navigateur + Maintenabilité  
**Status**: ✅ COMPLÉTÉ - 988 lignes extraites

**Créer** `data/css/styles.css`:
```
data/
├── index.html          (réduit de ~800 lignes)
└── css/
    └── styles.css      (nouveau fichier)
```

**Étapes**:
1. [x] Créer `data/css/styles.css`
2. [x] Couper/coller le bloc `<style>...</style>` de index.html
3. [x] Ajouter `<link rel="stylesheet" href="/css/styles.css">` dans `<head>`
4. [x] Mettre à jour `APIRoutes.h` pour servir le fichier CSS (route + cache 24h)

---

### 1.3 Frontend - Constantes WebSocket Commands ✅
**Effort**: 1h | **Impact**: Typo-safe, autocomplétion IDE  
**Status**: ✅ COMPLÉTÉ - Objet WS_CMD avec 50+ commandes

**Ajouter en haut de `<script>` dans index.html**:
```javascript
// ========== WEBSOCKET COMMANDS ==========
const CMD = {
  // System
  CALIBRATE: 'calibrate',
  GET_STATUS: 'getStatus',
  REBOOT: 'reboot',
  SAVE_STATS: 'saveStats',
  
  // Motion - Simple
  START: 'start',
  PAUSE: 'pause',
  STOP: 'stop',
  SET_START_POSITION: 'setStartPosition',
  SET_DISTANCE: 'setDistance',
  SET_SPEED_FORWARD: 'setSpeedForward',
  SET_SPEED_BACKWARD: 'setSpeedBackward',
  RETURN_TO_START: 'returnToStart',
  
  // Oscillation
  START_OSCILLATION: 'startOscillation',
  STOP_OSCILLATION: 'stopOscillation',
  SET_OSCILLATION: 'setOscillation',
  SET_OSCILLATION_CONFIG: 'setOscillationConfig',
  
  // Chaos
  START_CHAOS: 'startChaos',
  STOP_CHAOS: 'stopChaos',
  SET_CHAOS_CONFIG: 'setChaosConfig',
  
  // Pursuit
  PURSUIT_MOVE: 'pursuitMove',
  ENABLE_PURSUIT: 'enablePursuitMode',
  DISABLE_PURSUIT: 'disablePursuitMode',
  
  // Sequencer
  START_SEQUENCE: 'startSequence',
  STOP_SEQUENCE: 'stopSequence',
  PAUSE_SEQUENCE: 'pauseSequence',
  ADD_SEQUENCE_LINE: 'addSequenceLine',
  DELETE_SEQUENCE_LINE: 'deleteSequenceLine',
  MOVE_SEQUENCE_LINE: 'moveSequenceLine',
  
  // Config
  SET_DECEL_ZONE: 'setDecelZone',
  SET_CYCLE_PAUSE: 'setCyclePause',
  SET_MAX_DISTANCE_LIMIT: 'setMaxDistanceLimit'
};
```

Puis remplacer toutes les occurrences:
```javascript
// AVANT
sendCommand('calibrate');
sendCommand('setStartPosition', {startPosition: pos});

// APRÈS
sendCommand(CMD.CALIBRATE);
sendCommand(CMD.SET_START_POSITION, {startPosition: pos});
```

---

## 🔧 PHASE 2 - Refactoring Structurel (Risque moyen)

### 2.1 Backend - Split stepper_controller_restructured.ino
**Effort**: 8h | **Impact**: Maintenabilité +50%

**Structure cible**:
```
src/
├── main.cpp                    (~200 lignes - setup/loop)
├── MotorController.cpp         (~800 lignes - contrôle moteur)
├── MotorController.h
├── WebSocketHandler.cpp        (~600 lignes - commandes WS)
├── WebSocketHandler.h
├── OscillationEngine.cpp       (~400 lignes - mode oscillation)
├── OscillationEngine.h
├── ChaosEngine.cpp             (~500 lignes - mode chaos)
├── ChaosEngine.h
├── SequencerEngine.cpp         (~400 lignes - mode séquenceur)
├── SequencerEngine.h
├── CalibrationManager.cpp      (~200 lignes - calibration)
├── CalibrationManager.h
└── StateMachine.cpp            (~300 lignes - états système)
    StateMachine.h
```

**Ordre de découpage recommandé**:
1. [ ] `StateMachine` - Enum états + transitions (faible couplage)
2. [ ] `CalibrationManager` - Logique calibration isolée
3. [ ] `OscillationEngine` - Mode bien défini
4. [ ] `ChaosEngine` - Patterns indépendants
5. [ ] `SequencerEngine` - Exécution séquences
6. [ ] `MotorController` - Core moteur (couplage fort)
7. [ ] `WebSocketHandler` - Routage commandes

---

### 2.2 Frontend - Split updateUI()
**Effort**: 3h | **Impact**: Testabilité + Lisibilité

**Fonction actuelle**: ~500 lignes monolithiques

**Refactoring**:
```javascript
function updateUI(data) {
  if (!data || !('positionMM' in data)) return;
  
  updateSystemState(data);
  updatePositionDisplay(data);
  updateSpeedDisplay(data);
  updateCalibrationOverlay(data);
  updateInputFields(data);
  updateButtonStates(data);
  updateMilestones(data);
  
  // Mode-specific updates
  if (AppState.system.currentMode === 'oscillation') {
    updateOscillationUI(data);
  } else if (AppState.system.currentMode === 'chaos') {
    updateChaosUI(data);
  } else if (AppState.system.currentMode === 'simple') {
    updateSimpleModeUI(data);
  }
  
  updateSystemStats(data.system);
}

function updateSystemState(data) { /* ~30 lignes */ }
function updatePositionDisplay(data) { /* ~20 lignes */ }
function updateSpeedDisplay(data) { /* ~50 lignes */ }
function updateCalibrationOverlay(data) { /* ~15 lignes */ }
function updateInputFields(data) { /* ~40 lignes */ }
function updateButtonStates(data) { /* ~60 lignes */ }
function updateMilestones(data) { /* ~40 lignes */ }
function updateOscillationUI(data) { /* ~80 lignes */ }
function updateSimpleModeUI(data) { /* ~30 lignes */ }
```

---

### 2.3 Frontend - Factoriser Duplication Simple/Oscillation
**Effort**: 4h | **Impact**: -30% code JS

**Fonctions dupliquées à unifier**:
```javascript
// AVANT (2 fonctions quasi-identiques)
function sendCyclePauseConfig() { ... }      // Mode Simple
function sendCyclePauseConfigOsc() { ... }   // Mode Oscillation

function toggleCyclePauseSection() { ... }
function toggleCyclePauseOscSection() { ... }

// APRÈS (1 fonction paramétrable)
function sendCyclePauseConfig(mode) {
  const suffix = mode === 'oscillation' ? 'Osc' : '';
  const section = document.querySelector(
    `.section-collapsible:has(#cyclePause${suffix}HeaderText)`
  );
  const enabled = !section.classList.contains('collapsed');
  const isRandom = document.getElementById(`pauseModeRandom${suffix}`).checked;
  
  const config = {
    enabled: enabled,
    isRandom: isRandom,
    pauseDurationSec: parseFloat(document.getElementById(`cyclePauseDuration${suffix}`).value),
    minPauseSec: parseFloat(document.getElementById(`cyclePauseMin${suffix}`).value),
    maxPauseSec: parseFloat(document.getElementById(`cyclePauseMax${suffix}`).value)
  };
  
  const command = mode === 'oscillation' ? 'updateCyclePauseOsc' : 'updateCyclePause';
  sendCommand(command, config);
}
```

---

## 🏗️ PHASE 3 - Architecture Avancée (Risque élevé)

### 3.1 Backend - State Machine Formelle
**Effort**: 12h | **Impact**: Robustesse +40%, Bugs -60%

**État actuel**: Transitions implicites via `currentState`

**Architecture cible**:
```cpp
// StateMachine.h
class StateMachine {
public:
  enum class State { INIT, CALIBRATING, READY, RUNNING, PAUSED, ERROR };
  enum class Event { CALIBRATE, START, PAUSE, RESUME, STOP, ERROR_DETECTED, CALIBRATION_DONE };
  
  State getCurrentState() const;
  bool canTransition(Event event) const;
  bool transition(Event event);
  
  // Callbacks pour actions sur transition
  void onEnterState(State state, std::function<void()> callback);
  void onExitState(State state, std::function<void()> callback);
  
private:
  State m_state = State::INIT;
  std::map<std::pair<State, Event>, State> m_transitions;
  std::map<State, std::function<void()>> m_enterCallbacks;
  std::map<State, std::function<void()>> m_exitCallbacks;
  
  void initTransitions();
};
```

**Matrice de transitions**:
| État actuel | Événement | État suivant |
|-------------|-----------|--------------|
| INIT | CALIBRATE | CALIBRATING |
| CALIBRATING | CALIBRATION_DONE | READY |
| CALIBRATING | ERROR_DETECTED | ERROR |
| READY | START | RUNNING |
| READY | CALIBRATE | CALIBRATING |
| RUNNING | PAUSE | PAUSED |
| RUNNING | STOP | READY |
| RUNNING | ERROR_DETECTED | ERROR |
| PAUSED | RESUME | RUNNING |
| PAUSED | STOP | READY |
| ERROR | STOP | READY |

---

### 3.2 Frontend - Module Pattern
**Effort**: 16h | **Impact**: Encapsulation + Testabilité

**Architecture cible**:
```javascript
// modes/oscillation.js
const OscillationMode = (function() {
  // Private state
  let _config = {
    centerMM: 100,
    amplitudeMM: 20,
    frequencyHz: 1.0,
    waveform: 0
  };
  
  // Private methods
  function _validateLimits() { /* ... */ }
  function _updatePresetButtons() { /* ... */ }
  
  // Public API
  return {
    init: function() {
      _attachEventListeners();
      _initPresets();
    },
    
    update: function(data) {
      if (!data.oscillation) return;
      _syncUIFromBackend(data.oscillation);
      _updatePresetButtons();
    },
    
    sendConfig: function() {
      if (!_validateLimits()) return false;
      sendCommand(CMD.SET_OSCILLATION_CONFIG, _config);
      return true;
    },
    
    start: function() {
      if (!canStartOperation()) return;
      sendCommand(CMD.START_OSCILLATION);
    },
    
    stop: function() {
      sendCommand(CMD.STOP_OSCILLATION);
    },
    
    getConfig: function() {
      return { ..._config };
    }
  };
})();

// Initialisation
document.addEventListener('DOMContentLoaded', function() {
  OscillationMode.init();
});
```

---

### 3.3 Backend - Injection de Dépendances
**Effort**: 8h | **Impact**: Testabilité unitaire

**Concept**:
```cpp
// Interface abstraite
class IMotorDriver {
public:
  virtual void step(bool direction) = 0;
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isContactActive() = 0;
};

// Implémentation réelle
class HSS86Driver : public IMotorDriver {
  void step(bool direction) override {
    digitalWrite(DIR_PIN, direction);
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(PULSE_WIDTH_US);
    digitalWrite(STEP_PIN, LOW);
  }
  // ...
};

// Mock pour tests
class MockMotorDriver : public IMotorDriver {
  int stepCount = 0;
  void step(bool direction) override { stepCount++; }
  // ...
};

// Injection dans MotorController
class MotorController {
public:
  MotorController(IMotorDriver& driver) : m_driver(driver) {}
  // ...
private:
  IMotorDriver& m_driver;
};
```

---

## 📅 Planning Recommandé

### Sprint 1 (Semaine 1) - Quick Wins
| Jour | Tâche | Durée |
|------|-------|-------|
| J1 | 1.1 Extraction constantes magiques | 2h |
| J1 | 1.3 Constantes WS commands frontend | 1h |
| J2 | 1.2 Extraction CSS | 2h |
| J2 | Tests manuels + ajustements | 1h |
| J3 | Commit + Push "Phase 1 Complete" | 0.5h |

### Sprint 2 (Semaines 2-3) - Refactoring
| Semaine | Tâche | Durée |
|---------|-------|-------|
| S2-J1/J2 | 2.2 Split updateUI() | 3h |
| S2-J3/J4 | 2.3 Factoriser duplication | 4h |
| S2-J5 | Tests + ajustements | 2h |
| S3 | 2.1 Split backend (4 premiers modules) | 8h |

### Sprint 3 (Semaines 4-5) - Architecture
| Semaine | Tâche | Durée |
|---------|-------|-------|
| S4 | 3.1 State Machine backend | 12h |
| S5 | 3.2 Module Pattern frontend | 16h |

---

## ✅ Checklist de Validation

### Après chaque modification:
- [ ] Compilation réussie (PlatformIO build)
- [ ] Upload sur ESP32 fonctionnel
- [ ] WebSocket connecte correctement
- [ ] Calibration fonctionne
- [ ] Mode Simple: aller-retour OK
- [ ] Mode Oscillation: cycle complet OK
- [ ] Mode Chaos: patterns variés OK
- [ ] Séquenceur: lecture séquence OK
- [ ] Playlists: sauvegarde/chargement OK
- [ ] Stats: mise à jour distance totale OK

### Tests de régression critiques:
- [ ] Debouncing contact (pas de double trigger)
- [ ] Arrêt d'urgence via Stop button
- [ ] Reconnexion WebSocket après déconnexion
- [ ] Limites de course respectées
- [ ] Vitesse max 300mm/s non dépassée

---

## 📝 Notes Importantes

### Risques identifiés:
1. **Mémoire ESP32**: Surveiller heap après chaque split (> 100KB free)
2. **Timing critique**: Ne pas modifier les ISR ou le step timing
3. **WebSocket latence**: Garder les messages < 1KB

### Ne PAS modifier:
- `STEPS_PER_MM` (6.0) - Calibré hardware
- Logique ISR `contactISR()`
- Séquence de calibration (timings critiques)
- Format JSON des messages WS (breaking change frontend)

### Métriques à surveiller:
```cpp
// Ajouter dans sendStatus()
doc["debug"]["heapFree"] = ESP.getFreeHeap();
doc["debug"]["heapMin"] = ESP.getMinFreeHeap();
doc["debug"]["loopTimeUs"] = lastLoopDuration;
```

---

## 🔗 Ressources

- **Backup commit**: `f2c9d37`
- **Repo**: https://github.com/JujuVass/freenove_esp32_s3_wroom
- **Analyse backend complète**: Voir conversation du 4 décembre 2025
- **Analyse frontend complète**: Voir conversation du 4 décembre 2025

---

*Document généré le 4 décembre 2025 - Version 1.0*
