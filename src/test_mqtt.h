#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// --- INSERISCI I DATI DEL TUO WI-FI ---
const char* ssid = "IL_TUO_WIFI";
const char* password = "LA_TUA_PASSWORD";

// Usiamo un broker pubblico gratuito per il test
const char* mqtt_server = "broker.hivemq.com";
const char* topic_pub = "hotel/test/stanza101";

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n--- TEST CONNESSIONE MQTT ---");
  
  // 1. Connessione al Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connessione al Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n✅ Wi-Fi Connesso!");

  // 2. Connessione al Broker MQTT
  client.setServer(mqtt_server, 1883);
  while (!client.connected()) {
    Serial.print("Connessione a MQTT...");
    // Creiamo un ID client casuale
    String clientId = "ESP32Test-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("✅ Connesso al Broker!");
    } else {
      Serial.print("❌ Fallito, rc=");
      Serial.print(client.state());
      Serial.println(" Riprovo in 5 secondi...");
      delay(5000);
    }
  }
}

void loop() {
  client.loop(); // Mantiene viva la connessione

  // Creiamo il JSON come nella tua slide
  String payload = "{\"occupied\": true, \"ts\": " + String(millis()) + ", \"confidence\": 0.99}";

  // Pubblichiamo il messaggio
  Serial.print("Invio MQTT: ");
  Serial.println(payload);
  client.publish(topic_pub, payload.c_str());

  delay(3000); // Invia ogni 3 secondi
}