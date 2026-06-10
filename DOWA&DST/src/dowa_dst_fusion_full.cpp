#include <Arduino.h>
#include <time.h>

/*
  MOD/DOWA Fusion Engine - Full Implementation
  True Presence Detection using Dempster-Shafer Theory
  
  Sensors:
  - PIR (0.25 day / 0.10 night)
  - mmWave LD2410 (0.35 day / 0.50 night)
  - CO₂ SCD41 (0.20 day / 0.25 night)
  - Temperature SHT41 (0.10 day / 0.08 night)
  - Humidity SHT41 (0.10 day / 0.07 night)
*/

// ============================================================================
// STRUCTURES
// ============================================================================

struct MassFunction {
  float occupied;
  float empty;
  float unknown;
};

struct SensorReading {
  // PIR
  bool pirMotion;
  unsigned long pirLastTriggerTime;
  uint32_t pirTriggerCount60s;
  
  // mmWave LD2410
  uint8_t mmWaveState; // 0=None, 1=Moving, 2=Stationary, 3=?
  unsigned long mmWaveLastReadTime;
  
  // CO₂
  uint16_t co2Current;
  uint16_t co2Baseline;
  unsigned long lastStateChangeTime;
  
  // Temperature
  float tempCurrent;
  float tempBaseline;
  
  // Humidity
  float humCurrent;
  float humBaseline;
};

struct FusionState {
  MassFunction dowa;
  float conflictK;
  bool occupied;
  uint8_t cyclesAboveThreshold;
  uint8_t cyclesBelowThreshold;
  bool lastOccupancyOutput;
};

// ============================================================================
// CONSTANTS
// ============================================================================

// Day/Night mode
#define NIGHT_START_HOUR 23
#define NIGHT_END_HOUR 7

// Weights (day mode)
static constexpr float w_pir_day = 0.25f;
static constexpr float w_mmwave_day = 0.35f;
static constexpr float w_co2_day = 0.20f;
static constexpr float w_temp_day = 0.10f;
static constexpr float w_hum_day = 0.10f;

// Weights (night mode)
static constexpr float w_pir_night = 0.10f;
static constexpr float w_mmwave_night = 0.50f;
static constexpr float w_co2_night = 0.25f;
static constexpr float w_temp_night = 0.08f;
static constexpr float w_hum_night = 0.07f;

// CO₂ thresholds (ppm delta from baseline)
static constexpr float CO2_DELTA_50 = 50.0f;
static constexpr float CO2_DELTA_150 = 150.0f;
static constexpr float CO2_DELTA_300 = 300.0f;
static constexpr float CO2_DELTA_500 = 500.0f;
static constexpr float CO2_LAG_PENALTY = 0.5f;
static constexpr unsigned long CO2_LAG_DURATION_MS = 180000;

// Temperature thresholds (°C delta)
static constexpr float TEMP_DELTA_03 = 0.3f;
static constexpr float TEMP_DELTA_07 = 0.7f;
static constexpr float TEMP_DELTA_12 = 1.2f;

// Humidity thresholds (%RH delta)
static constexpr float HUM_DELTA_15 = 1.5f;
static constexpr float HUM_DELTA_30 = 3.0f;
static constexpr float HUM_DELTA_60 = 6.0f;

// PIR decay timing
static constexpr unsigned long PIR_HOLD_MS = 30000;
static constexpr unsigned long PIR_DECAY_MAX_MS = 120000;
static constexpr unsigned long PIR_NO_TRIGGER_THRESHOLD_MS = 120000;

// Decision threshold
static constexpr float OCCUPANCY_THRESHOLD = 0.50f;

// Hysteresis
static constexpr uint8_t HYSTERESIS_UP_CYCLES = 2;      // 10s (2 × 5s)
static constexpr uint8_t HYSTERESIS_DOWN_CYCLES = 6;    // 30s (6 × 5s)

// Sensor timeouts
static constexpr unsigned long SENSOR_TIMEOUT_MS = 60000;

// ============================================================================
// GLOBAL STATE
// ============================================================================

SensorReading sensorData;
FusionState fusionState;
unsigned long lastFusionCycleTime = 0;
static constexpr unsigned long FUSION_CYCLE_MS = 5000; // 5 seconds

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

