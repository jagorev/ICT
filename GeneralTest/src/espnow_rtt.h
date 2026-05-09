#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ==========================================
// CONFIGURAZIONE RUOLO E MAC ADDRESS
// ==========================================
// #define ROLE_IS_A

#ifdef ROLE_IS_A
  uint8_t peerAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; // MAC Scheda B
#else
  uint8_t peerAddress[] = {0x2C, 0xBC, 0xBB, 0x93, 0x1E, 0x38}; // MAC Scheda A
#endif

// ==========================================
// VARIABILI GLOBALI PER IL BENCHMARK
// ==========================================
typedef struct struct_message {
  uint32_t counter;
  uint8_t dummy_data[240]; // Riempiamo il payload vicino al limite di ESP-NOW (250 byte) per un test realistico
} struct_message;

struct_message myData;
uint32_t msgCount = 0;

unsigned long startTime;
unsigned long totalRTT = 0;
uint32_t successfulSends = 0;

esp_now_peer_info_t peerInfo;

// ==========================================
// CALLBACKS
// ==========================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  unsigned long endTime = micros(); // Usa i microsecondi!
  
  if (status == ESP_NOW_SEND_SUCCESS) {
    unsigned long rtt = endTime - startTime;
    totalRTT += rtt;
    successfulSends++;
    
    Serial.print("Pacchetto "); Serial.print(myData.counter);
    Serial.print(" | Consegna: ✅ | Latenza: ");
    Serial.print(rtt); Serial.println(" microsecondi");
  } else {
    Serial.print("Pacchetto "); Serial.print(myData.counter);
    Serial.println(" | Consegna: ❌ FALLITA");
  }
}

// Nel test di latenza puro, la ricezione non ci interessa particolarmente, ma la teniamo per completezza
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Ignoriamo la stampa per non rallentare l'esecuzione
}

// ==========================================
// SETUP E LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(3000); 
  
  Serial.println("\n=========================================");
  Serial.println("  BENCHMARK 1: LATENZA ESP-NOW (RTT)   ");
  Serial.println("=========================================");
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("Errore init ESP-NOW"); return; }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){ Serial.println("Errore aggiunta Peer"); return; }
}

void loop() {
  #ifdef ROLE_IS_A
    // Solo il Ruolo A "spara" per misurare la latenza
    myData.counter = msgCount++;
    
    // Riempiamo i dummy data per simulare un JSON corposo
    memset(myData.dummy_data, 0xAA, sizeof(myData.dummy_data));
    
    startTime = micros(); // Facciamo partire il cronometro
    esp_now_send(peerAddress, (uint8_t *) &myData, sizeof(myData));
    
    // Calcoliamo la media ogni 100 pacchetti
    if (msgCount % 100 == 0 && successfulSends > 0) {
      Serial.print("\n---> STATISTICHE: Latenza Media su ");
      Serial.print(successfulSends);
      Serial.print(" pacchetti: ");
      Serial.print(totalRTT / successfulSends);
      Serial.println(" microsecondi <---\n");
    }
    
    delay(500); // Mezzo secondo di pausa per non inondare il buffer
  #else
    // Il Ruolo B sta in silenzio e riceve (l'hardware gestisce le conferme)
    delay(1000);
  #endif
}