#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include <esp_now.h>
#include <esp_wifi.h>

#include <ArduTFLite.h>
#include <DHT.h>

#include "grenvis_sensor_model.h"

// ======================================================
// WIFI
// ======================================================

const char* WIFI_SSID = ".";
const char* WIFI_PASSWORD = "01020304";

// ======================================================
// EMQX CLOUD
// ======================================================

const char* MQTT_HOST =
  "m2da914a.ala.eu-central-1.emqxsl.com";

const uint16_t MQTT_PORT = 8883;

const char* MQTT_USERNAME =
  "grenvis_esp32";

const char* MQTT_PASSWORD =
  "grenvis123";

// ======================================================
// MQTT TOPICS
// ======================================================

const char* MQTT_TOPIC_DATA =
  "grenvis/sensor/data";

const char* MQTT_TOPIC_STATUS =
  "grenvis/sensor/status";

const char* MQTT_TOPIC_VISION =
  "grenvis/vision/data";

const char* MQTT_TOPIC_IMAGE =
  "grenvis/vision/image";

const char* MQTT_TOPIC_CONTROL =
  "grenvis/device/control";

const uint8_t ESP32_CAM_MAC[] = { 0xF8, 0xB3, 0xB7, 0xA6, 0xF3, 0x9C };

// ======================================================
// MQTT CLIENT
// ======================================================

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

// ======================================================
// PIN
// ======================================================

// DHT22
#define DHT_PIN 19
#define DHT_TYPE DHT22

// Soil Moisture
#define SOIL_PIN 34

// L298N
#define MOTOR_IN1 16
#define MOTOR_IN2 17

// Limit Switch
#define LIMIT_LEFT 32
#define LIMIT_RIGHT 33

DHT dht(DHT_PIN, DHT_TYPE);

// ======================================================
// SOIL CALIBRATION
// ======================================================

// Nanti kalibrasi ulang setelah kita lihat RAW sensor.
//
// Biasanya:
// RAW besar = kering
// RAW kecil = basah

#define SOIL_DRY 4095
#define SOIL_WET 1200

float soilMoisture = 0.0;
int soilRaw = 0;

// ======================================================
// STANDARD SCALER
//
// Input:
// 0 = Temperature
// 1 = Humidity
// 2 = Soil Moisture
// ======================================================

const float MEAN[3] = {
  28.62221905,
  59.83464446,
  67.15121274
};

const float STD_SCALE[3] = {
  4.90833472,
  14.30678582,
  17.56037094
};

// ======================================================
// SENSOR AI CLASSES
//
// 0 = High
// 1 = Low
// 2 = Moderate
// ======================================================

const char* CLASS_NAMES[3] = {
  "High",
  "Low",
  "Moderate"
};

// ======================================================
// TENSOR ARENA
// ======================================================

constexpr int TENSOR_ARENA_SIZE =
  12 * 1024;

byte tensorArena[TENSOR_ARENA_SIZE];

// ======================================================
// SENSOR / AI DATA
// ======================================================

float currentTemperature = 0;
float currentHumidity = 0;

float highProbability = 0;
float lowProbability = 0;
float moderateProbability = 0;

String currentRisk = "Unknown";

float currentConfidence = 0;

// ======================================================
// MOTOR STATE
// ======================================================

enum MotorState {
  MOTOR_STOPPED,
  MOTOR_LEFT,
  MOTOR_RIGHT
};

MotorState motorState = MOTOR_STOPPED;

// ======================================================
// ESP-NOW PACKET TYPE
//
// HARUS SAMA DENGAN ESP32-CAM
// ======================================================

#define PACKET_RESULT      1
#define PACKET_IMAGE_START 2
#define PACKET_IMAGE_CHUNK 3
#define PACKET_IMAGE_END   4
#define PACKET_CAPTURE_REQUEST 5

#define IMAGE_CHUNK_SIZE 200

// ======================================================
// VISION RESULT PACKET
// ======================================================

typedef struct __attribute__((packed)) {

  uint8_t packetType;

  uint32_t scanNumber;

  char visionClass[16];

  float confidence;

  float healthy;
  float powdery;
  float rust;

  uint32_t imageSize;

} VisionResultPacket;

// ======================================================
// IMAGE START PACKET
// ======================================================

