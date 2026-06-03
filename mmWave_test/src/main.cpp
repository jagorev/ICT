#include <Arduino.h>

#define RX_PIN 16
#define TX_PIN 17

HardwareSerial RadarSerial(1);

// Il comando che proviamo a inviare per attivare i dati complessi
const byte REPORT_MODE_CMD[] = {
  0xFD, 0xFC, 0xFB, 0xFA, 
  0x08, 0x00, 
  0x12, 0x00, 0x00, 0x00, 
  0x04, 0x00, 0x00, 0x00, 
  0x04, 0x03, 0x02, 0x01
};

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("--- Sniffer Dati Radar Avviato ---");

  RadarSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  // Proviamo a inviare nuovamente il comando
  Serial.println("Invio comando per Modalità Report...");
  RadarSerial.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);
  
  Serial.println("In attesa di risposta dal sensore...");
  Serial.println("====================================");
}

void loop() {
  // Se c'è almeno un byte in arrivo dal radar...
  if (RadarSerial.available()) {
    String hexString = "";
    String asciiString = "";
    
    // Leggi tutto il "blocco" di dati arrivato
    while (RadarSerial.available()) {
      byte b = RadarSerial.read();
      
      // Crea la versione Esadecimale (es. 4F per 'O')
      if (b < 0x10) hexString += "0";
      hexString += String(b, HEX) + " ";
      
      // Crea la versione Testo, scartando i caratteri non stampabili
      if (b >= 32 && b <= 126) {
        asciiString += (char)b;
      } else {
        asciiString += "."; // Sostituisce gli "a capo" o byte strani con un punto
      }
      
      // Breve pausa per permettere al buffer hardware di riempirsi col byte successivo
      delay(2); 
    }
    
    // Stampa il risultato
    Serial.print("HEX:   ");
    Serial.println(hexString);
    Serial.print("ASCII: ");
    Serial.println(asciiString);
    Serial.println("-------------------------");
  }
}