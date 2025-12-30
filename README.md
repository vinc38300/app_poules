# 🐔 Contrôleur de Poulailler ESP32 BLE

Système automatisé de gestion de poulailler basé sur ESP32 avec contrôle via application mobile Cordova et communication Bluetooth Low Energy (BLE).

## 📋 Fonctionnalités

### Contrôle Automatique
- **Distribution de grain** : Programmation avec heure de début et durée fixe
- **Ouverture/fermeture de porte** : Programmation avec plage horaire (début → fin)
- **Synchronisation horaire française** : Gestion automatique heure d'hiver/été
- **Mode automatique/manuel** : Basculement entre contrôle programmé et manuel

### Statistiques et Suivi
- **Compteurs journaliers** : Nombre d'activations par jour
- **Statistiques hebdomadaires** : Suivi sur 7 jours (Lundi-Dimanche)
- **Temps de fonctionnement** : Monitoring du système
- **Historique des actions** : Différenciation actions auto/manuelles

### Application Mobile
- **Interface intuitive** : Contrôle en temps réel
- **Actions rapides** : Arrêt d'urgence, distribution immédiate
- **Notifications BLE** : Mise à jour automatique des états
- **Validation des horaires** : Vérification en temps réel

## 🔧 Matériel Requis

### ESP32
- Carte ESP32 DevKit ou équivalent
- 2x Modules relais (actifs à l'état bas)
- Alimentation 5V

### Connexions
```
GPIO 2  → Relais 1 (Distribution de grain)
GPIO 4  → Relais 2 (Ouverture de porte)
```

## 📱 Installation Application Mobile

### Prérequis
- Node.js et npm
- Apache Cordova CLI
- Android Studio (pour Android) ou Xcode (pour iOS)

### Installation
```bash
# Installer Cordova globalement
npm install -g cordova

# Créer un projet Cordova
cordova create poulailler com.example.poulailler "Contrôle Poulailler"
cd poulailler

# Ajouter la plateforme Android
cordova platform add android

# Ajouter le plugin Bluetooth Low Energy
cordova plugin add cordova-plugin-ble-central

# Copier index.html dans www/
cp index.html www/

# Compiler et installer
cordova build android
cordova run android
```

## 💻 Installation ESP32

### Prérequis Arduino IDE
1. Installer l'IDE Arduino
2. Ajouter le support ESP32 :
   - **Fichier** → **Préférences**
   - URLs de gestionnaire de cartes : `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. **Outils** → **Type de carte** → **Gestionnaire de cartes** → Installer "ESP32"

### Bibliothèques Requises
Toutes incluses dans le SDK ESP32 :
- BLEDevice
- BLEServer
- BLEUtils
- BLE2902
- Preferences

### Téléversement
```bash
# Via Arduino IDE
1. Ouvrir Poulailler_BLE_2025.ino
2. Sélectionner la carte : ESP32 Dev Module
3. Configurer le port série
4. Téléverser

# Via PlatformIO (optionnel)
pio run --target upload
```

## 🎮 Utilisation

### Première Configuration

1. **Flasher l'ESP32** avec `Poulailler_BLE_2025.ino`
2. **Installer l'application** sur smartphone
3. **Lancer l'application** et cliquer sur "Se connecter à l'ESP32"
4. **Synchroniser l'heure** automatiquement

### Programmation Distribution de Grain

```
Heure de début : 08:00
Durée : 20 minutes
→ La distribution démarrera à 08:00 et s'arrêtera à 08:20
```

### Programmation Ouverture Porte

```
Heure d'ouverture : 07:00
Heure de fermeture : 21:00
→ La porte sera ouverte de 07:00 à 21:00
```

### Contrôle Manuel

Utilisez les boutons ON/OFF pour un contrôle immédiat, puis revenez en mode AUTO pour reprendre la programmation.

## 📊 Format des Données BLE

### UUIDs des Services
```cpp
SERVICE_UUID              "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CHARACTERISTIC_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
TIME_CHARACTERISTIC_UUID  "123e4567-e89b-12d3-a456-426614174000"
RELAY1_TIME_UUID         "5a786b5e-1234-5678-9abc-def012345678"
RELAY2_TIME_UUID         "6b897c6f-2345-6789-abcd-ef0123456789"
STATS_UUID               "87654321-4321-4321-4321-210987654321"
WEEK_STATS_UUID          "11111111-2222-3333-4444-555555555555"
```

### Protocole de Communication

#### Synchronisation Temps (code 6)
```
[6][timestamp_byte1][timestamp_byte2][timestamp_byte3][timestamp_byte4]
```

#### Contrôle Manuel Relais
```
Grain ON  : [1][1]
Grain OFF : [1][0]
Porte ON  : [2][1]
Porte OFF : [2][0]
```

#### Programmation Grain (code 3)
```
[3][hour][minute][second][duration_high][duration_low]
```

#### Programmation Porte (code 4)
```
[4][start_hour][start_min][start_sec][stop_hour][stop_min][stop_sec]
```

#### Mode Auto (code 5)
```
[5][relay_number]  // 1=grain, 2=porte
```

## 🔍 Débogage

### Monitor Série ESP32
```cpp
Vitesse : 115200 bauds
```

Messages typiques :
```
🚀 ESP32 Contrôleur Poulailler v4.0
📱 Application connectée
✅ Services prêts pour notifications v4.0
⏰ Heure synchronisée (France): 14:30:25
🌾 Distribution de grain: ACTIVÉE (Auto)
```

### Console Application
Tous les événements BLE sont affichés dans la console de logs en bas de l'interface.

## ⚙️ Configuration Avancée

### Modifier les Broches GPIO
```cpp
#define RELAY_1_PIN 2  // Changer selon votre câblage
#define RELAY_2_PIN 4
```

### Ajuster le Fuseau Horaire
```cpp
#define TIMEZONE_OFFSET_WINTER 1    // UTC+1
#define TIMEZONE_OFFSET_SUMMER 2    // UTC+2
```

### Personnaliser les Intervalles de Notification
```cpp
// Dans loop()
if (currentMillis - lastUpdate >= 5000) {  // 5 secondes
    updateTimeCharacteristic();
}
```

## 🐛 Résolution de Problèmes

### ESP32 ne se connecte pas
- Vérifier que le Bluetooth est activé sur le smartphone
- Redémarrer l'ESP32
- Vérifier les permissions Bluetooth de l'application

### Heure non synchronisée
- Vérifier la connexion BLE
- Cliquer sur "Synchroniser l'heure"
- Vérifier l'heure du smartphone

### Relais ne répondent pas
- Vérifier le câblage (GPIO 2 et 4)
- Contrôler l'alimentation des relais
- Tester en mode manuel d'abord

### Statistiques incorrectes
- Réinitialiser les compteurs via l'ESP32
- Vérifier la synchronisation horaire
- Redémarrer le système à minuit

## 📁 Structure du Projet

```
poulailler-esp32/
│
├── Poulailler_BLE_2025.ino    # Code ESP32
├── index.html                  # Application Cordova
├── README.md                   # Documentation
│
├── config.xml                  # Configuration Cordova
└── platforms/                  # Plateformes mobiles
    ├── android/
    └── ios/
```

## 🔐 Sécurité

- Le système n'utilise pas d'authentification BLE par défaut
- Pour un usage en production, implémenter :
  - Appairage BLE sécurisé
  - Chiffrement des communications
  - Validation des commandes

## 📝 Licence

Ce projet est distribué sous licence **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)**.

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

Vous êtes libre de :
- **Partager** — copier, distribuer et communiquer le matériel par tous moyens et sous tous formats
- **Adapter** — remixer, transformer et créer à partir du matériel

Selon les conditions suivantes :
- **Attribution** — Vous devez créditer l'œuvre, intégrer un lien vers la licence et indiquer si des modifications ont été effectuées
- **Pas d'Utilisation Commerciale** — Vous n'êtes pas autorisé à faire un usage commercial de cette œuvre, tout ou partie du matériel la composant
- **Partage dans les Mêmes Conditions** — Dans le cas où vous adaptez, transformez ou créez à partir du matériel, vous devez diffuser vos contributions sous la même licence que l'original

⚠️ **Usage non-commercial uniquement** - Pour toute utilisation commerciale, veuillez nous contacter.

Voir le fichier [LICENSE](LICENSE) pour plus de détails ou consultez [creativecommons.org/licenses/by-nc-sa/4.0/](https://creativecommons.org/licenses/by-nc-sa/4.0/)

## 👥 Contribution

Les contributions sont les bienvenues ! N'hésitez pas à :
- Signaler des bugs via les Issues
- Proposer des améliorations via Pull Requests
- Partager vos modifications

## 📞 Support

Pour toute question ou problème :
- Ouvrir une Issue sur GitHub
- Consulter le Monitor Série pour les logs détaillés
- Vérifier la documentation ESP32 BLE

## 🎯 Roadmap

- [ ] Interface web (WiFi)
- [ ] Capteurs de température/humidité
- [ ] Notifications push
- [ ] Alimentation solaire
- [ ] Multi-poulailler
- [ ] Cloud logging

---

**Version actuelle** : 4.0  
**Dernière mise à jour** : Décembre 2024  
**ESP32 SDK** : Compatible v2.0+  
**Cordova** : Compatible v12.0+
