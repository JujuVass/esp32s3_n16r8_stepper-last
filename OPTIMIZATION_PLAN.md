# 🚀 Plan d'Optimisation - ESP32 Stepper Controller

> **Date**: 4 décembre 2025  
> **Backup commit**: `f2c9d37` - "BACKUP: Pre-optimization state"  
> **État actuel**: Fonctionnel, 6660 lignes backend + 9887 lignes frontend
> 
> **Mise à jour**: 4 décembre 2025 - Analyse architecturale complète
> **Commits optimisations**: 
> - `405eac0` - "Phase 1 Optimizations" (Config.h, CSS, WS_CMD)
> - `268b038` - "Phase 2.4 COMPLETE: All sendCommand migrated to WS_CMD constants"

---

## 🎯 PRIORITÉ ACTUELLE: MODULARISATION BACKEND

> **Objectif**: Découper `stepper_controller_restructured.ino` (6660 lignes) en modules maintenables
> **Impact attendu**: Maintenabilité +60%, Compilation incrémentale, Tests unitaires possibles
> **Durée estimée**: 3-4 jours

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

### 2.1 🔥 MODULARISATION BACKEND (EN COURS)
**Effort**: 3-4 jours | **Impact**: Maintenabilité +60%  
**Status**: 🔄 EN COURS

#### 📁 Structure Cible

```
src/
├── main.cpp                      # Entry point (~200 lignes)
│                                 # setup(), loop(), includes
│
├── movement/
│   ├── VaetController.h/cpp      # VA-ET-VIENT (~500 lignes)
│   │   - doVaetStep()
│   │   - calculateStepDelay()
│   │   - handleCyclePause()
│   │   - applyPendingChanges()
│   │
│   ├── OscillationController.h/cpp  # OSCILLATION (~700 lignes)
│   │   - calculateOscillationPosition()
│   │   - startOscillation()
│   │   - stopOscillation()
│   │   - handleOscillationTransitions()
│   │
│   ├── ChaosController.h/cpp     # CHAOS (~1200 lignes)
│   │   - generateChaosPattern()
│   │   - executeChaosStep()
│   │   - startChaos() / stopChaos()
│   │   - Pattern implementations (ZIGZAG, WAVE, BRUTE_FORCE...)
│   │
│   ├── PursuitController.h/cpp   # PURSUIT (~400 lignes)
│   │   - updatePursuitTarget()
│   │   - doPursuitStep()
│   │   - enablePursuit() / disablePursuit()
│   │
│   └── CalibrationController.h/cpp  # CALIBRATION (~500 lignes)
│       - startCalibration()
│       - calibrationLoop()
│       - measureTotalDistance()
│
├── sequencer/
│   ├── SequenceEngine.h/cpp      # Moteur séquenceur (~600 lignes)
│   │   - startSequence()
│   │   - processSequenceExecution()
│   │   - positionForNextLine()
│   │   - onMovementComplete()
│   │
│   └── SequenceLine.h/cpp        # Gestion lignes (~300 lignes)
│       - addLine() / deleteLine()
│       - moveLine() / duplicateLine()
│       - serializeToJson()
│
├── communication/
│   ├── WebSocketHandler.h/cpp    # Handler WS (~800 lignes)
│   │   - webSocketEvent()
│   │   - processCommand()
│   │   - Command dispatch (Pattern Command)
│   │
│   └── StatusBroadcaster.h/cpp   # Broadcast status (~400 lignes)
│       - sendStatus()
│       - sendSequenceStatus()
│       - sendError() / sendLog()
│
└── hardware/
    ├── MotorDriver.h/cpp         # Abstraction HSS86 (~200 lignes)
    │   - stepMotor()
    │   - setMotorDirection()
    │   - enable() / disable()
    │
    └── ContactSensors.h/cpp      # Gestion contacts (~150 lignes)
        - readContactDebounced()
        - isAtStartContact()
        - isAtEndContact()
```

#### 📋 Plan de Découpage (Ordre Recommandé)

| Étape | Module | Lignes | Couplage | Risque |
|-------|--------|--------|----------|--------|
| 1 | `hardware/MotorDriver` | ~200 | Faible | ⭐ |
| 2 | `hardware/ContactSensors` | ~150 | Faible | ⭐ |
| 3 | `movement/CalibrationController` | ~500 | Moyen | ⭐⭐ |
| 4 | `movement/PursuitController` | ~400 | Moyen | ⭐⭐ |
| 5 | `movement/OscillationController` | ~700 | Moyen | ⭐⭐ |
| 6 | `movement/ChaosController` | ~1200 | Moyen | ⭐⭐⭐ |
| 7 | `movement/VaetController` | ~500 | Fort | ⭐⭐⭐ |
| 8 | `sequencer/SequenceEngine` | ~600 | Fort | ⭐⭐⭐ |
| 9 | `communication/StatusBroadcaster` | ~400 | Moyen | ⭐⭐ |
| 10 | `communication/WebSocketHandler` | ~800 | Fort | ⭐⭐⭐⭐ |

