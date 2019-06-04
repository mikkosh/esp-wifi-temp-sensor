// https://www.instructables.com/id/ESP8266-Pro-Tips/
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;

float temperature, humidity, pressure, altitude;


WiFiClient espClient;

// MQTT client
PubSubClient client(espClient);

// For creating the MQTT client id
const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
char id[17];

// This sets the ADC to read Vcc
ADC_MODE(ADC_VCC);


const char *ap_name = "ESP-SENSOR-AP";
const char *ap_password = "defaultpass"; // worth changing :)

// define your default values here, they can be changed through config portal
// length should be max size + 1
char mqtt_server[40] = "mqtt.beebotte.com";
char mqtt_port[6] = "1883";
char mqtt_token[33] = "YOUR_MQTT_AUTH_TOKEN"; // Note that this is readable in plain text through config portal
char sleep_seconds[5] = "120";
char mqtt_channel_prefix[40] = "homeautomation"; // change to correct channel

// flag for saving data
bool shouldSaveConfig = false;


// callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("-- APP START --");
  
  // Turn of wifi to save power until it's needed
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();
  delay( 1 );
    
  // Uncomment the line below to format storage resetting the settings (do if json format changes)
  // _resetSettings();

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

  // if pin 6 is high, launch the config portal
  if(digitalRead(D6) == HIGH){
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
  
    
    // starts the configuration portal (and blocks the execution)
    wifiManager.startConfigPortal(ap_name, ap_password);
  
    // if you get here you have connected to the WiFi
    Serial.println("Connected to WiFi");
  
    // read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_token, custom_mqtt_token.getValue());
    strcpy(sleep_seconds, custom_sleep_seconds.getValue());
    strcpy(mqtt_channel_prefix, custom_mqtt_channel_prefix.getValue());
    
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      Serial.println("Saving config");
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
    delay(2000);

  } else {
    
    /* Debug output
    Serial.println("local ip");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());
  
    Serial.println(mqtt_server);
    Serial.println(mqtt_port);
    Serial.println(mqtt_token);
    Serial.println(sleep_seconds);
    Serial.println(mqtt_channel_prefix);
    */
    
    bme.begin(0x76);
    
    WiFi.forceSleepWake();
    delay(1);
    WiFi.mode( WIFI_STA );
    WiFi.begin();
    while(!WiFi.isConnected()) {
       delay(100);
       Serial.print(".");
    }
    connectMqtt();
  
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
    
    delay(20);
    client.disconnect();
  }
  
  Serial.println("And now sleep!");
  ESP.deepSleep(atoi(sleep_seconds) * 1000000);
}

// resets all settings, for dev purposes
void _resetSettings() {
  SPIFFS.begin();
  SPIFFS.format();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  Serial.println("Settings reset");
  delay(5000);
  ESP.reset();
}

// generates the id for MQTT
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

// handles the MQTT connection
void connectMqtt() {
  client.setServer(strdup(mqtt_server), atoi(mqtt_port));

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

// posts one message to MQTT
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
}


void loop() {
  // nothing to loop here
}
