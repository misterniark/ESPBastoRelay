/*
 * ============================================
 * RELAIS WEBASTO - XIAO ESP32-C3
 * ============================================
 *
 * Reçoit les commandes ESP-NOW du contrôleur
 * et pilote le relais pour le chauffage Webasto
 *
 * Sécurité : Arrêt automatique si perte de connexion
 *
 * Corrections appliquées :
 *   - Race condition : les commandes reçues dans le callback WiFi
 *     sont stockées dans une queue FreeRTOS, puis traitées dans loop().
 *   - setRelay() n'est plus appelé depuis le callback WiFi.
 */

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"
#include "usb_cdc_kick.h"  // ZLP périodique anti-échouage des logs USB

// ============================================
// STRUCTURE POUR LA QUEUE DE COMMANDES
// ============================================

typedef struct {
    uint8_t command;       // Commande reçue (CMD_HEAT_ON, CMD_HEAT_OFF, CMD_PING)
    uint8_t mac[6];        // MAC de l'expéditeur
    bool    has_mac;       // true si c'est un nouveau contrôleur à enregistrer
    bool    broadcast;     // true si le message est arrivé en diffusion
} pending_command_t;

// ============================================
// VARIABLES GLOBALES
// (modifiées UNIQUEMENT depuis loop(), plus depuis le callback)
// ============================================

bool relayOn = false;
unsigned long lastPingReceived = 0;
bool controllerConnected = false;

// Sécurité anti-redémarrage rapide
unsigned long lastRelayOff = 0;
bool restartLockActive = false;
// Délai minimal entre une extinction et le rallumage suivant : un
// Webasto rallumé à chaud sans son cycle de purge noie la chambre de
// combustion et encrasse la bougie.
#ifdef TEST_CLI
// Banc de test uniquement : verrou raccourci pour enchaîner les
// scénarios sans attendre 3 minutes entre chaque cycle. Le mécanisme
// reste exercé à l'identique (armement, ACK_LOCKED, ACK_UNLOCKED) —
// seule sa durée change. Le firmware de production, lui, garde la
// valeur nominale : plus besoin de modifier cette constante à la main
// pour tester, donc plus de risque de livrer un verrou désactivé.
const unsigned long RESTART_DELAY_MS = 5000;    // 5 s (banc)
#else
const unsigned long RESTART_DELAY_MS = 180000;  // 3 minutes (production)
#endif

// Override manuel via bouton BOOT
bool manualOverrideActive = false;
bool manualForcedState = false;
bool manualButtonPressed = false;
bool manualButtonHandled = false;
unsigned long manualButtonPressStart = 0;

// Adresse MAC du contrôleur (apprise automatiquement)
uint8_t controllerMac[6] = {0};
bool controllerKnown = false;

// Queue FreeRTOS pour les commandes reçues du callback WiFi
static QueueHandle_t cmd_queue = NULL;
const int CMD_QUEUE_SIZE = 8;

// ============================================
// STRUCTURE MESSAGE ESP-NOW
// ============================================

// 2 octets : le code, plus un octet de charge utile qui porte l'état
// réel du relais dans les ACK_PONG (RELAY_STATE_OFF/ON) — voir config.h.
// La réception tolère 1 ou 2 octets pour rester compatible avec un
// contrôleur non encore mis à jour (l'octet d'état est alors absent).
typedef struct {
    uint8_t command;
    uint8_t payload;
} esp_now_message_t;

esp_now_message_t outgoingMsg;

// Déclarations anticipées
void sendResponse(uint8_t response, uint8_t payload = 0);
void addControllerPeer(bool encrypt);

// ============================================
// FONCTIONS LED
// ============================================

void setStatusLed(bool on) {
    if (LED_STATUS < 0) {
        return;
    }
    digitalWrite(LED_STATUS, on ? LOW : HIGH);  // LED active LOW
}

void sendCurrentStateResponse() {
    sendResponse(relayOn ? ACK_ON : ACK_OFF);
}

// ============================================
// FONCTIONS RELAIS
// ============================================

