/*
  wifi_teplomer2x.ino
  Program načítá dat z 2x čidla DS18B20 po zpracovani je odesila pres wifi na server Emoncms.
  Pro nastaveni Wifi a serveru Emoncms pouziva WiFiManager s ulozenim nastaveni do config souboru.
  HW: NodeMCU s ESP8266 

  Arduino IDE: 1.8.19;
  SDK:3.0.2 
  Petr Řezáč; www.pr-software.net
  19.12.2022
  
*/

#include <LittleFS.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/kentaylor/WiFiManager
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 2
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const char* CONFIG_FILE = "/config.json";

// Default configuration values
char emoncmsAddress[40] = "emoncms.org";// 192.168.xx.xx
word port = 80;
char emoncmsApiKey[35] = "xxxxxxxxxx";
char emoncmsNodeId[10] = "1";

unsigned long perKomunikace = 60; // perioda odesilani sec.
unsigned long currentMillis = 0;
unsigned long lastMillis;
unsigned long uptimeMillis = 0;
unsigned long restartMillis;
unsigned long intervalRes = 240000;

// Function Prototypes
bool readConfigFile();
bool writeConfigFile();

//=========================================== Setup ===============================
void setup() {
  // Put your setup code here, to run once
  //ESP.eraseConfig();
  WiFi.mode(WIFI_STA);
  WiFi.setOutputPower(20.5);
  WiFi.setPhyMode (WIFI_PHY_MODE_11N); //WIFI_PHY_MODE_11B,WIFI_PHY_MODE_11G,WIFI_PHY_MODE_11N
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  Serial.begin(115200);
  Serial.println("\n Starting");
  unsigned long startedAt = millis();
  Serial.println("Opening configuration portal");
 
  // Mount the filesystem
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
  }

  if (!readConfigFile()) {
    Serial.println("Failed to read configuration file, using default values");
  }

  //Remove this line if you do not want to see WiFi password printed
  // Extra parameters to be configured
  // After connecting, parameter.getValue() will get you the configured value
  // Format: <ID> <Placeholder text> <default value> <length> <custom HTML> <label placement>
  WiFiManagerParameter p_emoncmsAddress("emoncmsaddress", "EmonCMS Adresa", emoncmsAddress, 40);

  char convertedValue[4];
  sprintf(convertedValue, "%d", port);
  WiFiManagerParameter p_port("port", "Port", convertedValue, 4);
  // Thingspeak API Key - this is a straight forward string parameter
  WiFiManagerParameter p_emoncmsApiKey("emoncmsapikey", "Emoncms-API Key", emoncmsApiKey, 33);
  WiFiManagerParameter p_emoncmsNodeId("emoncmsnodeid", "Emoncms NodeId", emoncmsNodeId, 10);
    
  // Just a quick hint
  WiFiManagerParameter p_hint("<small>*Hint: if you want to reuse the currently active WiFi credentials, leave SSID and Password fields empty</small>");

  // Initialize WiFIManager
  WiFiManager wifiManager;

  //add all parameters here
  wifiManager.addParameter(&p_hint);
  wifiManager.addParameter(&p_emoncmsAddress);
  wifiManager.addParameter(&p_port);
  wifiManager.addParameter(&p_emoncmsApiKey);
  wifiManager.addParameter(&p_emoncmsNodeId);

  // set wifi AP params
  wifiManager.setWiFiAPChannel(13);
  wifiManager.setWiFiAPHidden(false);
  //IPAddress local_IP(192,168,4,22);
  //IPAddress gateway(192,168,4,1);
  //IPAddress subnet(255,255,255,0);
  //wifiManager.setAPStaticIPConfig(local_IP, gateway, subnet);
  //wifiManager.setWiFiAutoReconnect(true);  
  // Sets timeout in seconds until configuration portal gets turned off.
  // If not specified device will remain in configuration mode until
  // switched off via webserver or device is restarted.
  wifiManager.setConfigPortalTimeout(120);

  //if can reset setting remove rem 
  //LittleFS.format(); //
  //wifiManager.resetSettings();
  
  // It starts an access point
  // and goes into a blocking loop awaiting configuration.
  // Once the user leaves the portal with the exit button
  // processing will continue
  if (!wifiManager.startConfigPortal("ESP_Emoncms")) {
    Serial.println("Not connected to WiFi but continuing anyway.");
  } else {
    // If you get here you have connected to the WiFi
    Serial.println("Connected...yeey :)");
  }
  // Getting posted form values and overriding local variables parameters
  // Config file is written regardless the connection state
  strcpy(emoncmsAddress, p_emoncmsAddress.getValue());
  port = atoi(p_port.getValue());
  strcpy(emoncmsApiKey, p_emoncmsApiKey.getValue());
  strcpy(emoncmsNodeId, p_emoncmsNodeId.getValue());
    
  writeConfigFile();
  delay(5000);
  readConfigFile();

  Serial.print("After waiting ");
  int connRes = WiFi.waitForConnectResult();
  float waited = (millis()- startedAt);
  Serial.print(waited/1000);
  Serial.print(" secs in setup() connection result is ");
  Serial.println(connRes);

  if (WiFi.status()!=WL_CONNECTED){
    Serial.println("failed to connect, finishing setup anyway");
  } else{
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
  }
}

