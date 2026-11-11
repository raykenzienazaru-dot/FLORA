#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include <esp_now.h>
#include <esp_wifi.h>
#include <DHT.h>

// ======================================================
// WIFI
// ======================================================

const char* WIFI_SSID = ".";
const char* WIFI_PASSWORD = "01020304";

// ======================================================
// EMQX CLOUD
// ======================================================

const char* MQTT_HOST =
  "q663660b.ala.asia-southeast1.emqxsl.com";

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

// Target default ESP32-CAM MAC (akan diperbarui secara dinamis saat paket diterima)
const uint8_t ESP32_CAM_MAC[] = { 0xF8, 0xB3, 0xB7, 0xA6, 0xF3, 0x9C };
uint8_t targetCamMac[6] = { 0xF8, 0xB3, 0xB7, 0xA6, 0xF3, 0x9C };
bool hasLearnedCamMac = false;

// ======================================================
// MQTT CLIENT
// ======================================================

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

// ======================================================
// PIN & SENSOR CONFIGURATION
// ======================================================

// DHT Sensor (GPIO 19)
// Mendukung DHT22 (default) & DHT11 secara bergantian otomatis tanpa tabrakan pin
#define DHT_PIN 19
uint8_t dhtType = DHT22;
DHT dht(DHT_PIN, DHT22);

// Soil Moisture Sensor (GPIO 34 ADC1_CH6)
#define SOIL_PIN 34

// Kalibrasi Nilai ADC Sensor Tanah:
// Kapasitif v1.2: RAW besar = KERING (udara bebas), RAW kecil = BASAH (air)
// Nilai ini dapat dikalibrasi via perintah Serial 'C' (kering) dan 'W' (basah)
int soilDry = 3100;
int soilWet = 1350;

float soilMoisture = 0.0;
int soilRaw = 0;

// ======================================================
// MOTOR DRIVER L298N
// ======================================================
// Motor terhubung ke terminal OUT3 & OUT4 pada L298N.
// Sesuai konfigurasi hardware, kontrol tetap menggunakan pin input IN1 & IN2:
// - GPIO 16 -> IN1 (atau IN3)
// - GPIO 17 -> IN2 (atau IN4)
// CATATAN: Jumper ENA/ENB wajib terpasang & GND ESP32 wajib digabung ke GND L298N!
#define MOTOR_IN1 16
#define MOTOR_IN2 17

enum MotorState {
  MOTOR_STOPPED,
  MOTOR_LEFT,
  MOTOR_RIGHT
};

MotorState motorState = MOTOR_STOPPED;

// ======================================================
// LIMIT SWITCH
// ======================================================
#define LIMIT_LEFT 32
#define LIMIT_RIGHT 33

// Polaritas Limit Switch:
// LOW  = Normally Open (NO) switch ke GND (default INPUT_PULLUP)
// HIGH = Normally Closed (NC) atau Active HIGH
// Dapat diubah langsung saat alat berjalan dengan mengetik 'P' di Serial Monitor
uint8_t limitActiveLevel = LOW;

inline bool isLimitLeftActive() {
  return digitalRead(LIMIT_LEFT) == limitActiveLevel;
}

inline bool isLimitRightActive() {
  return digitalRead(LIMIT_RIGHT) == limitActiveLevel;
}

// ======================================================
// SENSOR DATA (RAW & PROCESSED REAL HARDWARE - NO DUMMY)
// ======================================================

float currentTemperature = 0.0;
float currentHumidity = 0.0;

// ======================================================
// ESP-NOW PACKET TYPE
// HARUS SAMA DENGAN ESP32-CAM
// ======================================================

#define PACKET_RESULT          1
#define PACKET_IMAGE_START     2
#define PACKET_IMAGE_CHUNK     3
#define PACKET_IMAGE_END       4
#define PACKET_CAPTURE_REQUEST 5

#define IMAGE_CHUNK_SIZE 200

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

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint32_t scanNumber;
  uint32_t imageSize;
  uint16_t totalChunks;
} ImageStartPacket;

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint32_t scanNumber;
  uint16_t chunkIndex;
  uint16_t dataLength;
  uint8_t data[IMAGE_CHUNK_SIZE];
} ImageChunkPacket;

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint32_t scanNumber;
  uint16_t totalChunks;
} ImageEndPacket;

// ======================================================
// VISION STATE
// ======================================================