bool isNightMode() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int hour = timeinfo->tm_hour;
  
  return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
}

void getWeights(float &w_pir, float &w_mmwave, float &w_co2, float &w_temp, float &w_hum) {
  if (isNightMode()) {
    w_pir = w_pir_night;
    w_mmwave = w_mmwave_night;
    w_co2 = w_co2_night;
    w_temp = w_temp_night;
    w_hum = w_hum_night;
  } else {
    w_pir = w_pir_day;
    w_mmwave = w_mmwave_day;
    w_co2 = w_co2_day;
    w_temp = w_temp_day;
    w_hum = w_hum_day;
  }
}

// ============================================================================
// MASS FUNCTION COMPUTATION
// ============================================================================

MassFunction computePIRMass() {
  unsigned long now = millis();
  unsigned long timeSinceLastTrigger = now - sensorData.pirLastTriggerTime;
  
  // Active trigger (motion detected)
  if (sensorData.pirMotion) {
    return {0.90f, 0.00f, 0.10f};
  }
  
  // Hold period (0–30s after last trigger)
  if (timeSinceLastTrigger <= PIR_HOLD_MS) {
    return {0.75f, 0.00f, 0.25f};
  }
  
  // Decay period (30–120s after last trigger)
  if (timeSinceLastTrigger <= PIR_DECAY_MAX_MS) {
    float progress = (timeSinceLastTrigger - PIR_HOLD_MS) / (float)(PIR_DECAY_MAX_MS - PIR_HOLD_MS);
    float decayedOccupied = 0.75f * (1.0f - progress);
    float decayedUnknown = 0.25f + (0.75f * progress);
    return {decayedOccupied, 0.00f, decayedUnknown};
  }
  
  // No trigger (>120s silence)
  return {0.00f, 0.30f, 0.70f};
}

MassFunction computeMMWaveMass() {
  unsigned long now = millis();
  unsigned long timeSinceRead = now - sensorData.mmWaveLastReadTime;
  
  // Check sensor timeout
  if (timeSinceRead > SENSOR_TIMEOUT_MS) {
    return {0.00f, 0.00f, 1.00f};
  }
  
  switch (sensorData.mmWaveState) {
    case 1: // Moving
      return {0.95f, 0.00f, 0.05f};
    case 2: // Stationary (breathing/heartbeat)
      return {0.85f, 0.00f, 0.15f};
    default: // None (0)
      return {0.05f, 0.70f, 0.25f};
  }
}

MassFunction computeCO2Mass() {
  unsigned long now = millis();
  unsigned long timeSinceStateChange = now - sensorData.lastStateChangeTime;
  
  float delta = (float)sensorData.co2Current - (float)sensorData.co2Baseline;
  
  MassFunction base;
  if (delta < CO2_DELTA_50) {
    base = {0.00f, 0.60f, 0.40f};
  } else if (delta < CO2_DELTA_150) {
    base = {0.20f, 0.30f, 0.50f};
  } else if (delta < CO2_DELTA_300) {
    base = {0.55f, 0.10f, 0.35f};
  } else if (delta < CO2_DELTA_500) {
    base = {0.80f, 0.00f, 0.20f};
  } else {
    base = {0.92f, 0.00f, 0.08f};
  }
  
  // Apply lag penalty for first 180s
  if (timeSinceStateChange < CO2_LAG_DURATION_MS) {
    float lagPenalty = CO2_LAG_PENALTY;
    float remainder = (1.0f - lagPenalty) / 2.0f;
    base.occupied = base.occupied * lagPenalty + remainder;
    base.empty = base.empty * lagPenalty + remainder;
    base.unknown = base.unknown * lagPenalty + remainder;
    // Normalize
    float sum = base.occupied + base.empty + base.unknown;
    if (sum > 0.001f) {
      base.occupied /= sum;
      base.empty /= sum;
      base.unknown /= sum;
    }
  }
  
  return base;
}

MassFunction computeTemperatureMass() {
  float deltaT = sensorData.tempCurrent - sensorData.tempBaseline;
  
  if (deltaT < TEMP_DELTA_03) {
    return {0.00f, 0.30f, 0.70f};
  } else if (deltaT < TEMP_DELTA_07) {
    return {0.20f, 0.10f, 0.70f};
  } else if (deltaT < TEMP_DELTA_12) {
    return {0.45f, 0.00f, 0.55f};
  } else {
    return {0.65f, 0.00f, 0.35f};
  }
}

