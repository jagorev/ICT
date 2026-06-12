#include <Arduino.h>
#include <WiFi.h>
#include <PicoMQTT.h>

PicoMQTT::Server mqtt;

void setup() {
  Serial.begin(115200);
  delay(2000);
  WiFi.softAP("Hotel_Rete_Locale", "hotel1234");
  Serial.print("✅ Rete Wi-Fi creata! IP Broker: ");
  Serial.println(WiFi.softAPIP());
  mqtt.begin();
  Serial.println("📡 SERVER MQTT: Broker Locale in ascolto...");
}

void loop() { 
  mqtt.loop(); 
}