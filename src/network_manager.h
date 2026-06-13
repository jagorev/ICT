#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_now.h>
#include <Preferences.h>
#include <ArduinoJson.h>

const char* ssid = "DreiPhone";         
const char* password = "AVANTISAVOIA"; 
const char* mqtt_server = "172.20.10.8"; 
const char* mqtt_data_topic = "vda-telkonet/team7/room4b"; 
const char* mqtt_setup_topic = "vda-telkonet/team7/setup";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences; 

String currentRoomID = "UNASSIGNED";
unsigned long lastMqttAttempt = 0; 

// Slave (PIR) State & Health
bool pirPresence = false;
unsigned long lastPIRUpdate = 0;
bool isSlaveOnline = false;

// ESP-NOW Strict Format
typedef struct struct_message { 
  uint8_t room_id; 
  bool isMotionDetected; 
} struct_message;
struct_message incomingData;

// Catch ESP-NOW Packets
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataBytes, int len) {
  if (len == sizeof(incomingData)) {
    memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
    pirPresence = incomingData.isMotionDetected;
    lastPIRUpdate = millis(); 
    isSlaveOnline = true;
  }
}

// Catch Technician Room Setups via MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  if (String(topic) == mqtt_setup_topic) {
    currentRoomID = message;
    preferences.putString("room_id", currentRoomID); // Save to Permanent Memory
    Serial.println(">>> TECH UPDATE: Room ID permanently set to: " + currentRoomID);
  }
}

void setup_network_and_memory() {
  // Load Room ID from Memory
  preferences.begin("vda-config", false);
  currentRoomID = preferences.getString("room_id", "UNASSIGNED");
  Serial.println("Booting up... Room Assigned: " + currentRoomID);

  WiFi.mode(WIFI_STA);
  Serial.print("MASTER MAC ADDRESS: ");
  Serial.println(WiFi.macAddress());
  WiFi.begin(ssid, password);
  
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
  }
}

void loop_network() {
  // Background Reconnect Logic
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (millis() - lastMqttAttempt > 5000) {
        lastMqttAttempt = millis();
        if (mqttClient.connect("VDA_MasterHub_77X")) {
          Serial.println("[NETWORK] MQTT Connected!");
          mqttClient.subscribe(mqtt_setup_topic); // Listen for React Mockup config
        }
      }
    } else {
      mqttClient.loop();
    }
  }

  // Slave Health Monitor
  if (millis() - lastPIRUpdate > 10000) {
    isSlaveOnline = false;
    pirPresence = false;
  }
}

// Data Publisher
void publish_data_to_mqtt(bool isOccupied, String roomStatus) {
    if (mqttClient.connected()) {
      StaticJsonDocument<256> doc;
      doc["room_id"] = currentRoomID;
      doc["status"] = roomStatus;      
      doc["is_occupied"] = isOccupied; 
      doc["pir_node_status"] = isSlaveOnline ? "ONLINE" : "OFFLINE";

      char jsonBuffer[256];
      serializeJson(doc, jsonBuffer);
      mqttClient.publish(mqtt_data_topic, jsonBuffer);
      
      Serial.print("Published MQTT: ");
      Serial.println(jsonBuffer);
    }
}
#endif