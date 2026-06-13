#ifndef DOWA_DST_FUSION_TEMPLATE_H
#define DOWA_DST_FUSION_TEMPLATE_H

#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SGP30.h"

/*
  MOD/DOWA Fusion Template - Functional Implementation
  Integrates real sensor reads:
  - mmWave LD2410 radar (UART)
  - PIR sensor (GPIO digital) - Note: Master overrides this with ESP-NOW data
  - SGP30 air quality (I2C)
*/

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

// mmWave LD2410 (UART)
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17
HardwareSerial radarSerial(1);

// PIR Sensor (GPIO) - Kept for compatibility, but ESP-NOW overrides it
#define PIR_PIN 13

// SGP30 (I2C) - Usa pin normali per ESP32
#define I2C_SDA_PIN 21 
#define I2C_SCL_PIN 22
Adafruit_SGP30 sgp30;

// mmWave packet parsing state
const uint8_t RADAR_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
int radarHeaderCount = 0;
uint8_t radarPacket[64];
int radarPacket_len = 0;
bool radarReadingPacket = false;
uint8_t lastRadarPresence = 0; // LD2410 state: 0=None, 1=Moving, 2=Stationary, 3=Both
unsigned long lastRadarReadTime = 0;

// SGP30 state
unsigned long lastSGP30ReadTime = 0;
uint16_t lastTVOC = 0;
uint16_t lasteCO2 = 400;

// PIR State
unsigned long lastPirTriggerTime = 0;

// ============================================================================
// DOWA-DST STRUCTURES & MATH (100% YOUR TEAM'S CODE)
// ============================================================================

struct MassFunction {
  float occupied;
  float empty;
  float unknown;
};