typedef struct __attribute__((packed)) {

  uint8_t packetType;

  uint32_t scanNumber;

  uint32_t imageSize;

  uint16_t totalChunks;

} ImageStartPacket;

// ======================================================
// IMAGE CHUNK PACKET
// ======================================================

typedef struct __attribute__((packed)) {

  uint8_t packetType;

  uint32_t scanNumber;

  uint16_t chunkIndex;

  uint16_t dataLength;

  uint8_t data[IMAGE_CHUNK_SIZE];

} ImageChunkPacket;

// ======================================================
// IMAGE END PACKET
// ======================================================

typedef struct __attribute__((packed)) {

  uint8_t packetType;

  uint32_t scanNumber;

  uint16_t totalChunks;

} ImageEndPacket;

// ======================================================
// VISION DATA
// ======================================================

bool visionAvailable = false;

unsigned long lastVisionReceived = 0;

char esp32CamMac[18] =
  "NOT_CONNECTED";

uint32_t visionScanNumber = 0;

char visionPrediction[16] =
  "Unknown";

float visionConfidence = 0;

float visionHealthy = 0;
float visionPowdery = 0;
float visionRust = 0;

// ======================================================
// IMAGE BUFFER
// ======================================================

#define MAX_IMAGE_SIZE 60000

uint8_t* imageBuffer = nullptr;

uint32_t currentImageSize = 0;
uint32_t imageScanNumber = 0;

uint16_t expectedChunks = 0;
uint16_t receivedChunks = 0;

bool receivingImage = false;
bool imageReady = false;
uint32_t lastPublishedImageScan = 0;

// ======================================================
// MQTT TIMER
// ======================================================

unsigned long lastPublish = 0;

const unsigned long PUBLISH_INTERVAL =
  5000;

// ======================================================
// MAC FORMAT
// ======================================================

void printMacAddress(
  const uint8_t* mac,
  char* output
) {

  sprintf(
    output,
    "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0],
    mac[1],
    mac[2],
    mac[3],
    mac[4],
    mac[5]
  );
}

// ======================================================
// MOTOR STOP
// ======================================================

void motorStop() {

  digitalWrite(
    MOTOR_IN1,
    LOW
  );

  digitalWrite(
    MOTOR_IN2,
    LOW
  );

  motorState =
    MOTOR_STOPPED;

  Serial.println(
    "[MOTOR] STOP"
  );
}

// ======================================================
// MOTOR RIGHT
// ======================================================

void motorRight() {

  if (
    digitalRead(
      LIMIT_RIGHT
    ) == LOW
  ) {

    Serial.println(
      "[LIMIT] RIGHT ACTIVE"
    );

    motorStop();

    return;
  }

  digitalWrite(
    MOTOR_IN1,
    HIGH
  );

  digitalWrite(
    MOTOR_IN2,
    LOW
  );

  motorState =
    MOTOR_RIGHT;

  Serial.println(
    "[MOTOR] >>> RIGHT"
  );
}

// ======================================================
// MOTOR LEFT
// ======================================================

void motorLeft() {

  if (
    digitalRead(
      LIMIT_LEFT
    ) == LOW
  ) {

    Serial.println(
      "[LIMIT] LEFT ACTIVE"
    );

    motorStop();

    return;
  }

  digitalWrite(
    MOTOR_IN1,
    LOW
  );

  digitalWrite(
    MOTOR_IN2,
    HIGH
  );

  motorState =
    MOTOR_LEFT;

  Serial.println(
    "[MOTOR] <<< LEFT"
  );
}

// ======================================================
// LIMIT SAFETY
// ======================================================

void checkLimitSwitch() {

  bool leftPressed =
    digitalRead(LIMIT_LEFT) == LOW;

  bool rightPressed =
    digitalRead(LIMIT_RIGHT) == LOW;

  // Kalau sedang ke kiri lalu kena limit kiri
  if (
    motorState == MOTOR_LEFT &&
    leftPressed
  ) {

    Serial.println();
    Serial.println("[LIMIT] LEFT PRESSED!");
    Serial.println("[MOTOR] BALIK KE KANAN");

    motorStop();

    delay(200);

    motorRight();
  }

  // Kalau sedang ke kanan lalu kena limit kanan
  if (
    motorState == MOTOR_RIGHT &&
    rightPressed
  ) {

    Serial.println();
    Serial.println("[LIMIT] RIGHT PRESSED!");
    Serial.println("[MOTOR] BALIK KE KIRI");

    motorStop();

    delay(200);

    motorLeft();
  }
}
// ======================================================
// READ SOIL
// ======================================================

