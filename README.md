# ESPBasto Relay - Module Relais Webasto

Module ESP32-C3 distant qui reçoit les commandes ESP-NOW du contrôleur et pilote le relais du chauffage Webasto.

## Câblage

| Composant | GPIO |
|-----------|------|
| Relais (signal) | GPIO2 |
| LED status | GPIO8 (interne) |

## Fonctionnement

1. Au démarrage, le relais est **OFF**
2. La LED clignote en attente de connexion
3. Dès qu'une commande est reçue, l'adresse MAC du contrôleur est mémorisée
4. Les commandes sont exécutées et des ACK sont renvoyés

## Sécurité

Si aucun PING n'est reçu pendant **3 minutes**, le chauffage s'éteint automatiquement.

## Commandes ESP-NOW

| Commande | Code | Réponse |
|----------|------|---------|
| HEAT_ON | 1 | ACK_ON (11) |
| HEAT_OFF | 2 | ACK_OFF (12) |
| PING | 3 | ACK_PONG (13) |

## Compilation

```bash
cd relay
pio run -t upload
```

## Configuration

Modifier `src/config.h` pour ajuster :
- `RELAY_PIN` : GPIO du relais
- `RELAY_ACTIVE_HIGH` : Logique du relais (true = HIGH active)
- `SAFETY_TIMEOUT_MS` : Timeout sécurité (défaut 3 min)

## Récupérer l'adresse MAC

Après le flash, ouvrir le moniteur série :
```bash
pio device monitor
```

L'adresse MAC s'affiche au démarrage. La copier dans le fichier `src/config.h` du contrôleur (RELAY_MAC).
