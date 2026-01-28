/*
 * ============================================
 * RELAIS WEBASTO - ESP32-C3 Super Mini
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

// ============================================
// FONCTIONS RELAIS
// ============================================

void setRelay(bool on) {
    relayOn = on;
    
    if (RELAY_ACTIVE_HIGH) {
        pinMode(RELAY_PIN, OUTPUT);
        digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    } else {
        pinMode(RELAY_PIN, INPUT);
        digitalWrite(RELAY_PIN, on ? LOW : HIGH);
    }
    
    // LED de statut
    digitalWrite(LED_STATUS, on ? LOW : HIGH);  // LED active LOW
    
    Serial.print("Relais: ");
    Serial.println(on ? "ON" : "OFF");
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
            setRelay(true);
            sendResponse(ACK_ON);
            controllerConnected = true;
            lastPingReceived = millis();
            break;
            
        case CMD_HEAT_OFF:
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
    pinMode(LED_STATUS, OUTPUT);
    
    /* Test du relais au démarrage
    Serial.println("Test relais...");
    Serial.print("GPIO: ");
    Serial.println(RELAY_PIN);*/
    
    // ON
   /* digitalWrite(RELAY_PIN, HIGH);
    Serial.println("Relais HIGH");
    delay(1000);*/
    
    // OFF
   /* digitalWrite(RELAY_PIN, LOW);
    Serial.println("Relais LOW");
    delay(1000);*/

    
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
