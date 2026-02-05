# ESPBastoRelay

Module relais ESP32-C3 pour chauffage Webasto, piloté via ESP-NOW.

> **Projet associé** : Ce module fonctionne avec le contrôleur [ESPBasto](https://github.com/misterniark/ESPBasto) qui gère l'interface utilisateur, l'écran, l'encodeur et le capteur de température.

## Description

Ce projet permet de contrôler un relais connecté à un chauffage Webasto via le protocole ESP-NOW. L'ESP32-C3 Super Mini reçoit les commandes d'un contrôleur distant et active/désactive le relais en conséquence.

### Fonctionnalités

- Communication sans fil via ESP-NOW (faible latence, faible consommation)
- Apprentissage automatique de l'adresse MAC du contrôleur
- LED de statut intégrée
- Mode économie d'énergie (CPU à 80 MHz)

### Sécurités intégrées

- **Arrêt automatique** : si perte de connexion pendant 3 minutes (chauffage actif)
- **Anti-redémarrage rapide** : délai obligatoire de 3 minutes après arrêt avant de pouvoir redémarrer

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

| Code | Réponse      | Description                          |
|------|--------------|--------------------------------------|
| 11   | ACK_ON       | Chauffage activé                     |
| 12   | ACK_OFF      | Chauffage désactivé                  |
| 13   | ACK_PONG     | Réponse au ping                      |
| 14   | ACK_LOCKED   | Redémarrage bloqué (délai sécurité)  |
| 15   | ACK_UNLOCKED | Redémarrage autorisé (délai expiré)  |

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

## Sécurités

Le module intègre deux mécanismes de sécurité pour protéger le chauffage Webasto :

### 1. Arrêt automatique (perte de connexion)

Si aucune commande n'est reçue pendant **3 minutes** alors que le chauffage est actif, le relais est automatiquement coupé. Cela évite que le chauffage reste allumé en cas de perte de connexion avec le contrôleur.

### 2. Anti-redémarrage rapide

Après chaque arrêt du chauffage (passage à OFF), un délai de **3 minutes** est imposé avant de pouvoir le redémarrer. Cette protection évite les cycles ON/OFF rapides qui pourraient endommager le Webasto.

- Si une commande `CMD_HEAT_ON` est reçue pendant ce délai, le module répond `ACK_LOCKED` (code 14)
- Dès que le délai expire, le module envoie automatiquement `ACK_UNLOCKED` (code 15)
- Le contrôleur peut ainsi informer l'utilisateur en temps réel

## Débogage

Le moniteur série (115200 bauds) affiche :
- L'adresse MAC du module au démarrage
- Les commandes reçues
- Les réponses envoyées
- Les alertes de sécurité :
  - `Verrou anti-redemarrage active (3 min)` - après arrêt
  - `!!! REDEMARRAGE BLOQUE - Attendre X secondes !!!` - si tentative prématurée
  - `Verrou anti-redemarrage expire` - délai écoulé
  - `!!! SECURITE: Perte connexion controleur !!!` - arrêt automatique

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