void setRelay(bool on) {
    bool wasOn = relayOn;
    relayOn = on;

    if (RELAY_ACTIVE_HIGH) {
        digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    } else {
        digitalWrite(RELAY_PIN, on ? LOW : HIGH);
    }

    setStatusLed(on);

    // Activer le verrou UNIQUEMENT si passage de ON à OFF
    if (wasOn && !on) {
        lastRelayOff = millis();
        restartLockActive = true;
        // Afficher la durée RÉELLE : elle diffère entre le firmware de
        // production (3 min) et celui du banc de test (5 s).
        Serial.printf("Verrou anti-redemarrage active (%lu s)\n",
                      RESTART_DELAY_MS / 1000UL);
    }

    Serial.print("Relais: ");
    Serial.println(on ? "ON" : "OFF");
}

bool canRestart() {
    if (!restartLockActive) {
        return true;
    }

    unsigned long elapsed = millis() - lastRelayOff;
    unsigned long remaining = (RESTART_DELAY_MS - elapsed) / 1000;
    Serial.print("!!! REDEMARRAGE BLOQUE - Attendre ");
    Serial.print(remaining);
    Serial.println(" secondes !!!");
    return false;
}

// ============================================
// OVERRIDE MANUEL (bouton BOOT)
// ============================================

void handleManualOverrideAction() {
    if (!manualOverrideActive) {
        manualOverrideActive = true;
        manualForcedState = !relayOn;
        Serial.println("Override manuel active");
    } else if (manualForcedState) {
        manualForcedState = false;
    } else {
        manualOverrideActive = false;
        Serial.println("Override manuel desactive - retour controle ESP-NOW");
        sendCurrentStateResponse();
        return;
    }

    if (manualForcedState) {
        if (canRestart()) {
            setRelay(true);
            Serial.println("Override manuel: chauffage FORCE ON");
            sendResponse(ACK_ON);
        } else {
            Serial.println("Override manuel: FORCE ON refuse par verrou");
            sendResponse(ACK_LOCKED);
        }
    } else {
        setRelay(false);
        Serial.println("Override manuel: chauffage FORCE OFF");
        sendResponse(ACK_OFF);
    }
}

void updateManualButton() {
    bool pressed = digitalRead(MANUAL_BUTTON_PIN) == MANUAL_BUTTON_ACTIVE_STATE;
    unsigned long now = millis();

    if (pressed) {
        if (!manualButtonPressed) {
            manualButtonPressed = true;
            manualButtonHandled = false;
            manualButtonPressStart = now;
        } else if (!manualButtonHandled &&
                   (now - manualButtonPressStart >= MANUAL_BUTTON_LONG_PRESS_MS)) {
            manualButtonHandled = true;
            handleManualOverrideAction();
        }
    } else {
        manualButtonPressed = false;
        manualButtonHandled = false;
    }
}

// ============================================
// FONCTIONS ESP-NOW
// ============================================

// ============================================
// PERSISTANCE DE LA MAC DU CONTRÔLEUR (NVS)
//
// Pourquoi : avec le chiffrement, un relais qui redémarre sans se
// souvenir de son contrôleur ne peut plus DÉCHIFFRER ses pings — il
// faudrait attendre l'échec des 3 tentatives unicast puis un
// réappairage par diffusion (~20 s), et la désynchronisation ne serait
// détectée que par la perte de connexion (2 min) au lieu du premier
// PONG (60 s). En mémorisant la MAC, le relais réenregistre le peer
// chiffré dès son démarrage et la resynchronisation reste immédiate.
// ============================================
Preferences prefs;
const char *NVS_NAMESPACE = "espbasto";
const char *NVS_KEY_CTRL_MAC = "ctrl_mac";

void saveControllerMac() {
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putBytes(NVS_KEY_CTRL_MAC, controllerMac, 6);
    prefs.end();
}

void forgetControllerMac() {
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.remove(NVS_KEY_CTRL_MAC);
    prefs.end();
}

