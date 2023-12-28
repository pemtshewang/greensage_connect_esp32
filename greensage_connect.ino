#include <WiFi.h>
#include <WebSocketsServer.h>
#include <DHT.h>
#define DHTPIN 4

// class declaration
// DHT temperature and humidity sensor
DHT dht(DHTPIN, DHT22);

// Declarations
const int lightPin = 2; //D2
const int exFanPin = 13; //D13
const int waterValvePin = 12;  
// Station mode credentials
const char* ssid = "pem";
const char* password = "pem44444";
// AP credentials -> type your credentials for the AP WIFI
const char* my_ssid = "mywifi";
const char* my_password = "password";
// Declaration ends
// keep track of the last reading time for the DHT22 sensor
unsigned long lastReadingTime = 0; 

// local connection setup with websocket on port 81
WebSocketsServer webSocket = WebSocketsServer(81); // Create a WebSocket server on port 81
/*
Define your websocket event here and perform certain actions when specific messages are received
*/

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("Client connected");
  } else if (type == WStype_TEXT) {
    if (length > 0) {
      String message = (char *)payload;

      if (message == "light:on") {
        digitalWrite(lightPin, HIGH);
      } else if (message == "light:off") {
        digitalWrite(lightPin, LOW);
      } else if (message == "ventilationFan:on") {
        digitalWrite(exFanPin, HIGH);
      } else if (message == "ventilationFan:off") {
        digitalWrite(exFanPin, LOW);
      } else if (message == "waterValve:open") {
        digitalWrite(waterValvePin, HIGH);
      } else if (message == "waterValve:close") {
        digitalWrite(waterValvePin, LOW);
      } else {
        // Handle any other text messages here
      }
    }
  } else {
    // Handle other WebSocket event types here
  }
}

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi in Station mode
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  // Print ESP32's IP address in Station mode
  Serial.println("\nConnected to WiFi");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  pinMode(lightPin, OUTPUT);
  pinMode(exFanPin, OUTPUT);
  pinMode(waterValvePin, OUTPUT);
  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  // Create a SoftAP
  WiFi.softAP("ESP32-AP", "password");
  Serial.println("\nSoftAP created");
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  // Start DHT sensor
  dht.begin();
  delay(500); 
  // print DHT22 on initialisation
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  webSocket.broadcastTXT("temperature:" + String(temperature));
  webSocket.broadcastTXT("humidity:" + String(humidity));
  Serial.println("Temperature: " + String(temperature) + " C");
  Serial.println("Humidity: " + String(humidity) + " %");
}

void loop() {
  webSocket.loop();
  
  unsigned long currentTime = millis();
  if (currentTime - lastReadingTime >= 0.5 * 60 * 1000 && webSocket.connectedClients() > 0) {
    // Time for a reading and client is connected
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      // Send readings to all connected clients
      Serial.println("Temperature and humidity readings sent to clients");
      // send temperature on temperature:value
      webSocket.broadcastTXT("temperature:" + String(temperature));
      webSocket.broadcastTXT("humidity:" + String(humidity));
      Serial.println("Temperature and humidity readings sent to clients");
    }

    lastReadingTime = currentTime;  // Update last reading time
  }
}