void readSoil() {

  soilRaw =
    analogRead(
      SOIL_PIN
    );

  soilMoisture =
    map(
      soilRaw,
      SOIL_DRY,
      SOIL_WET,
      0,
      100
    );

  soilMoisture =
    constrain(
      soilMoisture,
      0,
      100
    );
}

// ======================================================
// CONNECT WIFI
// ======================================================

void connectWiFi() {

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    return;
  }

  Serial.println();

  Serial.println(
    "[WiFi] Connecting..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  int attempt = 0;

  while (
    WiFi.status() !=
      WL_CONNECTED &&
    attempt < 40
  ) {

    delay(
      500
    );

    Serial.print(
      "."
    );

    attempt++;
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    Serial.println(
      "[WiFi] CONNECTED"
    );

    Serial.print(
      "[WiFi] IP      : "
    );

    Serial.println(
      WiFi.localIP()
    );

    Serial.print(
      "[WiFi] MAC     : "
    );

    Serial.println(
      WiFi.macAddress()
    );

    Serial.print(
      "[WiFi] Channel : "
    );

    Serial.println(
      WiFi.channel()
    );

  } else {

    Serial.println(
      "[WiFi] CONNECTION FAILED"
    );
  }
}

// ======================================================
// CONNECT MQTT
// ======================================================

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != MQTT_TOPIC_CONTROL || length == 0) return;
  const char command = payload[0];
  if (command == 'L' || command == 'l') motorLeft();
  else if (command == 'R' || command == 'r') motorRight();
  else if (command == 'S' || command == 's') motorStop();
  else if (command == 'C' || command == 'c') {
    const uint8_t request[] = { PACKET_CAPTURE_REQUEST };
    const esp_err_t result = esp_now_send(ESP32_CAM_MAC, request, sizeof(request));
    Serial.printf("[CAM] Capture request: %s\n", result == ESP_OK ? "SENT" : "FAILED");
  } else Serial.printf("[MQTT] Unknown control command: %c\n", command);
}

void connectMQTT() {

  if (
    mqttClient.connected()
  ) {

    return;
  }

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    return;
  }

  Serial.println();

  Serial.println(
    "[MQTT] Connecting to EMQX..."
  );

  String clientId =
    "grenvis-esp32-";

  String mac =
    WiFi.macAddress();

  mac.replace(
    ":",
    ""
  );

  clientId +=
    mac;

  bool connected =
    mqttClient.connect(
      clientId.c_str(),
      MQTT_USERNAME,
      MQTT_PASSWORD,
      MQTT_TOPIC_STATUS,
      0,
      true,
      "offline"
    );

  if (
    connected
  ) {

    Serial.println(
      "[MQTT] CONNECTED"
    );

    mqttClient.publish(
      MQTT_TOPIC_STATUS,
      "online",
      true
    );

    mqttClient.subscribe(MQTT_TOPIC_CONTROL, 1);
    Serial.printf("[MQTT] Subscribed: %s\n", MQTT_TOPIC_CONTROL);

  } else {

    Serial.print(
      "[MQTT] Failed. State = "
    );

    Serial.println(
      mqttClient.state()
    );
  }
}

// ======================================================
// PROCESS VISION RESULT
// ======================================================

