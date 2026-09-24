#include "esp_camera.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// =====================================================
// TFLITE CLOUD AI BACKEND (RAILWAY / PUBLIC API)
// =====================================================
// API Base URL: https://web-production-e0039.up.railway.app/
// Swagger Docs: https://web-production-e0039.up.railway.app/docs
// Endpoint: https://web-production-e0039.up.railway.app/predict
// Kirim citra daun asli langsung dari kamera OV2640 ke model AI_VISION.tflite
const char* AI_BACKEND_URL = "https://web-production-e0039.up.railway.app/predict";

// =====================================================
// AI THINKER ESP32-CAM PIN DEFINITIONS
// =====================================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// =====================================================
// FLASH LED
// =====================================================

#define FLASH_LED_PIN 4

// =====================================================
// MAC ESP32 UTAMA
// =====================================================

uint8_t receiverMAC[] = {
  0x8C,
  0xAA,
  0xB5,
  0x37,
  0x71,
  0x18
};

// =====================================================
// SCAN COUNTER & TRIGGERS
// =====================================================

uint32_t scanCounter = 0;
volatile bool captureRequested = false;
bool cameraHardwareReady = false;

// =====================================================
// ESP-NOW PACKET TYPE (HARUS SAMA DENGAN ESP32 UTAMA)
// =====================================================

#define PACKET_RESULT          1
#define PACKET_IMAGE_START     2
#define PACKET_IMAGE_CHUNK     3
#define PACKET_IMAGE_END       4
#define PACKET_CAPTURE_REQUEST 5

// Channel ESP-NOW harus sama persis dengan ESP32 Utama (Channel 6)
constexpr uint8_t ESP_NOW_CHANNEL = 6;

// =====================================================
// RESULT PACKET (LIGHTWEIGHT METADATA)
// =====================================================

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

// =====================================================
// IMAGE START PACKET
// =====================================================

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint32_t scanNumber;
  uint32_t imageSize;
  uint16_t totalChunks;
} ImageStartPacket;

// =====================================================
// IMAGE CHUNK PACKET
// =====================================================

#define IMAGE_CHUNK_SIZE 200

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint32_t scanNumber;
  uint16_t chunkIndex;
  uint16_t dataLength;
  uint8_t data[IMAGE_CHUNK_SIZE];
} ImageChunkPacket;

// =====================================================
// IMAGE END PACKET
// =====================================================

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint32_t scanNumber;
  uint16_t totalChunks;
} ImageEndPacket;

// =====================================================
// INIT HARDWARE CAMERA (OV2640)
// =====================================================

