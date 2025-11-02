# Freenove ESP32 S3 WROOM Project

Un projet complet basé sur **Freenove ESP32 S3 WROOM N8R8** utilisant PlatformIO pour la gestion du contrôleur de moteur pas à pas et des patterns chaotiques.

## 📋 Description

Ce projet implémente:
- **Contrôle de moteur pas à pas** restructuré et optimisé
- **Patterns chaotiques** configurables pour créer des mouvements dynamiques
- **Interface web HTML** pour le contrôle et le monitoring
- **Configuration flexible** pour différents environnements

## 🚀 Démarrage rapide

### Prérequis
- [PlatformIO](https://platformio.org/) installé
- [Visual Studio Code](https://code.visualstudio.com/) avec l'extension PlatformIO
- Python 3.x

### Installation

1. **Cloner le dépôt**
   ```bash
   git clone https://github.com/JujuVass/freenove_esp32_s3_wroom.git
   cd freenove_esp32_s3_wroom
   ```

2. **Compiler le projet**
   ```bash
   pio run
   ```

3. **Uploader sur l'ESP32**
   ```bash
   pio run --target upload
   ```

4. **Uploader les fichiers HTML** (optionnel)
   ```bash
   python upload_html.py
   ```
   
   Ou avec le mode watch (auto-upload):
   ```bash
   python upload_html.py --watch
   ```

## 📁 Structure du projet

```
├── src/
│   └── stepper_controller_restructured.ino    # Code principal
├── include/
│   ├── ChaosPatterns.h                        # Patterns chaotiques
│   ├── Config.h                               # Configuration générale
│   └── Types.h                                # Définitions de types
├── data/
│   └── index.html                             # Interface web
├── platformio.ini                             # Configuration PlatformIO
├── upload_html.py                             # Script upload fichiers HTML
└── default_8MB.csv                            # Partition par défaut
```

## ⚙️ Configuration

Modifiez `include/Config.h` pour ajuster:
- Les pins de contrôle du moteur
- Les paramètres de vitesse et accélération
- Les configurations de patterns chaotiques

## 🔧 Fonctionnalités principales

### Contrôleur de moteur pas à pas
- Contrôle complet du moteur NEMA 17 ou équivalent
- Support des microstepping
- Gestion des limites et sécurité

### Patterns chaotiques
Implémentation de patterns sophistiqués pour créer des mouvements imprévisibles mais contrôlés, définis dans `ChaosPatterns.h`

### Interface Web
Interface HTML responsive pour:
- Monitorer l'état du système
- Contrôler les paramètres
- Visualiser les données en temps réel

## 📚 Tâches disponibles

Les tâches PlatformIO suivantes sont configurées:

| Tâche | Description |
|-------|-------------|
| `pio run` | Compiler le projet |
| `pio run --target upload` | Compiler et uploader |
| `upload_html.py` | Uploader les fichiers HTML |
| `upload_html.py --watch` | Watch mode - auto-upload HTML |

## 🔌 Connexions matériel

| PIN ESP32 | Fonction |
|-----------|----------|
| À configurer dans Config.h | Moteur pas à pas |
| À configurer dans Config.h | Capteurs/Entrées |

*Consultez `include/Config.h` pour les détails complets des connexions*

## 📝 Notes de développement

- Le projet utilise la bibliothèque de types personnalisée (`Types.h`)
- Les patterns chaotiques peuvent être modifiés dans `ChaosPatterns.h`
- Les fichiers SPIFFS (HTML) sont stockés dans le dossier `data/`

## 🐛 Dépannage

### Erreur de compilation
```bash
pio run --target clean
pio run
```

### Problèmes d'upload
- Vérifiez que l'ESP32 est bien connecté
- Vérifiez le port COM dans `platformio.ini`
- Réinitialisez l'ESP32 (appuyez sur le bouton RESET)

## 📄 Licence

À spécifier selon votre préférence

## 👤 Auteur

**JujuVass**

---

**Dernière mise à jour:** Novembre 2025