bool visionAvailable = false;
unsigned long lastVisionReceived = 0;
char esp32CamMac[18] = "NOT_CONNECTED";
uint32_t visionScanNumber = 0;
char visionPrediction[16] = "Unknown";
float visionConfidence = 0.0;
float visionHealthy = 0.0;
float visionPowdery = 0.0;
float visionRust = 0.0;

volatile bool newVisionReady = false;
volatile bool newImageReady = false;

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
// SENSOR MONITORING & MQTT TIMER
// ======================================================

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 3000; // 3 detik per siklus baca sensor & publish

// ======================================================
// HELPER: MAC TO STRING
// ======================================================

void printMacAddress(const uint8_t* mac, char* output) {
  sprintf(output, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ======================================================
// MOTOR CONTROLS
// ======================================================

void motorStop() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  motorState = MOTOR_STOPPED;
  Serial.println("[MOTOR] STOP");
}

void motorRight() {
  if (isLimitRightActive()) {
    Serial.printf("[LIMIT BLOCKED] KANAN AKTIF (PIN %d RAW=%d)! Ketik 'P' di Serial jika polaritas terbalik.\n",
                  LIMIT_RIGHT, digitalRead(LIMIT_RIGHT));
    motorStop();
    return;
  }
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  motorState = MOTOR_RIGHT;
  Serial.println("[MOTOR] >>> JALAN KANAN (IN1 HIGH, IN2 LOW)");
}

void motorLeft() {
  if (isLimitLeftActive()) {
    Serial.printf("[LIMIT BLOCKED] KIRI AKTIF (PIN %d RAW=%d)! Ketik 'P' di Serial jika polaritas terbalik.\n",
                  LIMIT_LEFT, digitalRead(LIMIT_LEFT));
    motorStop();
    return;
  }
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
  motorState = MOTOR_LEFT;
  Serial.println("[MOTOR] <<< JALAN KIRI (IN1 LOW, IN2 HIGH)");
}

// ======================================================
// TRIGGER CAMERA CAPTURE (ESP-NOW)
// ======================================================

void triggerCameraCapture() {
  const uint8_t request[] = { PACKET_CAPTURE_REQUEST };
  const uint8_t* target = hasLearnedCamMac ? targetCamMac : ESP32_CAM_MAC;

  if (!esp_now_is_peer_exist(target)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, target, 6);
    peer.channel = WiFi.channel();
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&peer);
  }

  const esp_err_t result = esp_now_send(target, request, sizeof(request));
  Serial.println();
  Serial.printf("[ESP-NOW] Kirim trigger foto ke ESP32-CAM (%02X:%02X:%02X:%02X:%02X:%02X): %s (kode: %d)\n",
                target[0], target[1], target[2], target[3], target[4], target[5],
                result == ESP_OK ? "BERHASIL" : "GAGAL", result);
}

// ======================================================
// LIMIT SWITCH SAFETY & AUTO-TRIGGER
// ======================================================

void checkLimitSwitch() {
  bool leftPressed = isLimitLeftActive();
  bool rightPressed = isLimitRightActive();

  static bool prevLeftPressed = false;
  static bool prevRightPressed = false;
  static unsigned long lastLimitCaptureTime = 0;

  // Deteksi transisi saat limit switch baru saja tersentuh (dengan debounce waktu)
  if ((leftPressed && !prevLeftPressed) || (rightPressed && !prevRightPressed)) {
    if (millis() - lastLimitCaptureTime > 3000) {
      lastLimitCaptureTime = millis();
      Serial.println();
      Serial.println(">>> [LIMIT SWITCH TERSENTUH] Memicu jepretan foto ESP32-CAM otomatis! <<<");
      triggerCameraCapture();
    }
  }

  prevLeftPressed = leftPressed;
  prevRightPressed = rightPressed;

  // Hentikan motor seketika jika mencapai batas fisik rel
  if (motorState == MOTOR_LEFT && leftPressed) {
    Serial.println("[LIMIT] KIRI TERSENTUH! Motor berhenti.");
    motorStop();
  }

  if (motorState == MOTOR_RIGHT && rightPressed) {
    Serial.println("[LIMIT] KANAN TERSENTUH! Motor berhenti.");
    motorStop();
  }
}

// ======================================================
// READ SENSORS (PURE HARDWARE - ZERO DUMMY FALLBACK)
// ======================================================