// Recharge la MAC mémorisée et réenregistre le peer chiffré.
// @return true si un contrôleur était mémorisé
bool loadControllerMac() {
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    size_t len = prefs.getBytesLength(NVS_KEY_CTRL_MAC);
    bool found = (len == 6) && (prefs.getBytes(NVS_KEY_CTRL_MAC, controllerMac, 6) == 6);
    prefs.end();
    return found;
}

// (Ré)enregistre le contrôleur comme peer ESP-NOW, en clair ou chiffré.
// Un peer existant est supprimé d'abord : c'est ainsi qu'on passe du
// clair (appairage) au chiffré (exploitation).
void addControllerPeer(bool encrypt) {
    static const uint8_t lmk[16] = ESPNOW_LMK_BYTES;

    esp_now_del_peer(controllerMac);  // sans effet s'il n'existe pas

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, controllerMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = encrypt;
    if (encrypt) {
        memcpy(peerInfo.lmk, lmk, sizeof(peerInfo.lmk));
    }

    esp_err_t r = esp_now_add_peer(&peerInfo);
    if (r != ESP_OK) {
        Serial.printf("Erreur ajout peer controleur (%s): %d\n",
                      encrypt ? "chiffre" : "clair", r);
    }
}

void sendResponse(uint8_t response, uint8_t payload) {
    if (!controllerKnown) {
        Serial.println("Controleur inconnu, pas de reponse");
        return;
    }

    outgoingMsg.command = response;
    outgoingMsg.payload = payload;
    esp_err_t result = esp_now_send(controllerMac, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));

    if (result == ESP_OK) {
        Serial.print("Reponse envoyee: ");
        Serial.println(response);
    } else {
        Serial.println("Erreur envoi reponse");
    }
}

/*
 * Callback réception ESP-NOW (contexte WiFi).
 * Ne modifie AUCUNE variable globale directement.
 * Pousse la commande dans une queue FreeRTOS pour traitement dans loop().
 */
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    // Tolérance de taille : 2 octets (protocole courant, avec l'octet
    // d'état) ou 1 octet (contrôleur non encore mis à jour). Rejeter
    // tout le reste.
    if (len < 1 || len > (int)sizeof(esp_now_message_t)) {
        return;
    }

    pending_command_t cmd;
    cmd.command = data[0];
    memcpy(cmd.mac, recv_info->src_addr, 6);
    cmd.has_mac = !controllerKnown;  // Lecture seule, pas de modification

    // Le message est-il arrivé en diffusion ? Déterminant pour la
    // suite : un contrôleur qui diffuse est en phase de (re)découverte,
    // il a supprimé son peer et ne peut donc PAS déchiffrer nos
    // réponses (voir processCommand).
    static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    cmd.broadcast = (memcmp(recv_info->des_addr, BCAST, 6) == 0);

    // Pousser dans la queue (non bloquant, on abandonne si pleine)
    if (cmd_queue) {
        xQueueSend(cmd_queue, &cmd, 0);
    }
}

// Callback envoi ESP-NOW
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    // Rien à faire
}

// ============================================
// TRAITEMENT DES COMMANDES (appelé depuis loop)
// ============================================

/*
 * Traite une commande reçue. Appelé uniquement depuis loop(),
 * jamais depuis le callback WiFi. Toutes les modifications de
 * variables globales et les appels à setRelay() sont sûrs ici.
 */
#ifdef TEST_CLI
// ============================================
// BANC DE TEST SÉRIE — JAMAIS EN PRODUCTION
// (env seeed_xiao_esp32c3_test uniquement)
// Commandes :
//   mute <s>  → ignorer TOUT message ESP-NOW entrant pendant <s>
//               secondes (simule la perte de liaison : les pings ne
//               rafraîchissent plus lastPingReceived, le watchdog
//               SAFETY_TIMEOUT_MS doit couper le chauffage)
//   status    → état relais/verrou/connexion/mute
// L'ÉMISSION reste active pendant le mute (le relais peut toujours
// notifier ses coupures) — seule la RÉCEPTION est simulée coupée.
// ============================================
unsigned long tcliMuteUntilMs = 0;

bool tcliIsMuted() {
    return tcliMuteUntilMs != 0 && millis() < tcliMuteUntilMs;
}

