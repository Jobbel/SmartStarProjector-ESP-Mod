#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

//PIN configuration
#define R_PIN 4
#define G_PIN 12
#define B_PIN 14

#define MOTOR_PIN 13
#define LASER_PIN 5

#define RED_STATUS_PIN 15
#define BLUE_STATUS_PIN 0
#define BUTTON_PIN 16

#define getButtonState !digitalRead(BUTTON_PIN)

//WIFI configuration
#define wifi_ssid ""
#define wifi_password ""

//MQTT configuration
#define mqtt_server ""
#define mqtt_client_id ""
#define STATE_TOPIC ""
#define CMD_TOPIC ""

//MQTT client
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

//Necesary to make Arduino Software autodetect OTA device
WiFiServer TelnetServer(8266);

struct State {
  bool power_state = false;
  bool status_led = true;
  uint16_t laser;
  uint16_t motor;
  uint16_t red;
  uint16_t green;
  uint16_t blue;
};

State state;
bool last_button;


void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.print(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("OK");
  Serial.print("   IP address: ");
  Serial.println(WiFi.localIP());
}


void setup() {
  Serial.begin(115200);
  Serial.println("\r\nBooting...");

  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(LASER_PIN, OUTPUT);
  pinMode(RED_STATUS_PIN, OUTPUT);
  pinMode(BLUE_STATUS_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT);

  updateOutput(); //make sure everything is turned off to begin with
  //analogWriteFreq(40000); //original board uses 1khz as does the esp8266 in default mode

  setup_wifi();

  Serial.print("Configuring OTA device...");
  TelnetServer.begin();   //Necesary to make Arduino Software autodetect OTA device
  ArduinoOTA.onStart([]() {
    Serial.println("OTA starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA update finished!");
    Serial.println("Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA in progress: %u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OK");

  Serial.println("Configuring MQTT server...");
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
  Serial.printf("   Server IP: %s\r\n", mqtt_server);
  Serial.print("   Cliend Id: ");
  Serial.println(mqtt_client_id);
  Serial.println("   MQTT configured!");

  Serial.println("Setup completed! Running app...");
}


void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqtt_client.connect(mqtt_client_id)) {
      Serial.println("connected");
      mqtt_client.subscribe(CMD_TOPIC);
      publishState(); // publish current state once connected
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void loop() {

  ArduinoOTA.handle();

  if (!mqtt_client.connected()) {
    mqtt_reconnect();
  }
  mqtt_client.loop();

  if (getButtonState == true && last_button == false) {
    state.power_state = !state.power_state;
    updateOutput();
    publishState();
  }
  last_button = getButtonState;

  yield();
  delay(1); // give back
}


void callback(char* topic, byte * payload, unsigned int length) {
  // check for messages on subscribed topics
  payload[length] = '\0';
  Serial.print("Topic: ");
  Serial.println(String(topic));
  String value = String((char*)payload);

  if (String(topic) == CMD_TOPIC) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, value);
    JsonObject cmd = doc.as<JsonObject>();

    if (cmd.containsKey("power_state") && cmd["power_state"].is<bool>()) {
      state.power_state = cmd["power_state"];
    }
    if (cmd.containsKey("status_led") && cmd["status_led"].is<bool>()) {
      state.status_led = cmd["status_led"];
    }
    if (cmd.containsKey("motor") && cmd["motor"].is<int>()) {
      state.motor = cmd["motor"];
    }
    if (cmd.containsKey("laser") && cmd["laser"].is<int>()) {
      state.laser = cmd["laser"];
    }
    if (cmd.containsKey("red") && cmd["red"].is<int>()) {
      state.red = cmd["red"];
    }
    if (cmd.containsKey("green") && cmd["green"].is<int>()) {
      state.green = cmd["green"];
    }
    if (cmd.containsKey("blue") && cmd["blue"].is<int>()) {
      state.blue = cmd["blue"];
    }
    updateOutput();
    publishState();
  }
}

void publishState() {
  StaticJsonDocument<200> root;

  root["power_state"] = state.power_state;
  root["status_led"] = state.status_led;
  root["motor"] = state.motor;
  root["laser"] = state.laser;
  root["red"] = state.red;
  root["green"] = state.green;
  root["blue"] = state.blue;

  // use it as JSON
  char data[200];
  serializeJson(root, data);

  // mqtt report
  mqtt_client.publish(STATE_TOPIC, data);
}


void updateOutput() {
  if (state.power_state) {
    digitalWrite(BLUE_STATUS_PIN, !state.status_led);
    analogWrite(MOTOR_PIN, constrain(map(state.motor, 0, 254, 153, 512), 153, 512));
    analogWrite(LASER_PIN, constrain(map(state.laser, 0, 254, 922, 0), 0, 922));
    analogWrite(R_PIN, constrain(map(state.red, 0, 254, 1023, 0), 0, 1023));
    analogWrite(G_PIN, constrain(map(state.green, 0, 254, 1023, 0), 0, 1023));
    analogWrite(B_PIN, constrain(map(state.blue, 0, 254, 1023, 0), 0, 1023));
  } else {
    digitalWrite(R_PIN, HIGH);
    digitalWrite(G_PIN, HIGH);
    digitalWrite(B_PIN, HIGH);
    analogWrite(MOTOR_PIN, 153); //original board does this too
    digitalWrite(LASER_PIN, HIGH);
    digitalWrite(RED_STATUS_PIN, HIGH);
    digitalWrite(BLUE_STATUS_PIN, HIGH);
  }
}