bool initCamera() {
  Serial.println();
  Serial.println("========================================");
  Serial.println("CAMERA INIT (HARDWARE OV2640 SENSOR)");
  Serial.println("========================================");

  // Power cycle sensor OV2640 via PWDN (GPIO 32)
  if (PWDN_GPIO_NUM != -1) {
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // Power OFF sensor
    delay(100);
    digitalWrite(PWDN_GPIO_NUM, LOW);  // Power ON sensor
    delay(100);
  }

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.println("[CAM] PSRAM terdeteksi -> Mode High Quality QVGA");
    config.frame_size   = FRAMESIZE_QVGA; // 320x240 (Optimal untuk TFLite 224x224)
    config.jpeg_quality = 10;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("[CAM] PSRAM tidak aktif -> Alokasi internal DRAM");
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  }

  // Coba inisialisasi: Percobaan 1 (20MHz), Percobaan 2 (10MHz), Percobaan 3 (16.5MHz)
  esp_err_t err = ESP_FAIL;
  int freqs[] = { 20000000, 10000000, 16500000 };

  for (int attempt = 0; attempt < 3; attempt++) {
    config.xclk_freq_hz = freqs[attempt];
    Serial.printf("[CAM] Probe sensor OV2640 (Percobaan %d/3, XCLK: %d MHz)...\n", 
                  attempt + 1, config.xclk_freq_hz / 1000000);

    err = esp_camera_init(&config);
    if (err == ESP_OK) {
      Serial.printf("[CAM] Probe BERHASIL pada XCLK %d MHz!\n", config.xclk_freq_hz / 1000000);
      break;
    }

    Serial.printf("[CAM] Probe gagal (Error: 0x%x). Deinit & reset daya...\n", err);
    esp_camera_deinit();
    delay(200);

    // Reset daya modul sensor
    if (PWDN_GPIO_NUM != -1) {
      digitalWrite(PWDN_GPIO_NUM, HIGH);
      delay(100);
      digitalWrite(PWDN_GPIO_NUM, LOW);
      delay(150);
    }
  }

  if (err != ESP_OK) {
    Serial.printf("[CAM ERROR] Hardware OV2640 GAGAL diinisialisasi (0x%x)!\n", err);
    Serial.println("[TROUBLESHOOTING]:");
    Serial.println("  1. Pastikan pita kamera OV2640 terkunci rapat di konektor FPC board.");
    Serial.println("  2. Pastikan di Arduino IDE: Tools > Board: 'AI Thinker ESP32-CAM' & 'PSRAM: Enabled'.");
    Serial.println("  3. Gunakan catu daya stabil 5V 2A ke pin 5V ESP32-CAM, BUKAN 3.3V.");
    cameraHardwareReady = false;
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s != nullptr) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
  }

  cameraHardwareReady = true;
  Serial.println("[CAM] CAMERA READY (HARDWARE OV2640 AKTIF & SIAP CAPTURE)");
  return true;
}

// =====================================================
// WIFI (SINKRONISASI CHANNEL OTOMATIS KE ESP32 UTAMA)
// =====================================================
// Bila diisi sama dengan ESP32 Utama, ESP32-CAM akan otomatis memakai channel yang sama.
const char* WIFI_SSID     = ".";
const char* WIFI_PASSWORD = "01020304";

// =====================================================
// INIT ESP-NOW
// =====================================================

void onEspNowReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* incomingData,
  int len
) {
  if (incomingData == nullptr || len < 1) return;

  // Sinkronkan MAC pengirim (ESP32-Main) otomatis agar foto kembali ke board yang benar
  if (info != nullptr && info->src_addr != nullptr) {
    memcpy(receiverMAC, info->src_addr, 6);
    if (!esp_now_is_peer_exist(receiverMAC)) {
      esp_now_peer_info_t peer = {};
      memcpy(peer.peer_addr, receiverMAC, 6);
      peer.channel = WiFi.channel();
      peer.encrypt = false;
      esp_now_add_peer(&peer);
    }
  }

  Serial.printf("[ESP-NOW] Paket diterima: %d byte (Tipe: %d)\n", len, incomingData[0]);

  if (incomingData[0] == PACKET_CAPTURE_REQUEST) {
    captureRequested = true;
    Serial.println();
    Serial.println(">>> [ESP-NOW] TRIGGER FOTO DITERIMA DARI WEB / ESP32-MAIN! <<<");
  }
}

bool initESPNow() {
  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP-NOW INIT");
  Serial.println("==============================");

  WiFi.mode(WIFI_STA);

  uint8_t activeChannel = ESP_NOW_CHANNEL;

  // Coba sinkronisasi channel otomatis via WiFi AP yang sama dengan ESP32 Main
  if (strlen(WIFI_SSID) > 0) {
    Serial.printf("[WIFI] Menghubungkan ke AP '%s' untuk internet & channel...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 5000) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      activeChannel = WiFi.channel();
      Serial.printf("[WIFI] Terhubung! IP: %s | Channel: %d\n", WiFi.localIP().toString().c_str(), activeChannel);
    } else {
      Serial.printf("[WIFI] Belum terhubung. Menggunakan channel default: %d\n", ESP_NOW_CHANNEL);
      esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    }
  } else {
    esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  }

  Serial.printf("ESP32-CAM MAC : %s\n", WiFi.macAddress().c_str());
  Serial.printf("Target MAC    : %02X:%02X:%02X:%02X:%02X:%02X\n",
    receiverMAC[0], receiverMAC[1], receiverMAC[2],
    receiverMAC[3], receiverMAC[4], receiverMAC[5]);
  Serial.printf("WiFi Channel  : %d\n", activeChannel);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED!");
    return false;
  }

  esp_now_register_recv_cb(onEspNowReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = activeChannel;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Add peer FAILED!");
    return false;
  }

  Serial.println("[ESP-NOW] READY");
  return true;
}

