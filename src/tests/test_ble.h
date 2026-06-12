#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUID standard per test UART (trasmissione dati)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;

// Gestione delle connessioni Bluetooth
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("📱 Dispositivo Bluetooth Connesso!");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("📱 Dispositivo Disconnesso.");
      // Ricomincia a trasmettere la sua presenza
      BLEDevice::startAdvertising();
    }
};

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n--- TEST BLUETOOTH LE (BLE) ---");

  // Inizializza il BLE
  BLEDevice::init("Sensore_Hotel_BLE"); // Questo è il nome che vedrai sul telefono
  
  // Crea il Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Crea il Servizio
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Crea la Caratteristica (Il "tubo" dove passano i dati)
  pTxCharacteristic = pService->createCharacteristic(
										CHARACTERISTIC_UUID_TX,
										BLECharacteristic::PROPERTY_NOTIFY
									);
  pTxCharacteristic->addDescriptor(new BLE2902());

  // Avvia il servizio e inizia ad annunciare la presenza (Advertising)
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("📡 BLE Attivo! Apri l'app sul telefono e cerca 'Sensore_Hotel_BLE'");
}

void loop() {
  // Trasmette i dati SOLO se qualcuno è connesso
  if (deviceConnected) {
    String jsonPayload = "{\"occ\": true, \"t\":" + String(millis()) + "}";
    
    pTxCharacteristic->setValue(jsonPayload.c_str());
    pTxCharacteristic->notify(); // Spinge il dato al telefono
    
    Serial.print("Inviato via BLE: ");
    Serial.println(jsonPayload);
    
    delay(2000);
  }
}