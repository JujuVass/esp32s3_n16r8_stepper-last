# Analyse Complète - 3 Prochains Modules d'Extraction

**Date**: Analyse mise à jour
**État actuel**: ~6232 lignes dans `stepper_controller_restructured.ino`
**Objectif**: Réduire à ~3000 lignes via extraction modulaire

---

## 📊 Résumé des Extractions

| Module | Lignes estimées | Complexité | Dépendances | Priorité |
|--------|----------------|------------|-------------|----------|
| **CommandDispatcher** | ~850 lignes | 🟡 Moyenne | WebSocket, tous handlers | 1 |
| **VaetController** | ~700 lignes | 🟢 Faible | Motor, motion struct | 2 |
| **ChaosController** | ~1200 lignes | 🔴 Élevée | Motor, chaos struct, patterns | 3 |

**Total estimé**: ~2750 lignes → fichier main réduit à ~3500 lignes

---

## 🔷 MODULE 1: CommandDispatcher (~850 lignes)

### 1.1 Périmètre d'Extraction

**Fonctions à extraire** (lignes 3932-4800):
```
handleBasicCommands()      - L3932-4019  (87 lignes)
handleConfigCommands()     - L4021-4071  (50 lignes)
handleDecelZoneCommands()  - L4073-4108  (35 lignes)
handleCyclePauseCommands() - L4110-4170  (60 lignes)
handlePursuitCommands()    - L4172-4226  (54 lignes)
handleChaosCommands()      - L4228-4328  (100 lignes)
handleOscillationCommands()- L4330-4515  (185 lignes)
handleSequencerCommands()  - L4517-4740  (223 lignes)
webSocketEvent()           - L4741-4800  (59 lignes)
parseJsonCommand()         - L4800-4815  (15 lignes)
```

**Total**: ~868 lignes

### 1.2 Architecture Proposée

```cpp
// include/CommandDispatcher.h
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>

// Forward declarations
class CommandDispatcher {
public:
    // Singleton
    static CommandDispatcher& getInstance();
    
    // Main entry point (called by webSocketEvent)
    void handleCommand(uint8_t clientNum, const String& message);
    
    // WebSocket event handler (registered with webSocket.onEvent)
    void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    
    // Initialization
    void begin(WebSocketsServer* ws);
    
private:
    CommandDispatcher() = default;
    WebSocketsServer* webSocket = nullptr;
    
    // Command handlers (private - internal routing)
    bool handleBasicCommands(const char* cmd, JsonDocument& doc);
    bool handleConfigCommands(const char* cmd, JsonDocument& doc);
    bool handleDecelZoneCommands(const char* cmd, JsonDocument& doc, const String& message);
    bool handleCyclePauseCommands(const char* cmd, JsonDocument& doc);
    bool handlePursuitCommands(const char* cmd, JsonDocument& doc);
    bool handleChaosCommands(const char* cmd, JsonDocument& doc, const String& message);
    bool handleOscillationCommands(const char* cmd, JsonDocument& doc, const String& message);
    bool handleSequencerCommands(const char* cmd, JsonDocument& doc, const String& message);
    
    // Helpers
    bool parseJsonCommand(const String& jsonStr, JsonDocument& doc);
};

// Global accessor
extern CommandDispatcher& Dispatcher;
```

### 1.3 Dépendances Identifiées

**Dépendances EXTERNES (vers fonctions du main)**:
```cpp
// Appelées par les handlers - doivent être extern ou via callback
extern void startMovement(float distMM, float speedLevel);
extern void stopMovement();
extern void togglePause();
extern void sendStatus();
extern void sendError(String message);
extern void returnToStart();
extern void resetTotalDistance();
extern void saveCurrentSessionStats();
extern void setDistance(float dist);
extern void setStartPosition(float startMM);
extern void setSpeedForward(float speed);
extern void setSpeedBackward(float speed);
extern void startChaos();
extern void stopChaos();
extern void startOscillation();
extern void startSequenceExecution(bool loopMode);
extern void stopSequenceExecution();
extern void pursuitMove(float targetMM, float maxSpeed);
extern void broadcastSequenceTable();
extern void sendSequenceStatus();

// Variables globales nécessaires
extern MotionConfig motion;
extern ChaosConfig chaos;
extern OscillationConfig oscillation;
extern DecelZoneConfig decelZone;
extern SystemConfig config;
extern SequencerState seqState;
extern float effectiveMaxDistanceMM;
extern float maxDistanceLimitPercent;
extern bool isPaused;
extern MovementType currentMovement;
```