// =====================================================
// QUERY CLOUD TFLITE AI BACKEND (RAILWAY)
// =====================================================

bool queryCloudAiVision(const uint8_t* jpegBuf, size_t jpegLen, VisionResultPacket* outResult) {
  if (strlen(AI_BACKEND_URL) == 0 || jpegBuf == nullptr || jpegLen == 0) {
    Serial.println("[AI-CLOUD] Parameter buffer gambar kosong / URL belum diset");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[AI-CLOUD] WiFi tidak terhubung ke internet, tidak dapat mengakses Railway Cloud Backend.");
    return false;
  }

  Serial.println();
  Serial.printf("[AI-CLOUD] Mengirim %u byte JPEG asli ke TFLite Backend...\n", (unsigned int)jpegLen);
  Serial.printf("[AI-CLOUD] Endpoint: %s\n", AI_BACKEND_URL);

  WiFiClientSecure client;
  client.setInsecure(); // Bypass SSL cert verification untuk HTTPS Railway

  HTTPClient http;
  if (!http.begin(client, AI_BACKEND_URL)) {
    Serial.println("[AI-CLOUD] Inisialisasi HTTPClient gagal");
    return false;
  }

  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(10000); // 10 detik timeout

  int httpCode = http.POST((uint8_t*)jpegBuf, jpegLen);
  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    String payload = http.getString();
    Serial.printf("[AI-CLOUD] Respon TFLite: %s\n", payload.c_str());

    int predIdx = payload.indexOf("\"prediction\":\"");
    if (predIdx != -1) {
      int predEnd = payload.indexOf("\"", predIdx + 14);
      String pred = payload.substring(predIdx + 14, predEnd);
      strncpy(outResult->visionClass, pred.c_str(), sizeof(outResult->visionClass) - 1);
      outResult->visionClass[sizeof(outResult->visionClass) - 1] = '\0';
    }

    auto parseVal = [&](const char* key, float fallback) -> float {
      int idx = payload.indexOf(key);
      if (idx != -1) {
        int start = idx + strlen(key);
        int end = payload.indexOf(",", start);
        if (end == -1) end = payload.indexOf("}", start);
        if (end != -1) return payload.substring(start, end).toFloat();
      }
      return fallback;
    };

    outResult->healthy = parseVal("\"healthy\":", 0.0);
    outResult->powdery = parseVal("\"powdery\":", 0.0);
    outResult->rust = parseVal("\"rust\":", 0.0);
    outResult->confidence = parseVal("\"confidence\":", 0.0);

    http.end();
    Serial.printf("[AI-CLOUD] Sukses! Hasil TFLite: %s (%.2f%%) [H:%.1f%% P:%.1f%% R:%.1f%%]\n",
                  outResult->visionClass, outResult->confidence,
                  outResult->healthy, outResult->powdery, outResult->rust);
    return true;
  } else {
    Serial.printf("[AI-CLOUD] Backend belum merespon / offline (HTTP Code: %d)\n", httpCode);
  }

  http.end();
  return false;
}

// =====================================================
// SEND SCAN METADATA VIA ESP-NOW
// =====================================================

