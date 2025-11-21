//https://wokwi.com/projects/446100233451546625

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "Wokwi-GUEST"; //conexão gratuita disponibilizada pela wokwi
const char* password = "";

// MQTT Broker settings
const char* mqtt_server = "test.mosquitto.org";  // Broker gratuito
const int mqtt_port = 1883;
const char* mqtt_user = "";     
const char* mqtt_password = ""; 

// MQTT Topics
const char* topic_smoke_data = "smokeDetector/sensor/data";
const char* topic_smoke_alert = "smokeDetector/alert";
const char* topic_device_status = "smokeDetector/status";
const char* topic_led_control = "smokeDetector/led/control";
const char* topic_threshold_set = "smokeDetector/threshold/set";
const char* topic_device_info = "smokeDetector/info";

// Device ID
const char* device_id = "ESP32_Smoke_001";

// Pin definitions
const int MQ2_DIGITAL_PIN = 2;
const int MQ2_ANALOG_PIN = 36;
const int LED_RED_PIN = 16;
const int LED_GREEN_PIN = 17;
const int LED_BLUE_PIN = 18;

// PWM settings for ESP32
const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8;

// Smoke detection settings
int smokeThreshold = 2639;
bool lastSmokeState = false;
bool manualLEDControl = false;
unsigned long lastPublishTime = 0;
const unsigned long PUBLISH_INTERVAL = 5000;

// WiFi and MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Variáveis para medição de tempo de resposta
struct ResponseMetrics {
  unsigned long sensorReadTime;
  unsigned long ledResponseTime;
  unsigned long mqttResponseTime;
} metrics;

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando o leitor de qualidade do ar...");
  
  // Setup PWM channels for RGB LED
  setupLED();
  
  // Setup MQ2 sensor
  pinMode(MQ2_DIGITAL_PIN, INPUT);
  
  // Initialize LED (blue = connecting)
  setColor(0, 0, 255);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Setup MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  delay(20000);
  
  // Connect to MQTT
  connectToMQTT();
  
  // Publish device startup info
  publishDeviceInfo();
  
  Serial.println("Detector pronto!");
}

void loop() {
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    connectToMQTT();
  }
  mqttClient.loop();
  
  // Read sensor values
  unsigned long t1 = micros();
  int digitalValue = digitalRead(MQ2_DIGITAL_PIN);
  int analogValue = analogRead(MQ2_ANALOG_PIN);
  metrics.sensorReadTime = micros() - t1;

  bool smokeDetected = (analogValue > smokeThreshold);
  
  // Display readings
  Serial.print("Digital: ");
  Serial.print(digitalValue);
  Serial.print(" | Analógico: ");
  Serial.print(analogValue);
  Serial.print(" | Status: ");
  
  // Control LED if not in manual mode
  if (!manualLEDControl) {
    unsigned long t2 = micros();
    if (!smokeDetected) {
      Serial.println("QUALIDADE BOA");
      setColor(0, 255, 0);  // Green
    } else {
      Serial.println("QUALIDADE RUIM");
      setColor(255, 0, 0);  // Red
    }
     metrics.ledResponseTime = micros() - t2;
  } else {
    Serial.println(smokeDetected ? "SMOKE DETECTED! (Manual LED)" : "SAFE (Manual LED)");
  }
  
  // Publicação periódicoa
  if (millis() - lastPublishTime > PUBLISH_INTERVAL) {
    publishSensorData(analogValue, digitalValue, smokeDetected);
    lastPublishTime = millis();
  }
  
  // Gera alerta se há mudança de estado
  if (smokeDetected != lastSmokeState) {
    unsigned long t3 = micros();

    if (smokeDetected) {
      publishSmokeAlert(analogValue);
    }

    metrics.mqttResponseTime = micros() - t3;

    publishStatusUpdate(smokeDetected);
    lastSmokeState = smokeDetected;
  }
  
  delay(1000);
}