void tcliHandleLine(const char *line) {
    if (strncmp(line, "mute ", 5) == 0) {
        long s = atol(line + 5);
        tcliMuteUntilMs = millis() + (unsigned long)s * 1000UL;
        Serial.printf("[TCLI] mute %ld s (reception ESP-NOW ignoree)\n", s);
    } else if (strcmp(line, "status") == 0) {
        Serial.printf("[TCLI] status: relais=%d verrou=%d connecte=%d mute=%d\n",
                      (int)relayOn, (int)restartLockActive,
                      (int)controllerConnected, (int)tcliIsMuted());
    } else {
        Serial.printf("[TCLI] commande inconnue: '%s'\n", line);
    }
}

void tcliUpdate() {
    static char buf[32];
    static size_t len = 0;
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (len > 0) {
                buf[len] = '\0';
                tcliHandleLine(buf);
                len = 0;
            }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        } else {
            len = 0;
        }
    }
}
#endif // TEST_CLI

void processCommand(const pending_command_t &cmd) {
#ifdef TEST_CLI
    // Banc de test : liaison simulée coupée — ignorer le message AVANT
    // toute mise à jour (lastPingReceived ne doit pas être rafraîchi)
    if (tcliIsMuted()) {
        Serial.println("[TCLI] MUTE: message ESP-NOW ignore");
        return;
    }
#endif

    // FILTRAGE DE LA SOURCE : une fois appairé, n'obéir qu'à SON
    // contrôleur. Sans ce test, toute trame d'un octet, de n'importe
    // quelle source, allumait le Webasto (constat bloquant de l'audit
    // du 27/07/2026) — y compris, sans malveillance, celle du kit
    // identique d'un van voisin. La porte de sortie est le délai
    // d'oubli ci-dessous : un contrôleur remplacé peut se réappairer.
    if (controllerKnown && memcmp(cmd.mac, controllerMac, 6) != 0) {
        Serial.printf("Commande ignoree : source inattendue "
                      "%02X:%02X:%02X:%02X:%02X:%02X\n",
                      cmd.mac[0], cmd.mac[1], cmd.mac[2],
                      cmd.mac[3], cmd.mac[4], cmd.mac[5]);
        return;
    }

    // Enregistrer le contrôleur si nouveau.
    // Le peer est d'abord ajouté EN CLAIR : le contrôleur ne nous
    // connaît pas encore et ne pourrait pas déchiffrer notre réponse
    // (ESP-NOW déchiffre grâce à l'entrée de peer, qu'il n'a pas
    // encore). On lui répond donc en clair, puis on bascule en chiffré
    // juste après (voir la fin de cette fonction) : à partir de là tout
    // l'unicast est chiffré dans les deux sens.
    // Une trame de DIFFUSION ne peut pas être chiffrée : n'y accepter
    // que le ping de (re)découverte. Toute commande d'actionnement doit
    // arriver en unicast — donc chiffrée une fois l'appairage fait.
    // Sans cette règle, une simple diffusion en clair suffirait à
    // commander le chauffage en contournant le chiffrement.
    if (cmd.broadcast && cmd.command != CMD_PING) {
        Serial.printf("Commande %d ignoree : recue en diffusion\n", cmd.command);
        return;
    }

    bool justPaired = false;
    if (cmd.has_mac && !controllerKnown) {
        memcpy(controllerMac, cmd.mac, 6);
        controllerKnown = true;
        justPaired = true;

        Serial.print("Controleur enregistre: ");
        for (int i = 0; i < 6; i++) {
            Serial.printf("%02X", controllerMac[i]);
            if (i < 5) Serial.print(":");
        }
        Serial.println();
    }

    // RÉPONDRE EN CLAIR dans deux cas :
    //   - appairage initial : le contrôleur ne nous connaît pas encore ;
    //   - message reçu en DIFFUSION : le contrôleur est en train de nous
    //     redécouvrir, il a supprimé son peer et ne peut plus déchiffrer.
    // Ce second cas est indispensable : sans lui, un relais qui se
    // souvient du contrôleur lui répondait chiffré alors qu'il ne
    // pouvait plus lire — les deux appareils restaient bloqués face à
    // face indéfiniment (constaté au banc le 27/07/2026, campagne T9).
    // Le peer est rebasculé en chiffré juste après la réponse.
    bool clearReply = justPaired || cmd.broadcast;
    if (clearReply && controllerKnown) {
        addControllerPeer(false);
    }

    Serial.print("Commande recue: ");
    Serial.println(cmd.command);

    switch (cmd.command) {
        case CMD_HEAT_ON:
            if (manualOverrideActive) {
                Serial.println("Commande distante ignoree: override manuel actif");
                sendCurrentStateResponse();
            } else if (canRestart()) {
                setRelay(true);
                sendResponse(ACK_ON);
            } else {
                sendResponse(ACK_LOCKED);
            }
            controllerConnected = true;
            lastPingReceived = millis();
            break;

        case CMD_HEAT_OFF:
            if (manualOverrideActive) {
                Serial.println("Commande distante ignoree: override manuel actif");
                sendCurrentStateResponse();
            } else {
                setRelay(false);
                sendResponse(ACK_OFF);
                // Resynchronisation : si le relais était DÉJÀ éteint
                // (coupure de sécurité locale antérieure), setRelay(false)
                // n'a pas armé le verrou (pas de transition ON→OFF) et
                // aucun ACK_UNLOCKED ne viendra jamais — le contrôleur,
                // passé LOCKED sur notre ACK_OFF, resterait bloqué à vie.
                // Lui signaler tout de suite que le redémarrage est permis.
                if (!restartLockActive) {
                    sendResponse(ACK_UNLOCKED);
                }
            }
            controllerConnected = true;
            lastPingReceived = millis();
            break;

        case CMD_PING:
            // Le PONG porte l'état RÉEL du relais : c'est le mécanisme
            // de resynchronisation du système. Après un reboot de l'un
            // ou l'autre appareil, ou la perte d'un ACK, le contrôleur
            // détecte l'écart au ping suivant et se remet d'accord avec
            // la réalité au lieu d'afficher un état faux indéfiniment.
            sendResponse(ACK_PONG, relayOn ? RELAY_STATE_ON : RELAY_STATE_OFF);
            controllerConnected = true;
            lastPingReceived = millis();
            break;

        default:
            Serial.print("Commande inconnue: ");
            Serial.println(cmd.command);
            break;
    }

    // Appairage terminé : la réponse en clair est partie, on bascule le
    // peer en CHIFFRÉ. Le contrôleur en fait autant de son côté dès
    // qu'il a reçu cette réponse. Tout l'unicast suivant est chiffré.
    if (clearReply && controllerKnown) {
        addControllerPeer(true);
        saveControllerMac();  // pour rester appairé au prochain reboot
        Serial.println("Liaison chiffree activee");
    }
}

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);

    // Attendre que le port USB CDC soit prêt (max 3 secondes)
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 3000)) {
        delay(10);
    }
    delay(100);

    setCpuFrequencyMhz(80);

    Serial.println("\n=== RELAIS WEBASTO ===");

    // Initialisation NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Configuration pins
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(MANUAL_BUTTON_PIN, INPUT_PULLUP);
    if (LED_STATUS >= 0) {
        pinMode(LED_STATUS, OUTPUT);
    }

    // État initial : relais OFF (sécurité)
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    setStatusLed(false);

    // Créer la queue de commandes
    cmd_queue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(pending_command_t));
    if (!cmd_queue) {
        Serial.println("ERREUR: impossible de creer la queue");
    }

    // Afficher adresse MAC
    WiFi.mode(WIFI_STA);
    Serial.print("Adresse MAC: ");
    Serial.println(WiFi.macAddress());

    // Initialisation ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur init ESP-NOW");
        return;
    }

    // Clé primaire : protège les LMK des peers. Doit être identique
    // côté contrôleur (voir config.h des deux projets).
    {
        static const uint8_t pmk[16] = ESPNOW_PMK_BYTES;
        if (esp_now_set_pmk(pmk) != ESP_OK) {
            Serial.println("ATTENTION : echec configuration de la PMK");
        }
    }

    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    // Réappairage immédiat si un contrôleur était mémorisé : le peer
    // chiffré est reconstitué avant même le premier ping, donc aucune
    // phase de redécouverte après un simple reboot du relais.
    if (loadControllerMac()) {
        controllerKnown = true;
        addControllerPeer(true);
        Serial.print("Controleur memorise: ");
        for (int i = 0; i < 6; i++) {
            Serial.printf("%02X", controllerMac[i]);
            if (i < 5) Serial.print(":");
        }
        Serial.println(" (liaison chiffree)");
    }

    Serial.println("ESP-NOW initialise");
    Serial.println("En attente de commandes...");

    lastPingReceived = millis();
}

