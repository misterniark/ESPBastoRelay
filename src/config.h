#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// CONFIGURATION RELAIS - XIAO ESP32-C3
// ============================================

// Pin du relais (à adapter selon votre câblage)
// Sur XIAO ESP32-C3, D1 correspond à GPIO3
#define RELAY_PIN     D1

// Bouton manuel de secours
// Sur XIAO ESP32-C3, le bouton BOOT est sur D9 / GPIO9.
// Attention: si maintenu pendant le démarrage, la carte peut entrer en bootloader.
#define MANUAL_BUTTON_PIN              D9
#define MANUAL_BUTTON_ACTIVE_STATE     LOW
#define MANUAL_BUTTON_LONG_PRESS_MS    2000

// LED de statut (optionnel)
// Le XIAO ESP32-C3 n'expose pas de LED utilisateur pilotable intégrée.
// Mettre D10 si vous branchez une LED externe, sinon laisser -1.
#define LED_STATUS    -1

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
#define ACK_LOCKED    14    // Redémarrage bloqué (délai de sécurité)
#define ACK_UNLOCKED  15    // Redémarrage autorisé (délai expiré)

// Second octet du message : état réel du relais, porté par ACK_PONG.
// C'est ce qui permet au contrôleur de détecter toute désynchronisation
// (reboot d'un des deux appareils, ACK perdu) et de se resynchroniser
// en un intervalle de ping, au lieu d'afficher indéfiniment un état
// faux. Ignoré pour les autres codes de réponse.
#define RELAY_STATE_OFF  0
#define RELAY_STATE_ON   1

// ============================================
// CONFIGURATION RELAIS
// ============================================

// Logique du relais (HIGH = actif ou LOW = actif)
// La plupart des modules relais 5V sont actifs LOW (false)
#define RELAY_ACTIVE_HIGH  true   // false pour modules relais 5V courants

// Timeout sécurité : éteindre si pas de ping reçu (en ms)
#define SAFETY_TIMEOUT_MS  180000  // 3 minutes sans ping = arrêt sécurité

// Délai au bout duquel le relais OUBLIE son contrôleur appairé et
// redevient appairable par n'importe lequel. C'est la porte de sortie
// du filtrage par MAC : sans elle, un contrôleur remplacé (carte HS,
// nouveau matériel) ne pourrait plus JAMAIS piloter ce relais sans
// reflasher. Largement supérieur à l'intervalle de ping (60 s) : ce
// délai n'est atteint que si le contrôleur a réellement disparu.
#define CONTROLLER_FORGET_MS  600000  // 10 minutes sans message valide

// ============================================
// CHIFFREMENT ESP-NOW
//
// ⚠️ CES DEUX CLÉS DOIVENT ÊTRE IDENTIQUES DANS LES DEUX FIRMWARES.
// La copie de référence côté contrôleur est dans
// EspBastoClaude/src/config.h. Après modification, reflasher LES DEUX
// cartes : des clés différentes = plus aucune communication (donc plus
// de chauffage pilotable).
//
// Sans chiffrement, n'importe quel ESP32 à portée peut commander le
// chauffage : le filtrage par MAC seul se contourne par usurpation
// d'adresse. La découverte par diffusion reste en clair (ESP-NOW
// interdit le chiffrement en broadcast) ; seul le trafic unicast
// contrôleur ↔ relais est chiffré.
// ============================================
#define ESPNOW_PMK_BYTES { \
    0x45, 0x53, 0x50, 0x42, 0x61, 0x73, 0x74, 0x6F, \
    0x2D, 0x50, 0x4D, 0x4B, 0x2D, 0x76, 0x31, 0x21 }
#define ESPNOW_LMK_BYTES { \
    0x57, 0x65, 0x62, 0x61, 0x73, 0x74, 0x6F, 0x2D, \
    0x4C, 0x4D, 0x4B, 0x2D, 0x76, 0x31, 0x21, 0x3F }

#endif
