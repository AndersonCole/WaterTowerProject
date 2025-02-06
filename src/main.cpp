// COMP-10184 – Mohawk College
// Water Tower Project
//
// Simulates sensors within a water tower to monitor for issues
// Publishes all data to a MQTT broker every 5 seconds
//
// @author Cole Anderson
// 
#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "wifi.h"
#include "mqtt.h"

WiFiClient espClient;
PubSubClient client(espClient);

Adafruit_AHTX0 aht;

sensors_event_t humidity, temp;
int waterLevelValue;
int tankFillPercentage;

bool waterPumpOn = false;

volatile bool eStopAlert = false;
volatile bool highWaterAlert = false;

bool remoteStopAlert = false;
bool overheated = false;

bool publishedThisSecond = false;

//testing site https://testclient-cloud.mqtt.cool
#define pumpTempTopic "COMP-10184/Project4/pumpTemperature"
#define tankLevelTopic "COMP-10184/Project4/waterTankLevel"
#define eStopTopic "COMP-10184/Project4/emergencyStop"
#define highWaterTopic "COMP-10184/Project4/highWaterWarning"
#define remoteStopTopic "COMP-10184/Project4/remoteStop"

//initializes the temp sensor
void ahtInit(){
  Serial.println("Finding temp/humidity sensor...");
  if (!aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    ESP.deepSleep(0);
  }
  Serial.println("Found!");
}

//connects to wifi
void wifiConnect(){
  WiFi.begin(ssid, password);
  Serial.println("Connecting to wifi...");
  while ( WiFi.status() != WL_CONNECTED ) {
    delay(500);
  }
  Serial.println("Connected!");
}

//handles recieving the message from the topic
void callback(char* topic, byte* payload, unsigned int length) {
  if ((char)payload[0] == '1') {
    remoteStopAlert = true;
  }
}

//connects to the mqtt broker
void mqttConnect(){
  client.setServer(mqtt_broker, mqtt_broker_port);
  client.setCallback(callback);
  Serial.println("Connecting to MQTT...");
  while (!client.connected()) {
    if (client.connect(WiFi.macAddress().c_str())) {
      Serial.println("Connected!");
      client.subscribe(remoteStopTopic);
    } else {
      Serial.println("Failed, re-trying...");
      delay(5000);
    }
  }
}

//publishes a message to a publish topic
void publishToTopic(const char* pubTopic, const char* data){
  //Serial.println("Message " + String(data) + " published to " + String(pubTopic));
  client.publish(pubTopic, data);
}

//Interrupt for the emergency stop
void IRAM_ATTR eStopInterrupt(){
  eStopAlert = true;
}

//Interrupt for the high water level detector
void IRAM_ATTR highWaterInterrupt(){
  highWaterAlert = true;
}

//initial setup, connects to wifi, mqtt broker, sets up pin inputs
void setup() {
  Serial.begin(115200);

  wifiConnect();
  mqttConnect();
  
  ahtInit();

  // D6 is emergency stop
  pinMode(D6, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(D6), eStopInterrupt, FALLING);

  // D7 is high water level detector
  pinMode(D7, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(D7), highWaterInterrupt, FALLING);

  // D4 is the LED output
  pinMode(D4, OUTPUT);
  digitalWrite(D4, HIGH);
  
  analogWriteRange(1023);

  publishToTopic(remoteStopTopic, 0);

  Serial.println("Made by Cole Anderson");
  Serial.println("Monitoring Water Tower...");
}

//reads the values of the temperature and the water level
void readInputs(){
  aht.getEvent(&humidity, &temp);
  waterLevelValue = analogRead(A0);
  tankFillPercentage = round((float(waterLevelValue)/float(1023)) * 100);

  if (temp.temperature >= 30){
    overheated = true;
  }
}

//publishes data to 4 different mqtt topics every 5 seconds
void publishMQTTData(){
  if(((millis()/1000) % 5) == 0){
    if (!publishedThisSecond){
      publishToTopic(pumpTempTopic, String(temp.temperature).c_str());
      publishToTopic(tankLevelTopic, String(tankFillPercentage).c_str());
      if (eStopAlert){
        publishToTopic(eStopTopic, "Yes");
      }else{
        publishToTopic(eStopTopic, "No");
      }
      if (highWaterAlert){
        publishToTopic(highWaterTopic, "Yes");
      }else{
        publishToTopic(highWaterTopic, "No");
      }
      publishedThisSecond = true;
    }
  }else{
    publishedThisSecond = false;
  }
}

//manages the pump state and also handles stop conditions
void loop() {
  if (!client.connected()) {
    mqttConnect();
  }
  client.loop();
  
  readInputs();

  if (!remoteStopAlert && !eStopAlert && !highWaterAlert && !overheated){
    if (tankFillPercentage > 95 && waterPumpOn){
      waterPumpOn = false;
      Serial.println("Pump Off!");
    }else if(tankFillPercentage < 5 && !waterPumpOn){
      waterPumpOn = true;
      Serial.println("Pump On!");
    }
  }else{
    if (waterPumpOn){
      waterPumpOn = false;
      Serial.println("Pump Off!");
    }
    digitalWrite(D4, LOW);
  }

  publishMQTTData();
}