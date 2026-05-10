#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

uint8_t serverAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; 
unsigned long startTime = 0;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  unsigned long rtt = millis() - startTime;
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.print("✅ ESP-NOW Ping | Consegna: OK | Latenza: "); Serial.print(rtt); Serial.println(" ms");
  } else {
    Serial.println("❌ ESP-NOW Ping | Consegna: FALLITA (Fuori Portata!)");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  WiFi.mode(WIFI_STA);
  if(esp_now_init() != ESP_OK) { Serial.println("Errore Init"); return; }
  esp_now_register_send_cb(OnDataSent);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, serverAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  Serial.println("🚶 CLIENT ESP-NOW: Inizio Test Range...");
}

void loop() {
  delay(500); 
  uint8_t dummy_data[240] = {0}; 
  startTime = millis();
  esp_now_send(serverAddress, dummy_data, sizeof(dummy_data));
}