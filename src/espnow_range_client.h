#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

const int PIR_PIN = 4;        // Pin del segnale (OUT) del SimplyTronics
const int EN_PIN = 5;         // Pin di Enable (EN) del SimplyTronics
const int RGB_PIN = 48;       // Pin standard del LED RGB integrato sulla ESP32-S3

// Inizializziamo il LED RGB (1 solo LED, collegato al pin RGB_PIN)
Adafruit_NeoPixel rgbLed(1, RGB_PIN, NEO_GRB + NEO_KHZ800);

int lastState = LOW;

// Funzione comoda per cambiare colore al volo (Rosso, Verde, Blu)
void setLedColor(int r, int g, int b) {
  rgbLed.setPixelColor(0, rgbLed.Color(r, g, b));
  rgbLed.show();
}

void setup() {
  Serial.begin(115200);
  delay(2000); 
  
  Serial.println("\n=============================================");
  Serial.println("  TEST PORTATA - SIMPLYTRONICS ST-00081 (S3)");
  Serial.println("=============================================");

  // Inizializziamo il LED RGB e lo mettiamo al 100% di luminosità (per vederlo da lontano)
  rgbLed.begin();
  rgbLed.setBrightness(255); 
  setLedColor(0, 0, 0); // Spento

  // Abilitiamo il sensore PIR
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  // Calibrazione con feedback visivo GIALLO
  Serial.println("Calibrazione: ALLONTANATI! (10 secondi)");
  for(int i = 10; i > 0; i--) {
    Serial.print(i); Serial.print("... ");
    setLedColor(255, 200, 0); // Giallo acceso
    delay(500);
    setLedColor(0, 0, 0);     // Giallo spento (effetto lampeggio)
    delay(500);
  }

  Serial.print("\nAttendo che l'infrarosso si stabilizzi (LOW)...");
  while(digitalRead(PIR_PIN) == HIGH) {
    setLedColor(255, 200, 0); // Mantieni giallo se sta ancora leggendo residui
    delay(100);
    Serial.print("."); 
  }
  
  Serial.println("\n✅ Sensore ST-00081 PRONTO!");
  Serial.println("ISTRUZIONI: LED ROSSO = Nessun Movimento. LED VERDE = Ti vede.");
  
  // Impostiamo il LED su ROSSO (Pronto, ma nessun movimento)
  setLedColor(255, 0, 0); 
}

void loop() {
  int currentState = digitalRead(PIR_PIN);

  if (currentState != lastState) {
    // Antirimbalzo software per segnali spuri
    delay(50);
    currentState = digitalRead(PIR_PIN);
    
    if (currentState == HIGH) {
      setLedColor(0, 255, 0); // VERDE: Ti ha visto!
      Serial.println("🟢 MOVIMENTO RILEVATO! (Verde)");
    } 
    else if (currentState == LOW) {
      setLedColor(255, 0, 0); // ROSSO: Non ti vede più
      Serial.println("🔴 NESSUN MOVIMENTO. (Rosso)");
      Serial.println("- - - - - - - - - - - - - - - - - - - - - - -");
    }
    
    lastState = currentState;
  }
}