void sendScanResult(uint32_t scanNumber, uint32_t imageSize, const VisionResultPacket* cloudResult = nullptr) {
  VisionResultPacket packet;
  packet.packetType = PACKET_RESULT;
  packet.scanNumber = scanNumber;
  packet.imageSize  = imageSize;

  if (cloudResult != nullptr && strlen(cloudResult->visionClass) > 0) {
    // Gunakan hasil inferensi TFLite asli dari Cloud Backend Railway
    strncpy(packet.visionClass, cloudResult->visionClass, sizeof(packet.visionClass) - 1);
    packet.visionClass[sizeof(packet.visionClass) - 1] = '\0';
    packet.confidence = cloudResult->confidence;
    packet.healthy    = cloudResult->healthy;
    packet.powdery    = cloudResult->powdery;
    packet.rust       = cloudResult->rust;
    Serial.println("[AI] Mengirim hasil inferensi TFLite Railway asli!");
  } else {
    // Cloud offline / belum ada hasil inferensi
    strncpy(packet.visionClass, "Unprocessed", sizeof(packet.visionClass) - 1);
    packet.visionClass[sizeof(packet.visionClass) - 1] = '\0';
    packet.confidence = 0.0;
    packet.healthy    = 0.0;
    packet.powdery    = 0.0;
    packet.rust       = 0.0;
    Serial.println("[AI] Backend Cloud belum merespon (Tanpa data dummy).");
  }

  esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&packet, sizeof(packet));
  if (result == ESP_OK) {
    Serial.println("[ESP-NOW] Scan metadata sent to ESP32 Utama");
  } else {
    Serial.printf("[ESP-NOW] Result send error: %d\n", result);
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println("HASIL DETEKSI AI DAUN (ESP32-CAM)");
  Serial.println("========================================");
  Serial.printf("Healthy       : %.2f%%\n", packet.healthy);
  Serial.printf("Powdery       : %.2f%%\n", packet.powdery);
  Serial.printf("Rust          : %.2f%%\n", packet.rust);
  Serial.printf("Prediksi      : %s (%.2f%%)\n", packet.visionClass, packet.confidence);
  Serial.println("========================================");
}

// =====================================================
// SEND JPEG VIA ESP-NOW CHUNKS
// =====================================================

void sendImageBufferESPNow(const uint8_t* buf, uint32_t imageSize, uint32_t scanNumber) {
  if (buf == nullptr || imageSize == 0) return;

  uint16_t totalChunks = (imageSize + IMAGE_CHUNK_SIZE - 1) / IMAGE_CHUNK_SIZE;

  Serial.println();
  Serial.println("==============================");
  Serial.printf("SENDING JPEG VIA ESP-NOW (%u bytes, %u chunks)\n", (unsigned int)imageSize, totalChunks);
  Serial.println("==============================");

  // 1. START PACKET
  ImageStartPacket startPacket;
  startPacket.packetType = PACKET_IMAGE_START;
  startPacket.scanNumber = scanNumber;
  startPacket.imageSize  = imageSize;
  startPacket.totalChunks = totalChunks;

  esp_now_send(receiverMAC, (uint8_t*)&startPacket, sizeof(startPacket));
  delay(20);

  // 2. CHUNKS
  for (uint16_t i = 0; i < totalChunks; i++) {
    ImageChunkPacket chunkPacket;
    chunkPacket.packetType = PACKET_IMAGE_CHUNK;
    chunkPacket.scanNumber = scanNumber;
    chunkPacket.chunkIndex = i;

    uint32_t offset = i * IMAGE_CHUNK_SIZE;
    uint16_t remaining = imageSize - offset;
    uint16_t chunkLength = (remaining > IMAGE_CHUNK_SIZE) ? IMAGE_CHUNK_SIZE : remaining;

    chunkPacket.dataLength = chunkLength;
    memcpy(chunkPacket.data, buf + offset, chunkLength);

    esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&chunkPacket, sizeof(chunkPacket));
    if (result != ESP_OK) {
      Serial.printf("[IMAGE] Chunk error: %u\n", i);
    }

    // Delay singkat agar radio buffer tidak overflow
    delay(15);

    if (i % 10 == 0 || i == totalChunks - 1) {
      Serial.printf("[IMAGE] Sending chunk %u/%u\n", i + 1, totalChunks);
    }
  }

  // 3. END PACKET
  ImageEndPacket endPacket;
  endPacket.packetType = PACKET_IMAGE_END;
  endPacket.scanNumber = scanNumber;
  endPacket.totalChunks = totalChunks;

  esp_now_send(receiverMAC, (uint8_t*)&endPacket, sizeof(endPacket));
  Serial.println("[IMAGE] SEND FINISHED");
}

