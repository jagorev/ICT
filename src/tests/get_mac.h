#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(3000); // Il solito ritardo per dare tempo alla porta USB di avviarsi
  
  Serial.println("\n=========================================");
  Serial.println("        LETTURA MAC ADDRESS ESP32        ");
  Serial.println("=========================================\n");

  // Per leggere il MAC, dobbiamo accendere il modulo Wi-Fi in modalità Station
  WiFi.mode(WIFI_STA);

  // ---------------------------------------------------------
  // 1. FORMATO STANDARD (Stringa per Router e App)
  // ---------------------------------------------------------
  Serial.print("Formato Standard:\t");
  Serial.println(WiFi.macAddress());

  // ---------------------------------------------------------
  // 2. FORMATO ARRAY BYTE (Per ESP-NOW e codice C++)
  // ---------------------------------------------------------
  uint8_t mac[6];
  WiFi.macAddress(mac); // Questa funzione riempie l'array con i 6 byte puri

  Serial.print("Formato Array (C++):\t{");
  for (int i = 0; i < 6; i++) {
    // printf("0x%02X") formatta il numero in esadecimale (X) 
    // a 2 cifre (02), aggiungendo "0x" davanti
    Serial.printf("0x%02X", mac[i]); 
    
    // Aggiunge la virgola per tutti tranne che per l'ultimo
    if (i < 5) {
      Serial.print(", ");
    }
  }
  Serial.println("}");
  
  Serial.println("\n=========================================");
}

void loop() {
  // Non ci serve nulla nel loop, stampiamo una volta sola all'avvio
  delay(10000);
}