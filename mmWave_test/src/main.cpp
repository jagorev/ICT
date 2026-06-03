/* #include <Arduino.h>

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
} */

#include <Arduino.h>

// ---------------------------------------------------------
// 1. HARDWARE PINS & SERIAL SETUP
// ---------------------------------------------------------
// NEVER use SoftwareSerial at 115200 baud on an ESP32!
// We use the ESP32's built-in HardwareSerial (UART 1) for perfect reliability.
#define RX_PIN 16
#define TX_PIN 17
HardwareSerial mmWave(1); 

// ---------------------------------------------------------
// 2. RADAR CONFIGURATION COMMANDS
// ---------------------------------------------------------
// We use the manufacturer's updated command from the demo.
// The "0x64" (100 in decimal) ensures the radar refreshes exactly every 100ms.
const byte REPORT_MODE_CMD[] = {
  0xFD, 0xFC, 0xFB, 0xFA, // Command Header
  0x08, 0x00,             // Length of payload
  0x12, 0x00, 0x00, 0x00, // Target register / Command type
  0x04, 0x00, 0x00, 0x00, // Data to write (0x64 = 100ms refresh rate  --> DOES NOT WORK, RADAR IGNORES THIS VALUE FOR SOME REASON LIKE TF)
  0x04, 0x03, 0x02, 0x01  // Command Footer
};

// ---------------------------------------------------------
// 3. STATE MACHINE VARIABLES
// ---------------------------------------------------------
const uint8_t HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
int headerCount = 0;

uint8_t packet[64];
int packetLen = 0;
bool readingPacket = false;

void setup() {
  // Start PC communication
  Serial.begin(115200);
  
  // Start Radar communication using pure Hardware Serial
  mmWave.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);
  
  // Send the wake-up command efficiently (bytes are faster than Strings!)
  mmWave.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);
  
  Serial.println("--- ESP32 READY ---");
}

void loop() {
  while(mmWave.available()) {
    uint8_t b = mmWave.read();

    // 1. Hunt for the Header (F4 F3 F2 F1)
    if(!readingPacket) {
      if(b == HEADER[headerCount]) {
        headerCount++;
        if(headerCount == 4) {
          readingPacket = true;
          packet[0] = 0xF4; packet[1] = 0xF3; packet[2] = 0xF2; packet[3] = 0xF1;
          packetLen = 4;
          headerCount = 0;
        }
      } else {
        headerCount = 0; 
      }
    } 
    // 2. Read the packet data
    else {
      packet[packetLen] = b;
      packetLen++;

      // 3. Check if we hit the Footer (F8 F7 F6 F5)
      if(packetLen >= 4 && 
         packet[packetLen-4] == 0xF8 && 
         packet[packetLen-3] == 0xF7 && 
         packet[packetLen-2] == 0xF6 && 
         packet[packetLen-1] == 0xF5) {
        
        // --- DATA EXTRACTION ---
        int presence = packet[6]; // 0 = Empty, 1 = Occupied
        
        // Distance in cm, converted to meters
        int distanceCm = packet[7] + (packet[8] << 8);
        float distanceMeters = distanceCm / 100.0; 

        // Find the gate with the highest energy
        int maxEnergy = 0;
        int activeGate = 0;
        
        for (int i = 0; i < 16; i++) {
          int gateEnergy = packet[9 + (i*2)] + (packet[10 + (i*2)] << 8);
          if (gateEnergy > maxEnergy) {
            maxEnergy = gateEnergy;
            activeGate = i;
          }
        }

        // Print exactly what Python expects: Presence,DistanceMeters,ActiveGate,MaxEnergy
        Serial.print(presence);
        Serial.print(",");
        Serial.print(distanceMeters, 2); 
        Serial.print(",");
        Serial.print(activeGate);
        Serial.print(",");
        Serial.println(maxEnergy);

        readingPacket = false; 
      }
      
      // Safety reset to prevent memory leaks
      if(packetLen >= 64) {
        readingPacket = false;
      }
    }
  }
}