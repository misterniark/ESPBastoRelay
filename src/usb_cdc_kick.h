/**
 * @file usb_cdc_kick.h
 * @brief Contournement de l'échouage TX de l'USB-Serial/JTAG
 *
 * Copie conforme du fichier du projet contrôleur (EspBastoClaude) :
 * le XIAO ESP32-C3 expose lui aussi l'USB-Serial/JTAG natif et souffre
 * du même défaut. Constaté au banc le 27/07/2026 : l'accusé de
 * réception d'une commande de test est arrivé 53 SECONDES après son
 * envoi, ce qui a fait échouer des vérifications alors que le firmware
 * se comportait correctement.
 *
 * PROBLÈME (constaté au banc série du 27/07/2026, CrowPanel ESP32-S3) :
 * les lignes série (réponses [TCLI], logs [POWER]...) arrivaient parfois
 * à l'hôte avec 1 à 6 secondes de retard, collées à la ligne suivante.
 * L'instrumentation embarquée ([LOOPTIME] + horodatage t= dans les
 * réponses status) a prouvé que la boucle principale ne gèle JAMAIS :
 * le retard se produit dans le transport USB, sens device → hôte.
 *
 * CAUSE RACINE (documentée dans hal/usb_serial_jtag_ll.h d'ESP-IDF) :
 * la FIFO TX de l'USB-Serial/JTAG fait 64 octets. Quand un envoi se
 * termine par un paquet de exactement 64 octets, le matériel l'émet
 * automatiquement, mais un paquet USB « plein » signifie « transfert
 * incomplet » pour le protocole bulk : le driver CDC-ACM de l'hôte
 * (macOS notamment) retient alors les données SANS les livrer à
 * l'application, en attendant un paquet court ou un ZLP (zero-length
 * packet) de terminaison. Si le firmware n'écrit plus rien — système au
 * repos, écran en veille — la ligne reste invisible jusqu'à l'écriture
 * série suivante, d'où les « gels » apparents de plusieurs secondes.
 * Le driver HWCDC du core Arduino 2.0.17 n'émet pas ce ZLP de lui-même.
 *
 * REMÈDE (prescrit par la note de usb_serial_jtag_ll_txfifo_flush()) :
 * rappeler périodiquement la fonction de flush quand la FIFO est libre :
 * cela émet un ZLP qui termine toute transaction laissée ouverte. Appelé
 * à chaque tour de loop(), le retard résiduel maximal est d'un tour
 * (~5-60 ms) au lieu de « jusqu'au prochain print ».
 *
 * INNOCUITÉ : un ZLP hors transaction est ignoré par l'hôte ; le flush
 * depuis le contexte applicatif peut au pire scinder un envoi en cours
 * de l'ISR en deux paquets courts — le CDC est un flux d'octets, l'ordre
 * et l'intégrité sont préservés. Coût : une lecture + une écriture de
 * registre par tour de boucle.
 *
 * PORTÉE : uniquement les cibles où la console passe par l'USB-Serial/
 * JTAG natif (CrowPanel ESP32-S3, drapeau ARDUINO_USB_CDC_ON_BOOT).
 * Sur le CYD (UART classique), la fonction est un no-op vide.
 */

#ifndef USB_CDC_KICK_H
#define USB_CDC_KICK_H

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT

#include "soc/soc_caps.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED

#include "hal/usb_serial_jtag_ll.h"

/**
 * Termine par un ZLP toute transaction USB laissée ouverte par un
 * paquet de 64 octets pile. À appeler à chaque tour de loop().
 */
static inline void usb_cdc_kick()
{
    /* Ne flusher que si la FIFO est disponible en écriture : si elle
     * est pleine (envoi en cours), le matériel la videra lui-même et
     * l'ISR de HWCDC enchaînera — inutile d'interférer. */
    if (usb_serial_jtag_ll_txfifo_writable()) {
        usb_serial_jtag_ll_txfifo_flush();
    }
}

#else /* SoC sans USB-Serial/JTAG : rien à faire */
static inline void usb_cdc_kick() {}
#endif

#else /* Console sur UART classique (CYD) : rien à faire */
static inline void usb_cdc_kick() {}
#endif

#endif /* USB_CDC_KICK_H */
