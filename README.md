# ESPBastoRelay

Module relais `XIAO ESP32-C3` pour chauffage Webasto, piloté via `ESP-NOW`.

> **Projet associé** : ce module fonctionne avec le contrôleur [ESPBasto](https://github.com/misterniark/ESPBasto) qui gère l'interface utilisateur, l'écran, l'encodeur et le capteur de température.

## Description

Ce projet permet de commander un relais connecté à un chauffage Webasto via le protocole `ESP-NOW`. Le `XIAO ESP32-C3` reçoit les commandes d'un contrôleur distant, pilote le relais et applique plusieurs sécurités pour éviter un fonctionnement dangereux.

### Fonctionnalités

- Communication sans fil via `ESP-NOW`
- Apprentissage automatique de l'adresse MAC du contrôleur
- Commande manuelle de secours via le bouton `BOOT`
- Mode économie d'énergie avec CPU à `80 MHz`

### Sécurités intégrées

- Arrêt automatique si perte de connexion pendant 3 minutes lorsque le chauffage est actif
- Anti-redémarrage rapide : 3 minutes obligatoires après un arrêt avant toute remise en marche
- Override manuel local pour reprendre la main en cas de besoin

## Matériel requis

- `Seeed Studio XIAO ESP32-C3`
- Module relais `5V`
- Alimentation `5V`
- Chauffage Webasto

## Câblage

| XIAO ESP32-C3 | Module Relais |
|---------------|---------------|
| `D1` (`GPIO3`) | `IN` |
| `5V` | `VCC` |
| `GND` | `GND` |

### Bouton local

Le bouton `BOOT` intégré au `XIAO ESP32-C3` est utilisé comme commande manuelle de secours.

Important :
- ne pas le maintenir appuyé pendant le démarrage
- sur cette carte, le bouton `BOOT` est relié à `D9` / `GPIO9`
- `GPIO9` est une broche de boot/strapping

### LED de statut

Le `XIAO ESP32-C3` n'expose pas de LED utilisateur pilotable intégrée pour ce projet.

- par défaut, `LED_STATUS = -1`
- si vous voulez une LED de statut, câblez une LED externe sur une broche libre comme `D10`, puis adaptez `config.h`

## Configuration

Modifier `src/config.h` selon vos besoins :

```c
// Relais sur D1 du XIAO ESP32-C3
#define RELAY_PIN     D1

// Bouton BOOT pour commande manuelle
#define MANUAL_BUTTON_PIN              D9
#define MANUAL_BUTTON_ACTIVE_STATE     LOW
#define MANUAL_BUTTON_LONG_PRESS_MS    2000

// LED optionnelle
#define LED_STATUS    -1

// Logique du relais
#define RELAY_ACTIVE_HIGH  true

// Timeout sécurité
#define SAFETY_TIMEOUT_MS  180000
```

## Commande manuelle de secours

Le bouton `BOOT` permet de piloter localement le chauffage avec un appui long de `2 secondes`.

Comportement actuel :
- 1er appui long : active l'override manuel et force un état local
- appui long suivant : force l'état opposé
- appui long suivant lorsque l'état forcé est déjà `OFF` : désactive l'override et rend la main au contrôleur distant

Pendant l'override manuel :
- les commandes `CMD_HEAT_ON` et `CMD_HEAT_OFF` reçues via `ESP-NOW` sont ignorées
- les `PING` continuent de fonctionner
- le module renvoie toujours son état réel au contrôleur

## Protocole ESP-NOW

### Commandes reçues

| Code | Commande | Description |
|------|----------|-------------|
| `1` | `CMD_HEAT_ON` | Activer le chauffage |
| `2` | `CMD_HEAT_OFF` | Désactiver le chauffage |
| `3` | `CMD_PING` | Vérifier la connexion |

### Réponses envoyées

| Code | Réponse | Description |
|------|---------|-------------|
| `11` | `ACK_ON` | Chauffage activé |
| `12` | `ACK_OFF` | Chauffage désactivé |
| `13` | `ACK_PONG` | Réponse au ping |
| `14` | `ACK_LOCKED` | Redémarrage bloqué par délai de sécurité |
| `15` | `ACK_UNLOCKED` | Redémarrage de nouveau autorisé |

## Installation

### Prérequis

- [PlatformIO](https://platformio.org/) extension VS Code ou CLI

### Compilation et téléversement

```bash
pio run
pio run -t upload
pio device monitor
```

## Sécurités

### 1. Perte de connexion

Si aucune commande n'est reçue pendant `3 minutes` alors que le chauffage est actif, le relais est automatiquement coupé.

### 2. Anti-redémarrage rapide

Après chaque passage à `OFF`, un délai de `3 minutes` est imposé avant de pouvoir repasser à `ON`.

- une tentative trop tôt renvoie `ACK_LOCKED`
- à l'expiration du délai, le module envoie `ACK_UNLOCKED`

### 3. Override manuel local

Le bouton `BOOT` permet de reprendre la main localement en cas de panne du contrôleur ou de besoin d'intervention rapide.

## Débogage

Le moniteur série `115200 bauds` affiche :
- l'adresse MAC du module
- les commandes reçues
- les réponses envoyées
- l'état de l'override manuel
- les alertes de sécurité

Exemples de messages utiles :
- `Verrou anti-redemarrage active (3 min)`
- `!!! REDEMARRAGE BLOQUE - Attendre X secondes !!!`
- `Verrou anti-redemarrage expire`
- `Override manuel active`
- `Override manuel: chauffage FORCE ON`
- `Override manuel: chauffage FORCE OFF`
- `Override manuel desactive - retour controle ESP-NOW`

## Structure du projet

```text
ESPBastoRelay/
├── src/
│   ├── main.cpp
│   └── config.h
├── platformio.ini
├── README.md
├── specs.md
└── LICENSE
```

## Voir aussi

- [ESPBasto](https://github.com/misterniark/ESPBasto) - contrôleur thermostat avec écran `ST7735`, encodeur rotatif et capteur `AHT21`

## Licence

Voir le fichier [LICENSE](LICENSE).