void processVisionResult(
  const uint8_t* data,
  int len
) {

  if (
    len !=
    sizeof(
      VisionResultPacket
    )
  ) {

    Serial.println(
      "[VISION] Invalid packet size"
    );

    return;
  }

  VisionResultPacket packet;

  memcpy(
    &packet,
    data,
    sizeof(packet)
  );

  visionAvailable =
    true;

  lastVisionReceived =
    millis();

  visionScanNumber =
    packet.scanNumber;

  strncpy(
    visionPrediction,
    packet.visionClass,
    sizeof(
      visionPrediction
    ) - 1
  );

  visionPrediction[
    sizeof(
      visionPrediction
    ) - 1
  ] = '\0';

  visionConfidence =
    packet.confidence;

  visionHealthy =
    packet.healthy;

  visionPowdery =
    packet.powdery;

  visionRust =
    packet.rust;

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "ESP32-CAM AI RECEIVED"
  );

  Serial.println(
    "========================================"
  );

  Serial.print(
    "Scan       : "
  );

  Serial.println(
    visionScanNumber
  );

  Serial.print(
    "Prediction : "
  );

  Serial.println(
    visionPrediction
  );

  Serial.print(
    "Confidence : "
  );

  Serial.print(
    visionConfidence,
    2
  );

  Serial.println(
    "%"
  );

  Serial.print(
    "Healthy    : "
  );

  Serial.print(
    visionHealthy,
    2
  );

  Serial.println(
    "%"
  );

  Serial.print(
    "Powdery    : "
  );

  Serial.print(
    visionPowdery,
    2
  );

  Serial.println(
    "%"
  );

  Serial.print(
    "Rust       : "
  );

  Serial.print(
    visionRust,
    2
  );

  Serial.println(
    "%"
  );

  Serial.println(
    "========================================"
  );
}

// ======================================================
// IMAGE START
// ======================================================

void processImageStart(
  const uint8_t* data,
  int len
) {

  if (
    len !=
    sizeof(
      ImageStartPacket
    )
  ) {

    return;
  }

  ImageStartPacket packet;

  memcpy(
    &packet,
    data,
    sizeof(packet)
  );

  if (
    packet.imageSize >
    MAX_IMAGE_SIZE
  ) {

    Serial.println(
      "[IMAGE] TOO LARGE"
    );

    receivingImage =
      false;

    return;
  }

  if (
    imageBuffer !=
    nullptr
  ) {

    free(
      imageBuffer
    );

    imageBuffer =
      nullptr;
  }

  imageBuffer =
    (uint8_t*)
    malloc(
      packet.imageSize
    );

  if (
    imageBuffer ==
    nullptr
  ) {

    Serial.println(
      "[IMAGE] MEMORY FAILED"
    );

    receivingImage =
      false;

    return;
  }

  currentImageSize =
    packet.imageSize;

  imageScanNumber =
    packet.scanNumber;

  expectedChunks =
    packet.totalChunks;

  receivedChunks = 0;

  receivingImage =
    true;

  imageReady =
    false;

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "IMAGE TRANSFER START"
  );

  Serial.print(
    "Size   : "
  );

  Serial.println(
    currentImageSize
  );

  Serial.print(
    "Chunks : "
  );

  Serial.println(
    expectedChunks
  );

  Serial.println(
    "========================================"
  );
}

// ======================================================
// IMAGE CHUNK
// ======================================================

void processImageChunk(
  const uint8_t* data,
  int len
) {

  if (
    !receivingImage ||
    imageBuffer ==
      nullptr
  ) {

    return;
  }

  ImageChunkPacket packet;

  memset(
    &packet,
    0,
    sizeof(packet)
  );

  int copyLength =
    len;

  if (
    copyLength >
    sizeof(packet)
  ) {

    copyLength =
      sizeof(packet);
  }

  memcpy(
    &packet,
    data,
    copyLength
  );

  if (
    packet.scanNumber !=
    imageScanNumber
  ) {

    return;
  }

  if (
    packet.chunkIndex >=
    expectedChunks
  ) {

    return;
  }

  if (
    packet.dataLength >
    IMAGE_CHUNK_SIZE
  ) {

    return;
  }

  uint32_t offset =
    packet.chunkIndex *
    IMAGE_CHUNK_SIZE;

  if (
    offset +
      packet.dataLength >
    currentImageSize
  ) {

    return;
  }

  memcpy(
    imageBuffer +
      offset,
    packet.data,
    packet.dataLength
  );

  receivedChunks++;

  if (
    receivedChunks %
      10 ==
    0
  ) {

    Serial.print(
      "[IMAGE] "
    );

    Serial.print(
      receivedChunks
    );

    Serial.print(
      "/"
    );

    Serial.println(
      expectedChunks
    );
  }
}

// ======================================================
// IMAGE END
// ======================================================

