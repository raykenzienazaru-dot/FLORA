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

// DHT Sensor (GPIO 18)
// Mendukung DHT22 (default) & DHT11 secara bergantian otomatis tanpa tabrakan pin
#define DHT_PIN 18
uint8_t dhtType = DHT22;
DHT dht(DHT_PIN, DHT22);

// Soil Moisture Sensor (GPIO 32 ADC1_CH4)
#define SOIL_PIN 32

// Kalibrasi Nilai ADC Sensor Tanah (Kapasitif v1.2 bertenaga 5V):
// Daya 5V: di udara terbuka membaca ~3900 - 4095 (kering = 0%)
// Saat dicelupkan ke air: membaca ~1500 - 2200 (basah = 100%)
// Nilai ini dapat dikalibrasi via perintah Serial 'C' (kering) dan 'W' (basah)
// Atau langsung aktifkan preset via tombol '5' (5V) atau '3' (3.3V)
int soilDry = 4000;
int soilWet = 1600;

float soilMoisture = 0.0;
int soilRaw = 0;

// ======================================================
// MOTOR DRIVER L298N
// ======================================================
// PANDUAN PENTING KONEKSI L298N:
// Modul L298N adalah Dual H-Bridge dengan 2 Channel terpisah:
// - Channel A: Input IN1 & IN2 -> Menggerakkan terminal OUT1 & OUT2 (Jumper ENA wajib terpasang)
// - Channel B: Input IN3 & IN4 -> Menggerakkan terminal OUT3 & OUT4 (Jumper ENB wajib terpasang)
//
// JIKA MOTOR DI TERMINAL BAUT OUT3 & OUT4:
//   Kabel dari ESP32 GPIO 16 & GPIO 17 WAJIB DICOLOK KE PIN "IN3" & "IN4" PADA L298N!
//   (Jika dicolok ke IN1 & IN2, arus hanya keluar ke OUT1/OUT2, terminal OUT3/OUT4 TIDAK AKAN HIDUP).
// JIKA INGIN TETAP MENGGUNAKAN PIN "IN1" & "IN2":
//   Pindahkan 2 kabel motor ke terminal baut "OUT1" & "OUT2"!
// CATATAN WAJIB:
// - GND ESP32 wajib digabung ke GND L298N (Common Ground).
// - Jumper ENA (jika OUT1/OUT2) atau ENB (jika OUT3/OUT4) wajib terpasang.
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
#define LIMIT_LEFT 25
#define LIMIT_RIGHT 26

// Polaritas Limit Switch:
// LOW  = Normally Open (NO) switch ke GND (default INPUT_PULLUP)
// HIGH = Normally Closed (NC) atau Active HIGH
// Dapat diubah langsung saat alat berjalan dengan mengetik 'P' di Serial Monitor
uint8_t limitActiveLevel = LOW;
bool limitSafetyBypass = false; // Ketik 'B' di Serial untuk bypass/abaikan limit switch saat uji motor

inline bool isLimitLeftActive() {
  if (limitSafetyBypass) return false;
  return digitalRead(LIMIT_LEFT) == limitActiveLevel;
}

inline bool isLimitRightActive() {
  if (limitSafetyBypass) return false;
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
    Serial.printf("[LIMIT BLOCKED] KANAN AKTIF (PIN %d RAW=%d)! Ketik 'P' di Serial jika polaritas terbalik, atau 'B' untuk bypass.\n",
                  LIMIT_RIGHT, digitalRead(LIMIT_RIGHT));
    motorStop();
    return;
  }
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  motorState = MOTOR_RIGHT;
  Serial.println("[MOTOR] >>> JALAN KANAN (PIN 16 HIGH, PIN 17 LOW)");
}

void motorLeft() {
  if (isLimitLeftActive()) {
    Serial.printf("[LIMIT BLOCKED] KIRI AKTIF (PIN %d RAW=%d)! Ketik 'P' di Serial jika polaritas terbalik, atau 'B' untuk bypass.\n",
                  LIMIT_LEFT, digitalRead(LIMIT_LEFT));
    motorStop();
    return;
  }
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
  motorState = MOTOR_LEFT;
  Serial.println("[MOTOR] <<< JALAN KIRI (PIN 16 LOW, PIN 17 HIGH)");
}

