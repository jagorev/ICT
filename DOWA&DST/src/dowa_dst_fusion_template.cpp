#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SGP30.h"

/*
  MOD/DOWA Fusion Template - Functional Implementation
  Integrates real sensor reads:
  - mmWave LD2410 radar (UART)
  - PIR sensor (GPIO digital)
  - SGP30 air quality (I2C)
*/

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

// mmWave LD2410 (UART)
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17
HardwareSerial radarSerial(1);

// PIR Sensor (GPIO)
#define PIR_PIN 13

// SGP30 (I2C)
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
Adafruit_SGP30 sgp30;

// mmWave packet parsing state
const uint8_t RADAR_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
int radarHeaderCount = 0;
uint8_t radarPacket[64];
int radarPacketLen = 0;
bool radarReadingPacket = false;
uint8_t lastRadarPresence = 0;
unsigned long lastRadarReadTime = 0;

// SGP30 state
unsigned long lastSGP30ReadTime = 0;
uint16_t lastTVOC = 0;
uint16_t lasteCO2 = 400;

struct MassFunction {
  float occupied;
  float empty;
  float unknown;
};

struct SensorEvidence {
  bool radarMotion;
  bool pirMotion;
  uint16_t tvoc;
  uint16_t eco2;
};

struct FusionResult {
  float occupiedBelief;
  float emptyBelief;
  float confidence;
  float conflictK;
  bool occupied;
};

static constexpr float TVOC_OCCUPIED_THRESHOLD = 200.0f;
static constexpr float TVOC_WEAK_THRESHOLD = 100.0f;
static constexpr float DOWA_DECISION_THRESHOLD = 0.50f;

// Sensor weight coefficients (w_i)
// Tune these to reflect sensor reliability and importance
static constexpr float w_radar = 0.50f;  // mmWave radar weight
static constexpr float w_pir = 0.35f;    // PIR sensor weight
static constexpr float w_air = 0.15f;    // Air quality weight

static MassFunction makeRadarMass(bool motion) {
  if (motion) {
    return {0.90f, 0.05f, 0.05f};
  }
  return {0.05f, 0.90f, 0.05f};
}

static MassFunction makePirMass(bool motion) {
  if (motion) {
    return {0.80f, 0.10f, 0.10f};
  }
  return {0.10f, 0.80f, 0.10f};
}

static MassFunction makeAirMass(uint16_t tvoc, uint16_t eco2) {
  if (tvoc >= TVOC_OCCUPIED_THRESHOLD || eco2 >= 1000) {
    return {0.75f, 0.10f, 0.15f};
  }
  if (tvoc >= TVOC_WEAK_THRESHOLD || eco2 >= 800) {
    return {0.45f, 0.25f, 0.30f};
  }
  return {0.15f, 0.70f, 0.15f};
}

static MassFunction combineTwo(const MassFunction &a, const MassFunction &b, float &outK) {
  // Dempster-Shafer Combination Rule (PDF 5.3)
  // K = conflict coefficient: Σ m_A(X) × m_B(Y) for all X ≠ Y
  const float k = (a.occupied * b.empty) + (a.empty * b.occupied);
  outK = k;
  
  const float safeDenominator = 1.0f - k;

  if (safeDenominator <= 0.0001f) {
    // High conflict: default to UNKNOWN
    return {0.0f, 0.0f, 1.0f};
  }

  // m_combined(H) = [ Σ m_A(X) × m_B(Y) for all XY=H ] / (1 − K)
  MassFunction result;
  result.occupied = ((a.occupied * b.occupied) + (a.occupied * b.unknown) + (a.unknown * b.occupied)) / safeDenominator;
  result.empty = ((a.empty * b.empty) + (a.empty * b.unknown) + (a.unknown * b.empty)) / safeDenominator;
  result.unknown = ((a.unknown * b.unknown)) / safeDenominator;
  return result;
}

static MassFunction combineEvidence(const SensorEvidence &evidence, float &outMaxK) {
  const MassFunction radarMass = makeRadarMass(evidence.radarMotion);
  const MassFunction pirMass = makePirMass(evidence.pirMotion);
  const MassFunction airMass = makeAirMass(evidence.tvoc, evidence.eco2);

  // Weight Normalization (PDF 5.1)
  // Normalize weights to sum to 1.0, accounting for inactive sensors
  float totalWeight = w_radar + w_pir + w_air;
  float norm_radar = w_radar / totalWeight;
  float norm_pir = w_pir / totalWeight;
  float norm_air = w_air / totalWeight;
  
  // DOWA Weighted Aggregation (PDF section 5.2)
  // m_DOWA(H) = Σ [ w_i_normalized × m_i(H) ] for each hypothesis H
  
  MassFunction m_dowa;
  m_dowa.occupied = (norm_radar * radarMass.occupied) + (norm_pir * pirMass.occupied) + (norm_air * airMass.occupied);
  m_dowa.empty = (norm_radar * radarMass.empty) + (norm_pir * pirMass.empty) + (norm_air * airMass.empty);
  m_dowa.unknown = (norm_radar * radarMass.unknown) + (norm_pir * pirMass.unknown) + (norm_air * airMass.unknown);
  
  // Dempster-Shafer Sequential Combination (PDF 5.3)
  // Combine DOWA aggregate with each sensor's mass function
  float k = 0.0f;
  float maxK = 0.0f;
  
  MassFunction combined = m_dowa;
  
  // Combine with radar
  combined = combineTwo(combined, radarMass, k);
  maxK = max(maxK, k);
  
  // Combine with PIR
  combined = combineTwo(combined, pirMass, k);
  maxK = max(maxK, k);
  
  // Combine with air quality
  combined = combineTwo(combined, airMass, k);
  maxK = max(maxK, k);
  
  outMaxK = maxK;
  return combined;
}