struct SensorEvidence {
  uint8_t radarState; // 0=none, 1=stationary, 2=moving
  bool pirMotion;
  uint32_t msSinceLastPirTrigger;
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
static constexpr float w_radar = 0.50f;  // mmWave radar weight
static constexpr float w_pir = 0.35f;    // PIR sensor weight
static constexpr float w_air = 0.15f;    // Air quality weight

static MassFunction makeRadarMass(uint8_t radarState) {
  switch(radarState) {
    case 2: return {0.95f, 0.00f, 0.05f}; // moving
    case 1: return {0.85f, 0.00f, 0.15f}; // stationary/breathing
    default: return {0.05f, 0.70f, 0.25f}; // none
  }
}

static MassFunction makePirMass(bool motion, uint32_t msSinceLastTrigger) {
  if (motion) return {0.90f, 0.00f, 0.10f};
  if (msSinceLastTrigger < 30000)  return {0.75f, 0.00f, 0.25f}; // hold
  if (msSinceLastTrigger < 120000) {
    float ratio = 1.0f - ((msSinceLastTrigger - 30000.0f) / 90000.0f);
    return {0.75f * ratio, 0.00f, 1.0f - (0.75f * ratio)};
  }
  return {0.00f, 0.30f, 0.70f}; // silence > 120s
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
  const float k = (a.occupied * b.empty) + (a.empty * b.occupied);
  outK = k;
  const float safeDenominator = 1.0f - k;
  if (safeDenominator <= 0.0001f) return {0.0f, 0.0f, 1.0f};

  MassFunction result;
  result.occupied = ((a.occupied * b.occupied) + (a.occupied * b.unknown) + (a.unknown * b.occupied)) / safeDenominator;
  result.empty = ((a.empty * b.empty) + (a.empty * b.unknown) + (a.unknown * b.empty)) / safeDenominator;
  result.unknown = ((a.unknown * b.unknown)) / safeDenominator;
  return result;
}

static MassFunction combineEvidence(const SensorEvidence &evidence, float &outMaxK) {
  const MassFunction radarMass = makeRadarMass(evidence.radarState);
  const MassFunction pirMass = makePirMass(evidence.pirMotion, evidence.msSinceLastPirTrigger);
  const MassFunction airMass = makeAirMass(evidence.tvoc, evidence.eco2);

  float totalWeight = w_radar + w_pir + w_air;
  float norm_radar = w_radar / totalWeight;
  float norm_pir = w_pir / totalWeight;
  float norm_air = w_air / totalWeight;
  
  MassFunction m_dowa;
  m_dowa.occupied = (norm_radar * radarMass.occupied) + (norm_pir * pirMass.occupied) + (norm_air * airMass.occupied);
  m_dowa.empty = (norm_radar * radarMass.empty) + (norm_pir * pirMass.empty) + (norm_air * airMass.empty);
  m_dowa.unknown = (norm_radar * radarMass.unknown) + (norm_pir * pirMass.unknown) + (norm_air * airMass.unknown);
  
  float k = 0.0f;
  float maxK = 0.0f;
  MassFunction combined = m_dowa;
  
  combined = combineTwo(combined, radarMass, k);
  maxK = max(maxK, k);
  combined = combineTwo(combined, pirMass, k);
  maxK = max(maxK, k);
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

// ============================================================================
// HARDWARE PARSING (100% YOUR TEAM'S CODE)
// ============================================================================

static void processRadarData() {
  while(radarSerial.available()) {
    uint8_t b = radarSerial.read();

    if(!radarReadingPacket) {
      if(b == RADAR_HEADER[radarHeaderCount]) {
        radarHeaderCount++;
        if(radarHeaderCount == 4) {
          radarReadingPacket = true;
          radarPacket[0] = 0xF4; radarPacket[1] = 0xF3; radarPacket[2] = 0xF2; radarPacket[3] = 0xF1;
          radarPacket_len = 4;
          radarHeaderCount = 0;
        }
      } else { radarHeaderCount = 0; }
    } else {
      radarPacket[radarPacket_len] = b;
      radarPacket_len++;

      if(radarPacket_len >= 4 && radarPacket[radarPacket_len-4] == 0xF8 && radarPacket[radarPacket_len-3] == 0xF7 && radarPacket[radarPacket_len-2] == 0xF6 && radarPacket[radarPacket_len-1] == 0xF5) {
        lastRadarPresence = radarPacket[6]; // LD2410 byte 6: 0=None, 1=Moving, 2=Stationary, 3=Both
        lastRadarReadTime = millis();
        radarReadingPacket = false;
      }
      if(radarPacket_len >= 64) radarReadingPacket = false;
    }
  }
}

static void processSGP30Data() {
  unsigned long now = millis();
  if (now - lastSGP30ReadTime < 1000) return;
  lastSGP30ReadTime = now;
  
  if (sgp30.IAQmeasure()) {
    lastTVOC = sgp30.TVOC;
    lasteCO2 = sgp30.eCO2;
  }
}

static SensorEvidence readSensorsTemplate() {
  processRadarData();
  processSGP30Data();
  
  SensorEvidence evidence;
  
  evidence.pirMotion = digitalRead(PIR_PIN); // Will be overridden by main.cpp if ESP-NOW is active
  if (evidence.pirMotion) {
    lastPirTriggerTime = millis();
  }
  evidence.msSinceLastPirTrigger = millis() - (lastPirTriggerTime > 0 ? lastPirTriggerTime : millis()); 
  
  if (lastRadarPresence == 1 || lastRadarPresence == 3) {
      evidence.radarState = 2; // Moving
  } else if (lastRadarPresence == 2) {
      evidence.radarState = 1; // Stationary
  } else {
      evidence.radarState = 0; // None
  }
  
  evidence.tvoc = lastTVOC;
  evidence.eco2 = lasteCO2;
  
  return evidence;
}

// ============================================================================
// DOWA HARDWARE SETUP (RENAMED SO IT DOESN'T CONFLICT WITH MAIN.CPP)
// ============================================================================
void setup_dowa_sensors() {
  pinMode(PIR_PIN, INPUT);
  Serial.println("[PIR] Initialized on GPIO " + String(PIR_PIN));
  
  radarSerial.begin(115200, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  delay(500);
  
  const byte REPORT_MODE_CMD[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01
  };
  radarSerial.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD));
  delay(100);
  Serial.println("[mmWave] Initialized on UART (RX=" + String(RADAR_RX_PIN) + ", TX=" + String(RADAR_TX_PIN) + ")");
  
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(500);
  
  if (!sgp30.begin(&Wire)) {
    Serial.println("[SGP30] ERROR: Sensor not found! (Continuing anyway...)");
  } else {
    Serial.print("[SGP30] Found serial #");
    Serial.print(sgp30.serialnumber[0], HEX);
    Serial.println(sgp30.serialnumber[2], HEX);
  }
}

#endif