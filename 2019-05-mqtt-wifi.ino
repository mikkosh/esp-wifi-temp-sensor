#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//#include <ESP8266mDNS.h>        // included for reset web server

#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;

float temperature, humidity, pressure, altitude;


WiFiClient espClient;
PubSubClient client(espClient);

const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
char id[17];

ADC_MODE(ADC_VCC);


//define your default values here, if there are different values in config.json, they are overwritten.
//length should be max size + 1
char mqtt_server[40] = "mqtt.beebotte.com";
char mqtt_port[6] = "1883";
char mqtt_token[33] = "YOUR_MQTT_AUTH_TOKEN"; // change to: auth token
char sleep_seconds[5] = "120";
char mqtt_channel_prefix[40] = "homeautomation"; // change to: correct channel

//flag for saving data
bool shouldSaveConfig = false;


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  if(digitalRead(D6) == HIGH){
    Serial.println("D6 HIGH: Resetting settings");
    _resetSettings();
  }
  

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_token, json["mqtt_token"]);
          strcpy(sleep_seconds, json["sleep_seconds"]);
          strcpy(mqtt_channel_prefix, json["mqtt_channel_prefix"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_token("token", "mqtt token", mqtt_token, 34);
  WiFiManagerParameter custom_sleep_seconds("sleepseconds", "sleep seconds", sleep_seconds, 5);
  WiFiManagerParameter custom_mqtt_channel_prefix("mqtt_channel_prefix", "mqtt channel prefix", mqtt_channel_prefix, 40);
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_token);
  wifiManager.addParameter(&custom_sleep_seconds);
  wifiManager.addParameter(&custom_mqtt_channel_prefix);

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ESP-SENSOR-AP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_token, custom_mqtt_token.getValue());
  strcpy(sleep_seconds, custom_sleep_seconds.getValue());
  strcpy(mqtt_channel_prefix, custom_mqtt_channel_prefix.getValue());
  
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_token"] = mqtt_token;
    json["sleep_seconds"] = sleep_seconds;
    json["mqtt_channel_prefix"] = mqtt_channel_prefix;
    
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.gatewayIP());
  Serial.println(WiFi.subnetMask());

  Serial.println(mqtt_server);
  Serial.println(mqtt_port);
  Serial.println(mqtt_token);
  Serial.println(sleep_seconds);
  Serial.println(mqtt_channel_prefix);
  
  bme.begin(0x76);

  connectMqtt();

}


void _resetSettings() {
  SPIFFS.begin();
  SPIFFS.format();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  Serial.println("Settings reset");
  delay(5000);
  ESP.reset();
}

const char * generateID()
{
  randomSeed(analogRead(0));
  int i = 0;
  for (i = 0; i < sizeof(id) - 1; i++) {
    id[i] = chars[random(sizeof(chars))];
  }
  id[sizeof(id) - 1] = '\0';

  return id;
}

void connectMqtt() {
  client.setServer(strdup(mqtt_server), atoi(mqtt_port));
  // no callback used
  //client.setCallback(callback);

  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect(generateID(), mqtt_token, "" )) {

      Serial.println("connected");

    } else {

      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);

    }
  }
}
void do_post(char* path, float val) {

  char v_str[10];
  char message[120];
  dtostrf(val, 4, 2, v_str);
  sprintf(message, "{\"data\":%s,\"write\":true}", v_str);
  client.publish(path, message);
  Serial.print("Message sent to path:*");
  Serial.print(path);
  Serial.print("* with value: *");
  Serial.print(message);
  Serial.println("*");
  //client.loop();
  //yield();
  delay(1000); // for some reason this is required by the mqtt lib, otherwise only first val will be sent
}
void loop() {
  
  int tries = 0;
  float voltage = ESP.getVcc() / 1024.00f;
  char channel_name[80] ="";
  
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  pressure = bme.readPressure() / 100.0F;
  altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);

  sprintf(channel_name, "%s/humidity", mqtt_channel_prefix);
  do_post(channel_name, humidity);

  sprintf(channel_name, "%s/temperature", mqtt_channel_prefix);
  do_post(channel_name, temperature);
  
  sprintf(channel_name, "%s/pressure", mqtt_channel_prefix);
  do_post(channel_name, pressure);

  sprintf(channel_name, "%s/altitude", mqtt_channel_prefix);
  do_post(channel_name, altitude);

  sprintf(channel_name, "%s/voltage", mqtt_channel_prefix);
  do_post(channel_name, voltage);
  
  yield();
  delay(1000);
  Serial.println("And now sleep!");
  ESP.deepSleep(atoi(sleep_seconds) * 1000000); // 120 sec sleep

}