// =====================================================
// CAPTURE & STREAM REAL HARDWARE IMAGE
// =====================================================

void captureAndSend() {
  scanCounter++;
  Serial.println();
  Serial.println("========================================");
  Serial.printf("SCAN #%lu: CAPTURING REAL CAMERA IMAGE\n", (unsigned long)scanCounter);
  Serial.println("========================================");

  if (!cameraHardwareReady) {
    Serial.println("[CAM] Kamera belum siap, mencoba re-probe OV2640...");
    if (!initCamera()) {
      Serial.println("[CAM ERROR] Sensor OV2640 fisik tidak terhubung. Pembatalan capture.");
      return;
    }
  }

  // Flash LED singkat untuk efek pencahayaan foto
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(120);

  // Ambil frame asli dari sensor kamera OV2640
  camera_fb_t* fb = esp_camera_fb_get();

  // Matikan Flash LED segera setelah exposure
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!fb || fb->len == 0) {
    Serial.println("[CAM ERROR] Pengambilan gambar dari sensor OV2640 gagal!");
    if (fb) esp_camera_fb_return(fb);
    return;
  }

  const uint8_t* imageBuffer = fb->buf;
  uint32_t imageSize = fb->len;
  Serial.printf("[CAM SUCCESS] Berhasil capture foto asli OV2640: %u bytes\n", (unsigned int)imageSize);

  // 1. Jalankan inferensi TFLite Cloud di Railway jika WiFi & backend aktif
  VisionResultPacket cloudResult = {};
  bool hasCloud = queryCloudAiVision(imageBuffer, imageSize, &cloudResult);

  // 2. Kirim frame gambar asli via ESP-NOW ke ESP32 Utama
  sendImageBufferESPNow(imageBuffer, imageSize, scanCounter);
  delay(50);

  // 3. Kirim hasil klasifikasi AI asli
  sendScanResult(scanCounter, imageSize, hasCloud ? &cloudResult : nullptr);

  // Bebaskan framebuffer kamera
  esp_camera_fb_return(fb);
  Serial.println("[CAM] Frame selesai & memori framebuffer dibebaskan");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  // Matikan brownout detector agar modul tidak reset saat lonjakan arus kamera/flash/WiFi
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("GRENVIS FLORA ESP32-CAM SENSOR NODE");
  Serial.println("PURE HARDWARE CAMERA (NO DUMMY IMAGE)");
  Serial.println("========================================");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("[SYSTEM] PERINGATAN: Sensor kamera OV2640 fisik belum terdeteksi!");
    Serial.println("[SYSTEM] Periksa kabel pita & daya 5V. Ulangi inisialisasi saat capture dipanggil.");
  } else {
    Serial.println("[SYSTEM] Hardware OV2640 terdeteksi & siap 100%.");
  }

  if (!initESPNow()) {
    Serial.println("[SYSTEM] ESP-NOW failed!");
    while (true) delay(1000);
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println("SYSTEM READY (REAL HARDWARE STREAMER)");
  Serial.println("Ketik 'A' di Serial Monitor untuk uji capture fisik.");
  Serial.println("========================================");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  // 1. Trigger manual dari Serial Monitor PC (ketik 'A' lalu Enter)
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'A' || cmd == 'a') {
      Serial.println("[MANUAL] Trigger foto dari Serial Monitor");
      captureAndSend();
    }
  }

  // 2. Trigger otomatis dari Limit Switch atau tombol Capture di Web
  if (captureRequested) {
    captureRequested = false;
    Serial.println("[TRIGGER] Trigger foto dari Limit Switch / Web");
    captureAndSend();
  }

  delay(20);
}
