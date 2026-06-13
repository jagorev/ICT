#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>

#define JANITOR_SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"

void setup() {
    Serial.begin(115200);
    delay(2000); 
    Serial.println("\n--- Starting BLE Janitor Tag ---");
    
    BLEDevice::init("JANITOR");
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(JANITOR_SERVICE_UUID);
    
    BLEDevice::startAdvertising();

    Serial.println("Advertising started!");
    Serial.print("MAC: ");
    Serial.println(BLEDevice::getAddress().toString().c_str());
}

void loop() {
    delay(2000);
}