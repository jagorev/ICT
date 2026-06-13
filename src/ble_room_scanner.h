#ifndef BLE_ROOM_SCANNER_H
#define BLE_ROOM_SCANNER_H

#include <Arduino.h>
#include <NimBLEDevice.h>

// The exact same name the Tag advertises
#define JANITOR_DEVICE_NAME "JANITOR"
#define JANITOR_SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"
#define JANITOR_TIMEOUT_MS 10000
#define BLE_SCAN_DURATION 3
#define BLE_SCAN_INTERVAL_MS 5000

NimBLEScan* pBLEScan;
bool janitor_present = false;
int janitor_rssi = -100;
unsigned long last_janitor_seen = 0;
unsigned long lastScanTime = 0;

void processScanResults() {
    NimBLEScanResults results = pBLEScan->getResults();

    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        String name = String(device.getName().c_str());

        bool matchedByUUID = device.haveServiceUUID()
            && device.isAdvertisingService(NimBLEUUID(JANITOR_SERVICE_UUID));
        bool matchedByName = name.length() > 0 && name == JANITOR_DEVICE_NAME;

        if (matchedByUUID || matchedByName) {
            last_janitor_seen = millis();
            janitor_rssi = device.getRSSI();
            if (!janitor_present) {
                janitor_present = true;
                Serial.printf("[BLE] Janitor Detected! RSSI: %d\n", janitor_rssi);
            }
        }
    }

    pBLEScan->clearResults();
}

void setup_room_scanner() {
    Serial.println("Initializing BLE Room Scanner (Non-Blocking)...");
    NimBLEDevice::init("");
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(160);
    pBLEScan->setWindow(120);
}

void loop_room_scanner() {
    // Start a new scan if enough time has passed and no scan is running
    if (!pBLEScan->isScanning() && (millis() - lastScanTime > BLE_SCAN_INTERVAL_MS)) {
        lastScanTime = millis();
        pBLEScan->start(BLE_SCAN_DURATION, nullptr, false);
    }

    // If a scan just finished, process the results
    if (!pBLEScan->isScanning() && lastScanTime > 0 && (millis() - lastScanTime > (BLE_SCAN_DURATION * 1000))) {
        processScanResults();
    }

    // Check if the Janitor walked out of the room (Timeout)
    if (janitor_present && (millis() - last_janitor_seen > JANITOR_TIMEOUT_MS)) {
        janitor_present = false;
        Serial.println("[BLE] Janitor Timeout. Room returning to normal state.");
    }
}

#endif