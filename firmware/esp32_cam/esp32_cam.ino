#include "esp_camera.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

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

// =====================================================
// ESP-NOW PACKET TYPE (HARUS SAMA DENGAN ESP32 UTAMA)
// =====================================================

#define PACKET_RESULT          1
#define PACKET_IMAGE_START     2
#define PACKET_IMAGE_CHUNK     3
#define PACKET_IMAGE_END       4
#define PACKET_CAPTURE_REQUEST 5

// Channel ESP-NOW harus sama persis dengan ESP32 Utama (Channel 1)
constexpr uint8_t ESP_NOW_CHANNEL = 1;

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
// INIT CAMERA
// =====================================================

bool initCamera() {
  Serial.println();
  Serial.println("==============================");
  Serial.println("CAMERA INIT (FLORA SENSOR)");
  Serial.println("==============================");

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

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.println("[CAM] PSRAM ditemukan -> Frame Size QVGA / High Quality");
    config.frame_size   = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("[CAM] No PSRAM -> Fallback");
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init FAILED: 0x%x\n", err);
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

  Serial.println("[CAM] CAMERA READY");
  return true;
}

// =====================================================
// INIT ESP-NOW
// =====================================================

void onEspNowReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* incomingData,
  int len
) {
  if (info == nullptr || incomingData == nullptr || len != 1) return;
  if (incomingData[0] == PACKET_CAPTURE_REQUEST) {
    captureRequested = true;
    Serial.println("[ESP-NOW] CAPTURE REQUEST RECEIVED");
  }
}

bool initESPNow() {
  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP-NOW INIT");
  Serial.println("==============================");

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  delay(300);

  Serial.print("ESP32-CAM MAC : ");
  Serial.println(WiFi.macAddress());
  Serial.print("WiFi Channel  : ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED!");
    return false;
  }

  esp_now_register_recv_cb(onEspNowReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Add peer FAILED!");
    return false;
  }

  Serial.println("[ESP-NOW] READY");
  return true;
}

// =====================================================
// SEND SCAN METADATA VIA ESP-NOW
// =====================================================

void sendScanResult(uint32_t scanNumber, uint32_t imageSize) {
  VisionResultPacket packet;
  packet.packetType = PACKET_RESULT;
  packet.scanNumber = scanNumber;
  strncpy(packet.visionClass, "StreamReady", sizeof(packet.visionClass) - 1);
  packet.confidence = 100.0;
  packet.healthy    = 0.0;
  packet.powdery    = 0.0;
  packet.rust       = 0.0;
  packet.imageSize  = imageSize;

  esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&packet, sizeof(packet));
  if (result == ESP_OK) {
    Serial.println("[ESP-NOW] Scan metadata sent");
  } else {
    Serial.printf("[ESP-NOW] Result send error: %d\n", result);
  }
}

// =====================================================
// SEND JPEG VIA ESP-NOW CHUNKS
// =====================================================

void sendImageESPNow(camera_fb_t* fb, uint32_t scanNumber) {
  if (fb == nullptr || fb->len == 0) return;

  uint32_t imageSize = fb->len;
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
    memcpy(chunkPacket.data, fb->buf + offset, chunkLength);

    esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&chunkPacket, sizeof(chunkPacket));
    if (result != ESP_OK) {
      Serial.printf("[IMAGE] Chunk error: %u\n", i);
    }

    // Delay singkat agar radio tidak kebanjiran buffer
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
// CAPTURE & STREAM IMAGE
// =====================================================

void captureAndSend() {
  scanCounter++;
  Serial.println();
  Serial.println("==============================");
  Serial.printf("SCAN #%lu: CAPTURING IMAGE\n", (unsigned long)scanCounter);
  Serial.println("==============================");

  // Flash LED singkat untuk pencahayaan
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(150);

  camera_fb_t* fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!fb) {
    Serial.println("[CAM] Capture gagal!");
    return;
  }

  Serial.printf("[CAM] JPEG captured: %u bytes\n", (unsigned int)fb->len);
  Serial.println("[AI] AI Inference offloaded to software/dashboard (Hardware load: 0%)");

  // Kirim metadata terlebih dahulu
  sendScanResult(scanCounter, fb->len);
  delay(50);

  // Kirim frame gambar via ESP-NOW
  sendImageESPNow(fb, scanCounter);

  // Bebaskan framebuffer
  esp_camera_fb_return(fb);
  Serial.println("[CAM] Frame complete & memory released");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("FLORA ESP32-CAM SENSOR NODE");
  Serial.println("==============================");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("[SYSTEM] Camera failed!");
    while (true) delay(1000);
  }

  if (!initESPNow()) {
    Serial.println("[SYSTEM] ESP-NOW failed!");
    while (true) delay(1000);
  }

  Serial.println();
  Serial.println("==============================");
  Serial.println("SYSTEM READY (PURE SENSOR STREAMER)");
  Serial.println("Ketik 'A' di Serial Monitor untuk uji capture.");
  Serial.println("==============================");
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