void readDHT() {
  static int dhtFailStreak = 0;
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h) && t > -40.0 && t < 85.0 && h >= 0.0 && h <= 100.0) {
    currentTemperature = t;
    currentHumidity = h;
    dhtFailStreak = 0;
  } else {
    dhtFailStreak++;
    Serial.printf("[DHT] Gagal membaca mode %s di GPIO %d (streak: %d). Murni tanpa dummy.\n",
                  dhtType == DHT22 ? "DHT22" : "DHT11", DHT_PIN, dhtFailStreak);

    // Beralih jenis sensor setelah 3x gagal beruntun
    if (dhtFailStreak >= 3) {
      dhtType = (dhtType == DHT22) ? DHT11 : DHT22;
      Serial.printf("[DHT] Mencoba beralih ke protokol %s...\n", dhtType == DHT22 ? "DHT22" : "DHT11");
      dht = DHT(DHT_PIN, dhtType);
      dht.begin();
      dhtFailStreak = 0;
    }
    // Nilai suhu dan kelembapan riil dipertahankan (tidak di-overwrite ke 28.0C / 60.0%)!
  }
}

void readSoil() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(SOIL_PIN);
    delay(2);
  }
  soilRaw = sum / 20;

  // Deteksi kabel terlepas / 0V -> Laporkan 0.0%, BUKAN 100%!
  if (soilRaw <= 150) {
    soilMoisture = 0.0;
    Serial.printf("[SOIL] PERINGATAN: RAW = %d (kabel terlepas / 0V). Kelembapan: 0.0%%\n", soilRaw);
    return;
  }

  // Deteksi di udara terbuka (kering total)
  if (soilRaw >= 4050) {
    soilMoisture = 0.0;
    Serial.printf("[SOIL] RAW: %d (Kering total di udara bebas) -> 0.0%%\n", soilRaw);
    return;
  }

  // Pemetaan linier sesuai karakteristik sensor
  if (soilDry > soilWet) {
    // Sensor Kapasitif v1.2 (ADC tinggi = Kering, ADC rendah = Basah)
    if (soilRaw >= soilDry) {
      soilMoisture = 0.0;
    } else if (soilRaw <= soilWet) {
      soilMoisture = 100.0;
    } else {
      float moisture = (float)(soilDry - soilRaw) / (float)(soilDry - soilWet) * 100.0;
      soilMoisture = constrain(moisture, 0.0, 100.0);
    }
  } else {
    // Sensor Resistif
    if (soilRaw <= soilDry) {
      soilMoisture = 0.0;
    } else if (soilRaw >= soilWet) {
      soilMoisture = 100.0;
    } else {
      float moisture = (float)(soilRaw - soilDry) / (float)(soilWet - soilDry) * 100.0;
      soilMoisture = constrain(moisture, 0.0, 100.0);
    }
  }

  Serial.printf("[SOIL] RAW: %d | Terbaca: %.1f%%\n", soilRaw, soilMoisture);
}

// ======================================================
// CONNECT WIFI
// ======================================================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println();
  Serial.printf("[WiFi] Menghubungkan ke SSID: %s...\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 40) {
    delay(500);
    Serial.print(".");
    attempt++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] CONNECTED");
    Serial.printf("[WiFi] IP      : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] MAC     : %s\n", WiFi.macAddress().c_str());
    Serial.printf("[WiFi] Channel : %d\n", WiFi.channel());
  } else {
    Serial.println("[WiFi] CONNECTION FAILED");
  }
}

// ======================================================
// MQTT MESSAGE CALLBACK & CONNECT
// ======================================================

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != MQTT_TOPIC_CONTROL || length == 0) return;
  const char command = payload[0];
  Serial.printf("[MQTT] Perintah kontrol diterima: %c\n", command);

  if (command == 'L' || command == 'l') motorLeft();
  else if (command == 'R' || command == 'r') motorRight();
  else if (command == 'S' || command == 's') motorStop();
  else if (command == 'C' || command == 'c') triggerCameraCapture();
  else Serial.printf("[MQTT] Perintah tidak dikenal: %c\n", command);
}

void connectMQTT() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println();
  Serial.printf("[MQTT] Menghubungkan ke EMQX (%s:%u)...\n", MQTT_HOST, MQTT_PORT);

  String clientId = "grenvis-esp32-";
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  clientId += mac;

  bool connected = mqttClient.connect(
    clientId.c_str(),
    MQTT_USERNAME,
    MQTT_PASSWORD,
    MQTT_TOPIC_STATUS,
    0,
    true,
    "offline"
  );

  if (connected) {
    Serial.println("[MQTT] CONNECTED KE BROKER EMQX!");
    mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
    mqttClient.subscribe(MQTT_TOPIC_CONTROL, 1);
    Serial.printf("[MQTT] Subscribed topik kontrol: %s\n", MQTT_TOPIC_CONTROL);
  } else {
    Serial.printf("[MQTT] Gagal konek. State = %d\n", mqttClient.state());
  }
}

