#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Identificativi univoci (UUID) del tunnel Bluetooth
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_RX_UUID           "82482386-302a-4318-be13-d3a3bb0a7267" // Riceve dal Client
#define CHAR_TX_UUID           "beb5483e-36e1-4688-b7f5-ea07361b26a8" // Invia al Client (Notify)

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
bool deviceConnected = false;

// Variabili per lo Stress Test
bool startStressTest = false;
const int TOTAL_PACKETS = 20000;

// ==========================================
// CALLBACKS (Gestione connessione e ricezione dati)
// ==========================================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("📱 Client Connesso al Tunnel BLE!");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("📱 Client Disconnesso. Riparto ad annunciare...");
      BLEDevice::startAdvertising();
    }
};

class MyRxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      String rxValue = pChar->getValue().c_str();
      
      if (rxValue.length() > 0) {
        // Se il Client chiede il Test 1 (Latenza), facciamo l'ECHO istantaneo
        if (rxValue.indexOf("PING") != -1) {
          pTxCharacteristic->setValue(rxValue.c_str());
          pTxCharacteristic->notify(); // Rispedisce indietro il pacchetto
        }
        // Se il Client chiede il Test 2 (Stress), attiviamo la mitragliatrice
        else if (rxValue.indexOf("STRESS") != -1) {
          Serial.println("\n🔥 Richiesto STRESS TEST! Inizio invio di 20000 pacchetti...");
          startStressTest = true;
        }
      }
    }
};

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n=========================================");
  Serial.println("     SERVER BLE (In attesa di Test)      ");
  Serial.println("=========================================");

  BLEDevice::init("Hotel_BLE_Bench");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Creiamo il "Tubo" per mandare i dati al Client
  pTxCharacteristic = pService->createCharacteristic(CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());

  // Creiamo il "Tubo" per ricevere i comandi dal Client
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyRxCallbacks());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("📡 Server BLE Attivo e in ascolto...\n");
}

// ==========================================
// LOOP (Gestisce solo lo Stress Test)
// ==========================================
void loop() {
  if (deviceConnected && startStressTest) {
    int packetsSent = 0;
    
    for (int i = 0; i < TOTAL_PACKETS; i++) {
      String payload = "StressData_" + String(i);
      pTxCharacteristic->setValue(payload.c_str());
      pTxCharacteristic->notify();
      packetsSent++;
      delay(5); // Pausa fisica necessaria per non far crashare lo stack Bluetooth
    }
    
    Serial.print("✅ Finito. Inviati "); Serial.print(packetsSent); Serial.println(" pacchetti al Client.");
    startStressTest = false; // Ferma il test
  }
}