void processImageEnd(
  const uint8_t* data,
  int len
) {

  if (
    len !=
    sizeof(
      ImageEndPacket
    )
  ) {

    return;
  }

  ImageEndPacket packet;

  memcpy(
    &packet,
    data,
    sizeof(packet)
  );

  if (
    packet.scanNumber !=
    imageScanNumber
  ) {

    return;
  }

  receivingImage =
    false;

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "IMAGE TRANSFER END"
  );

  Serial.print(
    "Received : "
  );

  Serial.print(
    receivedChunks
  );

  Serial.print(
    "/"
  );

  Serial.println(
    expectedChunks
  );

  if (
    receivedChunks ==
    expectedChunks
  ) {

    bool jpegHeader =
      currentImageSize > 2 &&
      imageBuffer[0] ==
        0xFF &&
      imageBuffer[1] ==
        0xD8;

    bool jpegFooter =
      currentImageSize > 2 &&
      imageBuffer[
        currentImageSize - 2
      ] ==
        0xFF &&
      imageBuffer[
        currentImageSize - 1
      ] ==
        0xD9;

    if (
      jpegHeader &&
      jpegFooter
    ) {

      imageReady =
        true;

      Serial.println(
        "[IMAGE] JPEG COMPLETE"
      );

    } else {

      imageReady =
        false;

      Serial.println(
        "[IMAGE] JPEG CORRUPT"
      );
    }

  } else {

    imageReady =
      false;

    Serial.println(
      "[IMAGE] INCOMPLETE"
    );
  }

  Serial.println(
    "========================================"
  );
}

// ======================================================
// ESP-NOW CALLBACK
// ======================================================

void onEspNowReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* incomingData,
  int len
) {

  if (
    incomingData ==
      nullptr ||
    len <= 0
  ) {

    return;
  }

  printMacAddress(
    info->src_addr,
    esp32CamMac
  );

  uint8_t packetType =
    incomingData[0];

  switch (
    packetType
  ) {

    case PACKET_RESULT:

      processVisionResult(
        incomingData,
        len
      );

      break;

    case PACKET_IMAGE_START:

      processImageStart(
        incomingData,
        len
      );

      break;

    case PACKET_IMAGE_CHUNK:

      processImageChunk(
        incomingData,
        len
      );

      break;

    case PACKET_IMAGE_END:

      processImageEnd(
        incomingData,
        len
      );

      break;
  }
}

// ======================================================
// INIT ESP-NOW
// ======================================================

void initESPNow() {

  Serial.println();

  Serial.println(
    "[ESP-NOW] Initializing..."
  );

  if (
    esp_now_init() !=
    ESP_OK
  ) {

    Serial.println(
      "[ESP-NOW] INIT FAILED!"
    );

    return;
  }

  esp_now_register_recv_cb(
    onEspNowReceive
  );

  esp_now_peer_info_t cameraPeer = {};
  memcpy(cameraPeer.peer_addr, ESP32_CAM_MAC, sizeof(ESP32_CAM_MAC));
  cameraPeer.channel = WiFi.channel();
  cameraPeer.encrypt = false;
  cameraPeer.ifidx = WIFI_IF_STA;
  if (!esp_now_is_peer_exist(ESP32_CAM_MAC) && esp_now_add_peer(&cameraPeer) != ESP_OK) {
    Serial.println("[ESP-NOW] Camera peer add failed");
  }

  Serial.println(
    "[ESP-NOW] READY"
  );

  Serial.print(
    "[ESP-NOW] ESP32 MAC : "
  );

  Serial.println(
    WiFi.macAddress()
  );

  Serial.print(
    "[ESP-NOW] Channel   : "
  );

  Serial.println(
    WiFi.channel()
  );

  Serial.println();

  Serial.println(
    "ESP32-CAM HARUS CHANNEL SAMA!"
  );
}

// ======================================================
// RUN SENSOR AI
// ======================================================

bool runSensorAI(
  float temperature,
  float humidity,
  float soil
) {

  float inputTemperature =
    (
      temperature -
      MEAN[0]
    ) /
    STD_SCALE[0];

  float inputHumidity =
    (
      humidity -
      MEAN[1]
    ) /
    STD_SCALE[1];

  float inputSoil =
    (
      soil -
      MEAN[2]
    ) /
    STD_SCALE[2];

  modelSetInput(
    inputTemperature,
    0
  );

  modelSetInput(
    inputHumidity,
    1
  );

  modelSetInput(
    inputSoil,
    2
  );

  if (
    !modelRunInference()
  ) {

    Serial.println(
      "[AI] Inference FAILED"
    );

    return false;
  }

  highProbability =
    modelGetOutput(0);

  lowProbability =
    modelGetOutput(1);

  moderateProbability =
    modelGetOutput(2);

  float probs[3] = {

    highProbability,
    lowProbability,
    moderateProbability

  };

  int bestIndex = 0;

  for (
    int i = 1;
    i < 3;
    i++
  ) {

    if (
      probs[i] >
      probs[bestIndex]
    ) {

      bestIndex =
        i;
    }
  }

  currentRisk =
    CLASS_NAMES[
      bestIndex
    ];

  currentConfidence =
    probs[
      bestIndex
    ] *
    100.0;

  return true;
}

