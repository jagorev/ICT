/* #include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
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

  while (!Serial) { delay(10); } // Force it to wait until the Serial Monitor is open
  
  WiFi.mode(WIFI_MODE_STA);
  Serial.println(WiFi.macAddress());

  // Start Radar communication
  mmWave.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(2000);
  
  // Send wake-up/config command
  mmWave.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);

  Serial.println("\n--- VDA Telkonet True Presence Detection - FindMe ---");
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
} */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Adafruit_SGP30.h"

// ---------------------------------------------------------
// 1. PIN DEFINITIONS & HARDWARE SERIAL
// ---------------------------------------------------------
#define SDA_PIN 8 
#define SCL_PIN 9
#define RX_PIN 16
#define TX_PIN 17
HardwareSerial mmWave(1); 

// ---------------------------------------------------------
// 2. GLOBAL STATE VARIABLES
// ---------------------------------------------------------
Adafruit_SGP30 sgp;
unsigned long lastSGP30ReadTime = 0;
unsigned long lastFusionTime = 0;

// Sensor States
bool mmWavePresence = false; 
uint8_t mmWaveState = 0; 
float mmWaveDistance = 0.0;
uint16_t currentTVOC = 0;
uint16_t currenteCO2 = 400;

// ESP-NOW PIR State
bool pirPresence = false;
unsigned long lastPIRUpdate = 0;

// mmWave Protocol Variables
const uint8_t HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
int headerCount = 0;
uint8_t packet[64];
int packetLen = 0;
bool readingPacket = false;
float MAX_ROOM_DISTANCE = 5.0; // Distance of your glass wall

// mmWave Config Command (Wakes up the radar)
const byte REPORT_MODE_CMD[] = {
  0xFD, 0xFC, 0xFB, 0xFA, 
  0x08, 0x00,             
  0x12, 0x00, 0x00, 0x00, 
  0x04, 0x00, 0x00, 0x00, 
  0x04, 0x03, 0x02, 0x01  
};

// ---------------------------------------------------------
// 3. ESP-NOW RECEPTION CALLBACK
// ---------------------------------------------------------
typedef struct struct_message {
  bool isMotionDetected;
} struct_message;

struct_message incomingData;

// This function runs automatically whenever the Slave sends data!
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  pirPresence = incomingData.isMotionDetected;
  lastPIRUpdate = millis(); // Reset the safety timeout
}

// ---------------------------------------------------------
// 4. DOWA FUSION ENGINE (Upgraded for 3 Sensors)
// ---------------------------------------------------------
bool runDOWAFusion(bool isRadarMotion, uint16_t tvoc, bool isPirMotion) {
  // New Weight Distribution
  float mmWaveWeight = 0.50; 
  float pirWeight = 0.35;
  float envWeight = 0.15;    
  
  float mmWaveMass = isRadarMotion ? 1.0 : 0.0;
  float pirMass = isPirMotion ? 1.0 : 0.0;
  
  float envMass = 0.0;
  if (tvoc > 100) { envMass = 0.5; }
  if (tvoc > 200) { envMass = 1.0; }

  float fusionProbability = (mmWaveMass * mmWaveWeight) + (pirMass * pirWeight) + (envMass * envWeight);

  return (fusionProbability > 0.5); 
}

String getRadarState(uint8_t state) {
  if (state == 1) return "MACRO";
  if (state == 2) return "MICRO";
  if (state == 3) return "MACRO+MICRO";
  return "EMPTY";
}

// ---------------------------------------------------------
// 5. SYSTEM SETUP
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(3000); // Wait for Serial to open
  
  
  // Start mmWave
  mmWave.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  // ---> ADD THESE TWO LINES <---
  mmWave.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);
  
  // 1. Initialize WiFi as a Station for ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  // 2. Register the callback to catch PIR data
  esp_now_register_recv_cb(OnDataRecv);

  // Initialize SGP30
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!sgp.begin(&Wire)) {
    Serial.println("ERROR: SGP30 not found!");
    while (1) { delay(10); } 
  }
  
  Serial.println("\n--- VDA Telkonet: Master Hub Ready ---");
}

// ---------------------------------------------------------
// 6. MAIN LOOP
// ---------------------------------------------------------
void loop() {
  
  // SAFETY: If the PIR Slave gets unplugged, force PIR to False after 2 seconds
  if (millis() - lastPIRUpdate > 2000) {
    pirPresence = false;
  }

  // TASK 1: SGP30 POLLING
  if (millis() - lastSGP30ReadTime >= 1000) {
    lastSGP30ReadTime = millis();
    if (sgp.IAQmeasure()) {
      currentTVOC = sgp.TVOC;
      currenteCO2 = sgp.eCO2;
    }
  }

  // TASK 2: MMWAVE PARSING
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
      } else headerCount = 0; 
    } else {
      packet[packetLen] = b;
      packetLen++;
      if(packetLen >= 4 && packet[packetLen-4] == 0xF8 && packet[packetLen-3] == 0xF7 && packet[packetLen-2] == 0xF6 && packet[packetLen-1] == 0xF5) {
        
        uint8_t rawState = packet[6]; 
        int distanceCm = packet[7] + (packet[8] << 8);
        float rawDistance = distanceCm / 100.0; 

        // SOFTWARE DISTANCE GATE (Ignore the Hallway)
        if (rawDistance > MAX_ROOM_DISTANCE) {
          mmWaveState = 0; 
          mmWavePresence = false;
        } else {
          mmWaveState = rawState;
          mmWaveDistance = rawDistance;
          mmWavePresence = (mmWaveState > 0); 
        }
        readingPacket = false; 
      }
      if(packetLen >= 64) readingPacket = false;
    }
  }

  // TASK 3: FUSION & REPORT (1Hz)
  if (millis() - lastFusionTime >= 1000) {
    lastFusionTime = millis();
    bool isOccupied = runDOWAFusion(mmWavePresence, currentTVOC, pirPresence);

    // Beautiful Dashboard
    Serial.print("[PIR] "); 
    Serial.print(pirPresence ? "YES" : "NO ");
    Serial.print(" | [RADAR] "); 
    Serial.print(getRadarState(mmWaveState));
    Serial.print(" ("); Serial.print(mmWaveDistance, 1); Serial.print("m)");
    Serial.print(" | [AIR] TVOC:"); Serial.print(currentTVOC); 
    Serial.print(" | [DOWA]: ");
    Serial.print(isOccupied ? "OCCUPIED" : "EMPTY   ");

    // CSV Output (State, Distance, TVOC, eCO2, PIR)
    Serial.print(" | CSV:");
    Serial.print(mmWaveState); Serial.print(",");
    Serial.print(mmWaveDistance, 2); Serial.print(",");
    Serial.print(currentTVOC); Serial.print(",");
    Serial.print(currenteCO2); Serial.print(",");
    Serial.println(pirPresence ? 1 : 0);
  }
}