MassFunction computeHumidityMass() {
  float deltaRH = sensorData.humCurrent - sensorData.humBaseline;
  
  if (deltaRH < HUM_DELTA_15) {
    return {0.00f, 0.25f, 0.75f};
  } else if (deltaRH < HUM_DELTA_30) {
    return {0.15f, 0.10f, 0.75f};
  } else if (deltaRH < HUM_DELTA_60) {
    return {0.40f, 0.00f, 0.60f};
  } else {
    return {0.60f, 0.00f, 0.40f};
  }
}

// ============================================================================
// DEMPSTER-SHAFER COMBINATION
// ============================================================================

MassFunction combineTwo(const MassFunction &a, const MassFunction &b, float &outK) {
  // Calculate conflict K
  float k = (a.occupied * b.empty) + (a.empty * b.occupied);
  outK = k;
  
  float safeDenominator = 1.0f - k;
  
  // Handle conflict near 1.0
  if (safeDenominator <= 0.0001f) {
    return {0.0f, 0.0f, 1.0f};
  }
  
  MassFunction result;
  result.occupied = ((a.occupied * b.occupied) + (a.occupied * b.unknown) + (a.unknown * b.occupied)) / safeDenominator;
  result.empty = ((a.empty * b.empty) + (a.empty * b.unknown) + (a.unknown * b.empty)) / safeDenominator;
  result.unknown = ((a.unknown * b.unknown)) / safeDenominator;
  
  return result;
}

// ============================================================================
// FUSION ENGINE
// ============================================================================

void runDOWAFusion() {
  unsigned long now = millis();
  
  // Check fusion cycle timing
  if (now - lastFusionCycleTime < FUSION_CYCLE_MS) {
    return;
  }
  lastFusionCycleTime = now;
  
  // Get time-of-day weights
  float w_pir, w_mmwave, w_co2, w_temp, w_hum;
  getWeights(w_pir, w_mmwave, w_co2, w_temp, w_hum);
  
  // Compute individual sensor masses
  MassFunction m_pir = computePIRMass();
  MassFunction m_mmwave = computeMMWaveMass();
  MassFunction m_co2 = computeCO2Mass();
  MassFunction m_temp = computeTemperatureMass();
  MassFunction m_hum = computeHumidityMass();
  
  // DOWA Weighted Aggregation
  float totalWeight = w_pir + w_mmwave + w_co2 + w_temp + w_hum;
  float norm_pir = w_pir / totalWeight;
  float norm_mmwave = w_mmwave / totalWeight;
  float norm_co2 = w_co2 / totalWeight;
  float norm_temp = w_temp / totalWeight;
  float norm_hum = w_hum / totalWeight;
  
  MassFunction m_dowa;
  m_dowa.occupied = (norm_pir * m_pir.occupied) + (norm_mmwave * m_mmwave.occupied) + 
                    (norm_co2 * m_co2.occupied) + (norm_temp * m_temp.occupied) + (norm_hum * m_hum.occupied);
  m_dowa.empty = (norm_pir * m_pir.empty) + (norm_mmwave * m_mmwave.empty) + 
                 (norm_co2 * m_co2.empty) + (norm_temp * m_temp.empty) + (norm_hum * m_hum.empty);
  m_dowa.unknown = 1.0f - m_dowa.occupied - m_dowa.empty;
  
  // Dempster-Shafer combination (sequential pairs with K tracking)
  float maxK = 0.0f;
  MassFunction combined = m_dowa;
  
  // Combine in order: DOWA-base → PIR → mmWave → CO2 → Temp → Hum
  float tempK = 0.0f;
  MassFunction temp1 = combineTwo(combined, m_pir, tempK);
  maxK = max(maxK, tempK);
  combined = temp1;
  
  MassFunction temp2 = combineTwo(combined, m_mmwave, tempK);
  maxK = max(maxK, tempK);
  combined = temp2;
  
  MassFunction temp3 = combineTwo(combined, m_co2, tempK);
  maxK = max(maxK, tempK);
  combined = temp3;
  
  MassFunction temp4 = combineTwo(combined, m_temp, tempK);
  maxK = max(maxK, tempK);
  combined = temp4;
  
  MassFunction temp5 = combineTwo(combined, m_hum, tempK);
  maxK = max(maxK, tempK);
  combined = temp5;
  
  // Store fused result and conflict
  fusionState.dowa = combined;
  fusionState.conflictK = maxK;
  
  // Decision rule with hysteresis
  bool beliefAboveThreshold = (combined.occupied >= OCCUPANCY_THRESHOLD);
  
  if (beliefAboveThreshold) {
    fusionState.cyclesAboveThreshold++;
    fusionState.cyclesBelowThreshold = 0;
    
    if (fusionState.cyclesAboveThreshold >= HYSTERESIS_UP_CYCLES) {
      fusionState.occupied = true;
    }
  } else {
    fusionState.cyclesBelowThreshold++;
    fusionState.cyclesAboveThreshold = 0;
    
    if (fusionState.cyclesBelowThreshold >= HYSTERESIS_DOWN_CYCLES) {
      fusionState.occupied = false;
    }
  }
}

