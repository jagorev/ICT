#include <Arduino.h>

const int PIR_PIN = 4;        // Pin del segnale (OUT) del SimplyTronics
const int EN_PIN = 5;         // Pin di Enable (EN) del SimplyTronics
      // <--- IL NOSTRO NUOVO LED ESTERNO

int lastState = LOW;

void setup() {
  Serial.begin(115200);
  delay(2000); 
  
  Serial.println("\n=============================================");
  Serial.println("  TEST PORTATA - SIMPLYTRONICS ST-00081");
  Serial.println("=============================================");

  // 1. Abilitiamo il sensore
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);
  
  // 2. Prepariamo il LED esterno
  pinMode(RGB_BUILTIN, OUTPUT);
  digitalWrite(RGB_BUILTIN, LOW); // Spento di default

  // 3. Prepariamo il pin di lettura
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  // 4. Calibrazione
  Serial.println("Calibrazione in corso: ALLONTANATI DAL SENSORE! (10 secondi)");
  for(int i = 10; i > 0; i--) {
    Serial.print(i); Serial.print("... ");
    delay(1000);
  }

  Serial.print("\nAttendo che l'infrarosso si stabilizzi (segnale LOW)");
  while(digitalRead(PIR_PIN) == HIGH) {
    delay(100);
    Serial.print("."); 
  }
  
  Serial.println("\n✅ Sensore ST-00081 PRONTO!");
  Serial.println("ISTRUZIONI: Cammina. Se il LED si accende, sei nel range.");
}

void loop() {
  int currentState = digitalRead(PIR_PIN);

  if (currentState != lastState) {
    // Antirimbalzo software per evitare falsi segnali
    delay(50);
    currentState = digitalRead(PIR_PIN);
    
    if (currentState == HIGH) {
      digitalWrite(RGB_BUILTIN, HIGH); // ACCENDE IL LED SUL PIN 12
      Serial.println("🟢 MOVIMENTO RILEVATO! (Fermati e aspetta che si spenga)");
    } 
    else if (currentState == LOW) {
      digitalWrite(RGB_BUILTIN, LOW); // SPEGNE IL LED SUL PIN 12
      Serial.println("🔴 NESSUN MOVIMENTO. (Puoi fare un passo o spostarti lateralmente)");
      Serial.println("- - - - - - - - - - - - - - - - - - - - - - -");
    }
    
    lastState = currentState;
  }
}