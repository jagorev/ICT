const int PIR_PIN = 4; 
const int EN_PIN = 5;  

// Memoria per ricordarci com'era il sensore l'ultima volta che lo abbiamo guardato
int lastState = LOW;

void setup() {
  Serial.begin(115200);
  delay(3000); 
  
  Serial.println("\nAvvio Test SimplyTronics - Lettura Continua (Polling)");

  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);
  
  Serial.println("Calibrazione di 10 secondi...");
  for(int i = 10; i > 0; i--) {
    Serial.print(i); Serial.print("... ");
    delay(1000);
  }

  pinMode(PIR_PIN, INPUT_PULLDOWN);

  Serial.print("\nAttendo che il sensore si stabilizzi a LOW");
  while(digitalRead(PIR_PIN) == HIGH) {
    delay(100);
    Serial.print("."); 
  }
  Serial.println("\nSensore stabile e pronto!");
}

void loop() {
  // 1. Leggiamo lo stato attuale del sensore
  int currentState = digitalRead(PIR_PIN);

  // 2. È diverso da quello che ricordavamo?
  if (currentState != lastState) {
    
    // 3. Antirimbalzo Software: aspettiamo 50 millisecondi per far stabilizzare lo "scivolo" elettrico
    delay(50);
    
    // 4. Rileggiamo il pin per avere il dato definitivo e pulito
    currentState = digitalRead(PIR_PIN);
    
    // 5. Ora stampiamo l'azione corretta
    if (currentState == HIGH) {
      Serial.print("🟢 INIZIO MOVIMENTO (LED Rosso) - Timestamp: ");
      Serial.println(millis());
    } 
    else if (currentState == LOW) {
      Serial.print("🔴 FINE MOVIMENTO (LED Nero) - Timestamp: ");
      Serial.println(millis());
      Serial.println("-------------------------------------------------");
    }
    
    // 6. Aggiorniamo la nostra memoria per il prossimo giro
    lastState = currentState;
  }
}