#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SGP30.h"

// Define your I2C pins based on your ESP32-S3 board
#define SDA_PIN 8 
#define SCL_PIN 9

Adafruit_SGP30 sgp;

// Track warmup phase
int warmupCounter = 0;
bool warmupComplete = false;

void setup() {
  Serial.begin(115200);
  
  // FIX: Remove while(!Serial) and use a fixed delay instead
  //delay(3000); 

  Serial.println("\n--- SGP30 Air Quality Sensor Test ---");

  // Initialize the I2C bus with specific pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize the sensor
  if (!sgp.begin(&Wire)) {
    Serial.println("ERROR: SGP30 not found! Check your wiring.");
    while (1) { delay(10); } // Halt execution if no sensor is found
  }

  Serial.print("Found SGP30 serial #");
  Serial.print(sgp.serialnumber[0], HEX);
  Serial.print(sgp.serialnumber[1], HEX);
  Serial.println(sgp.serialnumber[2], HEX);
  
  Serial.println("Initialization complete. Warming up (First 15 readings will be baseline)...");
}

void loop() {
  // Request an indoor air quality measurement
  if (!sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    return;
  }

  warmupCounter++;
  
  Serial.print("[Reading ");
  Serial.print(warmupCounter);
  Serial.print("] TVOC: "); 
  Serial.print(sgp.TVOC); 
  Serial.print(" ppb\t");

  Serial.print("eCO2: "); 
  Serial.print(sgp.eCO2); 
  Serial.print(" ppm");
  
  // Warmup detection
  if (!warmupComplete) {
    if (warmupCounter >= 15) {
      warmupComplete = true;
      Serial.println(" ✓ WARMUP COMPLETE");
    } else {
      Serial.println(" (warming up...)");
    }
  } else {
    Serial.println(" [ACTIVE]");
  }

  // The SGP30 requires a reading exactly every 1 second
  delay(1000); 
}