/* #include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define PIR_PIN 4 

// --- MASTER'S MAC ADDRESS ---
// E.g., if MAC is 08:3A:F2:A1:B2:C3, write it like this:
uint8_t masterAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; 

// The data structure we will send to the Master
typedef struct struct_message {
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

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define PIR_PIN 4 

// ---> PUT YOUR MASTER MAC ADDRESS HERE <---
uint8_t masterAddress[] = {0xA0, 0x85, 0xE3, 0xE3, 0x5B, 0xAC}; 

typedef struct struct_message {
  uint8_t room_id; 
  bool isMotionDetected; 
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// --- ROBUST AUTO-SCANNER VARIABLES ---
uint8_t currentChannel = 1;
bool isLocked = false;
int failCount = 0;
const int MAX_FAILURES = 10; // The 10-Strike Rule!

// Callback: Updates our lock status based on delivery success
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    isLocked = true;
    failCount = 0; // Reset our strikes!
  } else {
    failCount++;
    if (failCount >= MAX_FAILURES) {
      isLocked = false; // We officially lost the Master
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Do not connect to the internet!

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, masterAddress, 6);
  peerInfo.channel = currentChannel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  // 1. Prepare data
  myData.room_id = 104; 
  myData.isMotionDetected = digitalRead(PIR_PIN);

  // 2. Send data
  esp_now_send(masterAddress, (uint8_t *) &myData, sizeof(myData));
  
  // Give the radio 50ms to process the delivery callback
  delay(50); 

  // 3. Decide what to do next based on the lock status
  if (isLocked) {
    Serial.printf("[CHANNEL %d] LOCKED! PIR: %d | Fails: %d\n", currentChannel, myData.isMotionDetected, failCount);
    delay(450); // Stay here, broadcast at normal speed
  } else {
    Serial.printf("[CHANNEL %d] No ACK. Hopping...\n", currentChannel);
    
    // Delete peer, jump channel, add peer back
    esp_now_del_peer(masterAddress);
    
    currentChannel++;
    if (currentChannel > 13) currentChannel = 1;
    
    // Force physical antenna change
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    peerInfo.channel = currentChannel;
    esp_now_add_peer(&peerInfo);
    
    delay(50); // Scan fast! Don't wait around if the channel is wrong
  }
}