// ======================================================
// PROCESS ESP-NOW PACKETS DARI ESP32-CAM
// ======================================================

void processVisionResult(const uint8_t* data, int len) {
  if (len != sizeof(VisionResultPacket)) {
    Serial.println("[VISION] Ukuran paket tidak valid!");
    return;
  }

  VisionResultPacket packet;
  memcpy(&packet, data, sizeof(packet));

  visionAvailable = true;
  lastVisionReceived = millis();
  visionScanNumber = packet.scanNumber;

  strncpy(visionPrediction, packet.visionClass, sizeof(visionPrediction) - 1);
  visionPrediction[sizeof(visionPrediction) - 1] = '\0';

  visionConfidence = packet.confidence;
  visionHealthy = packet.healthy;
  visionPowdery = packet.powdery;
  visionRust = packet.rust;

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32-CAM AI RESULT RECEIVED (ESP-NOW)");
  Serial.println("========================================");
  Serial.printf("Scan       : %u\n", visionScanNumber);
  Serial.printf("Prediction : %s\n", visionPrediction);
  Serial.printf("Confidence : %.2f %%\n", visionConfidence);
  Serial.printf("Healthy    : %.2f %%\n", visionHealthy);
  Serial.printf("Powdery    : %.2f %%\n", visionPowdery);
  Serial.printf("Rust       : %.2f %%\n", visionRust);
  Serial.println("========================================");

  // Tandai ada data label vision baru untuk segera dikirim ke Web via MQTT
  newVisionReady = true;
}

void processImageStart(const uint8_t* data, int len) {
  if (len != sizeof(ImageStartPacket)) return;

  ImageStartPacket packet;
  memcpy(&packet, data, sizeof(packet));

  if (packet.imageSize > MAX_IMAGE_SIZE) {
    Serial.printf("[IMAGE] Ukuran citra terlalu besar (%u bytes)\n", packet.imageSize);
    receivingImage = false;
    return;
  }

  if (imageBuffer != nullptr) {
    free(imageBuffer);
    imageBuffer = nullptr;
  }

  imageBuffer = (uint8_t*) malloc(packet.imageSize);
  if (imageBuffer == nullptr) {
    Serial.println("[IMAGE] Alokasi memori buffer gagal!");
    receivingImage = false;
    return;
  }

  currentImageSize = packet.imageSize;
  imageScanNumber = packet.scanNumber;
  expectedChunks = packet.totalChunks;
  receivedChunks = 0;
  receivingImage = true;
  imageReady = false;

  Serial.println();
  Serial.printf("[IMAGE] Mulai transfer foto JPEG: %u bytes (%u chunks)\n",
                currentImageSize, expectedChunks);
}

void processImageChunk(const uint8_t* data, int len) {
  if (!receivingImage || imageBuffer == nullptr) return;

  ImageChunkPacket packet;
  memset(&packet, 0, sizeof(packet));

  int copyLength = len > sizeof(packet) ? sizeof(packet) : len;
  memcpy(&packet, data, copyLength);

  if (packet.scanNumber != imageScanNumber) return;
  if (packet.chunkIndex >= expectedChunks) return;
  if (packet.dataLength > IMAGE_CHUNK_SIZE) return;

  uint32_t offset = (uint32_t)packet.chunkIndex * IMAGE_CHUNK_SIZE;
  if (offset + packet.dataLength > currentImageSize) return;

  memcpy(imageBuffer + offset, packet.data, packet.dataLength);
  receivedChunks++;

  if (receivedChunks % 15 == 0 || receivedChunks == expectedChunks) {
    Serial.printf("[IMAGE] Progres transfer: %u / %u chunks\n", receivedChunks, expectedChunks);
  }
}

void processImageEnd(const uint8_t* data, int len) {
  if (len != sizeof(ImageEndPacket)) return;

  ImageEndPacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (packet.scanNumber != imageScanNumber) return;

  receivingImage = false;

  if (receivedChunks == expectedChunks && currentImageSize > 4) {
    bool jpegHeader = (imageBuffer[0] == 0xFF && imageBuffer[1] == 0xD8);
    bool jpegFooter = (imageBuffer[currentImageSize - 2] == 0xFF && imageBuffer[currentImageSize - 1] == 0xD9);

    if (jpegHeader && jpegFooter) {
      imageReady = true;
      newImageReady = true;
      Serial.printf("[IMAGE] Foto JPEG LENGKAP & VALID (%u bytes)!\n", currentImageSize);
    } else {
      imageReady = false;
      Serial.println("[IMAGE] Header/footer JPEG corrupt");
    }
  } else {
    imageReady = false;
    Serial.printf("[IMAGE] Paket tidak lengkap (%u dari %u)\n", receivedChunks, expectedChunks);
  }
}

