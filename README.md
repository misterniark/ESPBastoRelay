# ESPBastoRelay

Module relais ESP32-C3 pour chauffage Webasto, piloté via ESP-NOW.

> **Projet associé** : Ce module fonctionne avec le contrôleur [ESPBasto](https://github.com/misterniark/ESPBasto) qui gère l'interface utilisateur, l'écran, l'encodeur et le capteur de température.

## Description

Ce projet permet de contrôler un relais connecté à un chauffage Webasto via le protocole ESP-NOW. L'ESP32-C3 Super Mini reçoit les commandes d'un contrôleur distant et active/désactive le relais en conséquence.

### Fonctionnalités

- Communication sans fil via ESP-NOW (faible latence, faible consommation)
- Apprentissage automatique de l'adresse MAC du contrôleur
- Test automatique du relais au démarrage
- Sécurité : arrêt automatique si perte de connexion (timeout 3 minutes)
- LED de statut intégrée
- Mode économie d'énergie (CPU à 80 MHz)

## Matériel requis

- ESP32-C3 Super Mini
- Module relais 5V
- Alimentation 5V

## Câblage

| ESP32-C3 | Module Relais |
|----------|---------------|
| GPIO7    | IN (Signal)   |
| 5V       | VCC           |
| GND      | GND           |

| ESP32-C3 | LED |
|----------|-----|
| GPIO8    | LED interne (statut) |

## Configuration

Modifier `src/config.h` selon vos besoins :

```c
// Pin du relais
#define RELAY_PIN     7     // GPIO7

// LED de statut
#define LED_STATUS    8     // GPIO8

// Logique du relais
#define RELAY_ACTIVE_HIGH  true   // true = HIGH pour activer

// Timeout sécurité (ms)
#define SAFETY_TIMEOUT_MS  180000  // 3 minutes
```

## Protocole ESP-NOW

### Commandes reçues

| Code | Commande     | Description              |
|------|--------------|--------------------------|
| 1    | CMD_HEAT_ON  | Activer le chauffage     |
| 2    | CMD_HEAT_OFF | Désactiver le chauffage  |
| 3    | CMD_PING     | Vérifier la connexion    |

### Réponses envoyées

| Code | Réponse  | Description        |
|------|----------|--------------------|
| 11   | ACK_ON   | Chauffage activé   |
| 12   | ACK_OFF  | Chauffage désactivé|
| 13   | ACK_PONG | Réponse au ping    |

## Installation

### Prérequis

- [PlatformIO](https://platformio.org/) (extension VS Code ou CLI)

### Compilation et téléversement

```bash
# Compiler
pio run

# Téléverser
pio run -t upload

# Moniteur série
pio device monitor
```

## Sécurité

Le module intègre une sécurité importante : si aucune commande n'est reçue pendant **3 minutes** alors que le chauffage est actif, le relais est automatiquement coupé. Cela évite que le chauffage reste allumé en cas de perte de connexion avec le contrôleur.

## Test au démarrage

Au démarrage, le module effectue un test automatique du relais :
1. Active le relais (HIGH) pendant 1 seconde
2. Désactive le relais (LOW) pendant 1 seconde
3. Initialise le relais en position OFF

Vous devriez entendre deux clics du relais au démarrage.

## Débogage

Le moniteur série (115200 bauds) affiche :
- L'adresse MAC du module au démarrage
- Le test du relais (GPIO utilisé, HIGH/LOW)
- Les commandes reçues
- Les réponses envoyées
- Les alertes de sécurité

## Structure du projet

```
ESPBastoRelay/
├── src/
│   ├── main.cpp      # Code principal
│   └── config.h      # Configuration (pins, constantes)
├── platformio.ini    # Configuration PlatformIO
├── README.md         # Ce fichier
├── specs.md          # Spécifications techniques
└── LICENSE
```

## Voir aussi

- [ESPBasto](https://github.com/misterniark/ESPBasto) - Contrôleur thermostat avec écran ST7735, encodeur rotatif et capteur AHT21

## Licence

Voir le fichier [LICENSE](LICENSE).