// ======================================================
// TRIGGER CAMERA CAPTURE (ESP-NOW)
// ======================================================

void triggerCameraCapture() {
  const uint8_t request[] = { PACKET_CAPTURE_REQUEST };
  const uint8_t* target = hasLearnedCamMac ? targetCamMac : ESP32_CAM_MAC;

  // 1. Kirim Unicast ke target MAC CAM
  if (!esp_now_is_peer_exist(target)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, target, 6);
    peer.channel = WiFi.channel();
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&peer);
  }
  const esp_err_t resUni = esp_now_send(target, request, sizeof(request));

  // 2. Kirim juga secara BROADCAST (FF:FF:FF:FF:FF:FF)
  // Menjamin ESP32-CAM pasti menerima sinyal capture meskipun MAC CAM berbeda dari default!
  const uint8_t BROADCAST_MAC[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, BROADCAST_MAC, 6);
    bcast.channel = WiFi.channel();
    bcast.encrypt = false;
    bcast.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&bcast);
  }
  const esp_err_t resBcast = esp_now_send(BROADCAST_MAC, request, sizeof(request));

  Serial.println();
  Serial.printf("[ESP-NOW] Trigger foto dikirim ke ESP32-CAM (Unicast: %s, Broadcast: %s)\n",
                resUni == ESP_OK ? "BERHASIL" : "GAGAL",
                resBcast == ESP_OK ? "BERHASIL" : "GAGAL");
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