void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
  if (incomingData == nullptr || len <= 0) return;

  if (info != nullptr && info->src_addr != nullptr) {
    memcpy(targetCamMac, info->src_addr, 6);
    hasLearnedCamMac = true;
    printMacAddress(info->src_addr, esp32CamMac);

    if (!esp_now_is_peer_exist(targetCamMac)) {
      esp_now_peer_info_t peer = {};
      memcpy(peer.peer_addr, targetCamMac, 6);
      peer.channel = WiFi.channel();
      peer.encrypt = false;
      peer.ifidx = WIFI_IF_STA;
      esp_now_add_peer(&peer);
    }
  }

  uint8_t packetType = incomingData[0];
  switch (packetType) {
    case PACKET_RESULT:
      processVisionResult(incomingData, len);
      break;
    case PACKET_IMAGE_START:
      processImageStart(incomingData, len);
      break;
    case PACKET_IMAGE_CHUNK:
      processImageChunk(incomingData, len);
      break;
    case PACKET_IMAGE_END:
      processImageEnd(incomingData, len);
      break;
  }
}

void initESPNow() {
  Serial.println("[ESP-NOW] Menginisialisasi...");

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Inisialisasi GAGAL!");
    return;
  }

  esp_now_register_recv_cb(onEspNowReceive);

  esp_now_peer_info_t cameraPeer = {};
  memcpy(cameraPeer.peer_addr, ESP32_CAM_MAC, sizeof(ESP32_CAM_MAC));
  cameraPeer.channel = WiFi.channel();
  cameraPeer.encrypt = false;
  cameraPeer.ifidx = WIFI_IF_STA;

  if (!esp_now_is_peer_exist(ESP32_CAM_MAC)) {
    esp_now_add_peer(&cameraPeer);
  }

  Serial.println("[ESP-NOW] SIAP MENERIMA DATA DARI ESP32-CAM");
  Serial.printf("[ESP-NOW] ESP32-Main MAC : %s\n", WiFi.macAddress().c_str());
  Serial.printf("[ESP-NOW] Saluran WiFi   : %d\n", WiFi.channel());
}

// ======================================================
// PRINT HARDWARE TELEMETRY KE SERIAL MONITOR
// ======================================================

void printSensorData() {
  Serial.println();
  Serial.println("========================================");
  Serial.println("FLORA HARDWARE TELEMETRY (PURE SENSOR)");
  Serial.println("========================================");
  Serial.printf("Temperature : %.2f C (Sensor: %s)\n", currentTemperature, dhtType == DHT22 ? "DHT22" : "DHT11");
  Serial.printf("Humidity    : %.2f %%\n", currentHumidity);
  Serial.printf("Soil RAW    : %d\n", soilRaw);
  Serial.printf("Soil        : %.2f %%\n", soilMoisture);
  Serial.printf("Limit Kiri  : %s (PIN %d RAW: %d)\n", isLimitLeftActive() ? "TERTEKAN" : "LEPAS", LIMIT_LEFT, digitalRead(LIMIT_LEFT));
  Serial.printf("Limit Kanan : %s (PIN %d RAW: %d)\n", isLimitRightActive() ? "TERTEKAN" : "LEPAS", LIMIT_RIGHT, digitalRead(LIMIT_RIGHT));
  Serial.printf("Polaritas   : Active-%s\n", limitActiveLevel == LOW ? "LOW (NO)" : "HIGH (NC)");
  Serial.println("========================================");
}

// ======================================================
// PUBLISH SENSOR TELEMETRY (JSON RINGAN TANPA BUFFER OVERFLOW)
// ======================================================

