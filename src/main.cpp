#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Adafruit_SGP30.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- WIFI & MQTT CREDENTIALS ---
const char* ssid = "DreiPhone";         
const char* password = "AVANTISAVOIA"; 
const char* mqtt_server = "172.20.10.8"; // UPDATE THIS TO YOUR LAPTOP IP!
const char* mqtt_topic = "vda-telkonet/team7/room4b";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- HARDWARE PINS ---
#define SDA_PIN 8 
#define SCL_PIN 9
#define RX_PIN 16
#define TX_PIN 17
HardwareSerial mmWave(1); 

// --- GLOBAL VARIABLES ---
Adafruit_SGP30 sgp;
unsigned long lastSGP30ReadTime = 0;
unsigned long lastFusionTime = 0;
unsigned long lastMqttAttempt = 0; // For non-blocking reconnect

bool mmWavePresence = false; 
uint8_t mmWaveState = 0; 
float mmWaveDistance = 0.0;
uint16_t currentTVOC = 0;
uint16_t currenteCO2 = 400;

bool pirPresence = false;
unsigned long lastPIRUpdate = 0;
bool staffBeaconDetected = false; // BLE Override State

const uint8_t HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
int headerCount = 0;
uint8_t packet[64];
int packetLen = 0;
bool readingPacket = false;
float MAX_ROOM_DISTANCE = 2.0;

const byte REPORT_MODE_CMD[] = {
  0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 
  0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01  
};

// --- ESP-NOW CALLBACK ---

typedef struct struct_message { 
  uint8_t room_id; 
  bool isMotionDetected; 
} struct_message;

struct_message incomingData;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataBytes, int len) {
  if (len == sizeof(incomingData)) {
    memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
    
    // THE BOUNCER: Only accept data if it's from Room 4!
    if (incomingData.room_id == 4) {
      pirPresence = incomingData.isMotionDetected;
      lastPIRUpdate = millis(); 
    }
  }
}

// --- DOWA FUSION ENGINE ---
bool runDOWAFusion(bool isRadarMotion, uint16_t tvoc, bool isPirMotion) {
  float mmWaveWeight = 0.50; 
  float pirWeight = 0.35;
  float envWeight = 0.15;    
  
  float mmWaveMass = isRadarMotion ? 1.0 : 0.0;
  float pirMass = isPirMotion ? 1.0 : 0.0;
  float envMass = 0.0;
  if (tvoc > 100) { envMass = 0.5; }
  if (tvoc > 200) { envMass = 1.0; }

  return ((mmWaveMass * mmWaveWeight) + (pirMass * pirWeight) + (envMass * envWeight)) > 0.5; 
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  // Start mmWave
  mmWave.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(200); 
  mmWave.write(REPORT_MODE_CMD, sizeof(REPORT_MODE_CMD)); 

  // Start WiFi in background (NON-BLOCKING)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("WiFi connection started in background...");

  mqttClient.setServer(mqtt_server, 1883);

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
  }

  // Initialize SGP30
  Wire.begin(SDA_PIN, SCL_PIN);
  if (sgp.begin(&Wire)) {
    Serial.println("SGP30 initialized.");
  }
}

void loop() {
    // 1. CHECK WIFI
  if (WiFi.status() != WL_CONNECTED) {
    // Print this once every 2 seconds so it doesn't flood the screen
    static unsigned long lastWifiPrint = 0;
    if (millis() - lastWifiPrint > 2000) {
      Serial.println("[NETWORK] Searching for iPhone Wi-Fi...");
      lastWifiPrint = millis();
    }
  } else {
    // 2. WIFI IS CONNECTED, CHECK MQTT
    if (!mqttClient.connected()) {
      if (millis() - lastMqttAttempt > 5000) {
        lastMqttAttempt = millis();
        Serial.print("[NETWORK] Wi-Fi OK! Connecting to Mosquitto IP: ");
        Serial.print(mqtt_server);
        Serial.print(" ...");
        if (mqttClient.connect("VDA_MasterHub_77X")) {
          Serial.println(" SUCCESS!");
        } else {
          Serial.println(" FAILED. Check Windows Firewall or IP.");
        }
      }
    } else {
      mqttClient.loop();
    }
  }

  // Safety: Timeout PIR if connection lost
  if (millis() - lastPIRUpdate > 2000) pirPresence = false;

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
        float rawDistance = (packet[7] + (packet[8] << 8)) / 100.0; 
        if (rawDistance > MAX_ROOM_DISTANCE) {
          mmWaveState = 0; mmWavePresence = false;
        } else {
          mmWaveState = packet[6];
          mmWaveDistance = rawDistance;
          mmWavePresence = (mmWaveState > 0); 
        }
        readingPacket = false; 
      }
      if(packetLen >= 64) readingPacket = false;
    }
  }

  // TASK 3: FUSION & PUBLISH JSON
  if (millis() - lastFusionTime >= 1000) {
    lastFusionTime = millis();
    bool isOccupied = runDOWAFusion(mmWavePresence, currentTVOC, pirPresence);

    // Evaluate Janitor vs Guest
    String roomStatus = "empty";
    if (staffBeaconDetected) {
      roomStatus = "staff"; 
      isOccupied = true; 
    } else if (isOccupied) {
      roomStatus = "guest";
    }

    // Print to Console immediately (always works!)
    Serial.printf("[DOWA] Occupied:%d | Status:%s | TVOC:%d | Radar:%dm | PIR:%d\n", 
                   isOccupied, roomStatus.c_str(), currentTVOC, (int)mmWaveDistance, pirPresence);

    // Publish if connected
    if (mqttClient.connected()) {
      StaticJsonDocument<200> doc;
      doc["room_id"] = "VDA-4B";
      doc["status"] = roomStatus;      
      doc["is_occupied"] = isOccupied; 

      char jsonBuffer[256];
      serializeJson(doc, jsonBuffer);
      mqttClient.publish(mqtt_topic, jsonBuffer);
    }
  }
}