#ifndef BLE_ROOM_SCANNER_H
#define BLE_ROOM_SCANNER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define JANITOR_TAG_NAME "JANITOR_TAG"

int scanTime = 5; // Tempo di scansione in secondi
BLEScan* pBLEScan;
bool janitor_present = false;
int janitor_rssi = -100;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // Controlla se il device trovato ha un nome e se corrisponde al tag del janitor
        if (advertisedDevice.haveName() && advertisedDevice.getName() == JANITOR_TAG_NAME) {
            janitor_present = true;
            janitor_rssi = advertisedDevice.getRSSI(); // Per capire quanto è vicino
            Serial.printf("Janitor Rilevato! RSSI: %d \n", janitor_rssi);
        }
    }
};

void setup_room_scanner() {
    Serial.begin(115200);
    Serial.println("Inizializzando il BLE Room Scanner...");

    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan(); // Crea lo scanner
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); // Scansione attiva consuma più energia ma è più rapida
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
}

void loop_room_scanner() {
    Serial.println("\nScansione BLE in corso...");
    janitor_present = false; // Resettiamo la variabile ad ogni ciclo di scansione
    
    // Avvia la scansione e blocca per 'scanTime' secondi
    BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
    
    if (janitor_present) {
        Serial.println("--> STATO: Il Janitor è PRESENTE nella stanza.");
    } else {
        Serial.println("--> STATO: Nessun Janitor rilevato.");
    }
    
    // Libera la memoria allocata per i risultati della scansione
    pBLEScan->clearResults();   
    
    // Pausa prima della prossima scansione
    delay(2000);
}

void setup() {
    setup_room_scanner();
}

void loop() {
    loop_room_scanner();
}

#endif