//============================================ Loop ================================
void loop() {
  //===========================================================================

  uptimeMillis = millis();
  // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
  if ((WiFi.status() != WL_CONNECTED) && (unsigned long)(uptimeMillis - restartMillis >=intervalRes)) {
    Serial.print(millis());
    Serial.println("Reconnecting to WiFi...");
    // you can restart your board
    ESP.restart();
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print('.');
      delay(1000);
    }
    Serial.println(WiFi.localIP());
    restartMillis = uptimeMillis;
  }else{
    // Odeslani dat na server Emoncms //
    currentMillis = millis();
    if ((unsigned long)(currentMillis - lastMillis >= (perKomunikace * 1000)))
    {
      Serial.print("Connecting to ");
      Serial.println(emoncmsAddress);
      // Use WiFiClient class to create TCP connections
      WiFiClient client;

      if (!client.connect(emoncmsAddress, port)) {
        Serial.println("connection failed");
        return;
      }

      // prevod RSSI na kvalitu signalu 0-100%
      long rssi = WiFi.RSSI();
      int quality = 0;
      if (rssi <= -100) {
        quality = 0;
      } else if (rssi >= -50) {
        quality = 100;
      } else {
        quality = 2 * (rssi + 100);
      }

      // Read temperature as Celsius (the default)
      sensors.requestTemperatures();
      double temp1 = sensors.getTempCByIndex(0);
      double temp2 = sensors.getTempCByIndex(1);
      Serial.print("  Temperature1 = ");
      Serial.println(temp1);
      Serial.print("  Temperature2 = ");
      Serial.println(temp2);
      Serial.print("  Wifi signal quality = ");
      Serial.println(quality);

      //Build the JSON for emoncms to send data
      String json = "{Wifi:";
      json += quality;
      json += ",Teplota1:";
      json += temp1;
      json += ",Teplota2:";
      json += temp2;
      json += "}";

      //Build emoncms URL for sending data to
      String url = "/emoncms/input/post.json?node=";
      url += "PTT_"; //Peta_;
      url += emoncmsNodeId;
      url += "&json=";
      url += json;
      url += "&apikey=";
      url += emoncmsApiKey;
      Serial.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + emoncmsAddress + "\r\n" +
                 "Connection: close\r\n\r\n");

      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + emoncmsAddress + "\r\n" +
                 "Connection: close\r\n\r\n");
      delay(10);

      ///////////////// timeout //////////////////////
      unsigned long timeout = millis();
      while (client.available() == 0) {
        if (millis() - timeout > 5000) {
          Serial.println(">>> Client Timeout !");
          client.stop();
          return;
        }
      }

      // Read all the lines of the reply from server and print them to Serial
      while (client.available()) {

        String line = client.readStringUntil('\r');
        Serial.print(line);

      }

      Serial.println();
      Serial.println("closing connection");

      lastMillis = currentMillis;
    }
  }
}
//=============================================================================
bool readConfigFile() {
  // this opens the config file in read-mode
  File f = LittleFS.open(CONFIG_FILE, "r");

  if (!f) {
    Serial.println("Configuration file not found");
    return false;
  } else {
    // we could open the file
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size]);

    // Read and store file contents in buf
    f.readBytes(buf.get(), size);
    // Closing file
    f.close();
    // Using dynamic JSON buffer which is not the recommended memory model, but anyway
    // See https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model
    DynamicJsonDocument json(1024); //upraveno na json v6
    // Parse JSON string
    auto deserializeError = deserializeJson(json, buf.get());
    serializeJson(json, Serial);
    // Test if parsing succeeds.
    if (deserializeError) {
      //Serial.println("JSON deserialize failed");
        switch (deserializeError.code()) {
          case DeserializationError::Ok:
            Serial.print("Deserialization succeeded");
          break;
          case DeserializationError::InvalidInput:
            Serial.print("Invalid input!");
          break;
          case DeserializationError::NoMemory:
            Serial.print("Not enough memory");
          break;
          default:
            Serial.print("Deserialization failed");
          break;
        }
      return false;
    }
    //json.printTo(Serial);

    // Parse all config file parameters, override
    // local config variables with parsed values
    if (json.containsKey("emoncmsAddress")) {
      strcpy(emoncmsAddress, json["emoncmsAddress"]);
    }
    if (json.containsKey("emoncmsApiKey")) {
      strcpy(emoncmsApiKey, json["emoncmsApiKey"]);
    }
    if (json.containsKey("emoncmsNodeId")) {
      strcpy(emoncmsNodeId, json["emoncmsNodeId"]);
    }

    if (json.containsKey("port")) {
      port = json["port"];
    }

  }
  Serial.println("\nConfig file was successfully parsed");
  return true;
}
//=============================================================================
bool writeConfigFile() {
  Serial.println("Saving config file");
  DynamicJsonDocument json(1024);//upraveno na json v6
  

  // JSONify local configuration parameters
  json["emoncmsAddress"] = emoncmsAddress;
  json["port"] = port;
  json["emoncmsApiKey"] = emoncmsApiKey;
  json["emoncmsNodeId"] = emoncmsNodeId;
  
  // Open file for writing
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, Serial);
  serializeJson(json, f);
  f.flush();
  f.close();

  Serial.println("\nConfig file was successfully saved");
  return true;
}