// ============================================
// LOOP
// ============================================

void loop() {
    unsigned long now = millis();

#ifdef TEST_CLI
    // Banc de test : traiter les commandes série (mute/status)
    tcliUpdate();
#endif

    // Traiter les commandes reçues via la queue (thread-safe)
    pending_command_t cmd;
    while (xQueueReceive(cmd_queue, &cmd, 0) == pdTRUE) {
        processCommand(cmd);
    }

    // Gestion du bouton BOOT pour override manuel
    updateManualButton();

    // Vérifier si le verrou anti-redémarrage vient d'expirer.
    // ATTENTION : utiliser millis() FRAIS et non `now` capturé en début
    // de tour — processCommand() vient peut-être de poser lastRelayOff
    // à un instant POSTÉRIEUR à `now`, et `now - lastRelayOff` en non
    // signé déborderait (~2^32) > RESTART_DELAY_MS : le verrou
    // « expirait » instantanément après chaque coupure par commande
    // (bug historique découvert au banc de test du 27/07/2026).
    if (restartLockActive) {
        if (millis() - lastRelayOff >= RESTART_DELAY_MS) {
            restartLockActive = false;
            Serial.println("Verrou anti-redemarrage expire");
            sendResponse(ACK_UNLOCKED);
        }
    }

    // Oubli du contrôleur appairé après une absence prolongée : porte
    // de sortie du filtrage par MAC (voir CONTROLLER_FORGET_MS). Le
    // relais redevient appairable, ce qui permet de remplacer le
    // contrôleur sans reflasher le relais. Sans danger : le chauffage
    // a déjà été coupé par le watchdog bien avant (3 min).
    if (controllerKnown && !relayOn
        && millis() - lastPingReceived > CONTROLLER_FORGET_MS) {
        Serial.println("Controleur absent depuis longtemps : oubli, "
                       "relais reappairable");
        esp_now_del_peer(controllerMac);
        forgetControllerMac();
        controllerKnown = false;
        controllerConnected = false;
    }

    // Sécurité : arrêt si pas de ping depuis trop longtemps.
    // Même piège de débordement que ci-dessus : lastPingReceived est
    // rafraîchi par processCommand() APRÈS la capture de `now` — avec
    // `now`, le chauffage se coupait « perte connexion » dans la
    // milliseconde suivant chaque allumage.
    if (controllerConnected && relayOn) {
        if (millis() - lastPingReceived > SAFETY_TIMEOUT_MS) {
            Serial.println("!!! SECURITE: Perte connexion controleur !!!");
            Serial.println("!!! Arret automatique du chauffage !!!");
            setRelay(false);
            controllerConnected = false;
            // Informer le contrôleur (best effort : la connexion est
            // peut-être réellement morte) : sans cet envoi, il resterait
            // en HEATING alors que le relais est OFF — chauffage coupé
            // silencieusement. ACK_OFF le fait passer en LOCKED, puis
            // l'ACK_UNLOCKED d'expiration du verrou le libérera.
            sendResponse(ACK_OFF);
        }
    }

    delay(10);  // 10ms au lieu de 100ms pour plus de réactivité

    // Terminer toute transaction USB laissée ouverte par un paquet de
    // 64 octets pile, sinon les logs peuvent rester invisibles côté
    // hôte pendant des dizaines de secondes (voir usb_cdc_kick.h).
    usb_cdc_kick();
}