void publishMQTT() {
  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Skip publish: Broker belum terhubung");
    return;
  }

  String payload = "{";
  payload += "\"temperature\":" + String(currentTemperature, 2) + ",";
  payload += "\"humidity\":" + String(currentHumidity, 2) + ",";
  payload += "\"soil_raw\":" + String(soilRaw) + ",";
  payload += "\"soil_moisture\":" + String(soilMoisture, 2) + ",";
  payload += "\"esp32_mac\":\"" + WiFi.macAddress() + "\",";
  payload += "\"uptime_seconds\":" + String(millis() / 1000UL) + ",";
  payload += "\"wifi_channel\":" + String(WiFi.channel()) + ",";
  payload += "\"limit_left\":" + String(isLimitLeftActive() ? "true" : "false") + ",";
  payload += "\"limit_right\":" + String(isLimitRightActive() ? "true" : "false") + ",";
  payload += "\"vision_connected\":";
  payload += visionAvailable ? "true" : "false";

  if (visionAvailable) {
    payload += ",";
    payload += "\"esp32cam_mac\":\"" + String(esp32CamMac) + "\",";
    payload += "\"vision_scan\":" + String(visionScanNumber) + ",";
    payload += "\"vision_healthy\":" + String(visionHealthy, 2) + ",";
    payload += "\"vision_powdery\":" + String(visionPowdery, 2) + ",";
    payload += "\"vision_rust\":" + String(visionRust, 2) + ",";
    payload += "\"vision_prediction\":\"" + String(visionPrediction) + "\",";
    payload += "\"vision_confidence\":" + String(visionConfidence, 2) + ",";
    payload += "\"image_ready\":" + String(imageReady ? "true" : "false") + ",";
    payload += "\"image_size\":" + String(currentImageSize);
  }

  payload += "}";

  bool success = mqttClient.publish(MQTT_TOPIC_DATA, payload.c_str());
  if (success) {
    Serial.printf("[MQTT] Publish telemetri BERHASIL (%u bytes ke %s)\n", payload.length(), MQTT_TOPIC_DATA);
  } else {
    Serial.println("[MQTT] Publish telemetri GAGAL!");
  }
}

// ======================================================
// PUBLISH VISION SEPARATELY (METADATA DETEKSI DAUN)
// ======================================================

void publishVisionMQTT() {
  if (!mqttClient.connected() || !visionAvailable) return;

  String payload = "{";
  payload += "\"scan\":" + String(visionScanNumber) + ",";
  payload += "\"vision_scan\":" + String(visionScanNumber) + ",";
  payload += "\"prediction\":\"" + String(visionPrediction) + "\",";
  payload += "\"vision_prediction\":\"" + String(visionPrediction) + "\",";
  payload += "\"confidence\":" + String(visionConfidence, 2) + ",";
  payload += "\"vision_confidence\":" + String(visionConfidence, 2) + ",";
  payload += "\"healthy\":" + String(visionHealthy, 2) + ",";
  payload += "\"vision_healthy\":" + String(visionHealthy, 2) + ",";
  payload += "\"powdery\":" + String(visionPowdery, 2) + ",";
  payload += "\"vision_powdery\":" + String(visionPowdery, 2) + ",";
  payload += "\"rust\":" + String(visionRust, 2) + ",";
  payload += "\"vision_rust\":" + String(visionRust, 2) + ",";
  payload += "\"vision_connected\":true,";
  payload += "\"image_ready\":" + String(imageReady ? "true" : "false") + ",";
  payload += "\"image_size\":" + String(currentImageSize);
  payload += "}";

  mqttClient.publish(MQTT_TOPIC_VISION, payload.c_str());
  Serial.printf("[MQTT] Vision metadata dipublish ke %s\n", MQTT_TOPIC_VISION);
}

// ======================================================
// PUBLISH FOTO JPEG SECARA CHUNKED & CRASH-PROOF
// ======================================================

void publishImageMQTT() {
  if (!mqttClient.connected() || !imageReady || imageBuffer == nullptr ||
      currentImageSize == 0 || imageScanNumber == lastPublishedImageScan) {
    return;
  }

  Serial.printf("[MQTT] Mulai publish foto JPEG (%u bytes) ke %s...\n",
                currentImageSize, MQTT_TOPIC_IMAGE);

  bool started = mqttClient.beginPublish(MQTT_TOPIC_IMAGE, currentImageSize, false);
  if (!started) {
    Serial.println("[MQTT] Gagal beginPublish untuk image!");
    return;
  }

  // Kirim bertahap dalam potongan 512 byte agar tidak overflow buffer mbedTLS ESP32
  size_t remaining = currentImageSize;
  size_t offset = 0;
  bool writeOk = true;

  while (remaining > 0 && mqttClient.connected()) {
    size_t chunkSize = remaining > 512 ? 512 : remaining;
    size_t written = mqttClient.write(imageBuffer + offset, chunkSize);
    if (written != chunkSize) {
      Serial.printf("[MQTT] Write error pada offset %u (ditulis %u dari %u)\n",
                    offset, written, chunkSize);
      writeOk = false;
      break;
    }
    offset += written;
    remaining -= written;
    delay(2);
  }

  if (writeOk && mqttClient.endPublish()) {
    lastPublishedImageScan = imageScanNumber;
    Serial.printf("[MQTT] JPEG sukses dipublish (%u bytes)!\n", currentImageSize);
  } else {
    Serial.println("[MQTT] endPublish untuk image gagal!");
  }
}