#### 🔧 Étape 1: MotorDriver (Base Hardware)

**Créer** `include/hardware/MotorDriver.h`:
```cpp
#pragma once

#include <Arduino.h>
#include "Config.h"

class MotorDriver {
public:
    static MotorDriver& getInstance();
    
    void init();
    void stepMotor();
    void setDirection(bool forward);
    void enable();
    void disable();
    bool isEnabled() const;
    
    // Référence à currentStep externe (dans main.cpp)
    void setStepCounter(volatile long* stepPtr);
    
private:
    MotorDriver() = default;
    volatile long* m_currentStep = nullptr;
    bool m_enabled = false;
    bool m_direction = true;
};

// Macro pour accès global simplifié
#define Motor MotorDriver::getInstance()
```

**Créer** `src/hardware/MotorDriver.cpp`:
```cpp
#include "hardware/MotorDriver.h"

MotorDriver& MotorDriver::getInstance() {
    static MotorDriver instance;
    return instance;
}

void MotorDriver::init() {
    pinMode(PIN_STEP, OUTPUT);
    pinMode(PIN_DIR, OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    disable();
}

void MotorDriver::stepMotor() {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(STEP_PULSE_WIDTH_US);
    digitalWrite(PIN_STEP, LOW);
}

void MotorDriver::setDirection(bool forward) {
    m_direction = forward;
    digitalWrite(PIN_DIR, forward ? MOTOR_DIR_FORWARD : MOTOR_DIR_BACKWARD);
}

void MotorDriver::enable() {
    digitalWrite(PIN_ENABLE, LOW);  // Active LOW
    m_enabled = true;
}

void MotorDriver::disable() {
    digitalWrite(PIN_ENABLE, HIGH);
    m_enabled = false;
}

bool MotorDriver::isEnabled() const {
    return m_enabled;
}

void MotorDriver::setStepCounter(volatile long* stepPtr) {
    m_currentStep = stepPtr;
}
```

#### 🔧 Étape 2: ContactSensors

**Créer** `include/hardware/ContactSensors.h`:
```cpp
#pragma once

#include <Arduino.h>
#include "Config.h"

class ContactSensors {
public:
    static ContactSensors& getInstance();
    
    void init();
    
    // Lecture avec debounce
    bool readStartContact(uint8_t samples = 3, uint16_t delayUs = 50);
    bool readEndContact(uint8_t samples = 3, uint16_t delayUs = 50);
    
    // Lecture rapide (sans debounce)
    bool isStartContactActive() const;
    bool isEndContactActive() const;
    
private:
    ContactSensors() = default;
    bool readContactDebounced(uint8_t pin, uint8_t activeState, 
                              uint8_t samples, uint16_t delayUs);
};

#define Contacts ContactSensors::getInstance()
```

#### 🔧 Étape 3: Pattern Command pour WebSocket

**Créer** `include/communication/CommandHandler.h`:
```cpp
#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <map>

// Type de callback pour commandes
using CommandCallback = std::function<void(JsonDocument& request, JsonDocument& response)>;

class CommandDispatcher {
public:
    static CommandDispatcher& getInstance();
    
    // Enregistrer un handler
    void registerCommand(const String& cmd, CommandCallback callback);
    
    // Dispatcher une commande
    bool dispatch(const String& cmd, JsonDocument& request, JsonDocument& response);
    
private:
    CommandDispatcher() = default;
    std::map<String, CommandCallback> m_handlers;
};

// Macro pour enregistrement simplifié
#define REGISTER_COMMAND(cmd, handler) \
    CommandDispatcher::getInstance().registerCommand(cmd, handler)
```

**Usage dans setup()**:
```cpp
void setupCommands() {
    // Core commands
    REGISTER_COMMAND("start", [](auto& req, auto& res) {
        startMovement();
        res["success"] = true;
    });
    
    REGISTER_COMMAND("stop", [](auto& req, auto& res) {
        stopMovement();
        res["success"] = true;
    });
    
    REGISTER_COMMAND("setParams", [](auto& req, auto& res) {
        if (req.containsKey("startPosition")) {
            motion.startPositionMM = req["startPosition"].as<float>();
        }
        // ... autres params
        res["success"] = true;
    });
    
    // Oscillation commands
    REGISTER_COMMAND("startOscillation", [](auto& req, auto& res) {
        startOscillation();
        res["success"] = true;
    });
    
    // ... etc
}
```

#### 📊 Checklist de Progression

**Étape 1 - Hardware Abstraction** [ ]
- [ ] Créer `include/hardware/MotorDriver.h`
- [ ] Créer `src/hardware/MotorDriver.cpp`
- [ ] Créer `include/hardware/ContactSensors.h`
- [ ] Créer `src/hardware/ContactSensors.cpp`
- [ ] Modifier `main.cpp` pour utiliser Motor et Contacts
- [ ] Compiler et tester calibration

**Étape 2 - CalibrationController** [ ]
- [ ] Extraire `startCalibration()` et fonctions liées
- [ ] Créer `include/movement/CalibrationController.h`
- [ ] Créer `src/movement/CalibrationController.cpp`
- [ ] Tester calibration complète