### 1.4 Stratégie d'Extraction

**Option A - Extern Functions (Recommandé)**:
- Déclarer les fonctions comme `extern` dans CommandDispatcher.h
- Implémentation reste dans main.ino
- Avantage: Pas de refactoring des fonctions existantes

**Option B - Callbacks**:
- Passer des pointeurs de fonction au Dispatcher
- Plus propre mais plus de travail initial

### 1.5 Risques et Mitigations

| Risque | Impact | Mitigation |
|--------|--------|------------|
| Références circulaires | Élevé | Utiliser forward declarations |
| Ordre d'initialisation | Moyen | Initialiser Dispatcher dans setup() après globals |
| État partagé | Moyen | Documenter les extern clairement |

---

## 🔷 MODULE 2: VaetController (~700 lignes)

### 2.1 Périmètre d'Extraction

**Fonctions à extraire**:
```
startMovement()           - L1200-1294  (94 lignes)
updateEffectiveMaxDistance() - L1296-1302  (6 lignes)
calculateStepDelay()      - L1304-1362  (58 lignes)
doStep()                  - L1364-1689  (325 lignes) ⚠️ GROS
checkChaosLimits()        - L1590-1689  (helper, peut rester)
togglePause()             - L1691-1711  (20 lignes)
stopMovement()            - L1713-1753  (40 lignes)
setDistance()             - L1755-1785  (30 lignes)
setStartPosition()        - L1787-1849  (62 lignes)
setSpeedForward()         - L1851-1889  (38 lignes)
setSpeedBackward()        - L1891-1935  (44 lignes)
resetTotalDistance()      - L1868-1876  (8 lignes)
saveCurrentSessionStats() - L1878-1907  (29 lignes)
pursuitMove()             - L1937-2050  (113 lignes)
doPursuitStep()           - L2052-2150  (~98 lignes)
```

**Total**: ~700 lignes (doStep = 325!)

### 2.2 Architecture Proposée

```cpp
// include/VaetController.h
#pragma once

#include <Arduino.h>
#include "Types.h"
#include "MotorDriver.h"

class VaetController {
public:
    static VaetController& getInstance();
    
    // Initialization
    void begin();
    
    // Movement control
    void startMovement(float distMM, float speedLevel);
    void stopMovement();
    void togglePause();
    
    // Step execution (called from main loop)
    void doStep();
    
    // Parameter updates
    void setDistance(float dist);
    void setStartPosition(float startMM);
    void setSpeedForward(float speed);
    void setSpeedBackward(float speed);
    
    // Pursuit mode
    void pursuitMove(float targetMM, float maxSpeed);
    void doPursuitStep();
    
    // Stats
    void resetTotalDistance();
    void saveCurrentSessionStats();
    
    // State queries
    bool isRunning() const;
    bool isPaused() const;
    
private:
    VaetController() = default;
    
    void calculateStepDelay();
    void updateEffectiveMaxDistance();
    bool checkChaosLimits();  // Helper for chaos mode safety
    
    // Internal state
    unsigned long stepDelayMicrosForward = 1000;
    unsigned long stepDelayMicrosBackward = 1000;
};

extern VaetController& Vaet;
```

### 2.3 Dépendances Identifiées

**Dépendances vers modules existants**:
```cpp
#include "MotorDriver.h"      // Motor.step(), Motor.setDirection()
#include "ContactSensors.h"   // Contacts.readDebounced()
#include "CalibrationManager.h" // Calibration.startCalibration()
#include "UtilityEngine.h"    // engine->info(), incrementDailyStats()
#include "Validators.h"       // validateSpeed(), validateDistance()
```

**Variables globales requises**:
```cpp
extern SystemConfig config;
extern MotionConfig motion;
extern PendingMotion pendingMotion;
extern PursuitState pursuit;
extern long currentStep;
extern long startStep, targetStep;
extern bool movingForward;
extern bool isPaused;
extern bool hasReachedStartStep;
extern unsigned long lastStepMicros;
extern float effectiveMaxDistanceMM;
extern float maxDistanceLimitPercent;
extern unsigned long totalDistanceTraveled;
extern unsigned long lastSavedDistance;
extern MovementType currentMovement;
```

### 2.4 Défi Principal: `doStep()`

La fonction `doStep()` fait 325 lignes et contient:
- Logique drift detection (START et END)
- Cycle completion
- Pause entre cycles
- Integration avec Chaos mode

