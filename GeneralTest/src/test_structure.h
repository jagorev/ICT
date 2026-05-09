#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(3000); // Il solito ritardo per la porta USB della tua ESP32-S3
  
  Serial.println("\n=========================================");
  Serial.println("   ✅ TEST STRUTTURA: SUCCESSO! ✅       ");
  Serial.println("=========================================");
  Serial.println("Il trucco del file .h funziona perfettamente.");
}

void loop() {
  Serial.println("Sto girando all'interno del file test_struttura.h...");
  delay(2000);
}