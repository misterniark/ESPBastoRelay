# Spécifications Techniques - ESPBastoRelay

## Matériel

### Microcontrôleur

| Paramètre | Valeur |
|-----------|--------|
| Modèle | ESP32-C3 Super Mini |
| Architecture | RISC-V 32-bit |
| Fréquence CPU | 80 MHz (mode économie) |
| Flash | 4 MB |
| RAM | 400 KB |
| WiFi | 802.11 b/g/n |

### Module Relais

| Paramètre | Valeur |
|-----------|--------|
| Tension alimentation | 5V DC |
| Tension commande | 3.3V (compatible ESP32) |
| Logique | Active HIGH |
| Courant commutation | Selon modèle (typique 10A) |

## Brochage

### GPIO Utilisés

| GPIO | Fonction | Direction | Notes |
|------|----------|-----------|-------|
| 7    | Commande relais | OUTPUT | Éviter GPIO2 (strapping pin) |
| 8    | LED statut | OUTPUT | LED interne ESP32-C3 Super Mini |

### Connexions

```
ESP32-C3 Super Mini          Module Relais 5V
┌─────────────────┐          ┌─────────────┐
│             GPIO7├─────────►│IN           │
│               5V├─────────►│VCC          │
│              GND├─────────►│GND          │
└─────────────────┘          └─────────────┘
                                    │
                              Vers Webasto
```

## Communication ESP-NOW

### Caractéristiques

| Paramètre | Valeur |
|-----------|--------|
| Protocole | ESP-NOW (Espressif) |
| Portée | ~200m (ligne de vue) |
| Latence | < 10ms |
| Encryption | Non (désactivée) |
| Canal WiFi | Auto (canal 0) |

### Structure des messages

```c
typedef struct {
    uint8_t command;  // 1 octet
} esp_now_message_t;
```

### Codes commandes

| Direction | Code | Nom | Description |
|-----------|------|-----|-------------|
| Contrôleur → Relais | 1 | CMD_HEAT_ON | Activer chauffage |
| Contrôleur → Relais | 2 | CMD_HEAT_OFF | Désactiver chauffage |
| Contrôleur → Relais | 3 | CMD_PING | Vérifier connexion |
| Relais → Contrôleur | 11 | ACK_ON | Confirmation activation |
| Relais → Contrôleur | 12 | ACK_OFF | Confirmation désactivation |
| Relais → Contrôleur | 13 | ACK_PONG | Réponse ping |
| Relais → Contrôleur | 14 | ACK_LOCKED | Redémarrage bloqué (délai sécurité) |
| Relais → Contrôleur | 15 | ACK_UNLOCKED | Redémarrage autorisé (délai expiré) |

## Sécurités

### 1. Timeout watchdog (perte de connexion)

| Paramètre | Valeur |
|-----------|--------|
| Durée | 180 000 ms (3 minutes) |
| Condition | Chauffage actif ET aucune commande reçue |
| Action | Arrêt automatique du relais |

### 2. Anti-redémarrage rapide

| Paramètre | Valeur |
|-----------|--------|
| Durée | 180 000 ms (3 minutes) |
| Déclencheur | Passage du relais à OFF |
| Action | Blocage de CMD_HEAT_ON, réponse ACK_LOCKED |

Cette protection évite les cycles ON/OFF rapides qui pourraient endommager le chauffage Webasto.

### Diagramme de sécurité watchdog

```
Chauffage ON
    │
    ▼
┌─────────────────┐
│ Attente commande│◄──────┐
│ ou ping         │       │
└────────┬────────┘       │
         │                │
    Commande reçue?       │
         │                │
    ┌────┴────┐           │
    │OUI      │NON        │
    ▼         ▼           │
  Reset    Timer > 3min?  │
  timer         │         │
    │      ┌────┴────┐    │
    │      │NON      │OUI │
    │      ▼         ▼    │
    └──────┘    ARRÊT     │
                SECURITE  │
                    │     │
                    ▼     │
              Relais OFF  │
              Log erreur  │
```

### Diagramme anti-redémarrage

```
Relais passe à OFF
    │
    ▼
Verrou activé (3 min)
    │
    ▼
┌─────────────────────┐
│ Loop: vérification  │◄─────┐
│ périodique          │      │
└─────────┬───────────┘      │
          │                  │
     Délai écoulé ?          │
          │                  │
     ┌────┴────┐             │
     │NON      │OUI          │
     ▼         ▼             │
   Attente   ACK_UNLOCKED    │
     │       envoyé          │
     │       Verrou OFF      │
     └───────────────────────┘


CMD_HEAT_ON reçu
    │
    ▼
┌─────────────────┐
│ Verrou actif ?  │
└────────┬────────┘
         │
    ┌────┴────┐
    │NON      │OUI
    ▼         ▼
 Relais ON  ACK_LOCKED
 ACK_ON     (bloqué)
```

## Séquence de démarrage

1. Initialisation Serial (115200 bauds)
2. Attente USB CDC (max 3 secondes)
3. Configuration CPU 80 MHz
4. Initialisation NVS
5. Configuration GPIO (relais + LED)
6. Initialisation WiFi (mode STA)
7. Affichage adresse MAC
8. Initialisation ESP-NOW
9. Enregistrement callbacks
10. Attente commandes

## Consommation électrique

| État | Consommation estimée |
|------|---------------------|
| Veille (attente) | ~20 mA |
| Réception ESP-NOW | ~80 mA |
| Relais actif | +50-80 mA (selon modèle) |

## Compatibilité

### Framework

| Composant | Version |
|-----------|---------|
| Platform | espressif32 |
| Framework | Arduino |
| Board | esp32-c3-devkitm-1 |

### Flags de compilation

```ini
build_flags = 
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

## Limitations connues

1. **Pas de chiffrement ESP-NOW** : Les commandes sont envoyées en clair
2. **Single peer** : Un seul contrôleur peut être enregistré (le premier à envoyer une commande)
3. **Pas de persistance** : L'adresse du contrôleur n'est pas sauvegardée après redémarrage
4. **GPIO2 à éviter** : Strapping pin pouvant causer des problèmes au boot
5. **Délai anti-redémarrage** : 3 minutes obligatoires après arrêt (le contrôleur doit gérer ACK_LOCKED)

## Historique des versions

| Version | Date | Modifications |
|---------|------|---------------|
| 1.0.0 | Janvier 2026 | Version initiale production |
| 1.1.0 | Janvier 2026 | Ajout sécurité anti-redémarrage rapide (3 min) |