# État du Refactoring Frontend - ESP32 Stepper Controller

**Date:** 6 décembre 2025  
**Version actuelle:** v2.5  
**Objectif:** Découpler et modulariser le JavaScript frontend (initialement 7300+ lignes)

---

## 📊 État Actuel (v2.4)

### Structure des fichiers JS (`data/js/`)

| Fichier | Lignes | Rôle | État |
|---------|--------|------|------|
| `app.js` | 256 | AppState, constantes, SystemState enum | ✅ Stable |
| `utils.js` | 241 | Utilitaires (formatage, helpers) | ✅ Stable |
| `milestones.js` | ~100 | Données achievements/milestones | ✅ Stable |
| `websocket.js` | 223 | Connexion WS, handlers | ✅ Stable |
| `stats.js` | 290 | Statistiques, graphiques Chart.js | ✅ Stable |
| `context.js` | ~160 | Container DI, fonctions génériques | ✅ Stable |
| `chaos.js` | ~120 | Module mode Chaos | ✅ Fonctionnel |
| `oscillation.js` | ~180 | Module mode Oscillation | ✅ Fonctionnel |
| `sequencer.js` | **757** | Module séquenceur + templates | ✅ Étendu v2.4 |
| `presets.js` | ~220 | Module presets (name/tooltip/decel) | ✅ Fonctionnel |
| `formatting.js` | ~240 | Module formatage (wifi/uptime/state) | ✅ Fonctionnel |
| `validation.js` | **397** | **NOUVEAU v2.4** - Validation & field mapping | ✅ Fonctionnel |
| `main.js` | **6828** | Logique principale (wrappers DOM) | 🔄 En cours |

### Progression main.js
```
Initial:  ~7300 lignes
v2.3:      7177 lignes (-123)
v2.4:      6852 lignes (-325)
v2.5:      6828 lignes (-24)  ← Actuel
─────────────────────────────
Total:    -472 lignes extraites
```

### Ordre de chargement dans `index.html`
```html
<script src="/js/milestones.js"></script>
<script src="/js/app.js"></script>
<script src="/js/utils.js"></script>
<script src="/js/websocket.js"></script>
<script src="/js/stats.js"></script>
<script src="/js/context.js"></script>
<script src="/js/chaos.js"></script>
<script src="/js/oscillation.js"></script>
<script src="/js/sequencer.js"></script>
<script src="/js/presets.js"></script>
<script src="/js/formatting.js"></script>
<script src="/js/validation.js"></script>  <!-- NOUVEAU v2.4 -->
<script src="/js/main.js"></script>
```

---

## ✅ Ce qui a été fait

### 1. Extraction main.js (Phase 1)
- `index.html` réduit de 9003 → 1703 lignes (HTML pur)
- Tout le JS inline extrait dans `main.js`

### 2. Création context.js (DI Container)
Container d'injection de dépendances avec fonctions génériques :

```javascript
// Container DI
const Context = { config: null, stepsPerMM: 100 };
initContext()
// Utilitaires génériques
mmToSteps(mm, stepsPerMM)
stepsToMm(steps, stepsPerMM)
getEffectiveMaxDistMM()
getTotalDistMM()
// Wrappers contextuels (utilisent Context)
validateChaosLimitsCtx(), validateOscillationLimitsCtx()
```

### 3. Création chaos.js (Module Chaos - Phase 2)
Module dédié au mode Chaos avec constantes et fonctions pures :

```javascript
// Constantes
CHAOS_LIMITS = { SPEED_MIN, SPEED_MAX, AMPLITUDE_MIN, AMPLITUDE_MAX, ... }
CHAOS_PATTERNS = { RANDOM: 1, SWEEP: 2, BURST: 4, ... }
// Fonctions PURES
validateChaosLimitsPure(centerPos, amplitude, totalDistMM)
buildChaosConfigPure(formValues)
countEnabledPatternsPure(patterns)
generateChaosTooltipPure(config)
```

### 4. Création oscillation.js (Module Oscillation - Phase 2)
Module dédié au mode Oscillation avec constantes et fonctions pures :

```javascript
// Constantes
OSCILLATION_LIMITS = { AMPLITUDE_MIN, AMPLITUDE_MAX, SPEED_MIN, SPEED_MAX, ... }
WAVEFORM_TYPE = { SINE: 0, TRIANGLE: 1, SQUARE: 2 }
// Fonctions PURES
validateOscillationLimitsPure(centerPos, amplitude, totalDistMM)
buildOscillationConfigPure(formValues)
calculateOscillationPeakSpeedPure(amplitude, speed)
generateOscillationTooltipPure(config)
formatCyclePauseInfoPure(cyclesBeforePause, pauseDuration)
```