**Solution proposée**:
```cpp
void VaetController::doStep() {
    if (movingForward) {
        doStepForward();  // ~150 lignes
    } else {
        doStepBackward(); // ~175 lignes
    }
}

// Sous-fonctions privées
void doStepForward();
void doStepBackward();
bool checkDriftEnd();     // ~30 lignes
bool checkDriftStart();   // ~30 lignes
void handleCycleEnd();    // ~50 lignes
```

### 2.5 Risques et Mitigations

| Risque | Impact | Mitigation |
|--------|--------|------------|
| doStep() trop couplé | Élevé | Extraire sous-fonctions d'abord |
| Timing critique | Élevé | Tests de performance après extraction |
| État partagé avec Chaos | Moyen | Interface claire pour checkChaosLimits() |

---

## 🔷 MODULE 3: ChaosController (~1200 lignes)

### 3.1 Périmètre d'Extraction

**Fonctions à extraire**:
```
// Chaos mode helpers (L2730-2770)
calculateChaosLimits()         - L2744-2750  (6 lignes)
calculateMaxPossibleAmplitude()- L2752-2765  (13 lignes)
forceDirectionAtLimits()       - L2767-2783  (16 lignes)

// Pattern generation (L2785-3220)
generateChaosPattern()         - L2785-3220  (435 lignes) ⚠️ ÉNORME

// Step delay calculation
calculateChaosStepDelay()      - L3222-3262  (40 lignes)

// Execution (L3264-3780)
processChaosExecution()        - L3264-3780  (516 lignes) ⚠️ ÉNORME

// Control functions (L3784-3930)
startChaos()                   - L3784-3905  (121 lignes)
stopChaos()                    - L3907-3930  (23 lignes)
```

**Total**: ~1170 lignes (2 fonctions de 435 et 516!)

### 3.2 Architecture Proposée

```cpp
// include/ChaosController.h
#pragma once

#include <Arduino.h>
#include "Types.h"
#include "ChaosPatterns.h"  // Existing pattern configs

class ChaosController {
public:
    static ChaosController& getInstance();
    
    // Initialization
    void begin();
    
    // Control
    void start();
    void stop();
    
    // Main execution (called from loop)
    void process();
    
    // Configuration
    void setConfig(const ChaosConfig& cfg);
    ChaosConfig getConfig() const;
    
    // State queries
    bool isRunning() const;
    ChaosState getState() const;
    
private:
    ChaosController() = default;
    
    // Pattern generation
    void generatePattern();
    void generateZigzag(float craziness);
    void generateSweep(float craziness);
    void generatePulse(float craziness);
    void generateDrift(float craziness);
    void generateBurst(float craziness);
    void generateWave(float craziness);
    void generatePendulum(float craziness);
    void generateSpiral(float craziness);
    void generateCalm(float craziness);
    void generateBruteForce(float craziness);
    void generateLiberator(float craziness);
    
    // Pattern execution helpers
    void processWave();
    void processCalm();
    void processBruteForce();
    void processLiberator();
    void processPendulum();
    void processSpiral();
    void processSweep();
    void processPulse();
    void processDiscretePattern();
    
    // Utilities
    void calculateStepDelay();
    void calculateLimits(float& minLimit, float& maxLimit);
    float calculateMaxAmplitude(float minLimit, float maxLimit);
    bool forceDirectionAtLimits(float currentPos, float min, float max, bool& forward);
};

extern ChaosController& Chaos;
```

### 3.3 Refactoring de `generateChaosPattern()` (435 lignes)

**Problème**: Un switch géant avec 11 patterns, chacun ~40 lignes.

**Solution - Pattern Strategy**:
```cpp
// Dans ChaosPatterns.h (existant, à étendre)
struct PatternGenerator {
    virtual void generate(ChaosController& ctrl, float craziness) = 0;
};

// Ou simplement: sous-fonctions privées
void ChaosController::generatePattern() {
    float craziness = config.crazinessPercent / 100.0;
    
    switch (state.currentPattern) {
        case CHAOS_ZIGZAG:    generateZigzag(craziness);    break;
        case CHAOS_SWEEP:     generateSweep(craziness);     break;
        case CHAOS_PULSE:     generatePulse(craziness);     break;
        // ... etc
    }
    
    // Common post-processing
    calculateStepDelay();
    state.nextPatternChangeTime = millis() + patternDuration;
}
```

Chaque `generateXxx()` devient une fonction de ~35 lignes.

### 3.4 Refactoring de `processChaosExecution()` (516 lignes)

