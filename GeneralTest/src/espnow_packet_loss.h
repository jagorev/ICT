#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ==========================================
// CONFIGURAZIONE RUOLO E MAC ADDRESS
// ==========================================
// #define ROLE_IS_A

#ifdef ROLE_IS_A
  // MAC della Scheda B (Ricevitore)
  uint8_t peerAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; 
#else
  // MAC della Scheda A (Trasmittente)
  uint8_t peerAddress[] = {0x2C, 0xBC, 0xBB, 0x93, 0x1E, 0x38}; 
#endif

// ==========================================
// VARIABILI PER LO STRESS TEST
// ==========================================
const int TOTAL_PACKETS = 20000;
int packetsAttempted = 0;

// Variabili 'volatile' perché modificate dentro un Interrupt (il Callback)
volatile int successfulDeliveries = 0;
volatile int failedDeliveries = 0;

bool testCompleted = false;
unsigned long startTime = 0;

// Payload pesante da ~200 byte per simulare un JSON complesso
typedef struct struct_message {
  uint32_t counter;
  uint8_t dummy_data[200]; 
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// ==========================================
// CALLBACKS
// ==========================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    successfulDeliveries++;
  } else {
    failedDeliveries++;
  }
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Il ricevitore sta zitto per non sprecare risorse durante il bombardamento
}

// ==========================================
// SETUP E LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(3000); 
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("Errore init ESP-NOW"); return; }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){ Serial.println("Errore aggiunta Peer"); return; }

  Serial.println("\n=========================================");
#ifdef ROLE_IS_A
  Serial.println("  BENCHMARK 2: STRESS TEST (Trasmittente)");
  Serial.print("  Inizio invio di "); Serial.print(TOTAL_PACKETS); Serial.println(" pacchetti...");
  startTime = millis();
#else
  Serial.println("  BENCHMARK 2: RICEVITORE IN ATTESA      ");
  Serial.println("  (Monitora i risultati dalla Scheda A)  ");
#endif
  Serial.println("=========================================\n");
}

void loop() {
#ifdef ROLE_IS_A
  if (!testCompleted) {
    
    // 1. Fase di invio: Spara finché non raggiunge 20000 tentativi
    if (packetsAttempted < TOTAL_PACKETS) {
      myData.counter = packetsAttempted;
      
      esp_err_t result = esp_now_send(peerAddress, (uint8_t *) &myData, sizeof(myData));
      
      if (result == ESP_OK) {
        packetsAttempted++;
      }
      
      // Delay minuscolo per non far collassare il buffer TX dell'hardware
      delayMicroseconds(50); 
      
    } 
    // 2. Fase di calcolo: Aspetta che tutti i callback siano tornati
    else if ((successfulDeliveries + failedDeliveries) == TOTAL_PACKETS) {
      testCompleted = true;
      unsigned long totalTime = millis() - startTime;
      
      // Calcolo percentuale Packet Loss
      float packetLoss = ((float)failedDeliveries / TOTAL_PACKETS) * 100.0;
      float successRate = ((float)successfulDeliveries / TOTAL_PACKETS) * 100.0;
      
      // Stampa il report in stile Dashboard
      Serial.println("\n📊 === RISULTATI FINALI STRESS TEST ===");
      Serial.print("Tempo totale esecuzione: \t"); Serial.print(totalTime); Serial.println(" ms");
      Serial.print("Pacchetti inviati:       \t"); Serial.println(TOTAL_PACKETS);
      Serial.print("✅ Consegne Riuscite:    \t"); Serial.print(successfulDeliveries); Serial.print(" ("); Serial.print(successRate); Serial.println("%)");
      Serial.print("❌ Consegne Fallite:     \t"); Serial.print(failedDeliveries); Serial.print(" ("); Serial.print(packetLoss); Serial.println("%)");
      Serial.println("=======================================\n");
      
      if (packetLoss == 0.0) {
        Serial.println("🏆 PERFEZIONE! Rete super stabile. Nessun dato perso.");
      } else if (packetLoss < 5.0) {
        Serial.println("👍 OTTIMO. Perdita minima, gestibile facilmente dall'architettura.");
      } else {
        Serial.println("⚠️ ATTENZIONE. Alta perdita di pacchetti. Controlla le interferenze.");
      }
    }
  }
#else
  // Il Ruolo B non fa nulla nel loop, il suo modulo radio lavora da solo in background
  delay(1000);
#endif
}