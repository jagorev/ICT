#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ==========================================
// 1. CONFIGURAZIONE RUOLI
// ==========================================
// Scommenta questa riga per la prima scheda (Scheda A).
// Commentala (// #define ROLE_IS_A) per la seconda scheda (Scheda B).
#define ROLE_IS_A

#ifdef ROLE_IS_A
  // Se IO sono la Scheda A, invio alla Scheda B.
  // Metti qui l'indirizzo MAC della tua SECONDA ESP32
  uint8_t peerAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; 
#else
  // Se IO sono la Scheda B, invio alla Scheda A.
  // Metti qui l'indirizzo MAC della tua PRIMA ESP32
  uint8_t peerAddress[] = {0x2C, 0xBC, 0xBB, 0x93, 0x1E, 0x38}; 
#endif

// ==========================================
// 2. STRUTTURA DATI
// ==========================================
typedef struct struct_message {
  char text[32];
  int counter;
} struct_message;

struct_message myData;
int msgCount = 0;

esp_now_peer_info_t peerInfo;

// ==========================================
// 3. CALLBACKS
// ==========================================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Evitiamo il ciclo for e il printf nel callback se possibile
  Serial.print("\r\n[INVIO] Stato consegna: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Riuscita" : "❌ Fallita");
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  struct_message incomingMessage;
  memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
  
  // Stampa solo i dati, non il MAC in esadecimale per evitare rallentamenti nel callback
  Serial.print("[RICEZIONE] Messaggio: '");
  Serial.print(incomingMessage.text);
  Serial.print("' | N. Pacchetto: ");
  Serial.println(incomingMessage.counter);
}

// ==========================================
// 4. SETUP E LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(3000); 
  
  Serial.println("\n=========================================");
#ifdef ROLE_IS_A
  Serial.println("   TEST ESP-NOW: RUOLO A (Invia a B)");
#else
  Serial.println("   TEST ESP-NOW: RUOLO B (Invia ad A)");
#endif
  Serial.println("=========================================");
  
  WiFi.mode(WIFI_STA);
  Serial.print("Il mio indirizzo MAC è: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Errore critico: Inizializzazione ESP-NOW fallita!");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Registra il Peer (l'altra scheda) usando il MAC unicast invece del broadcast
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;  // Assicurati che se forzi un canale, sia uguale su entrambe
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("❌ Errore nell'aggiunta del Peer");
    return;
  }
  Serial.println("ESP-NOW Pronto. Inizio comunicazioni...\n");
}

void loop() {
  // Prepariamo il messaggio differenziandolo in base al ruolo
#ifdef ROLE_IS_A
  strcpy(myData.text, "Ciao da ESP32 A!");
#else
  strcpy(myData.text, "Risposta da ESP32 B");
#endif

  myData.counter = msgCount++;
  
  // Spediamo il messaggio al MAC unicast
  esp_err_t result = esp_now_send(peerAddress, (uint8_t *) &myData, sizeof(myData));
  
  // Aspettiamo 3 secondi prima del prossimo invio (da entrambe le parti)
  delay(3000);
}