// ======================================================
// PRINT SENSOR AI
// ======================================================

void printAIResult() {

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "GRENVIS AI SENSOR"
  );

  Serial.println(
    "========================================"
  );

  Serial.print(
    "Temperature : "
  );

  Serial.print(
    currentTemperature,
    2
  );

  Serial.println(
    " C"
  );

  Serial.print(
    "Humidity    : "
  );

  Serial.print(
    currentHumidity,
    2
  );

  Serial.println(
    " %"
  );

  Serial.print(
    "Soil RAW    : "
  );

  Serial.println(
    soilRaw
  );

  Serial.print(
    "Soil        : "
  );

  Serial.print(
    soilMoisture,
    2
  );

  Serial.println(
    " %"
  );

  Serial.println();

  Serial.println(
    "AI Probability"
  );

  Serial.print(
    "High     : "
  );

  Serial.print(
    highProbability *
      100.0,
    2
  );

  Serial.println(
    "%"
  );

  Serial.print(
    "Low      : "
  );

  Serial.print(
    lowProbability *
      100.0,
    2
  );

  Serial.println(
    "%"
  );

  Serial.print(
    "Moderate : "
  );

  Serial.print(
    moderateProbability *
      100.0,
    2
  );

  Serial.println(
    "%"
  );

  Serial.println();

  Serial.print(
    "Prediction : "
  );

  Serial.println(
    currentRisk
  );

  Serial.print(
    "Confidence : "
  );

  Serial.print(
    currentConfidence,
    2
  );

  Serial.println(
    "%"
  );

  Serial.println(
    "========================================"
  );
}

// ======================================================
// PUBLISH SENSOR + AI + VISION
// ======================================================

void publishImageMQTT() {
  if (!mqttClient.connected() || !imageReady || imageBuffer == nullptr ||
      currentImageSize == 0 || imageScanNumber == lastPublishedImageScan) return;

  // Publish raw JPEG to prevent a large Base64 allocation on the ESP32.
  const bool started = mqttClient.beginPublish(MQTT_TOPIC_IMAGE, currentImageSize, false);
  const size_t written = started ? mqttClient.write(imageBuffer, currentImageSize) : 0;
  const bool ended = started && mqttClient.endPublish();
  if (ended && written == currentImageSize) {
    lastPublishedImageScan = imageScanNumber;
    Serial.printf("[MQTT] JPEG published: %lu bytes\n", (unsigned long) currentImageSize);
  } else Serial.println("[MQTT] JPEG publish failed");
}