// ============================================================================
// PLACEHOLDER SENSOR READS
// ============================================================================

void readAllSensors() {
  // PIR
  sensorData.pirMotion = false; // Replace with actual read
  // sensorData.pirLastTriggerTime updated on interrupt
  
  // mmWave
  sensorData.mmWaveState = 2; // Replace with actual read (0/1/2)
  sensorData.mmWaveLastReadTime = millis();
  
  // CO₂ (ppm)
  sensorData.co2Current = 520; // Replace with actual SCD41 read
  sensorData.co2Baseline = 450; // Update dynamically during confirmed EMPTY
  
  // Temperature (°C)
  sensorData.tempCurrent = 22.5f; // Replace with actual SHT41 read
  sensorData.tempBaseline = 21.2f;
  
  // Humidity (%RH)
  sensorData.humCurrent = 52.0f; // Replace with actual SHT41 read
  sensorData.humBaseline = 48.0f;
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize state
  fusionState.occupied = false;
  fusionState.cyclesAboveThreshold = 0;
  fusionState.cyclesBelowThreshold = 0;
  fusionState.lastOccupancyOutput = false;
  fusionState.conflictK = 0.0f;
  sensorData.lastStateChangeTime = millis();
  sensorData.mmWaveLastReadTime = millis();
  sensorData.co2Baseline = 450;
  sensorData.tempBaseline = 21.0f;
  sensorData.humBaseline = 48.0f;
  
  Serial.println("\n=== MOD/DOWA Fusion Engine Ready ===");
  Serial.println("Measurement cycle: 5 seconds");
  Serial.println("Night mode: 23:00–07:00");
}

void loop() {
  // Read all sensors
  readAllSensors();
  
  // Run fusion
  runDOWAFusion();
  
  // Print diagnostics every cycle
  Serial.print("[");
  Serial.print(isNightMode() ? "NIGHT" : "DAY");
  Serial.print("] PIR=");
  Serial.print(sensorData.pirMotion ? "1" : "0");
  Serial.print(" mmWave=");
  Serial.print(sensorData.mmWaveState);
  Serial.print(" CO2=");
  Serial.print(sensorData.co2Current);
  Serial.print("ppm(Δ");
  Serial.print((int)(sensorData.co2Current - sensorData.co2Baseline));
  Serial.print(") T=");
  Serial.print(sensorData.tempCurrent, 1);
  Serial.print("°C RH=");
  Serial.print(sensorData.humCurrent, 1);
  Serial.print("%");
  
  Serial.print(" | m(O)=");
  Serial.print(fusionState.dowa.occupied, 3);
  Serial.print(" m(E)=");
  Serial.print(fusionState.dowa.empty, 3);
  Serial.print(" m(U)=");
  Serial.print(fusionState.dowa.unknown, 3);
  
  Serial.print(" | K=");
  Serial.print(fusionState.conflictK, 3);
  
  Serial.print(" | Hysteresis[");
  Serial.print(fusionState.cyclesAboveThreshold);
  Serial.print("/");
  Serial.print(fusionState.cyclesBelowThreshold);
  Serial.print("]");
  
  Serial.print(" → ");
  Serial.println(fusionState.occupied ? "OCCUPIED" : "EMPTY");
  
  delay(100); // Small delay to avoid blocking
}