### 5. Extension sequencer.js (Module Séquenceur - Phase 2)
Module complet avec validation ET génération de tooltips :

```javascript
// Constantes
SEQUENCER_LIMITS = { SPEED_MIN, SPEED_MAX, DECEL_ZONE_MIN, ... }
MOVEMENT_TYPE = { VAET: 0, OSCILLATION: 1, CHAOS: 2, CALIBRATION: 4 }
DECEL_MODES = { NONE: 0, EARLY: 1, VERY_EARLY: 2 }
// Fonctions PURES
validateSequencerLinePure(line, movementType, effectiveMax)
buildSequenceLineDefaultsPure(effectiveMax)
generateSequenceLineTooltipPure(line, movementType, config)
generateVaetTooltipPure(line, effectiveMax)
generateCalibrationTooltipPure(line)
```

### 6. Modification main.js - Pattern de délégation
Les fonctions de main.js délèguent aux fonctions pures avec fallback :

```javascript
// Exemple: generateSequenceLineTooltip() délègue à generateSequenceLineTooltipPure()
function generateSequenceLineTooltip(line, movementType) {
  if (typeof generateSequenceLineTooltipPure === 'function') {
    const config = { ... };  // Récupération du contexte
    return generateSequenceLineTooltipPure(line, movementType, config);
  }
  // Fallback si module non chargé
  return '';
}
```

### 7. Création validation.js (Module Validation - v2.4) ✅ NOUVEAU
Module dédié à la validation des champs et mapping erreurs :

```javascript
// Données
ERROR_FIELD_MAPPING = { 'Position de départ': 'editStartPos', ... }
ALL_EDIT_FIELDS = ['editStartPos', 'editDistance', ...]
VALIDATION_LIMITS = { common: {...}, vaet: {...}, oscillation: {...}, chaos: {...} }

// Fonctions
getErrorFieldIdsPure(errorMessages)       // Extraction IDs depuis erreurs
getAllInvalidFieldsPure(line, type, max, errors)  // Validation complète
validateVaetFieldsPure(line, effectiveMax)
validateOscillationFieldsPure(line, effectiveMax)
validateChaosFieldsPure(line, effectiveMax)
validateCommonFieldsPure(line, movementType)
checkEmptyFieldsPure(formValues, movementType)
```

### 8. Extension sequencer.js (Templates - v2.4)
Ajout des données de template JSON :

```javascript
// Données template
SEQUENCE_TEMPLATE = { version: "2.0", lineCount: 5, lines: [...] }
SEQUENCE_TEMPLATE_HELP = { "🔧 GUIDE": {...}, "📋 TYPES": {...}, ... }

// Fonction
getSequenceTemplateDocPure()  // Retourne { TEMPLATE, DOCUMENTATION }
```

### 9. Routes serveur (APIRoutes.cpp)
Toutes les routes JS configurées avec cache 24h :
- `/js/app.js`, `/js/utils.js`, `/js/milestones.js`
- `/js/websocket.js`, `/js/stats.js`
- `/js/context.js`, `/js/chaos.js`, `/js/oscillation.js`
- `/js/sequencer.js`, `/js/presets.js`, `/js/formatting.js`
- `/js/validation.js`, `/js/main.js`

---

## 🎯 Prochaines Étapes (Phase 3 - Modules Domaine)

### Stratégie : Un module = Un domaine fonctionnel
Les modules contiennent **TOUTES** les fonctions de leur domaine (pas que les "pures").
Le suffixe "Pure" était juste pour faciliter l'extraction initiale.

### Module Validation (validation.js) - v2.4 ✅
- [x] `ERROR_FIELD_MAPPING` - Mapping erreur→champ DOM
- [x] `ALL_EDIT_FIELDS` - Liste champs éditables
- [x] `VALIDATION_LIMITS` - Limites par type
- [x] `getErrorFieldIdsPure()` - IDs depuis messages
- [x] `getAllInvalidFieldsPure()` - Validation complète ligne
- [x] `validateVaetFieldsPure()` - Validation VA-ET-VIENT
- [x] `validateOscillationFieldsPure()` - Validation OSCILLATION
- [x] `validateChaosFieldsPure()` - Validation CHAOS
- [x] `checkEmptyFieldsPure()` - Détection champs vides

### Module Sequencer (sequencer.js) - v2.4 ✅
- [x] `SEQUENCER_LIMITS`, `MOVEMENT_TYPE`, `DECEL_MODES`
- [x] `validateSequencerLinePure()` - Validation ligne
- [x] `buildSequenceLineDefaultsPure()` - Valeurs par défaut
- [x] `generateSequenceLineTooltipPure()` - Tooltip ligne
- [x] `generateVaetTooltipPure()` - Tooltip VAET
- [x] `generateCalibrationTooltipPure()` - Tooltip Calibration
- [x] `getMovementTypeDisplayPure()` - Affichage type
- [x] `getDecelSummaryPure()` - Résumé décélération
- [x] `getLineSpeedsDisplayPure()` - Affichage vitesses
- [x] `getLineCyclesPausePure()` - Affichage cycles/pause
- [x] `SEQUENCE_TEMPLATE` - Template JSON 5 exemples
- [x] `SEQUENCE_TEMPLATE_HELP` - Documentation template
- [x] `getSequenceTemplateDocPure()` - Accès template complet

