# 📊 Analyse Architecture Backend - 4 Décembre 2025

## 📁 Structure Actuelle (Post-Modularisation Phase 1)

```
stepper_controller_restructured.ino (6454 lignes)   ← Principal
├── include/
│   ├── Config.h                  (~200 lignes)     ← Constantes, GPIO, timing
│   ├── Types.h                   (~400 lignes)     ← Structs, enums
│   ├── ChaosPatterns.h           (~300 lignes)     ← Config patterns chaos
│   ├── APIRoutes.h               (~150 lignes)     ← Routes HTTP
│   ├── FilesystemManager.h       (~100 lignes)     ← Gestion fichiers
│   ├── UtilityEngine.h           (~530 lignes)     ← Logging, FS, JSON, Config
│   ├── hardware/
│   │   ├── MotorDriver.h         (~100 lignes)     ✅ NEW
│   │   └── ContactSensors.h      (~120 lignes)     ✅ NEW
│   └── controllers/
│       └── CalibrationManager.h  (~220 lignes)     ✅ NEW
└── src/
    ├── Config.cpp                (~20 lignes)      ✅ NEW
    ├── UtilityEngine.cpp         (~950 lignes)     ← Implémentation logging
    ├── hardware/
    │   ├── MotorDriver.cpp       (~80 lignes)      ✅ NEW
    │   └── ContactSensors.cpp    (~60 lignes)      ✅ NEW
    └── controllers/
        └── CalibrationManager.cpp (~400 lignes)    ✅ NEW
```

**Total**: ~9100 lignes backend (vs 10000+ avant modularisation)

---

## 📈 Catégorisation des Fonctions (.ino)

### 🟢 MIGRÉ vers Modules (~600 lignes extraites)
| Fonction | Module | Status |
|----------|--------|--------|
| `Motor.step()` | MotorDriver | ✅ |
| `Motor.setDirection()` | MotorDriver | ✅ |
| `Motor.enable()/disable()` | MotorDriver | ✅ |
| `Contacts.readDebounced()` | ContactSensors | ✅ |
| `Contacts.isStartContactActive()` | ContactSensors | ✅ |
| `Calibration.startCalibration()` | CalibrationManager | ✅ |

### 🟡 PEUT MIGRER vers UtilityEngine (~200 lignes)
| Fonction | Lignes | Raison |
|----------|--------|--------|
| `serviceWebSocketFor()` | ~8 | Utilitaire WebSocket générique |
| `sendError()` | ~15 | Déjà utilise engine->error(), peut être intégré |
| `sendJsonResponse()` | ~10 | Pattern JSON response |
| `incrementDailyStats()` | ~45 | Gestion stats/fichiers → UtilityEngine |
| `saveCurrentSessionStats()` | ~25 | Gestion stats/fichiers → UtilityEngine |
| `resetTotalDistance()` | ~10 | Lié aux stats |

### 🔵 PEUT CRÉER NOUVEAUX MODULES (~3500 lignes)
| Module Proposé | Fonctions | Lignes | Priorité |
|----------------|-----------|--------|----------|
| **VaetController** | startMovement, doStep, calculateStepDelay, togglePause, stopMovement, setDistance, setStartPosition, setSpeedForward/Backward | ~600 | ⭐⭐ |
| **OscillationController** | startOscillation, doOscillationStep, validateOscillationParams/Amplitude | ~350 | ⭐⭐ |
| **ChaosController** | startChaos, stopChaos, generateChaosPattern, processChaosExecution, calculateChaosStepDelay, validateChaosParams | ~1200 | ⭐⭐⭐ |
| **PursuitController** | pursuitMove, doPursuitStep | ~150 | ⭐ |
| **SequenceEngine** | startSequenceExecution, processSequenceExecution, positionForNextLine, stopSequenceExecution, onMovementComplete | ~500 | ⭐⭐⭐ |
| **SequenceLineManager** | addSequenceLine, updateSequenceLine, deleteSequenceLine, moveSequenceLine, duplicateSequenceLine, clearSequenceTable, import/exportSequence | ~300 | ⭐⭐ |
| **CommandDispatcher** | handleBasicCommands, handleConfigCommands, handleDecelZoneCommands, handleCyclePauseCommands, handlePursuitCommands, handleChaosCommands, handleOscillationCommands, handleSequencerCommands | ~800 | ⭐⭐⭐⭐ |
| **StatusBroadcaster** | sendStatus, sendSequenceStatus, broadcastSequenceTable | ~300 | ⭐⭐ |