**Problème**: Logique imbriquée complexe avec switch dans switch.

**Solution - State Machine**:
```cpp
void ChaosController::process() {
    if (!state.isRunning) return;
    
    // 1. Handle pauses
    if (handlePause()) return;
    
    // 2. Check duration limit
    if (checkDurationComplete()) return;
    
    // 3. Generate new pattern if needed
    if (millis() >= state.nextPatternChangeTime) {
        generatePattern();
    }
    
    // 4. Process continuous patterns
    switch (state.currentPattern) {
        case CHAOS_WAVE:        processWave();        break;
        case CHAOS_CALM:        processCalm();        break;
        case CHAOS_BRUTE_FORCE: processBruteForce();  break;
        case CHAOS_LIBERATOR:   processLiberator();   break;
        default:                processDiscretePattern(); break;
    }
    
    // 5. Execute movement
    executeMovement();
}
```

### 3.5 Dépendances Identifiées

```cpp
// Modules existants
#include "MotorDriver.h"
#include "UtilityEngine.h"

// Variables globales
extern ChaosConfig chaos;          // Configuration
extern ChaosState chaosState;      // État runtime
extern long currentStep;
extern long targetStep;
extern bool movingForward;
extern float effectiveMaxDistanceMM;
extern SystemConfig config;
extern MovementType currentMovement;

// Fonctions externes
extern void doStep();              // Du VaetController
extern void onMovementComplete();  // Du Sequencer
extern void stopSequenceExecution();
extern void sendError(String msg);
```

### 3.6 Risques et Mitigations

| Risque | Impact | Mitigation |
|--------|--------|------------|
| 11 patterns à maintenir | Moyen | Factoriser avec config structs |
| État machine complexe | Élevé | Diagramme d'état + tests |
| Intégration séquenceur | Moyen | Interface via onMovementComplete() |
| Performance génération | Faible | Patterns pré-calculés dans config |

---

## 📋 Plan d'Exécution Recommandé

### Phase 1: CommandDispatcher (2-3h)
1. Créer `include/CommandDispatcher.h` et `src/CommandDispatcher.cpp`
2. Déclarer les extern pour toutes les fonctions appelées
3. Migrer les 8 handlers + webSocketEvent
4. Tester compilation + fonctionnalité WebSocket

### Phase 2: VaetController (3-4h)
1. Refactorer `doStep()` en sous-fonctions DANS le main
2. Créer `include/VaetController.h` et `src/VaetController.cpp`
3. Migrer startMovement, stopMovement, togglePause
4. Migrer doStep (déjà refactoré)
5. Migrer setters (setDistance, setSpeed, etc.)
6. Tester tous les modes VAET + Pursuit

### Phase 3: ChaosController (4-5h)
1. Créer `include/ChaosController.h` et `src/ChaosController.cpp`
2. Extraire les 11 fonctions `generateXxx()` 
3. Refactorer `processChaosExecution()` en sous-fonctions
4. Migrer startChaos, stopChaos
5. Tester tous les patterns
6. Tester intégration séquenceur

### Temps Total Estimé: 9-12h

---

## 📏 Métriques Attendues

| Métrique | Avant | Après Phase 3 |
|----------|-------|---------------|
| Lignes main.ino | 6232 | ~3500 |
| Fichiers modules | 8 | 11 |
| Complexité cyclomatique | Très haute | Moyenne |
| Couplage | Élevé | Réduit |

---

## ⚠️ Points d'Attention Critiques

### 1. Timing Step Motor
Les fonctions `doStep()` et `doPursuitStep()` sont time-critical.
- Ne pas ajouter de latence (appels de fonction minimaux)
- Éviter les allocations mémoire dans ces fonctions
- Tester avec oscilloscope si doute

### 2. État Global Partagé
Beaucoup de variables globales partagées entre modules:
- `currentStep`, `targetStep`, `movingForward`
- `config`, `motion`, `chaos`, `oscillation`
- Solution: Documentation claire des responsabilités

### 3. Ordre d'Initialisation
Les singletons doivent être initialisés dans le bon ordre:
1. UtilityEngine (logging)
2. Motor, Contacts
3. CalibrationManager
4. VaetController, ChaosController
5. CommandDispatcher (dernier, utilise tout)

### 4. Intégration Séquenceur
Le séquenceur appelle:
- `startMovement()` → VaetController
- `startChaos()` → ChaosController
- `startOscillation()` → (futur OscillationController)

L'interface `onMovementComplete()` reste dans main pour l'instant.
