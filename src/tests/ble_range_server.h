#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_RX_UUID "82482386-302a-4318-be13-d3a3bb0a7267"
#define CHAR_TX_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { Serial.println("📱 Client BLE Connesso!"); };
    void onDisconnect(BLEServer* pServer) { Serial.println("📱 Client Disconnesso. Annuncio..."); BLEDevice::startAdvertising(); }
};

class MyRxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      pTxCharacteristic->setValue("PONG");
      pTxCharacteristic->notify();
    }
};

void setup() {
  Serial.begin(115200);
  delay(2000);
  BLEDevice::init("Hotel_BLE_Bench");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyRxCallbacks());
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("📡 SERVER BLE: Attivo e in ascolto...");
}

void loop() { 
  delay(1000); 
}