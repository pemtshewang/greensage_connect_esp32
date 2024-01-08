#include <WiFi.h>
#include <WebSocketsServer.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "RTClib.h"
#include <Preferences.h>
#define DHTPIN 4

RTC_DS3231 rtc;
WiFiClient espClient;
PubSubClient client(espClient);
WebSocketsServer webSocket = WebSocketsServer(80);
Preferences prefs;
DHT dht(DHTPIN, DHT22);

// mqtt config
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
// mqtt ends

// pin config
const int lightPin = 2;         // D2
const int exFanPin = 13;        // D13
const int soilMoisturePin = 34;
const int waterValvePin = 4;    // D4
// pin config

const char* ssid = "pem";
const char* password = "pem44444";

const int dryValue = 850;
const int wetValue = 200;
bool isFanManuallyOn = false;
bool isWaterValveManuallyOn = false;
bool isWaterValveScheduled = false;

// soil moisture conversion 
int getMoisturePercentage(int sensorValue) {
  return map(sensorValue, dryValue, wetValue, 0, 100);
}

void storeScheduledDates(String slotNumber, String startTime, String endTime) {
  Serial.println("Storing scheduled dates");
  prefs.begin("slot1", false);
  prefs.putString("start", startTime);
  prefs.putString("end", endTime);
  prefs.end();
}

String getStartDate(){
  prefs.begin("slot1", false);
  String startDate = prefs.getString("start");
  prefs.end();
  return startDate;
}

String getEndDate(){
  prefs.begin("slot1", false);
  String endDate = prefs.getString("end");
  prefs.end();
  return endDate;
}

unsigned long lastReadingTime = 0;

void handleDeviceControl(const String& topic, const String& message) {
  if (topic == "light") {
    digitalWrite(lightPin, (message == "on") ? HIGH : LOW);
  } else if (topic == "ventilationFan") {
    isFanManuallyOn = message == "on";
    digitalWrite(exFanPin, (message == "on") ? HIGH : LOW);
  } else if (topic == "waterValve") {
    isWaterValveManuallyOn = message == "open";
    digitalWrite(waterValvePin, (message == "open") ? HIGH : LOW);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String receivedTopic = String(topic);
  String receivedPayload;
  for (int i = 0; i < length; i++) {
    receivedPayload += (char)payload[i];
  }
  handleDeviceControl(receivedTopic, receivedPayload);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("Connected to WebSocket");
  } else if (type == WStype_TEXT) {
    if (length > 0) { 
      String message = (char *)payload;
      if (message == "ping") {
        // Respond to 'ping' with 'pong'
        webSocket.sendTXT(num, "pong");
        // if the message recieved starts from "threshold:" then update the threshold valueh
      }else if(message.startsWith("threshold:")){
        // the message will be threshold:temperature:value
        String thresholdType = message.substring(message.indexOf(':') + 1, message.lastIndexOf(':'));
        float thresholdValue = message.substring(message.lastIndexOf(':') + 1).toFloat();
        prefs.begin("my-app", false);
        if(thresholdType == "temperature"){
          prefs.putFloat("temp", thresholdValue);
        }else if(thresholdType == "humidity"){
          prefs.putFloat("hum", thresholdValue);
        }else if(thresholdType == "soilMoisture"){
          prefs.putFloat("soil", thresholdValue);
        }
        prefs.end();
      }else if(message.startsWith("schedule|")) {
        String scheduleInfo = message.substring(9); // Skip "schedule|"
        int firstDelimiterPos = scheduleInfo.indexOf('|');
        int secondDelimiterPos = scheduleInfo.indexOf('|', firstDelimiterPos + 1);

        String slotNumber = scheduleInfo.substring(0, firstDelimiterPos);
        String startTime = scheduleInfo.substring(firstDelimiterPos + 1, secondDelimiterPos);
        String endTime = scheduleInfo.substring(secondDelimiterPos + 1);

        Serial.println("Slot: " + slotNumber);
        Serial.println("Start Time: " + startTime);
        Serial.println("End Time: " + endTime);

        storeScheduledDates(slotNumber, startTime, endTime);
      }else{
        String device = message.substring(0, message.indexOf(':'));
        String action = message.substring(message.indexOf(':') + 1);
        String topic = device;
        handleDeviceControl(topic, action);
      }
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
  prefs.begin("my-app", false);
  float defaultThreshold = 25.0;
  prefs.putFloat("temp", defaultThreshold);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  WiFi.softAP("ESP32-Access-Point", "123456789");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  pinMode(lightPin, OUTPUT);
  pinMode(exFanPin, OUTPUT);
  pinMode(waterValvePin, OUTPUT);
  pinMode(soilMoisturePin, INPUT);

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
  float settedThreshold = prefs.getFloat("temp");
  prefs.end();
  delay(500);
}

void loop() {
  client.loop();
  webSocket.loop();
  prefs.begin("my-app", false);
  float tempThreshold = prefs.getFloat("temp");
  prefs.end();

  String startDate = getStartDate();
  String endDate = getEndDate();
  Serial.println("Start date: " + startDate);
  Serial.println("End date: " + endDate);

  DateTime now = rtc.now();
  String isoformat = now.timestamp(DateTime::TIMESTAMP_FULL);
  Serial.println("Current time: " + isoformat);
  // check if startDate is not empyt and end date is not empty
  if(startDate != "" && endDate != ""){
    // check if the current time is between the start and end time
    if(isoformat >= startDate && isoformat <= endDate){
      Serial.println("Water valve is open due to scheduled time");
      digitalWrite(waterValvePin, HIGH);
      isWaterValveScheduled = true;
    }else{
      if(!isWaterValveManuallyOn && !isFanManuallyOn && startDate >= isoformat && endDate >= isoformat && isWaterValveScheduled){
          digitalWrite(waterValvePin, LOW);
          isWaterValveScheduled = false;
          Serial.println("Water valve is closed due to scheduled time");
      }
    }
  }
  
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if(temperature > tempThreshold && !isnan(temperature)){
    Serial.println("Temperature is greater than threshold");
    digitalWrite(exFanPin, HIGH);
  }
  Serial.println("Temperature: " + String(temperature));
  
  if(temperature < tempThreshold){
    if(!isFanManuallyOn){
      digitalWrite(exFanPin, LOW);
    }
  }

  unsigned long currentTime = millis();
  if (currentTime - lastReadingTime >= 0.5 * 60 * 1000 && webSocket.connectedClients() > 0) {
    // the readings are sent after 30secs from the current elapsed time
    int soilMoisture = getMoisturePercentage(analogRead(soilMoisturePin));
    if (!isnan(temperature) && !isnan(humidity && !isnan(soilMoisture))) {
      webSocket.broadcastTXT("temperature:" + String(temperature));
      webSocket.broadcastTXT("humidity:" + String(humidity));
      webSocket.broadcastTXT("soilMoisture:"+ String(soilMoisture));
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }
    lastReadingTime = currentTime;
  }
  delay(1000);
}
