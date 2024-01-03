#include <WiFi.h>
#include <WebSocketsServer.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "RTClib.h"

#define DHTPIN 4

RTC_DS3231 rtc;
WiFiClient espClient;
PubSubClient client(espClient);
WebSocketsServer webSocket = WebSocketsServer(80);

DHT dht(DHTPIN, DHT22);

const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

char days[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const int lightPin = 2;         // D2
const int exFanPin = 13;        // D13
const int waterValvePin = 4;    // D4

const char* ssid = "pem";
const char* password = "pem44444";

unsigned long lastReadingTime = 0;

void handleDeviceControl(const String& topic, const String& message) {
  if (topic == "light") {
    digitalWrite(lightPin, (message == "on") ? HIGH : LOW);
  } else if (topic == "ventilationFan") {
    digitalWrite(exFanPin, (message == "on") ? HIGH : LOW);
  } else if (topic == "waterValve") {
    digitalWrite(waterValvePin, (message == "open") ? HIGH : LOW);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String receivedTopic = String(topic);
  String receivedPayload;
  for (int i = 0; i < length; i++) {
    receivedPayload += (char)payload[i];
  }
  Serial.println("Received message on topic: " + receivedTopic + " - Payload: " + receivedPayload);
  handleDeviceControl(receivedTopic, receivedPayload);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("Client connected");
  } else if (type == WStype_TEXT) {
    Serial.println("Received message on websocket");
    if (length > 0) { String message = (char *)payload;
      Serial.println(message);
      String device = message.substring(0, message.indexOf(':'));
      String action = message.substring(message.indexOf(':') + 1);
      String topic = device;
      handleDeviceControl(topic, action);
    }
  }
}

void setup() {
  Serial.begin(115200);
  rtc.begin();
  if (!rtc.begin()) {
    Serial.println("Could not find RTC! Check circuit.");
    while (1);
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());

  pinMode(lightPin, OUTPUT);
  pinMode(exFanPin, OUTPUT);
  pinMode(waterValvePin, OUTPUT);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
    if (client.connect("ESP32Client", "", "")) {
      Serial.println("Connected to MQTT broker");
      client.subscribe("light");
      client.subscribe("ventilationFan");
      client.subscribe("waterValve");
    } else {
      Serial.print("Failed to connect, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }

  dht.begin();
  delay(500);
}

void loop() {
  DateTime now = rtc.now();
  Serial.print("Time: ");
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.print(now.minute(), DEC);
  Serial.print(":");
  Serial.println(now.second(), DEC);
  Serial.print("Date: ");
  Serial.print(now.day(), DEC);
  Serial.print(" ");
  Serial.print(days[now.dayOfTheWeek()]);
  Serial.print(" ");
  Serial.print(now.month(), DEC);
  Serial.print("-");
  Serial.println(now.year(), DEC);

  webSocket.loop();
  client.loop();

  unsigned long currentTime = millis();
  if (currentTime - lastReadingTime >= 0.5 * 60 * 1000 && webSocket.connectedClients() > 0) {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (!isnan(temperature) && !isnan(humidity)) {
      webSocket.broadcastTXT("temperature:" + String(temperature));
      webSocket.broadcastTXT("humidity:" + String(humidity));
      Serial.println("Temperature and humidity readings sent to clients");
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }

    lastReadingTime = currentTime;
  }
  delay(1000);
}