void setupLED() {
  ledcAttach(LED_RED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(LED_GREEN_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(LED_BLUE_PIN, PWM_FREQ, PWM_RESOLUTION);
  
  Serial.println("LED configurado");
}

void setColor(int red, int green, int blue) {
  ledcWrite(LED_RED_PIN, red);
  ledcWrite(LED_GREEN_PIN, green);
  ledcWrite(LED_BLUE_PIN, blue);
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    // Blink blue while connecting
    setColor(0, 0, 255);
    delay(200);
    setColor(0, 0, 0);
    delay(200);
  }
  
  Serial.println();
  Serial.println("WiFi conectado!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando ao broker MQTT...");
    
    if (mqttClient.connect(device_id, mqtt_user, mqtt_password)) {
      Serial.println(" conectado!");
      
      mqttClient.subscribe(topic_led_control);
      mqttClient.subscribe(topic_threshold_set);
      
      Serial.println("Subscribed to control topics");
      
      publishStatusUpdate(false);
      
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" trying again in 5 seconds");
      
      // Pisca vermelho enquanto conecta
      setColor(255, 0, 0);
      delay(2500);
      setColor(0, 0, 0);
      delay(2500);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  if (String(topic) == topic_led_control) {
    handleLEDControl(message);
  }
  
  if (String(topic) == topic_threshold_set) {
    handleThresholdSet(message);
  }
}

void handleLEDControl(String message) {
  unsigned long cmdStart = micros();

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.println("Failed to parse LED control JSON");
    return;
  }
  
  if (doc["manual"].as<bool>()) {
    manualLEDControl = true;
    int red = doc["red"] | 0;
    int green = doc["green"] | 0;
    int blue = doc["blue"] | 0;
    
    setColor(red, green, blue);

    unsigned long cmdLatency = micros() - cmdStart;

    Serial.println("LED em modo manual");

    StaticJsonDocument<150> ack;
    ack["device_id"] = device_id;
    ack["timestamp"] = millis();
    ack["command_latency_ms"] = cmdLatency / 1000.0;
    
    String jsonAck;
    serializeJson(ack, jsonAck);
    mqttClient.publish("smokeDetector/led/ack", jsonAck.c_str());

  } else {
    manualLEDControl = false;
    Serial.println("LED em modo automátoco");
  }
}

void handleThresholdSet(String message) {
  int newThreshold = message.toInt();
  if (newThreshold > 0 && newThreshold < 4095) {
    smokeThreshold = newThreshold;
    Serial.print("Limite atualizado: ");
    Serial.println(smokeThreshold);
    
    // Acknowledge threshold change
    StaticJsonDocument<100> doc;
    doc["threshold"] = smokeThreshold;
    doc["timestamp"] = millis();
    
    String jsonString;
    serializeJson(doc, jsonString);
    mqttClient.publish("smokeDetector/threshold/ack", jsonString.c_str());
  }
}

void publishSensorData(int analog, int digital, bool smokeDetected) {
  StaticJsonDocument<300> doc;
  
  doc["device_id"] = device_id;
  doc["timestamp"] = millis();
  doc["analog_value"] = analog;
  doc["digital_value"] = digital;
  doc["threshold"] = smokeThreshold;
  doc["smoke_detected"] = smokeDetected;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  mqttClient.publish(topic_smoke_data, jsonString.c_str());
}

void publishSmokeAlert(int smokeLevel) {
  StaticJsonDocument<200> doc;
  
  doc["device_id"] = device_id;
  doc["timestamp"] = millis();
  doc["alert_type"] = "SMOKE_DETECTED";
  doc["smoke_level"] = smokeLevel;
  doc["threshold"] = smokeThreshold;
  doc["location"] = "Your Location";

  JsonObject response = doc.createNestedObject("response_times_ms");
  response["sensor_read"] = metrics.sensorReadTime / 1000.0;
  response["led_response"] = metrics.ledResponseTime / 1000.0;
  response["mqtt_publish"] = metrics.mqttResponseTime / 1000.0;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  mqttClient.publish(topic_smoke_alert, jsonString.c_str());
  
  Serial.println("🚨 ALERTA PUBLICADO!");
}

void publishStatusUpdate(bool smokeDetected) {
  StaticJsonDocument<200> doc;
  
  doc["device_id"] = device_id;
  doc["timestamp"] = millis();
  doc["online"] = true;
  doc["smoke_detected"] = smokeDetected;
  doc["led_manual"] = manualLEDControl;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  mqttClient.publish(topic_device_status, jsonString.c_str());
}

void publishDeviceInfo() {
  StaticJsonDocument<300> doc;
  
  doc["device_id"] = device_id;
  doc["device_type"] = "smoke_detector";
  doc["firmware_version"] = "1.0.0";
  doc["wifi_ssid"] = ssid;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  doc["chip_model"] = "ESP32";
  doc["startup_time"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  mqttClient.publish(topic_device_info, jsonString.c_str());
}
