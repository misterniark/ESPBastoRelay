#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// CONFIGURATION RELAIS - ESP32-C3 Super Mini
// ============================================

// Pin du relais (à adapter selon votre câblage)
#define RELAY_PIN     2     // GPIO2 - Commande relais

// LED de statut (optionnel)
#define LED_STATUS    8     // GPIO8 - LED interne ESP32-C3

// ============================================
// ESP-NOW - COMMANDES (identiques au contrôleur)
// ============================================

// Commandes reçues du contrôleur
#define CMD_HEAT_ON   1
#define CMD_HEAT_OFF  2
#define CMD_PING      3

// Réponses envoyées au contrôleur
#define ACK_ON        11
#define ACK_OFF       12
#define ACK_PONG      13

// ============================================
// CONFIGURATION RELAIS
// ============================================

// Logique du relais (HIGH = actif ou LOW = actif)
#define RELAY_ACTIVE_HIGH  true   // true si le relais s'active avec HIGH

// Timeout sécurité : éteindre si pas de ping reçu (en ms)
#define SAFETY_TIMEOUT_MS  180000  // 3 minutes sans ping = arrêt sécurité

#endif
