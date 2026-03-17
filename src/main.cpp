/*
 * ============================================
 * RELAIS WEBASTO - XIAO ESP32-C3
 * ============================================
 * 
 * Reçoit les commandes ESP-NOW du contrôleur
 * et pilote le relais pour le chauffage Webasto
 * 
 * Sécurité : Arrêt automatique si perte de connexion
 */

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include "config.h"

// ============================================
// VARIABLES GLOBALES
// ============================================

bool relayOn = false;
unsigned long lastPingReceived = 0;
bool controllerConnected = false;

// Sécurité anti-redémarrage rapide
unsigned long lastRelayOff = 0;           // Moment du dernier arrêt
bool restartLockActive = false;           // Verrou actif
const unsigned long RESTART_DELAY_MS = 180000;  // 3 minutes de délai

// Override manuel via bouton BOOT
bool manualOverrideActive = false;
bool manualForcedState = false;
bool manualButtonPressed = false;
bool manualButtonHandled = false;
unsigned long manualButtonPressStart = 0;

// Adresse MAC du contrôleur (sera apprise automatiquement)
uint8_t controllerMac[6] = {0};
bool controllerKnown = false;

// ============================================
// STRUCTURE MESSAGE (identique au contrôleur)
// ============================================

typedef struct {
    uint8_t command;
} esp_now_message_t;

esp_now_message_t outgoingMsg;

// Déclarations anticipées
void sendResponse(uint8_t response);

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
    bool wasOn = relayOn;  // État précédent
    relayOn = on;
    
    // Commander le relais selon la logique configurée
    if (RELAY_ACTIVE_HIGH) {
        digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    } else {
        digitalWrite(RELAY_PIN, on ? LOW : HIGH);
    }
    
    // LED de statut
    setStatusLed(on);
    
    // Activer le verrou UNIQUEMENT si passage de ON à OFF
    if (wasOn && !on) {
        lastRelayOff = millis();
        restartLockActive = true;
        Serial.println("Verrou anti-redemarrage active (3 min)");
    }
    
    Serial.print("Relais: ");
    Serial.println(on ? "ON" : "OFF");
}

// Vérifie si le redémarrage est autorisé
bool canRestart() {
    if (!restartLockActive) {
        return true;
    }
    
    // Afficher le temps restant
    unsigned long elapsed = millis() - lastRelayOff;
    unsigned long remaining = (RESTART_DELAY_MS - elapsed) / 1000;
    Serial.print("!!! REDEMARRAGE BLOQUE - Attendre ");
    Serial.print(remaining);
    Serial.println(" secondes !!!");
    return false;
}

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

void sendResponse(uint8_t response) {
    if (!controllerKnown) {
        Serial.println("Controleur inconnu, pas de reponse");
        return;
    }
    
    outgoingMsg.command = response;
    esp_err_t result = esp_now_send(controllerMac, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
    
    if (result == ESP_OK) {
        Serial.print("Reponse envoyee: ");
        Serial.println(response);
    } else {
        Serial.println("Erreur envoi reponse");
    }
}

// Callback réception ESP-NOW (ESP-IDF 5.x)
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(esp_now_message_t)) {
        return;
    }
    
    // Mémoriser l'adresse du contrôleur
    if (!controllerKnown) {
        memcpy(controllerMac, recv_info->src_addr, 6);
        controllerKnown = true;
        
        // Ajouter le peer pour pouvoir répondre
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, controllerMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
        
        Serial.print("Controleur enregistre: ");
        for (int i = 0; i < 6; i++) {
            Serial.printf("%02X", controllerMac[i]);
            if (i < 5) Serial.print(":");
        }
        Serial.println();
    }
    
    esp_now_message_t incomingMsg;
    memcpy(&incomingMsg, data, sizeof(incomingMsg));
    
    Serial.print("Commande recue: ");
    Serial.println(incomingMsg.command);
    
    switch (incomingMsg.command) {
        case CMD_HEAT_ON:
            if (manualOverrideActive) {
                Serial.println("Commande distante ignoree: override manuel actif");
                sendCurrentStateResponse();
                controllerConnected = true;
                lastPingReceived = millis();
                break;
            }
            if (canRestart()) {
                setRelay(true);
                sendResponse(ACK_ON);
            } else {
                sendResponse(ACK_LOCKED);  // Redémarrage bloqué
            }
            controllerConnected = true;
            lastPingReceived = millis();
            break;
            
        case CMD_HEAT_OFF:
            if (manualOverrideActive) {
                Serial.println("Commande distante ignoree: override manuel actif");
                sendCurrentStateResponse();
                controllerConnected = true;
                lastPingReceived = millis();
                break;
            }
            setRelay(false);
            sendResponse(ACK_OFF);
            controllerConnected = true;
            lastPingReceived = millis();
            break;
            
        case CMD_PING:
            sendResponse(ACK_PONG);
            controllerConnected = true;
            lastPingReceived = millis();
            Serial.println("PING recu, PONG envoye");
            break;
            
        default:
            Serial.print("Commande inconnue: ");
            Serial.println(incomingMsg.command);
            break;
    }
}

// Callback envoi ESP-NOW (ESP-IDF 5.x)
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    // Rien à faire ici
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
    delay(100);  // Petit délai supplémentaire pour stabilité
    
    setCpuFrequencyMhz(80);  // Économie d'énergie
    
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
    
    // Afficher adresse MAC
    WiFi.mode(WIFI_STA);
    Serial.print("Adresse MAC: ");
    Serial.println(WiFi.macAddress());
    
    // Initialisation ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur init ESP-NOW");
        return;
    }
    
    // Enregistrer callbacks
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);
    
    Serial.println("ESP-NOW initialise");
    Serial.println("En attente de commandes...");
    
    lastPingReceived = millis();
}

// ============================================
// LOOP
// ============================================

void loop() {
    unsigned long now = millis();

    // Gestion du bouton BOOT pour override manuel
    updateManualButton();
    
    // Vérifier si le verrou anti-redémarrage vient d'expirer
    if (restartLockActive) {
        if (now - lastRelayOff >= RESTART_DELAY_MS) {
            restartLockActive = false;
            Serial.println("Verrou anti-redemarrage expire");
            sendResponse(ACK_UNLOCKED);  // Notifier le contrôleur
        }
    }
    
    // Sécurité : arrêt si pas de ping depuis trop longtemps
    if (controllerConnected && relayOn) {
        if (now - lastPingReceived > SAFETY_TIMEOUT_MS) {
            Serial.println("!!! SECURITE: Perte connexion controleur !!!");
            Serial.println("!!! Arret automatique du chauffage !!!");
            setRelay(false);
            controllerConnected = false;
        }
    }
    
    delay(100);
}