static float computeDOWAConfidence(const MassFunction &mass) {
  const float signalStrength = mass.occupied + (0.5f * mass.unknown);
  return constrain(signalStrength, 0.0f, 1.0f);
}

static FusionResult runDOWA_DST_Fusion(const SensorEvidence &evidence) {
  float maxK = 0.0f;
  const MassFunction fusedMass = combineEvidence(evidence, maxK);
  const float confidence = computeDOWAConfidence(fusedMass);

  FusionResult result;
  result.occupiedBelief = fusedMass.occupied;
  result.emptyBelief = fusedMass.empty;
  result.confidence = confidence;
  result.conflictK = maxK;
  result.occupied = (fusedMass.occupied >= DOWA_DECISION_THRESHOLD);
  return result;
}

static void processRadarData() {
  // Parse incoming mmWave LD2410 packets
  while(radarSerial.available()) {
    uint8_t b = radarSerial.read();

    if(!radarReadingPacket) {
      if(b == RADAR_HEADER[radarHeaderCount]) {
        radarHeaderCount++;
        if(radarHeaderCount == 4) {
          radarReadingPacket = true;
          radarPacket[0] = 0xF4;
          radarPacket[1] = 0xF3;
          radarPacket[2] = 0xF2;
          radarPacket[3] = 0xF1;
          radarPacketLen = 4;
          radarHeaderCount = 0;
        }
      } else {
        radarHeaderCount = 0;
      }
    } else {
      radarPacket[radarPacketLen] = b;
      radarPacketLen++;

      // Check for footer (F8 F7 F6 F5)
      if(radarPacketLen >= 4 && 
         radarPacket[radarPacketLen-4] == 0xF8 && 
         radarPacket[radarPacketLen-3] == 0xF7 && 
         radarPacket[radarPacketLen-2] == 0xF6 && 
         radarPacket[radarPacketLen-1] == 0xF5) {
        
        lastRadarPresence = radarPacket[6]; // 0=Empty, 1=Occupied
        lastRadarReadTime = millis();
        radarReadingPacket = false;
      }

      if(radarPacketLen >= 64) {
        radarReadingPacket = false;
      }
    }
  }
}

static void processSGP30Data() {
  unsigned long now = millis();
  
  // SGP30 requires exactly 1 second between measurements
  if (now - lastSGP30ReadTime < 1000) {
    return;
  }
  lastSGP30ReadTime = now;
  
  if (sgp30.IAQmeasure()) {
    lastTVOC = sgp30.TVOC;
    lasteCO2 = sgp30.eCO2;
  }
}

static SensorEvidence readSensorsTemplate() {
  // Process incoming sensor data continuously
  processRadarData();
  processSGP30Data();
  
  SensorEvidence evidence;
  
  // PIR: simple GPIO read
  evidence.pirMotion = digitalRead(PIR_PIN);
  
  // mmWave: parsed from packet
  // (lastRadarPresence is updated in processRadarData)
  evidence.radarMotion = (lastRadarPresence > 0);
  
  // SGP30: air quality
  evidence.tvoc = lastTVOC;
  evidence.eco2 = lasteCO2;
  
  return evidence;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== MOD/DOWA Fusion Engine - Functional ===\n");
  
  // Initialize PIR pin
  pinMode(PIR_PIN, INPUT);
  Serial.println("[PIR] Initialized on GPIO " + String(PIR_PIN));
  
  // Initialize mmWave UART
  radarSerial.begin(115200, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  delay(500);
  
  // Send wakeup command to radar
  const byte REPORT_MODE_CMD[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x08, 0x00,
    0x12, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00,
    0x04, 0x03, 0x02, 0x01
  };
  radarSerial.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);
  Serial.println("[mmWave] Initialized on UART (RX=" + String(RADAR_RX_PIN) + ", TX=" + String(RADAR_TX_PIN) + ")");
  
  // Initialize I2C and SGP30
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(500);
  
  if (!sgp30.begin(&Wire)) {
    Serial.println("[SGP30] ERROR: Sensor not found!");
    while(1) { delay(100); }
  }
  
  Serial.print("[SGP30] Found serial #");
  Serial.print(sgp30.serialnumber[0], HEX);
  Serial.print(sgp30.serialnumber[1], HEX);
  Serial.println(sgp30.serialnumber[2], HEX);
  
  Serial.println("\n[FUSION] Warming up... (1Hz measurement cycle)\n");
  Serial.println("========================================");
}

void loop() {
  static unsigned long lastFusionMs = 0;

  if (millis() - lastFusionMs < 1000) {
    return;
  }
  lastFusionMs = millis();

  const SensorEvidence evidence = readSensorsTemplate();
  const FusionResult result = runDOWA_DST_Fusion(evidence);

  Serial.print("[");
  Serial.print(millis() / 1000);
  Serial.print("s] ");
  
  Serial.print("RADAR=");
  Serial.print(evidence.radarMotion ? "1" : "0");
  Serial.print(" PIR=");
  Serial.print(evidence.pirMotion ? "1" : "0");
  Serial.print(" TVOC=");
  Serial.print(evidence.tvoc);
  Serial.print("ppb eCO2=");
  Serial.print(evidence.eco2);
  Serial.print("ppm | ");
  
  Serial.print("m(O)=");
  Serial.print(result.occupiedBelief, 3);
  Serial.print(" m(E)=");
  Serial.print(result.emptyBelief, 3);
  Serial.print(" | K=");
  Serial.print(result.conflictK, 3);
  Serial.print(" | ");
  Serial.println(result.occupied ? ">>> OCCUPIED <<<" : "EMPTY");
}
