# Architecture des Headers - ESP32 Stepper Controller

## Vue d'ensemble

Ce document décrit la stratégie d'includes et la propriété des données
après la refactorisation Phase 4D (Décembre 2024).

---

## Hiérarchie des Includes

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        COUCHE FONDATION                                 │
│  (Pas de dépendances projet, uniquement Arduino/libs externes)          │
├─────────────────────────────────────────────────────────────────────────┤
│  Types.h          → Structures de données (enums, structs)              │
│  Config.h         → Constantes de configuration                         │
│  ChaosPatterns.h  → Définitions des patterns chaos                      │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                        COUCHE UTILITAIRE                                │
├─────────────────────────────────────────────────────────────────────────┤
│  UtilityEngine.h  → SystemConfig, engine*, fonctions utilitaires        │
│  GlobalState.h    → Variables globales partagées (motion, steps...)     │
│  Validators.h     → Fonctions inline de validation (pure, sans état)    │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                        COUCHE HARDWARE                                  │
├─────────────────────────────────────────────────────────────────────────┤
│  hardware/MotorDriver.h     → Singleton Motor, contrôle bas niveau      │
│  hardware/ContactSensors.h  → Singleton Contacts, gestion capteurs      │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                        COUCHE CONTRÔLEURS                               │
│  (Chaque module possède ses propres données via extern + définition)    │
├─────────────────────────────────────────────────────────────────────────┤
│  movement/BaseMovementController.h  → Orchestrateur mouvement (base)    │
│  movement/OscillationController.h → oscillation, oscillationState...    │
│  movement/ChaosController.h       → chaos, chaosState                   │
│  movement/PursuitController.h     → pursuit                             │
│  movement/DecelZoneController.h   → decelZone                           │
│  sequencer/SequenceExecutor.h     → seqState, currentMovement           │
│  sequencer/SequenceTableManager.h → sequenceTable[], sequenceLineCount  │
│  controllers/CalibrationManager.h → Calibration                         │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                        COUCHE COMMUNICATION                             │
│  (Inclut TOUS les contrôleurs pour dispatcher les commandes)            │
├─────────────────────────────────────────────────────────────────────────┤
│  communication/CommandDispatcher.h  → Réception commandes WebSocket     │
│  communication/StatusBroadcaster.h  → Envoi status JSON                 │
│  web/APIRoutes.h                    → Routes HTTP REST                  │
│  web/FilesystemManager.h            → Gestion LittleFS                  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Propriété des Données (Pattern Extern)

### Principe
Chaque module **possède** ses données :
- **Définition** dans le `.cpp` du module
- **Extern** dans le `.h` du module (AVANT la classe si utilisé par inline)
- Les autres modules qui ont besoin de ces données **incluent le header**

### Table de Propriété

| Variable(s)                      | Propriétaire (définition)     | Header à inclure                    |
|----------------------------------|-------------------------------|-------------------------------------|
| `config`                         | main (.ino)                   | `GlobalState.h`                     |
| `currentStep`, `targetStep`...   | main (.ino)                   | `GlobalState.h`                     |
| `motion`, `pendingMotion`        | main (.ino)                   | `GlobalState.h`                     |
| `sequenceTable[]`, `Count`       | SequenceTableManager.cpp      | `sequencer/SequenceTableManager.h`  |
| `seqState`, `currentMovement`    | SequenceExecutor.cpp          | `sequencer/SequenceExecutor.h`      |
| `chaos`, `chaosState`            | ChaosController.cpp           | `movement/ChaosController.h`        |
| `oscillation`, `oscillationState`| OscillationController.cpp     | `movement/OscillationController.h`  |
| `pursuit`                        | PursuitController.cpp         | `movement/PursuitController.h`      |
| `decelZone`                      | DecelZoneController.cpp       | `movement/DecelZoneController.h`    |
| `engine`                         | UtilityEngine.cpp             | `UtilityEngine.h`                   |

---

## Règles d'Include

### ✅ À FAIRE

1. **Inclure le header du propriétaire** pour accéder à une donnée
   ```cpp
   #include "movement/ChaosController.h"  // Pour accéder à chaos, chaosState
   ```

2. **Extern AVANT la classe** si des méthodes inline utilisent ces variables
   ```cpp
   extern ChaosRuntimeConfig chaos;  // Avant class ChaosController
   class ChaosController { ... };
   ```

3. **Un seul endroit pour chaque extern** - pas de duplication

### ❌ À ÉVITER

1. **NE PAS** déclarer extern dans plusieurs headers
   ```cpp
   // MAUVAIS - extern dans GlobalState.h ET dans ChaosController.h
   ```

2. **NE PAS** forward-declare + extern si un include suffit
   ```cpp
   // MAUVAIS
   class UtilityEngine;
   extern UtilityEngine* engine;
   
   // BON
   #include "UtilityEngine.h"
   ```

3. **NE PAS** inclure des headers lourds dans des headers légers
   ```cpp
   // Types.h ne doit PAS inclure GlobalState.h
   ```

---

## Singleton Pattern Utilisé

Chaque contrôleur utilise le pattern Meyers Singleton :

```cpp
// Header
class ChaosController {
public:
    static ChaosController& getInstance();
    // ...
};
extern ChaosController& Chaos;  // Alias global

// CPP
ChaosController& ChaosController::getInstance() {
    static ChaosController instance;
    return instance;
}
ChaosController& Chaos = ChaosController::getInstance();
```

**Accès** : `Chaos.start()` ou `ChaosController::getInstance().start()`

---

## Fichiers Clés

| Fichier | Rôle |
|---------|------|
| `Types.h` | Toutes les structures/enums (pas de logique) |
| `Config.h` | Constantes compilées (#define, const) |
| `GlobalState.h` | Extern des variables partagées cross-modules |
| `UtilityEngine.h` | SystemConfig runtime + fonctions helper |

---

## Historique Refactoring

- **Phase 4A-4C** : Extraction modules, création singletons
- **Phase 4D** : Migration propriété données vers modules
  - sequenceTable → SequenceTableManager
  - seqState/currentMovement → SequenceExecutor  
  - chaos/chaosState → ChaosController
  - oscillation/* → OscillationController
  - pursuit → PursuitController
  - decelZone → DecelZoneController

---

*Dernière mise à jour : Décembre 2024*
