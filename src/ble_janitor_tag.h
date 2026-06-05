#ifndef BLE_JANITOR_TAG_H
#define BLE_JANITOR_TAG_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Puoi personalizzare il nome con cui il tag viene pubblicizzato
#define JANITOR_TAG_NAME "JANITOR_TAG"

void setup_janitor_tag() {
    Serial.begin(115200);
    Serial.println("Inizializzando il BLE Janitor Tag...");

    // Inizializza il device BLE
    BLEDevice::init(JANITOR_TAG_NAME);
    
    // Crea il server BLE
    BLEServer *pServer = BLEDevice::createServer();
    
    // Configura l'advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    
    // Aiuta il rilevamento da parte degli scanner
    pAdvertising->setScanResponse(true);
    
    // Funzioni raccomandate da Apple/Android per i beacon
    pAdvertising->setMinPreferred(0x06);  
    pAdvertising->setMaxPreferred(0x12);
    
    // Avvia la pubblicizzazione del Tag
    BLEDevice::startAdvertising();
    Serial.println("Janitor Tag in advertising! Ora è visibile.");
}

void loop_janitor_tag() {
    // Essendo un semplice tag che fa advertising, non deve fare molto nel loop.
    // L'ESP32 continuerà a pubblicizzare la sua presenza in background.
    delay(2000);
}

#include <Arduino.h>
#include "ble_janitor_tag.h"

void setup() {
    setup_janitor_tag();
}

void loop() {
    loop_janitor_tag();
}

#endif