### 🟠 VALIDATEURS (~200 lignes) → Peut rester ou migrer
| Fonction | Lignes | Option |
|----------|--------|--------|
| validateDistance() | ~20 | → UtilityEngine ou Validators.h |
| validateSpeed() | ~15 | → UtilityEngine ou Validators.h |
| validatePosition() | ~25 | → UtilityEngine ou Validators.h |
| validateMotionRange() | ~30 | → UtilityEngine ou Validators.h |
| validateChaosParams() | ~40 | → ChaosController |
| validateOscillationParams() | ~35 | → OscillationController |
| validateOscillationAmplitude() | ~25 | → OscillationController |
| validateDecelZone() | ~35 | → VaetController ou Config |
| validateAndReport() | ~8 | Helper générique → UtilityEngine |

### ⚪ DOIT RESTER DANS MAIN (~1000 lignes)
| Section | Lignes | Raison |
|---------|--------|--------|
| setup() | ~250 | Point d'entrée |
| loop() | ~100 | Boucle principale |
| webSocketEvent() | ~50 | Handler WebSocket principal |
| Global variables | ~200 | État système |
| Forward declarations | ~100 | Prototypes |
| Inline helpers (calculateChaosLimits, etc.) | ~100 | Performance critique |

---

## 🎯 Recommandations Prioritaires

### Option A: Migration vers UtilityEngine (QUICK WIN - 1-2h)
Fonctions utilitaires simples qui n'ont pas de dépendances complexes:

```cpp
// Ajouter à UtilityEngine.h:
void serviceFor(unsigned long ms);          // serviceWebSocketFor
void sendError(const String& msg);          // sendError unifié
void incrementDailyStats(float distMM);     // Stats
void saveSessionStats();                    // Stats
bool validateAndReport(bool ok, String msg); // Validation helper
```

### Option B: Créer Validators.h (2h)
Extraire tous les validateurs dans un header dédié:

```cpp
// include/Validators.h
namespace Validators {
  bool distance(float mm, String& err);
  bool speed(float level, String& err);
  bool position(float mm, String& err);
  bool motionRange(float start, float dist, String& err);
  bool chaosParams(...);
  bool oscillationParams(...);
  bool report(bool ok, const String& err);  // sendError if !ok
}
```

### Option C: CommandDispatcher (4h) - IMPACT MAJEUR
Extraire les 8 handlers de webSocketEvent dans un module dédié:
- Réduit webSocketEvent de ~800 lignes à ~50 lignes
- Améliore testabilité des commandes

---

## 📊 Métriques Actuelles

| Métrique | Valeur |
|----------|--------|
| Lignes .ino | 6454 |
| Fonctions dans .ino | ~70 |
| Modules extraits | 3 (Motor, Contacts, Calibration) |
| Lignes extraites | ~600 |
| RAM usage | 18.2% |
| Flash usage | 32.4% |

---

## 🔥 Prochaines Actions (par priorité)

1. **Quick Win - UtilityEngine** (Option A)
   - Migrer `serviceWebSocketFor()`, `sendError()`, stats functions
   - Temps: 1-2h | Impact: Nettoyage -80 lignes

2. **Validators.h** (Option B)  
   - Extraire validateurs dans header dédié
   - Temps: 2h | Impact: Meilleure organisation -200 lignes

3. **CommandDispatcher** (Option C)
   - Plus gros impact mais plus risqué
   - Temps: 4h | Impact: -800 lignes du main

4. **Autres Controllers** (VaetController, ChaosController...)
   - À faire après stabilisation des modules de base
   - Temps: 2-3 jours | Impact: Architecture complètement modulaire