// Pembaca bit-bang presisi mikrosekon untuk DHT22/DHT11 pada ESP32 (anti gangguan interrupt WiFi)
bool readDHTDirect(uint8_t pin, float &temp, float &hum) {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  // 1. Kirim start pulse: tarik LOW minimal 20ms (kompatibel DHT11 & DHT22 di daya 3.3V/5V)
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(20);
  digitalWrite(pin, HIGH);
  delayMicroseconds(35);

  // 2. Siapkan pin mendengarkan respon sensor
  pinMode(pin, INPUT_PULLUP);

  // Kunci interrupt sementara agar timing mikrosekon tidak diputus oleh stack WiFi/RTOS
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);

  // Tunggu sensor menarik LOW (80us)
  uint32_t timeout = 3000;
  while (digitalRead(pin) == HIGH) {
    if (--timeout == 0) { portEXIT_CRITICAL(&mux); return false; }
    delayMicroseconds(1);
  }

  // Tunggu sensor menarik HIGH (80us)
  timeout = 3000;
  while (digitalRead(pin) == LOW) {
    if (--timeout == 0) { portEXIT_CRITICAL(&mux); return false; }
    delayMicroseconds(1);
  }

  // Tunggu sensor mengakhiri respon HIGH
  timeout = 3000;
  while (digitalRead(pin) == HIGH) {
    if (--timeout == 0) { portEXIT_CRITICAL(&mux); return false; }
    delayMicroseconds(1);
  }

  // 3. Baca 40 bit data
  for (int i = 0; i < 40; i++) {
    // Tunggu sinyal awal bit (LOW ~50us)
    timeout = 3000;
    while (digitalRead(pin) == LOW) {
      if (--timeout == 0) { portEXIT_CRITICAL(&mux); return false; }
      delayMicroseconds(1);
    }

    // Ukur durasi pulsa HIGH: 26-28us = '0', 70us = '1'
    uint32_t tStart = micros();
    timeout = 3000;
    while (digitalRead(pin) == HIGH) {
      if (--timeout == 0) { portEXIT_CRITICAL(&mux); return false; }
      delayMicroseconds(1);
    }
    uint32_t pulseDuration = micros() - tStart;

    if (pulseDuration > 40) {
      data[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  portEXIT_CRITICAL(&mux);

  // 4. Verifikasi checksum 8-bit
  uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
  if (data[4] != checksum) {
    return false;
  }

  // 5. Decode data sesuai tipe sensor yang aktif
  if (dhtType == DHT11) {
    hum = (float)data[0] + (float)data[1] * 0.1f;
    temp = (float)data[2] + (float)data[3] * 0.1f;
  } else {
    // DHT22 (AM2302)
    uint16_t rawHum = ((uint16_t)data[0] << 8) | data[1];
    int16_t rawTemp = (((int16_t)(data[2] & 0x7F)) << 8) | data[3];
    if (data[2] & 0x80) rawTemp = -rawTemp;

    hum = (float)rawHum * 0.1f;
    temp = (float)rawTemp * 0.1f;
  }

  return (hum >= 0.0f && hum <= 100.0f && temp >= -40.0f && temp <= 80.0f);
}

void readDHT() {
  static int dhtFailStreak = 0;
  static unsigned long lastDhtAttempt = 0;

  // Sensor DHT membutuhkan jeda minimal 2 detik antar pembacaan fisik
  if (millis() - lastDhtAttempt < 2000 && lastDhtAttempt != 0) {
    return;
  }
  lastDhtAttempt = millis();

  float t = NAN;
  float h = NAN;

  // Coba metode 1 (Utama): Direct Bit-Bang dengan portENTER_CRITICAL (paling stabil di ESP32)
  float tDirect = 0.0;
  float hDirect = 0.0;
  if (readDHTDirect(DHT_PIN, tDirect, hDirect)) {
    t = tDirect;
    h = hDirect;
  } else {
    // Coba metode 2 (Fallback): Library Adafruit
    t = dht.readTemperature();
    h = dht.readHumidity();
  }

  if (!isnan(t) && !isnan(h) && t > -40.0 && t < 85.0 && h >= 0.0 && h <= 100.0) {
    currentTemperature = t;
    currentHumidity = h;
    dhtFailStreak = 0;
    Serial.printf("[%s] SUKSES: Suhu = %.2f C | Kelembapan = %.2f %%\n",
                  dhtType == DHT22 ? "DHT22" : "DHT11", currentTemperature, currentHumidity);
  } else {
    dhtFailStreak++;
    Serial.printf("[%s] Belum terbaca di GPIO %d (streak: %d). Murni tanpa dummy.\n",
                  dhtType == DHT22 ? "DHT22" : "DHT11", DHT_PIN, dhtFailStreak);
    // Nilai suhu dan kelembapan riil dipertahankan (tidak di-overwrite ke dummy 28.0C / 60.0%)!
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

  // Pemetaan linier sesuai karakteristik sensor kapasitif v1.2 pada daya 5V
  if (soilDry > soilWet) {
    if (soilRaw >= soilDry) {
      soilMoisture = 0.0;
    } else if (soilRaw <= soilWet) {
      soilMoisture = 100.0;
    } else {
      float moisture = (float)(soilDry - soilRaw) / (float)(soilDry - soilWet) * 100.0;
      soilMoisture = constrain(moisture, 0.0, 100.0);
    }
  } else {
    if (soilRaw <= soilDry) {
      soilMoisture = 0.0;
    } else if (soilRaw >= soilWet) {
      soilMoisture = 100.0;
    } else {
      float moisture = (float)(soilRaw - soilDry) / (float)(soilWet - soilDry) * 100.0;
      soilMoisture = constrain(moisture, 0.0, 100.0);
    }
  }

  Serial.printf("[SOIL] RAW: %d | Terbaca: %.1f%% (Batas Kering: %d, Basah: %d)\n",
                soilRaw, soilMoisture, soilDry, soilWet);
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

  char msg[64];
  size_t copyLen = length < 63 ? length : 63;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  char command = msg[0];

  // Dukung JSON: {"command":"R"} atau {"action":"right"}
  if (command == '{') {
    String str = String(msg);
    if (str.indexOf("\"R\"") != -1 || str.indexOf("\"r\"") != -1 || str.indexOf("\"right\"") != -1) command = 'R';
    else if (str.indexOf("\"L\"") != -1 || str.indexOf("\"l\"") != -1 || str.indexOf("\"left\"") != -1) command = 'L';
    else if (str.indexOf("\"S\"") != -1 || str.indexOf("\"s\"") != -1 || str.indexOf("\"stop\"") != -1) command = 'S';
    else if (str.indexOf("\"C\"") != -1 || str.indexOf("\"c\"") != -1 || str.indexOf("\"capture\"") != -1) command = 'C';
  }

  Serial.printf("[MQTT] Perintah kontrol diterima: %c (payload: %s)\n", command, msg);

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
    Serial.println("==================================================");
    Serial.println(">>> UJI MOTOR L298N (BYPASS LIMIT SWITCH 1.5s) <<<");
    Serial.println("==================================================");
    Serial.println("INFO PIN: GPIO 16 & GPIO 17");
    Serial.println("CATATAN: Jika motor di baut OUT3/OUT4, kabel ESP32 WAJIB ke IN3/IN4 (bukan IN1/IN2)!");
    Serial.println("Pastikan jumper hitam ENB terpasang & GND L298N digabung ke GND ESP32.");
    Serial.println("Memutar KANAN (PIN 16 HIGH, PIN 17 LOW) selama 1.5s...");
    digitalWrite(MOTOR_IN1, HIGH);
    digitalWrite(MOTOR_IN2, LOW);
    delay(1500);
    Serial.println("Memutar KIRI (PIN 16 LOW, PIN 17 HIGH) selama 1.5s...");
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, HIGH);
    delay(1500);
    motorStop();
    Serial.println(">>> Selesai uji motor. <<<");
    Serial.println("==================================================");
  } else if (command == 'B' || command == 'b') {
    limitSafetyBypass = !limitSafetyBypass;
    Serial.println();
    Serial.printf(">>> LIMIT SWITCH SAFETY BYPASS: %s <<<\n",
                  limitSafetyBypass ? "AKTIF (Limit Switch Diabaikan / Motor Bebas Berjalan)" : "NONAKTIF (Proteksi Limit Switch Aktif)");
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
  } else if (command == '1') {
    dhtType = DHT11;
    dht = DHT(DHT_PIN, DHT11);
    dht.begin();
    Serial.println(">>> MODE DHT DIPAKSA KE: DHT11 (Sensor Warna Biru) <<<");
    readDHT();
    printSensorData();
  } else if (command == '2') {
    dhtType = DHT22;
    dht = DHT(DHT_PIN, DHT22);
    dht.begin();
    Serial.println(">>> MODE DHT DIPAKSA KE: DHT22 (Sensor Warna Putih AM2302) <<<");
    readDHT();
    printSensorData();
  } else if (command == '5') {
    soilDry = 4000;
    soilWet = 1600;
    Serial.println(">>> PRESET KALIBRASI SENSOR 5V DIAKTIFKAN: Kering=4000, Basah=1600 <<<");
    readSoil();
    printSensorData();
  } else if (command == '3') {
    soilDry = 3000;
    soilWet = 1350;
    Serial.println(">>> PRESET KALIBRASI SENSOR 3.3V DIAKTIFKAN: Kering=3000, Basah=1350 <<<");
    readSoil();
    printSensorData();
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
  Serial.println("B = Toggle Safety Bypass Limit Switch (Abaikan limit switch untuk uji motor)");
  Serial.println("K = Cek diagnosa limit switch");
  Serial.println("P = Balik polaritas limit switch (Active-LOW <-> Active-HIGH)");
  Serial.println("D = Baca langsung DHT & sensor tanah sekarang");
  Serial.println("C = Kalibrasi nilai RAW saat ini sebagai KERING");
  Serial.println("W = Kalibrasi nilai RAW saat ini sebagai BASAH");
  Serial.println("1 = Paksa protokol DHT11 (Sensor Warna Biru)");
  Serial.println("2 = Paksa protokol DHT22 (Sensor Warna Putih AM2302)");
  Serial.println("5 = Atur preset ambang batas sensor tanah daya 5V (default)");
  Serial.println("3 = Atur preset ambang batas sensor tanah daya 3.3V");
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