void publishMQTT() {

  if (
    !mqttClient.connected()
  ) {

    return;
  }

  String payload = "{";

  payload +=
    "\"temperature\":" +
    String(
      currentTemperature,
      2
    ) +
    ",";

  payload +=
    "\"humidity\":" +
    String(
      currentHumidity,
      2
    ) +
    ",";

  payload +=
    "\"soil_raw\":" +
    String(
      soilRaw
    ) +
    ",";

  payload +=
    "\"soil_moisture\":" +
    String(
      soilMoisture,
      2
    ) +
    ",";

  payload +=
    "\"sensor_risk\":\"" +
    currentRisk +
    "\",";

  payload +=
    "\"sensor_confidence\":" +
    String(
      currentConfidence,
      2
    ) +
    ",";

  payload +=
    "\"high_probability\":" +
    String(
      highProbability *
        100.0,
      2
    ) +
    ",";

  payload +=
    "\"low_probability\":" +
    String(
      lowProbability *
        100.0,
      2
    ) +
    ",";

  payload +=
    "\"moderate_probability\":" +
    String(
      moderateProbability *
        100.0,
      2
    ) +
    ",";

  payload +=
    "\"esp32_mac\":\"" +
    WiFi.macAddress() +
    "\",";

  payload +=
    "\"uptime_seconds\":" +
    String(millis() / 1000UL) +
    ",";

  payload +=
    "\"wifi_channel\":" +
    String(
      WiFi.channel()
    ) +
    ",";

  payload +=
    "\"limit_left\":" +
    String(
      digitalRead(
        LIMIT_LEFT
      ) == LOW
        ? "true"
        : "false"
    ) +
    ",";

  payload +=
    "\"limit_right\":" +
    String(
      digitalRead(
        LIMIT_RIGHT
      ) == LOW
        ? "true"
        : "false"
    ) +
    ",";

  payload +=
    "\"vision_connected\":";

  payload +=
    visionAvailable
      ? "true"
      : "false";

  if (
    visionAvailable
  ) {

    payload += ",";

    payload +=
      "\"esp32cam_mac\":\"" +
      String(
        esp32CamMac
      ) +
      "\",";

    payload +=
      "\"vision_scan\":" +
      String(
        visionScanNumber
      ) +
      ",";

    payload +=
      "\"vision_healthy\":" +
      String(
        visionHealthy,
        2
      ) +
      ",";

    payload +=
      "\"vision_powdery\":" +
      String(
        visionPowdery,
        2
      ) +
      ",";

    payload +=
      "\"vision_rust\":" +
      String(
        visionRust,
        2
      ) +
      ",";

    payload +=
      "\"vision_prediction\":\"" +
      String(
        visionPrediction
      ) +
      "\",";

    payload +=
      "\"vision_confidence\":" +
      String(
        visionConfidence,
        2
      ) +
      ",";

    payload +=
      "\"image_ready\":" +
      String(
        imageReady
          ? "true"
          : "false"
      ) +
      ",";

    payload +=
      "\"image_size\":" +
      String(
        currentImageSize
      );
  }

  payload +=
    "}";

  bool success =
    mqttClient.publish(
      MQTT_TOPIC_DATA,
      payload.c_str()
    );

  Serial.println();

  if (
    success
  ) {

    Serial.println(
      "[MQTT] Publish SUCCESS"
    );

  } else {

    Serial.println(
      "[MQTT] Publish FAILED"
    );
  }

  Serial.println(
    payload
  );

  publishImageMQTT();
}

// ======================================================
// PUBLISH VISION SEPARATELY
// ======================================================

void publishVisionMQTT() {

  if (
    !mqttClient.connected() ||
    !visionAvailable
  ) {

    return;
  }

  String payload =
    "{";

  payload +=
    "\"scan\":" +
    String(
      visionScanNumber
    ) +
    ",";

  payload +=
    "\"prediction\":\"" +
    String(
      visionPrediction
    ) +
    "\",";

  payload +=
    "\"confidence\":" +
    String(
      visionConfidence,
      2
    ) +
    ",";

  payload +=
    "\"healthy\":" +
    String(
      visionHealthy,
      2
    ) +
    ",";

  payload +=
    "\"powdery\":" +
    String(
      visionPowdery,
      2
    ) +
    ",";

  payload +=
    "\"rust\":" +
    String(
      visionRust,
      2
    ) +
    ",";

  payload +=
    "\"image_ready\":" +
    String(
      imageReady
        ? "true"
        : "false"
    ) +
    ",";

  payload +=
    "\"image_size\":" +
    String(
      currentImageSize
    );

  payload +=
    "}";

  mqttClient.publish(
    MQTT_TOPIC_VISION,
    payload.c_str()
  );
}

// ======================================================
// SERIAL COMMAND
// ======================================================