### Module Oscillation (oscillation.js) ✅
- [x] `OSCILLATION_LIMITS`, `WAVEFORM_TYPE`
- [x] `validateOscillationLimitsPure()` - Validation limites
- [x] `buildOscillationConfigPure()` - Construction config
- [x] `calculateOscillationPeakSpeedPure()` - Calcul vitesse de pointe
- [x] `generateOscillationTooltipPure()` - Génération tooltip
- [x] `formatCyclePauseInfoPure()` - Formatage info pause

### Module Chaos (chaos.js) ✅
- [x] `CHAOS_LIMITS`, `CHAOS_PATTERNS`
- [x] `validateChaosLimitsPure()` - Validation limites
- [x] `buildChaosConfigPure()` - Construction config
- [x] `countEnabledPatternsPure()` - Comptage patterns actifs
- [x] `generateChaosTooltipPure()` - Génération tooltip

### Module Formatting (formatting.js) ✅
- [x] `getWifiQualityPure()` - Qualité WiFi (RSSI)
- [x] `formatUptimePure()` - Formatage uptime
- [x] `getStateDisplayPure()` - Affichage état système

### Module Presets (presets.js) ✅
- [x] `generatePresetNamePure()` - Nom automatique preset
- [x] `generatePresetTooltipPure()` - Tooltip preset
- [x] `calculateSlowdownFactorPure()` - Facteur ralentissement

### Prochains candidats à extraire
- [ ] **Playlist management** → `playlist.js` (gestion playlist UI)
- [ ] **Modal management** → `modals.js` (ouverture/fermeture modales)
- [ ] **Form helpers** → fonctions collectFormValues, populateForm, etc.

---

## 🔧 Commandes Utiles

```powershell
# Compiler firmware
C:\Users\Administrator\.platformio\penv\Scripts\platformio.exe run

# Upload firmware
C:\Users\Administrator\.platformio\penv\Scripts\platformio.exe run -t upload

# Upload fichiers web
python upload_html.py --all           # Tous les fichiers
python upload_html.py --file data/js/main.js   # Un fichier spécifique
python upload_html.py --js            # Tous les JS

# Vérifier un fichier
Select-String -Path "data\js\main.js" -Pattern "validateChaos"
```

---

## ⚠️ Notes Importantes

1. **VS Code peut avoir des problèmes** avec la création/édition de fichiers. En cas de doute, utiliser PowerShell directement pour vérifier le contenu réel des fichiers.

2. **Les logs console.log dans context.js** ne s'affichent que si DevTools est ouvert AVANT le chargement de la page.

3. **Tester les fonctions pures** dans la console navigateur :
```javascript
validateChaosLimitsPure(500, 200, 1000)  // {valid: true, ...}
validateChaosLimitsPure(100, 200, 1000)  // {valid: false, error: "..."}
```

4. **Le pattern de découplage** :
   - Fonction pure dans `context.js` (logique sans DOM)
   - Wrapper dans `main.js` qui récupère les valeurs DOM et appelle la fonction pure
   - Permet les tests unitaires et la réutilisation

---

## 📁 Fichiers Clés

- `data/index.html` - HTML pur (~1710 lignes)
- `data/js/context.js` - Container DI + utilitaires (~160 lignes)
- `data/js/chaos.js` - Module Chaos (~120 lignes)
- `data/js/oscillation.js` - Module Oscillation (~180 lignes)
- `data/js/sequencer.js` - Module Séquenceur (**757 lignes** v2.4)
- `data/js/presets.js` - Module Presets (~220 lignes)
- `data/js/formatting.js` - Module Formatage (~240 lignes)
- `data/js/validation.js` - **NOUVEAU** Module Validation (**397 lignes** v2.4)
- `data/js/main.js` - Logique principale (**6852 lignes** v2.4)
- `src/web/APIRoutes.cpp` - Routes serveur HTTP
- `upload_html.py` - Script d'upload vers ESP32

---

## 📜 Historique des versions

| Version | Lignes main.js | Changements |
|---------|---------------|-------------|
| Initial | ~7300 | Extraction depuis index.html |
| v2.3 | 7177 | sequencer.js display helpers |
| v2.4 | **6852** | validation.js + template extraction (-325 lignes) |
