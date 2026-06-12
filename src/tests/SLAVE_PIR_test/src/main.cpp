#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define PIR_PIN 4 

// --- MASTER'S MAC ADDRESS ---
// E.g., if MAC is 08:3A:F2:A1:B2:C3, write it like this:
uint8_t masterAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; 

// The data structure we will send to the Master
typedef struct struct_message {
  uint8_t room_id;          // <-- NEW: The Room ID badge
  bool isMotionDetected;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

void setup() {
  Serial.begin(115200);
  delay(2000); // Give USB time to connect
  
  pinMode(PIR_PIN, INPUT);

  // Set ESP32 to WiFi Station mode
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the Master ESP32 as a peer
  memcpy(peerInfo.peer_addr, masterAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  Serial.println("PIR Slave Node Ready. Sending data to Master...");
}

void loop() {
  // Read the PIR sensor
  bool currentPIRState = digitalRead(PIR_PIN);
  // Inside the Slave's loop():
  myData.room_id = 4;         // <-- NEW: Stamp the packet as Room 4
  myData.isMotionDetected = currentPIRState;

  // Send the message via ESP-NOW
  esp_err_t result = esp_now_send(masterAddress, (uint8_t *) &myData, sizeof(myData));
   
  if (result == ESP_OK) {
    Serial.println(currentPIRState ? "[PIR] MOTION" : "[PIR] STILL");
  } else {
    Serial.println("Error sending data");
  }

  delay(500); // Send an update twice a second
}

/* 
#include <Arduino.h>

// Define the pin where the PIR OUT/DATA wire is connected
#define PIR_PIN 4 

void setup() {
  // Start the Serial Monitor
  Serial.begin(115200);
  
  // Configure the PIR pin as an input
  pinMode(PIR_PIN, INPUT);

  // Give the PIR sensor a few seconds to calibrate to the room's IR signature
  Serial.println("Calibrating PIR Sensor...");
  delay(3000); 
  Serial.println("PIR Ready! Waiting for motion...");
}

void loop() {
  // Read the state of the PIR sensor
  int motionDetected = digitalRead(PIR_PIN);

  if (motionDetected == HIGH) {
    Serial.println("MOTION DETECTED!");
  } else {
    Serial.println("All clear.");
  }

  // A small delay to keep the Serial Monitor from flooding too fast
  delay(500); 
} */