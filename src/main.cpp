/* #include <Arduino.h>
// #include "wide_angle_PIR_testing.h"
// #include "test_mqtt.h"
// #include "test_ble.h"
// #include "test_espnow.h"
// #include "test_structure.h"
// #include "get_mac.h"
// #include "espnow_rtt.h"
// #include "espnow_packet_loss.h"
// #include "mqtt_local_broker.h"
// #include "mqtt_local_client.h"
// #include "ble_server.h"
// #include "ble_client.h"
// #include "espnow_range_client.h"
// #include "espnow_range_server.h"
// #include "ble_janitor_tag.h"
// #include "ble_room_scanner.h"
#include "dowa_dst_fusion_template.h"
*/

#include <Arduino.h>

// Import all modularized logic
#include "network_manager.h"
#include "ble_room_scanner.h"
#include "dowa_dst_fusion_template.h"

unsigned long lastFusionEngineTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // 1. Initialize Wi-Fi, MQTT, ESP-NOW, and Memory
  setup_network_and_memory();
  
  // 2. Initialize BLE Background Scanner
  setup_room_scanner();
  
  // 3. Initialize Radar & SGP30 
  // (Assuming your friend put the radar.begin() and sgp.begin() inside this function)
  // setup_sensors_template(); 
}

void loop() {
  // 1. Run Background Network Tasks
  loop_network();
  
  // 2. Run Background BLE Tasks (Timeout & Cache clearing)
  loop_room_scanner();
  
  // 3. Fusion & Dashboard Publish (1Hz)
  if (millis() - lastFusionEngineTime >= 1000) {
     lastFusionEngineTime = millis();
     
     // ---------------------------------------------------------
     // A. Get Sensor Data (Assuming readSensorsTemplate() exists in DOWA .h)
     // SensorEvidence evidence = readSensorsTemplate();
     
     // Override the PIR data with our ESP-NOW Slave data!
     // evidence.pirMotion = pirPresence; 

     // B. Run Fusion Engine
     // FusionResult result = runDOWA_DST_Fusion(evidence);
     // bool isOccupied = result.occupied;
     // ---------------------------------------------------------
     
     // (TEMPORARY PLACEHOLDER FOR COMPILING - Replace with real DOWA result)
     bool isOccupied = pirPresence; 

     // Determine Staff Override
     String roomStatus = "empty";
     if (janitor_present) {
        roomStatus = "staff";
        isOccupied = true;
     } else if (isOccupied) {
        roomStatus = "guest";
     }
     
     // Send it to the React Mockup!
     publish_data_to_mqtt(isOccupied, roomStatus);
  }
}