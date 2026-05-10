#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  Serial.print("Ricevuto pacchetto ESP-NOW da ");
  Serial.print(len); Serial.println(" bytes.");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("Errore ESP-NOW"); return; }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("📡 SERVER ESP-NOW: In ascolto...");
}

void loop() { 
  delay(1000); 
}