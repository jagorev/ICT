#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SGP30.h"

// ---------------------------------------------------------
// 1. PIN DEFINITIONS & HARDWARE SERIAL
// ---------------------------------------------------------
// I2C for SGP30
#define SDA_PIN 8 
#define SCL_PIN 9

// UART for mmWave Radar
#define RX_PIN 16
#define TX_PIN 17
HardwareSerial mmWave(1); 

// ---------------------------------------------------------
// 2. GLOBAL STATE VARIABLES & OBJECTS
// ---------------------------------------------------------
// SGP30 Object & Timing
Adafruit_SGP30 sgp;
unsigned long lastSGP30ReadTime = 0;
unsigned long lastFusionTime = 0;

// Fusion Engine Data Containers
bool mmWavePresence = false; 
uint8_t mmWaveState = 0; 
float mmWaveDistance = 0.0;
uint16_t currentTVOC = 0;
uint16_t currenteCO2 = 400;

// mmWave State Machine Variables
const uint8_t HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
int headerCount = 0;
uint8_t packet[64];
int packetLen = 0;
bool readingPacket = false;

// mmWave Config Command (100ms refresh attempt)
const byte REPORT_MODE_CMD[] = {
  0xFD, 0xFC, 0xFB, 0xFA, 
  0x08, 0x00,             
  0x12, 0x00, 0x00, 0x00, 
  0x04, 0x00, 0x00, 0x00, 
  0x04, 0x03, 0x02, 0x01  
};

// Helper function to decode the radar's state byte
String getRadarState(uint8_t state) {
  if (state == 1) return "MACRO-MOTION";
  if (state == 2) return "MICRO-MOTION";
  if (state == 3) return "MACRO+MICRO ";
  return "EMPTY       ";
}
// ---------------------------------------------------------
// 3. DOWA FUSION ENGINE
// ---------------------------------------------------------
bool runDOWAFusion(bool isMotionDetected, uint16_t tvoc) {
  // Weights based on your architecture
  float mmWaveWeight = 0.85; 
  float envWeight = 0.15;    
  
  // Calculate evidence mass
  float mmWaveMass = isMotionDetected ? 1.0 : 0.0;
  
  // TVOC scaling (Assuming baseline is ~0-50 ppb)
  float envMass = 0.0;
  if (tvoc > 100) { envMass = 0.5; }
  if (tvoc > 200) { envMass = 1.0; }

  // Fusion probability
  float fusionProbability = (mmWaveMass * mmWaveWeight) + (envMass * envWeight);

  // If combined evidence is > 50%, state is OCCUPIED
  return (fusionProbability > 0.5); 
}

// ---------------------------------------------------------
// 4. SYSTEM SETUP
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  
  // Start Radar communication
  mmWave.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);
  
  // Send wake-up/config command
  mmWave.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);

  Serial.println("\n--- VDA Telkonet True Presence Detection ---");
  Serial.println("Initializing MOD/DOWA Fusion Engine...");

  // Initialize SGP30
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!sgp.begin(&Wire)) {
    Serial.println("ERROR: SGP30 not found! Check wiring.");
    while (1) { delay(10); } 
  }
  Serial.println("SGP30 initialized. Warming up (15s baseline)...");
}

// ---------------------------------------------------------
// 5. MAIN LOOP (NON-BLOCKING)
// ---------------------------------------------------------

void loop() {
  // ==========================================
  // TASK 1: SGP30 ENVIRONMENTAL POLLING (1Hz)
  // ==========================================
  if (millis() - lastSGP30ReadTime >= 1000) {
    lastSGP30ReadTime = millis();
    if (sgp.IAQmeasure()) {
      currentTVOC = sgp.TVOC;
      currenteCO2 = sgp.eCO2;
    }
  }

  // ==========================================
  // TASK 2: MMWAVE RADAR STATE MACHINE
  // ==========================================
  while(mmWave.available()) {
    uint8_t b = mmWave.read();

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
    else {
      packet[packetLen] = b;
      packetLen++;

      if(packetLen >= 4 && packet[packetLen-4] == 0xF8 && packet[packetLen-3] == 0xF7 && packet[packetLen-2] == 0xF6 && packet[packetLen-1] == 0xF5) {
        
        // --- EXTRACTION UPGRADE ---
        mmWaveState = packet[6]; // 0=None, 1=Moving, 2=Stationary(Micro), 3=Both
        int distanceCm = packet[7] + (packet[8] << 8);
        mmWaveDistance = distanceCm / 100.0; 

        // Update the binary presence flag for the DOWA engine (True if state is 1, 2, or 3)
        mmWavePresence = (mmWaveState > 0); 

        readingPacket = false; 
      }
      if(packetLen >= 64) readingPacket = false;
    }
  }

  // ==========================================
  // TASK 3: EXECUTE DOWA FUSION & REPORT (1Hz)
  // ==========================================
  if (millis() - lastFusionTime >= 1000) {
    lastFusionTime = millis();
    
    // Evaluate combined evidence (Keeps your dashboard working)
    bool isOccupied = runDOWAFusion(mmWavePresence, currentTVOC);

    // 1. Print Detailed Dashboard to Serial
    Serial.print("[mmWave] "); 
    Serial.print(getRadarState(mmWaveState));
    Serial.print(" (");
    Serial.print(mmWaveDistance, 2);
    Serial.print("m) | [SGP30] TVOC: "); 
    Serial.print(currentTVOC);
    Serial.print(" ppb  eCO2: ");
    Serial.print(currenteCO2);
    Serial.print(" ppm | [DOWA OUTPUT]: ");
    Serial.print(isOccupied ? "OCCUPIED" : "EMPTY   ");

    // 2. Attach the ML Raw Data Payload
    // Format: State(0-3), Distance(m), TVOC(ppb), eCO2(ppm)
    Serial.print(" | CSV:");
    Serial.print(mmWaveState); 
    Serial.print(",");
    Serial.print(mmWaveDistance, 2);
    Serial.print(",");
    Serial.print(currentTVOC);
    Serial.print(",");
    Serial.println(currenteCO2);
  }
}