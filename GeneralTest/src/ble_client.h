#include <Arduino.h>
#include <BLEDevice.h>

// ==========================================
// SELETTORE DEL TEST (Imposta 1 o 2)
// 1 = Test Latenza (RTT)
// 2 = Stress Test (Packet Loss)
// ==========================================
#define TEST_TYPE 2  // <--- CAMBIA QUESTO NUMERO

static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charTxUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8"); // Dove ascoltiamo
static BLEUUID charRxUUID("82482386-302a-4318-be13-d3a3bb0a7267"); // Dove scriviamo

static BLERemoteCharacteristic* pRemoteTx;
static BLERemoteCharacteristic* pRemoteRx;
BLEClient* pClient;

bool connected = false;
unsigned long startTime = 0;
int msgCount = 0;

// Variabili Test 1
unsigned long totalRTT = 0;
bool waitingForEcho = false;

// Variabili Test 2
const int TOTAL_PACKETS = 20000;
volatile int echoesReceived = 0;
bool stressTestRequested = false;
bool stressTestCompleted = false;

// ==========================================
// CALLBACK (Quando il Server ci risponde)
// ==========================================
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (TEST_TYPE == 1) {
    unsigned long rtt = millis() - startTime;
    totalRTT += rtt;
    msgCount++;
    waitingForEcho = false;

    Serial.print("Pacchetto "); Serial.print(msgCount);
    Serial.print(" | Echo RICEVUTO ✅ | Latenza: ");
    Serial.print(rtt); Serial.println(" ms");

    if (msgCount % 50 == 0) {
      Serial.print("\n---> STATISTICHE LATENZA BLE: Media su ");
      Serial.print(msgCount); Serial.print(" pkg: ");
      Serial.print(totalRTT / msgCount); Serial.println(" ms <---\n");
    }
  } 
  else if (TEST_TYPE == 2) {
    echoesReceived++;
    // Aggiorna il cronometro per capire quando il server smette di sparare
    startTime = millis(); 
  }
}

// ==========================================
// SCANSIONE E CONNESSIONE
// ==========================================
BLEAdvertisedDevice* myDevice;
bool doConnect = false;

// Callback per la scansione BLE
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Cerchiamo il nome del dispositivo invece del Service UUID
    if (advertisedDevice.getName() == "Hotel_BLE_Bench") {
      Serial.print("Trovato Server BLE compatibile: ");
      Serial.println(advertisedDevice.toString().c_str());
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

bool connectToServer() {
  Serial.print("Creazione Client...");
  pClient = BLEDevice::createClient();
  Serial.println(" Fatto.");

  Serial.print("Connessione al Server: ");
  Serial.println(myDevice->getAddress().toString().c_str());
  
  // Passiamo l'AdvertisedDevice trovato invece del nome
  if (!pClient->connect(myDevice)) return false;

  Serial.println(" ✅ Connesso!");

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) { Serial.println("❌ Servizio non trovato"); return false; }

  pRemoteTx = pRemoteService->getCharacteristic(charTxUUID);
  pRemoteRx = pRemoteService->getCharacteristic(charRxUUID);
  if (pRemoteTx == nullptr || pRemoteRx == nullptr) return false;

  pRemoteTx->registerForNotify(notifyCallback);
  return true;
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n=========================================");
  Serial.println("  CLIENT BLE (Esecutore Test Benchmark)  ");
  if (TEST_TYPE == 1) Serial.println("  MODALITÀ: TEST 1 (LATENZA RTT)");
  else Serial.println("  MODALITÀ: TEST 2 (STRESS TEST)");
  Serial.println("=========================================");

  BLEDevice::init("");
  
  // Avvia la scansione BLE
  Serial.println("Avvio scansione BLE continua...");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);

  // Scansiona a blocchi di 5 secondi finchè non lo troviamo
  while (!doConnect) {
    Serial.println("Scansione in corso...");
    pBLEScan->start(5, false);
    // Aspettiamo che i 5 secondi finiscano (più un piccolo buffer)
    delay(5500); 
    pBLEScan->clearResults(); // Pulisce i risultati prima della prossima passata
  }
}

// ==========================================
// LOOP E LOGICA DEI TEST
// ==========================================
void loop() {
  // Se la scansione ha trovato il dispositivo (e lo abbiamo trovato nel setup), proviamo a connettere
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("📡 Tunnel Pronto. Inizio test...\n");
      connected = true;
    } else {
      Serial.println("❌ Connessione fallita.");
    }
    doConnect = false; // per non riprovare in loop continuo se fallisce
  }
  
  if (!connected) return;

  if (TEST_TYPE == 1) {
    // TEST 1: LATENZA
    if (!waitingForEcho) {
      delay(200);
      String payload = "PING_" + String(msgCount);
      startTime = millis();
      waitingForEcho = true;
      pRemoteRx->writeValue(payload.c_str(), payload.length());
    }
  } 
  else if (TEST_TYPE == 2) {
    // TEST 2: STRESS
    if (!stressTestRequested) {
      Serial.println("Inviando comando di INIZIO STRESS TEST al Server...");
      String payload = "STRESS";
      pRemoteRx->writeValue(payload.c_str(), payload.length());
      stressTestRequested = true;
      startTime = millis();
    } 
    else if (!stressTestCompleted) {
      // Se sono passati 5 secondi dall'ultimo pacchetto ricevuto, dichiariamo chiuso il test
      if (millis() - startTime > 5000 && echoesReceived > 0) {
        stressTestCompleted = true;
        
        float lossRate = 100.0 - (((float)echoesReceived / TOTAL_PACKETS) * 100.0);
        Serial.println("\n📊 === RISULTATI STRESS TEST BLE ===");
        Serial.print("Attesi dal Server: \t"); Serial.println(TOTAL_PACKETS);
        Serial.print("Ricevuti (RX):     \t"); Serial.println(echoesReceived);
        Serial.print("📉 PACKET LOSS:    \t"); Serial.print(lossRate); Serial.println("%");
        Serial.println("====================================\n");
      }
    }
  }
}