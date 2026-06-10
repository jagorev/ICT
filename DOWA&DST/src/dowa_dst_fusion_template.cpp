#include <Arduino.h>

/*
  Raw template: DOWA + Dempster-Shafer fusion model
  - Radar / mmWave
  - PIR
  - Air quality (TVOC / eCO2)

  Fill in the sensor acquisition code and tune the mass assignments / thresholds
  for your hardware and environment.
*/

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

static SensorEvidence readSensorsTemplate() {
  // Replace these placeholders with your real sensor reads.
  SensorEvidence evidence;
  evidence.radarMotion = false;
  evidence.pirMotion = false;
  evidence.tvoc = 0;
  evidence.eco2 = 400;
  return evidence;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("DOWA + DST fusion template ready");
}

void loop() {
  static unsigned long lastFusionMs = 0;

  if (millis() - lastFusionMs < 1000) {
    return;
  }
  lastFusionMs = millis();

  const SensorEvidence evidence = readSensorsTemplate();
  const FusionResult result = runDOWA_DST_Fusion(evidence);

  Serial.print("RADAR=");
  Serial.print(evidence.radarMotion ? "1" : "0");
  Serial.print(" PIR=");
  Serial.print(evidence.pirMotion ? "1" : "0");
  Serial.print(" TVOC=");
  
  Serial.print(evidence.tvoc);
  Serial.print(" eCO2=");
  Serial.print(evidence.eco2);
  Serial.print(" | m(O)=");
  Serial.print(result.occupiedBelief, 3);
  Serial.print(" m(E)=");
  Serial.print(result.emptyBelief, 3);
  Serial.print(" | K=");
  Serial.print(result.conflictK, 3);
  Serial.print(" | DOWA=");
  Serial.println(result.occupied ? "OCCUPIED" : "EMPTY");
}