// ======================================================
// SERIAL COMMAND INTERFACE (UJI LANGSUNG TANPA RELOAD)
// ======================================================

void handleSerial() {
  if (!Serial.available()) return;

  char command = Serial.read();

  if (command == 'R' || command == 'r') {
    motorRight();
  } else if (command == 'L' || command == 'l') {
    motorLeft();
  } else if (command == 'S' || command == 's') {
    motorStop();
  } else if (command == 'T' || command == 't') {
    Serial.println();
    Serial.println(">>> [UJI MOTOR L298N (BYPASS LIMIT SWITCH 1.5 DETIK)] <<<");
    Serial.println("Memutar KANAN selama 1.5 detik...");
    digitalWrite(MOTOR_IN1, HIGH);
    digitalWrite(MOTOR_IN2, LOW);
    delay(1500);
    Serial.println("Memutar KIRI selama 1.5 detik...");
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, HIGH);
    delay(1500);
    motorStop();
    Serial.println(">>> Selesai uji motor. <<<");
  } else if (command == 'K' || command == 'k') {
    Serial.println();
    Serial.println("========================================");
    Serial.println("DIAGNOSA LIMIT SWITCH");
    Serial.println("========================================");
    int rawL = digitalRead(LIMIT_LEFT);
    int rawR = digitalRead(LIMIT_RIGHT);
    Serial.printf("PIN %d (KIRI)  : RAW=%d -> Status: %s\n",
                  LIMIT_LEFT, rawL, isLimitLeftActive() ? "TERTEKAN (BLOCKED)" : "LEPAS (AMAN)");
    Serial.printf("PIN %d (KANAN) : RAW=%d -> Status: %s\n",
                  LIMIT_RIGHT, rawR, isLimitRightActive() ? "TERTEKAN (BLOCKED)" : "LEPAS (AMAN)");
    Serial.printf("Konfigurasi LIMIT_ACTIVE_LEVEL: Active-%s\n",
                  limitActiveLevel == LOW ? "LOW (Normally Open ke GND)" : "HIGH (Normally Closed)");
    Serial.println("Ketik 'P' untuk membalik polaritas Active-LOW <-> Active-HIGH.");
    Serial.println("========================================");
  } else if (command == 'P' || command == 'p') {
    limitActiveLevel = (limitActiveLevel == LOW) ? HIGH : LOW;
    Serial.println();
    Serial.printf(">>> POLARITAS LIMIT SWITCH DIBALIK KE: Active-%s <<<\n",
                  limitActiveLevel == LOW ? "LOW (Normally Open)" : "HIGH (Normally Closed)");
    Serial.printf("Status Kiri : %s | Status Kanan: %s\n",
                  isLimitLeftActive() ? "TERTEKAN" : "LEPAS",
                  isLimitRightActive() ? "TERTEKAN" : "LEPAS");
  } else if (command == 'D' || command == 'd') {
    Serial.println();
    Serial.println("=== UJI BACA SENSOR LANGSUNG ===");
    readDHT();
    readSoil();
    printSensorData();
  } else if (command == 'C' || command == 'c') {
    soilDry = soilRaw;
    Serial.printf(">>> KALIBRASI SOIL KERING DISET KE: %d <<<\n", soilDry);
  } else if (command == 'W' || command == 'w') {
    soilWet = soilRaw;
    Serial.printf(">>> KALIBRASI SOIL BASAH DISET KE: %d <<<\n", soilWet);
  } else if (command == 'F' || command == 'f') {
    Serial.println(">>> Pemicu jepretan foto kamera dikirim manual via Serial! <<<");
    triggerCameraCapture();
  }
}

