# Spécifications Techniques - ESPBastoRelay

## Matériel

### Microcontrôleur

| Paramètre | Valeur |
|-----------|--------|
| Modèle | Seeed Studio XIAO ESP32-C3 |
| Architecture | RISC-V 32-bit |
| Fréquence CPU | 80 MHz |
| Flash | 4 MB |
| RAM | 400 KB |
| WiFi | 802.11 b/g/n |
| Bluetooth | BLE 5 |

### Module Relais

| Paramètre | Valeur |
|-----------|--------|
| Tension alimentation | 5V DC |
| Tension commande | 3.3V compatible ESP32-C3 |
| Logique | Configurable via `RELAY_ACTIVE_HIGH` |
| Courant commutation | Selon le module utilisé |

## Brochage

### GPIO utilisés

| Broche | Fonction | Direction | Notes |
|--------|----------|-----------|-------|
| `D1 / GPIO3` | Commande relais | OUTPUT | Sortie principale du relais |
| `D9 / GPIO9` | Bouton `BOOT` | INPUT_PULLUP | Appui long de secours |
| `LED_STATUS` | LED optionnelle | OUTPUT | `-1` par défaut, LED externe possible |

### Connexions

```text
XIAO ESP32-C3               Module Relais 5V
┌─────────────────┐         ┌─────────────┐
│ D1 / GPIO3      ├────────►│ IN          │
│ 5V              ├────────►│ VCC         │
│ GND             ├────────►│ GND         │
└─────────────────┘         └─────────────┘
                                   │
                             Vers Webasto
```

### Boutons physiques

| Bouton | Usage | Réutilisable |
|--------|-------|--------------|
| `RESET` | Redémarrage matériel | Non |
| `BOOT` | Entrée manuelle de secours | Oui, après démarrage |

Contraintes :
- `BOOT` est relié à `D9 / GPIO9`
- `GPIO9` est une broche de strapping
- si `BOOT` est maintenu pendant l'allumage, la carte peut entrer en bootloader

## Communication ESP-NOW

### Caractéristiques

| Paramètre | Valeur |
|-----------|--------|
| Protocole | ESP-NOW |
| Portée | ~200 m en ligne de vue |
| Latence | < 10 ms |
| Chiffrement | Non |
| Canal WiFi | Auto (`0`) |

### Structure des messages

```c
typedef struct {
    uint8_t command;
} esp_now_message_t;
```

### Codes de commande

| Direction | Code | Nom | Description |
|-----------|------|-----|-------------|
| Contrôleur → Relais | `1` | `CMD_HEAT_ON` | Activer le chauffage |
| Contrôleur → Relais | `2` | `CMD_HEAT_OFF` | Désactiver le chauffage |
| Contrôleur → Relais | `3` | `CMD_PING` | Vérifier la connexion |
| Relais → Contrôleur | `11` | `ACK_ON` | Confirmation ON |
| Relais → Contrôleur | `12` | `ACK_OFF` | Confirmation OFF |
| Relais → Contrôleur | `13` | `ACK_PONG` | Réponse au ping |
| Relais → Contrôleur | `14` | `ACK_LOCKED` | Redémarrage bloqué |
| Relais → Contrôleur | `15` | `ACK_UNLOCKED` | Redémarrage de nouveau autorisé |

## Sécurités

### 1. Watchdog de communication

| Paramètre | Valeur |
|-----------|--------|
| Durée | 180000 ms |
| Condition | Chauffage ON et absence de commande |
| Action | Coupure automatique du relais |

### 2. Anti-redémarrage rapide

| Paramètre | Valeur |
|-----------|--------|
| Durée | 180000 ms |
| Déclenchement | Transition réelle `ON -> OFF` |
| Action | Blocage de `CMD_HEAT_ON` |
| Réponses | `ACK_LOCKED`, puis `ACK_UNLOCKED` à expiration |

Cette sécurité évite des cycles ON/OFF trop rapides sur le chauffage.

### 3. Override manuel local

| Paramètre | Valeur |
|-----------|--------|
| Entrée | Bouton `BOOT` |
| Broche | `D9 / GPIO9` |
| Déclenchement | Appui long `2000 ms` |
| Effet | Prise de contrôle locale du relais |

Comportement :
- 1er appui long : activation de l'override manuel et forçage d'un état
- appui long suivant : inversion de l'état forcé
- appui long suivant quand l'état forcé est `OFF` : sortie de l'override

Pendant l'override :
- `CMD_HEAT_ON` et `CMD_HEAT_OFF` sont ignorées
- `CMD_PING` continue à être traité
- le module renvoie toujours son état réel

### Diagramme watchdog

```text
Chauffage ON
    │
    ▼
Attente commandes / ping
    │
    ├── commande reçue -> reset timer
    └── timeout 3 min -> relais OFF
```

### Diagramme anti-redémarrage

```text
Relais ON
    │
    ▼
Relais OFF
    │
    ▼
Verrou 3 min actif
    │
    ├── CMD_HEAT_ON -> ACK_LOCKED
    └── délai expiré -> ACK_UNLOCKED
```

### Diagramme override manuel

```text
Appui long BOOT
    │
    ▼
Override actif ?
    │
    ├── non -> activation override + forçage état
    └── oui -> changement état forcé ou sortie override
```

## Séquence de démarrage

1. Initialisation `Serial` à `115200`
2. Attente USB CDC jusqu'à 3 secondes
3. Passage CPU à `80 MHz`
4. Initialisation `NVS`
5. Configuration des GPIO : relais, bouton `BOOT`, LED optionnelle
6. Mise du relais à l'état `OFF`
7. Initialisation WiFi en mode `STA`
8. Affichage de l'adresse MAC
9. Initialisation `ESP-NOW`
10. Enregistrement des callbacks
11. Attente des commandes

## Consommation électrique

| État | Consommation estimée |
|------|---------------------|
| Veille | ~20 mA |
| Réception ESP-NOW | ~80 mA |
| Relais actif | dépend du module relais |

## Compatibilité

### Framework

| Composant | Valeur |
|-----------|--------|
| Platform | `espressif32` |
| Framework | `arduino` |
| Board | `seeed_xiao_esp32c3` |

### Flags de compilation

```ini
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

## Limitations connues

1. Les échanges `ESP-NOW` ne sont pas chiffrés.
2. Un seul contrôleur est appris automatiquement.
3. L'adresse MAC du contrôleur n'est pas persistée après redémarrage.
4. Le bouton `BOOT` ne doit pas être maintenu pendant l'allumage.
5. `GPIO9` est une broche sensible de boot/strapping.
6. Une LED externe est nécessaire si un retour visuel est souhaité.

## Historique des versions

| Version | Date | Modifications |
|---------|------|---------------|
| `1.0.0` | Janvier 2026 | Version initiale production |
| `1.1.0` | Janvier 2026 | Ajout anti-redémarrage rapide |
| `1.2.0` | Janvier 2026 | Migration XIAO ESP32-C3 et override manuel via `BOOT` |