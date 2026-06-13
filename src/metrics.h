#pragma once
#include <Arduino.h>

// ============================================================================
// CONFUSION MATRIX + METRICS TRACKER
// ============================================================================
//
// Ground truth convention:
//   true  = room is OCCUPIED (ground truth)
//   false = room is EMPTY    (ground truth)
//
// Prediction convention (from FusionResult.occupied):
//   true  = system says OCCUPIED
//   false = system says EMPTY
//
// Confusion matrix:
//                    Predicted OCCUPIED   Predicted EMPTY
//  Actual OCCUPIED         TP                   FN
//  Actual EMPTY            FP                   TN
// ============================================================================

struct ConfusionMatrix {
  uint32_t tp = 0;
  uint32_t tn = 0;
  uint32_t fp = 0;
  uint32_t fn = 0;

  uint32_t total() const {
    return tp + tn + fp + fn;
  }
};

struct Metrics {
  float accuracy;
  float precision;
  float recall;
  float f1score;
  float falseNegativeRate;
  float falsePositiveRate;
};

// ----------------------------------------------------------------------
// Update the confusion matrix with one new (prediction, groundTruth) pair
// ----------------------------------------------------------------------
inline void confusion_update(ConfusionMatrix &cm, bool predicted, bool groundTruth) {
  if (groundTruth && predicted)        cm.tp++;
  else if (!groundTruth && !predicted) cm.tn++;
  else if (!groundTruth && predicted)  cm.fp++;
  else if (groundTruth && !predicted)  cm.fn++;
}

// ----------------------------------------------------------------------
// Compute all metrics from the confusion matrix
// Returns zeros safely if denominators are zero (no division by zero)
// ----------------------------------------------------------------------
inline Metrics confusion_compute_metrics(const ConfusionMatrix &cm) {
  Metrics m;

  uint32_t total = cm.total();
  m.accuracy = (total > 0) ? (float)(cm.tp + cm.tn) / (float)total : 0.0f;

  uint32_t predictedPositive = cm.tp + cm.fp;
  m.precision = (predictedPositive > 0) ? (float)cm.tp / (float)predictedPositive : 0.0f;

  uint32_t actualPositive = cm.tp + cm.fn;
  m.recall = (actualPositive > 0) ? (float)cm.tp / (float)actualPositive : 0.0f;

  m.f1score = (m.precision + m.recall > 0.0f)
                ? 2.0f * (m.precision * m.recall) / (m.precision + m.recall)
                : 0.0f;

  m.falseNegativeRate = (actualPositive > 0) ? (float)cm.fn / (float)actualPositive : 0.0f;

  uint32_t actualNegative = cm.tn + cm.fp;
  m.falsePositiveRate = (actualNegative > 0) ? (float)cm.fp / (float)actualNegative : 0.0f;

  return m;
}

// ----------------------------------------------------------------------
// Print confusion matrix + metrics to Serial in a readable format
// ----------------------------------------------------------------------
inline void confusion_print(const ConfusionMatrix &cm) {
  Metrics m = confusion_compute_metrics(cm);

  Serial.println("\n========== CONFUSION MATRIX ==========");
  Serial.print("TP=");  Serial.print(cm.tp);
  Serial.print("  FN="); Serial.print(cm.fn);
  Serial.print("  FP="); Serial.print(cm.fp);
  Serial.print("  TN="); Serial.println(cm.tn);
  Serial.print("Total samples: "); Serial.println(cm.total());

  Serial.println("------------ METRICS ------------------");
  Serial.print("Accuracy:           "); Serial.println(m.accuracy, 4);
  Serial.print("Precision:          "); Serial.println(m.precision, 4);
  Serial.print("Recall:             "); Serial.println(m.recall, 4);
  Serial.print("F1-Score:           "); Serial.println(m.f1score, 4);
  Serial.print("False Neg. Rate:    "); Serial.println(m.falseNegativeRate, 4);
  Serial.print("False Pos. Rate:    "); Serial.println(m.falsePositiveRate, 4);
  Serial.println("========================================\n");
}

// ----------------------------------------------------------------------
// Print one CSV row: timestamp, prediction, ground truth, mass values
// Use this for offline analysis in Python/Excel
// ----------------------------------------------------------------------
inline void confusion_print_csv_header() {
  Serial.println("time_s,predicted,ground_truth,m_occupied,m_empty,m_unknown,conflict_k");
}

inline void confusion_print_csv_row(uint32_t timeSec, bool predicted, bool groundTruth,
                                     float mO, float mE, float mU, float K) {
  Serial.print(timeSec);          Serial.print(",");
  Serial.print(predicted ? 1 : 0); Serial.print(",");
  Serial.print(groundTruth ? 1 : 0); Serial.print(",");
  Serial.print(mO, 4); Serial.print(",");
  Serial.print(mE, 4); Serial.print(",");
  Serial.print(mU, 4); Serial.print(",");
  Serial.println(K, 4);
}