**Étape 3 - PursuitController** [ ]
- [ ] Extraire `updatePursuitTarget()`, `doPursuitStep()`
- [ ] Créer fichiers Pursuit
- [ ] Tester mode pursuit manuel

**Étape 4 - OscillationController** [ ]
- [ ] Extraire `calculateOscillationPosition()`, `startOscillation()`
- [ ] Extraire gestion transitions (freq, amplitude, center)
- [ ] Tester oscillation standalone + séquenceur

**Étape 5 - ChaosController** [ ]
- [ ] Extraire `generateChaosPattern()`, patterns individuels
- [ ] Créer base class `ChaosPatternBase` pour factoriser
- [ ] Tester tous les patterns

**Étape 6 - VaetController** [ ]
- [ ] Extraire `doVaetStep()`, `calculateStepDelay()`
- [ ] Extraire gestion pending changes
- [ ] Tester VA-ET-VIENT complet

**Étape 7 - SequenceEngine** [ ]
- [ ] Extraire `processSequenceExecution()`
- [ ] Extraire gestion lignes séquence
- [ ] Tester séquences multi-modes

**Étape 8 - Communication** [ ]
- [ ] Créer `CommandDispatcher` (Pattern Command)
- [ ] Extraire `sendStatus()` vers `StatusBroadcaster`
- [ ] Migrer `webSocketEvent()` vers dispatcher

#### 🛡️ Règles de Sécurité

1. **Compilation à chaque étape** - Ne jamais accumuler trop de changements
2. **Tests manuels** - Après chaque module extrait :
   - [ ] Calibration fonctionne
   - [ ] Mode concerné fonctionne
   - [ ] WebSocket connecte
3. **Git commit** - Un commit par module extrait
4. **Heap monitoring** - Vérifier `ESP.getFreeHeap() > 100000`

#### 📝 Variables Globales à Centraliser

```cpp
// globals.h - État partagé entre modules
extern volatile long currentStep;
extern volatile long targetStep;
extern volatile long startStep;

extern SystemConfig config;
extern MotionConfig motion;
extern OscillationConfig oscillation;
extern ChaosConfig chaos;
extern PursuitState pursuit;
extern SequencerState seqState;

extern float totalDistanceTraveled;
extern float effectiveMaxDistanceMM;
extern bool isPaused;
```

---

### 2.2 Frontend - Split updateUI() (REPORTÉ)
**Status**: ⏸️ REPORTÉ - Après modularisation backend
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

### 🔥 Sprint Actuel - Modularisation Backend (3-4 jours)

| Jour | Tâche | Modules | Status |
|------|-------|---------|--------|
| **J1** | Hardware Abstraction | `MotorDriver`, `ContactSensors` | ⏳ |
| **J1** | Test compilation + calibration | - | ⏳ |
| **J2** | Controllers Simples | `CalibrationController`, `PursuitController` | ⏳ |
| **J2** | Test modes concernés | - | ⏳ |
| **J3** | Controllers Complexes | `OscillationController`, `VaetController` | ⏳ |
| **J3** | Test oscillation + VA-ET-VIENT | - | ⏳ |
| **J4** | Chaos + Sequencer | `ChaosController`, `SequenceEngine` | ⏳ |
| **J4** | Test intégration complète | - | ⏳ |
| **J5** | Communication | `CommandDispatcher`, `StatusBroadcaster` | ⏳ |
| **J5** | Tests finaux + commit | - | ⏳ |

### Sprints Futurs (Post-Modularisation)

| Sprint | Focus | Durée |
|--------|-------|-------|
| **Sprint 2** | Tests unitaires PlatformIO | 2 jours |
| **Sprint 3** | Frontend - Split updateUI() | 1 jour |
| **Sprint 4** | Frontend - Modularisation JS | 3 jours |
| **Sprint 5** | State Machine formelle | 2 jours |

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

### 🔥 Notes Spécifiques - Modularisation Backend

**Dépendances entre modules**:
```
MotorDriver ← (base, aucune dépendance)
ContactSensors ← (base, aucune dépendance)
    ↓
CalibrationController ← MotorDriver, ContactSensors
PursuitController ← MotorDriver
    ↓
OscillationController ← MotorDriver
VaetController ← MotorDriver
ChaosController ← MotorDriver
    ↓
SequenceEngine ← Tous les Controllers
    ↓
WebSocketHandler ← Tous les modules
StatusBroadcaster ← État global
```

**Variables critiques à NE PAS dupliquer**:
- `currentStep` - Position moteur (volatile, unique)
- `config.currentState` - État système (enum unique)
- `config.totalDistanceMM` - Distance calibrée
- `seqState` - État séquenceur

**Fichiers à modifier dans platformio.ini**:
```ini
; Après modularisation, ajouter:
build_src_filter = 
    +<*>
    +<movement/>
    +<sequencer/>
    +<communication/>
    +<hardware/>
```

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

*Document mis à jour le 4 décembre 2025 - Version 2.0 (Focus Modularisation Backend)*
