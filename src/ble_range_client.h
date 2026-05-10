#include <Arduino.h>
#include <BLEDevice.h>

static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charTxUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
static BLEUUID charRxUUID("82482386-302a-4318-be13-d3a3bb0a7267");

static BLERemoteCharacteristic* pRemoteRx;
BLEClient* pClient;
bool connected = false;

static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  int rssi = pClient->getRssi(); 
  Serial.print("✅ BLE Ping Echo | Segnale BLE (RSSI): ");
  Serial.print(rssi); Serial.println(" dBm");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  BLEDevice::init("");
  pClient = BLEDevice::createClient();
  Serial.print("Cerco Server BLE...");
  while(!pClient->connect("Hotel_BLE_Bench")) { delay(1000); Serial.print("."); }
  Serial.println(" ✅");
  
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  pRemoteService->getCharacteristic(charTxUUID)->registerForNotify(notifyCallback);
  pRemoteRx = pRemoteService->getCharacteristic(charRxUUID);
  connected = true;
  Serial.println("🚶 CLIENT BLE: Inizio Test Range...");
}

void loop() {
  if (connected) {
    delay(500); 
    pRemoteRx->writeValue("PING", 4);
  }
}