// ======================================================
// SETUP
// ======================================================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("GRENVIS ESP32 MAIN CONTROLLER v2.0");
  Serial.println("PURE HARDWARE TELEMETRY (NO DUMMY DATA)");
  Serial.println("========================================");

  // ==================================================
  // DHT
  // ==================================================
  pinMode(DHT_PIN, INPUT_PULLUP);
  dht.begin();
  Serial.printf("[DHT] Sensor diaktifkan pada GPIO %d (default mode: %s)\n",
                DHT_PIN, dhtType == DHT22 ? "DHT22" : "DHT11");

  // ==================================================
  // SOIL ANALOG
  // ==================================================
  pinMode(SOIL_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);
  Serial.printf("[SOIL] Sensor tanah diaktifkan pada GPIO %d (ADC 12-bit, 11dB attenuation)\n", SOIL_PIN);

  // ==================================================
  // MOTOR L298N
  // ==================================================
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  motorStop();
  Serial.printf("[MOTOR] Pin motor diatur: IN1=%d, IN2=%d (OUT3/OUT4)\n", MOTOR_IN1, MOTOR_IN2);

  // ==================================================
  // LIMIT SWITCH
  // ==================================================
  pinMode(LIMIT_LEFT, INPUT_PULLUP);
  pinMode(LIMIT_RIGHT, INPUT_PULLUP);
  Serial.printf("[LIMIT] Pin switch: Kiri=%d, Kanan=%d (Boot RAW: L=%d, R=%d)\n",
                LIMIT_LEFT, LIMIT_RIGHT, digitalRead(LIMIT_LEFT), digitalRead(LIMIT_RIGHT));

  if (isLimitLeftActive() && isLimitRightActive()) {
    Serial.println("[LIMIT] PERINGATAN: Kedua limit switch aktif saat boot.");
    Serial.println("[LIMIT] Jika motor tertahan tidak mau gerak, ketik 'P' di Serial Monitor untuk ganti ke Active-HIGH!");
  }

  // ==================================================
  // WIFI & ESP-NOW
  // ==================================================
  connectWiFi();
  initESPNow();

  // ==================================================
  // MQTT TLS
  // ==================================================
  secureClient.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(2048);
  mqttClient.setKeepAlive(30);
  mqttClient.setCallback(onMqttMessage);
  connectMQTT();

  Serial.println();
  Serial.println("========================================");
  Serial.println("SISTEM SIAP - PERINTAH SERIAL TERSEDIA:");
  Serial.println("R = Motor jalan Kanan");
  Serial.println("L = Motor jalan Kiri");
  Serial.println("S = Motor Stop");
  Serial.println("T = Uji jalan motor 1.5s kanan & kiri (bypass limit)");
  Serial.println("K = Cek diagnosa limit switch");
  Serial.println("P = Balik polaritas limit switch (Active-LOW <-> Active-HIGH)");
  Serial.println("D = Baca langsung DHT & sensor tanah sekarang");
  Serial.println("C = Kalibrasi nilai RAW saat ini sebagai KERING");
  Serial.println("W = Kalibrasi nilai RAW saat ini sebagai BASAH");
  Serial.println("F = Pemicu jepretan kamera ke ESP32-CAM");
  Serial.println("========================================");
}

// ======================================================
// LOOP
// ======================================================

void loop() {
  // 1. Pemeriksaan fisik keamanan limit switch
  checkLimitSwitch();

  // 2. Kontrol interaktif Serial
  handleSerial();

  // 3. Jaga koneksi WiFi
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // 4. Jaga koneksi MQTT
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // 5. Kirim data label vision secepatnya begitu diterima dari ESP32-CAM via ESP-NOW
  if (newVisionReady) {
    newVisionReady = false;
    Serial.println("[MQTT] Data inferensi daun baru diterima dari ESP32-CAM -> Langsung kirim ke Web!");
    // Perbarui pembacaan sensor agar telemetri terkini
    readDHT();
    readSoil();
    publishVisionMQTT();
    publishMQTT();
  }

  // 6. Kirim foto JPEG secepatnya begitu transfer ESP-NOW selesai
  if (newImageReady) {
    newImageReady = false;
    Serial.println("[MQTT] Foto JPEG selesai diterima dari ESP32-CAM -> Mulai publish ke Web!");
    publishImageMQTT();
  }

  // 7. Timer periodik sampling sensor (setiap 3 detik)
  if (millis() - lastPublish < PUBLISH_INTERVAL) {
    delay(5);
    return;
  }
  lastPublish = millis();

  // Baca sensor riil dari pin fisik
  readDHT();
  readSoil();

  // Cetak status perangkat
  printSensorData();

  // Publish sensor telemetry JSON ke MQTT
  publishMQTT();

  // Jika data vision pernah ada, sinkronkan juga
  if (visionAvailable) {
    publishVisionMQTT();
  }
}