void handleSerial() {

  if (
    !Serial.available()
  ) {

    return;
  }

  char command =
    Serial.read();

  if (
    command == 'R' ||
    command == 'r'
  ) {

    motorRight();
  }

  else if (
    command == 'L' ||
    command == 'l'
  ) {

    motorLeft();
  }

  else if (
    command == 'S' ||
    command == 's'
  ) {

    motorStop();
  }

  else if (
    command == 'K' ||
    command == 'k'
  ) {

    Serial.println();

    Serial.print(
      "LEFT LIMIT  : "
    );

    Serial.println(
      digitalRead(
        LIMIT_LEFT
      ) == LOW
        ? "PRESSED"
        : "OPEN"
    );

    Serial.print(
      "RIGHT LIMIT : "
    );

    Serial.println(
      digitalRead(
        LIMIT_RIGHT
      ) == LOW
        ? "PRESSED"
        : "OPEN"
    );
  }
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(
    1500
  );

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "GRENVIS ESP32 MAIN CONTROLLER"
  );

  Serial.println(
    "========================================"
  );

  // ==================================================
  // DHT
  // ==================================================

  dht.begin();

  // ==================================================
  // SOIL
  // ==================================================

  pinMode(
    SOIL_PIN,
    INPUT
  );

  analogReadResolution(
    12
  );

  // ==================================================
  // MOTOR
  // ==================================================

  pinMode(
    MOTOR_IN1,
    OUTPUT
  );

  pinMode(
    MOTOR_IN2,
    OUTPUT
  );

  motorStop();

  // ==================================================
  // LIMIT SWITCH
  // ==================================================

  pinMode(
    LIMIT_LEFT,
    INPUT_PULLUP
  );

  pinMode(
    LIMIT_RIGHT,
    INPUT_PULLUP
  );

  // ==================================================
  // SENSOR AI
  // ==================================================

  Serial.println(
    "[AI] Loading sensor model..."
  );

  bool modelLoaded =
    modelInit(
      grenvis_sensor_model_tflite,
      tensorArena,
      TENSOR_ARENA_SIZE
    );

  if (
    !modelLoaded
  ) {

    Serial.println(
      "[AI] MODEL LOAD FAILED!"
    );

    while (
      true
    ) {

      delay(
        1000
      );
    }
  }

  Serial.println(
    "[AI] MODEL READY"
  );

  // ==================================================
  // WIFI
  // ==================================================

  connectWiFi();

  Serial.println();

  Serial.println(
    "========== DEVICE INFO =========="
  );

  Serial.print(
    "ESP32 MAC      : "
  );

  Serial.println(
    WiFi.macAddress()
  );

  Serial.print(
    "WiFi Channel   : "
  );

  Serial.println(
    WiFi.channel()
  );

  Serial.println(
    "================================="
  );

  // ==================================================
  // ESP-NOW
  // ==================================================

  initESPNow();

  // ==================================================
  // MQTT TLS
  // ==================================================

  secureClient.setInsecure();

  mqttClient.setServer(
    MQTT_HOST,
    MQTT_PORT
  );

  mqttClient.setBufferSize(
    1024
  );

  mqttClient.setCallback(onMqttMessage);

  connectMQTT();

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "SYSTEM READY"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "R = motor kanan"
  );

  Serial.println(
    "L = motor kiri"
  );

  Serial.println(
    "S = motor stop"
  );

  Serial.println(
    "K = cek limit"
  );
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  // ==================================================
  // SAFETY MOTOR
  // ==================================================

  checkLimitSwitch();

  // ==================================================
  // SERIAL CONTROL
  // ==================================================

  handleSerial();

  // ==================================================
  // WIFI
  // ==================================================

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    connectWiFi();
  }

  // ==================================================
  // MQTT
  // ==================================================

  if (
    !mqttClient.connected()
  ) {

    connectMQTT();
  }

  mqttClient.loop();

  // ==================================================
  // TIMER
  // ==================================================

  if (
    millis() -
    lastPublish <
    PUBLISH_INTERVAL
  ) {

    delay(
      5
    );

    return;
  }

  lastPublish =
    millis();

  // ==================================================
  // DHT22
  // ==================================================

  currentTemperature =
    dht.readTemperature();

  currentHumidity =
    dht.readHumidity();

  if (
    isnan(
      currentTemperature
    ) ||
    isnan(
      currentHumidity
    )
  ) {

    Serial.println(
      "[DHT22] READ FAILED"
    );

    return;
  }

  // ==================================================
  // SOIL REAL SENSOR
  // ==================================================

  readSoil();

  // ==================================================
  // SENSOR AI
  // ==================================================

  if (
    !runSensorAI(
      currentTemperature,
      currentHumidity,
      soilMoisture
    )
  ) {

    return;
  }

  // ==================================================
  // PRINT
  // ==================================================

  printAIResult();

  // ==================================================
  // MQTT SENSOR
  // ==================================================

  publishMQTT();

  // ==================================================
  // MQTT VISION
  // ==================================================

  publishVisionMQTT();
}
