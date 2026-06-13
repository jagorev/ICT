#ifndef DOWA_DST_FUSION_TEMPLATE_H
#define DOWA_DST_FUSION_TEMPLATE_H

#include <Arduino.h>
#include <Wire.h>
#include "network_manager.h"
#include "Adafruit_SGP30.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "metrics.h"

/*
  MOD/DOWA Fusion Template - Functional Implementation
  Integrates real sensor reads:
  - mmWave LD2410 radar (UART)
  - PIR sensor (GPIO digital)
  - SGP30 air quality (I2C)
  - BLE Scanner for Janitor Override
  - On-device metrics tracking (accuracy, precision, recall, F1, FNR, FPR)
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
bool sgp30_initialized = false;
unsigned long lastSGP30ReadTime = 0;
uint16_t lastTVOC = 0;
uint16_t lasteCO2 = 400;

// PIR State
unsigned long lastPirTriggerTime = 0;

// BLE Janitor Override State
#define JANITOR_TAG_NAME "JANITOR_TAG"
BLEScan* pBLEScan;
bool staff_present = false;
unsigned long lastStaffDetectTime = 0;
const int RSSI_THRESHOLD = -85; // Lowered from -60 to be more permissive
const unsigned long DEPARTURE_TIMEOUT = 10000; // 10 seconds of no detection to clear flag
bool saved_occupancy_state = false; // State to freeze when staff enters

int current_staff_rssi = -100;

// ============================================================================
// METRICS STATE
// ============================================================================
ConfusionMatrix cm;
bool groundTruthOccupied = false; // updated via Serial command ('1'/'0')

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // Debug: Print all named devices (rate limited to avoid spam)
        static unsigned long lastPrint = 0;
        if (advertisedDevice.haveName() && millis() - lastPrint > 2000) {
            Serial.printf("[BLE] Saw nearby device named: '%s' (RSSI: %d)\n", advertisedDevice.getName().c_str(), advertisedDevice.getRSSI());
            lastPrint = millis();
        }

        if (advertisedDevice.haveName() && advertisedDevice.getName() == JANITOR_TAG_NAME) {
            int rssi = advertisedDevice.getRSSI();
            
            if (rssi > RSSI_THRESHOLD) {
                current_staff_rssi = rssi;
                lastStaffDetectTime = millis();
                if (!staff_present) {
                    staff_present = true;
                    Serial.printf("\n[BLE] STAFF TAG DETECTED! RSSI: %d - FREEZING STATE\n", rssi);
                }
            } else {
                Serial.printf("[BLE] Seen Staff Tag, but too far away (RSSI %d < Threshold %d)\n", rssi, RSSI_THRESHOLD);
            }
        }
    }
};

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
  // radarState: 0=none, 1=stationary, 2=moving
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
    // linear decay of m(O) toward 0, m(U) toward 1.0
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

  if (safeDenominator <= 0.0001f) {
    return {0.0f, 0.0f, 1.0f};
  }

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

static void processRadarData() {
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
          radarPacket_len = 4;
          radarHeaderCount = 0;
        }
      } else {
        radarHeaderCount = 0;
      }
    } else {
      radarPacket[radarPacket_len] = b;
      radarPacket_len++;

      if(radarPacket_len >= 4 && 
         radarPacket[radarPacket_len-4] == 0xF8 && 
         radarPacket[radarPacket_len-3] == 0xF7 && 
         radarPacket[radarPacket_len-2] == 0xF6 && 
         radarPacket[radarPacket_len-1] == 0xF5) {
        
        lastRadarPresence = radarPacket[6]; // LD2410 byte 6: 0=None, 1=Moving, 2=Stationary, 3=Both
        lastRadarReadTime = millis();
        radarReadingPacket = false;
      }

      if(radarPacket_len >= 64) {
        radarReadingPacket = false;
      }
    }
  }
}

static void processSGP30Data() {
  if (!sgp30_initialized) return; // Prevent I2C hang if sensor is missing!

  unsigned long now = millis();
  
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
  processRadarData();
  processSGP30Data();
  
  SensorEvidence evidence;
  
  evidence.pirMotion = pirPresence; // From network_manager.h (ESP-NOW)
  if (evidence.pirMotion) {
    lastPirTriggerTime = millis();
  }
  evidence.msSinceLastPirTrigger = millis() - (lastPirTriggerTime > 0 ? lastPirTriggerTime : millis()); // Prevent overflow on startup buffer
  
  // LD2410 parsing logic maps nicely:
  if (lastRadarPresence == 1 || lastRadarPresence == 3) {
      evidence.radarState = 2; // Moving/Both -> 2 (Moving)
  } else if (lastRadarPresence == 2) {
      evidence.radarState = 1; // Stationary -> 1 (Stationary)
  } else {
      evidence.radarState = 0; // None -> 0
  }
  
  evidence.tvoc = lastTVOC;
  evidence.eco2 = lasteCO2;
  
  return evidence;
}

// ============================================================================
// METRICS: GROUND TRUTH INPUT VIA SERIAL
// ============================================================================
// Send '1' = mark room as OCCUPIED (ground truth)
// Send '0' = mark room as EMPTY (ground truth)
// Send 'p' = print confusion matrix + metrics snapshot
// ============================================================================
static void checkSerialGroundTruth() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '1') {
      groundTruthOccupied = true;
      Serial.println("\n[GT] Ground truth set to OCCUPIED\n");
    } else if (c == '0') {
      groundTruthOccupied = false;
      Serial.println("\n[GT] Ground truth set to EMPTY\n");
    } else if (c == 'p' || c == 'P') {
      confusion_print(cm);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== MOD/DOWA Fusion Engine - Functional ===\n");

  setup_network_and_memory();

  // Inizializza il BLE Scanner
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(30);
  
  pBLEScan->start(0, nullptr, false); 
  
  Serial.println("[BLE] Scanner Initialized and Listening!");
  
  pinMode(PIR_PIN, INPUT);
  Serial.println("[PIR] Initialized on GPIO " + String(PIR_PIN));
  
  radarSerial.begin(115200, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  delay(500);
  
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
  
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(500);
  
  if (!sgp30.begin(&Wire)) {
    Serial.println("[SGP30] ERROR: Sensor not found! (Continuing anyway...)");
    sgp30_initialized = false;
  } else {
    sgp30_initialized = true;
    Serial.print("[SGP30] Found serial #");
    Serial.print(sgp30.serialnumber[0], HEX);
    Serial.print(sgp30.serialnumber[1], HEX);
    Serial.println(sgp30.serialnumber[2], HEX);
  }
  
  Serial.println("\n[FUSION] Warming up... (1Hz measurement cycle)\n");
  Serial.println("========================================");

  // Metrics setup
  confusion_print_csv_header();

Serial.println("DATA,Timestamp,Occupied,GroundTruth,mOccupied,mEmpty,mUnknown,ConflictK,TP,TN,FP,FN,Accuracy,Precision,Recall,F1_Score");  Serial.println("[METRICS] Send '1' = room occupied, '0' = room empty, 'p' = print metrics\n");
}

void loop() {
  loop_network(); // Run background network tasks continuously

  // Check for ground-truth commands every loop iteration (not gated by 1Hz timer)
  checkSerialGroundTruth();

  static unsigned long lastFusionMs = 0;

  // Verifica il timeout del tag del personale (se è uscito dalla stanza)
  if (staff_present && (millis() - lastStaffDetectTime > DEPARTURE_TIMEOUT)) {
      staff_present = false;
      current_staff_rssi = -100;
      Serial.println("\n[BLE] STAFF DEPARTED! Tag Timeout elapsed. Resuming normal operations.\n");
  }

  if (millis() - lastFusionMs < 1000) {
    return;
  }
  lastFusionMs = millis();

  const SensorEvidence evidence = readSensorsTemplate();
  FusionResult result = runDOWA_DST_Fusion(evidence);

  // LOGICA FREEZE STAFF TAG OVERRIDE
  if (staff_present) {
      result.occupied = saved_occupancy_state; 
  } else {
      saved_occupancy_state = result.occupied;
  }

  // ==========================================================
  // METRICS: update confusion matrix + CSV row
  // Skipped during staff override, since that scenario does
  // not represent a guest occupancy ground-truth condition.
  // ==========================================================
  if (!staff_present) {
    confusion_update(cm, result.occupied, groundTruthOccupied);

    float mU = 1.0f - result.occupiedBelief - result.emptyBelief;
    confusion_print_csv_row(millis() / 1000, result.occupied, groundTruthOccupied,
                             result.occupiedBelief, result.emptyBelief, mU, result.conflictK);

   // ========================================================================
    // NUOVO: Calcolo locale delle metriche per la riga CSV estesa
    // ========================================================================
    static unsigned long tp = 0, tn = 0, fp = 0, fn = 0;
    if (result.occupied && groundTruthOccupied) tp++;
    else if (!result.occupied && !groundTruthOccupied) tn++;
    else if (result.occupied && !groundTruthOccupied) fp++;
    else if (!result.occupied && groundTruthOccupied) fn++;

    unsigned long total = tp + tn + fp + fn;
    float accuracy = (total > 0) ? (float)(tp + tn) / total : 0.0f;
    float precision = (tp + fp) > 0 ? (float)tp / (tp + fp) : 0.0f;
    float recall = (tp + fn) > 0 ? (float)tp / (tp + fn) : 0.0f;
    float f1 = (precision + recall) > 0 ? 2.0f * (precision * recall) / (precision + recall) : 0.0f;

    // Stampa la riga unica CSV super-completa
    Serial.printf("DATA,%lu,%d,%d,%0.4f,%0.4f,%0.4f,%0.4f,%lu,%lu,%lu,%lu,%0.4f,%0.4f,%0.4f,%0.4f\n", 
                  millis() / 1000, result.occupied, groundTruthOccupied, 
                  result.occupiedBelief, result.emptyBelief, mU, result.conflictK,
                  tp, tn, fp, fn, accuracy, precision, recall, f1);
    // ========================================================================
  }

  Serial.print("[");
  Serial.print(millis() / 1000);
  Serial.print("s] ");
  
  Serial.print("RADAR=");
  Serial.print(evidence.radarState);
  Serial.print(" PIR=");
  Serial.print(evidence.pirMotion ? "1" : "0");
  Serial.print(" TVOC=");
  Serial.print(evidence.tvoc);
  Serial.print("ppb eCO2=");
  Serial.print(evidence.eco2);
  Serial.print("ppm | ");
  
  if (staff_present) {
    Serial.print(" [STAFF OVERRIDE FREEZE] ");
  } else {
    Serial.print("m(O)=");
    Serial.print(result.occupiedBelief, 3);
    Serial.print(" m(E)=");
    Serial.print(result.emptyBelief, 3);
    Serial.print(" | K=");
    Serial.print(result.conflictK, 3);
  }
  
  Serial.print(" | GT=");
  Serial.print(groundTruthOccupied ? "OCC" : "EMP");
  Serial.print(" | ");
  Serial.println(result.occupied ? ">>> OCCUPIED <<<" : "EMPTY");

  // Format and send to MQTT
  String roomStatus = "empty";
  if (staff_present) {
      roomStatus = "staff"; // Trigger staff view in mockup
  } else if (result.occupied) {
      roomStatus = "guest";
  }
  
  publish_data_to_mqtt(result.occupied, roomStatus, staff_present, current_staff_rssi);

  // ==========================================================
  // METRICS: periodic summary print (every 60 cycles = ~60s)
  // ==========================================================
  static int metricsCounter = 0;
  if (++metricsCounter >= 60) {
    confusion_print(cm);
    metricsCounter = 0;
  }
}

#endif // DOWA_DST_FUSION_TEMPLATE_H