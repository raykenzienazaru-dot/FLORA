#include "esp_camera.h"
#include "AI_VISION_INT8.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "img_converters.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// =====================================================
// AI THINKER ESP32-CAM PIN
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
// FLASH
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
// LABEL AI
// =====================================================

const char* CLASS_NAMES[3] = {
  "Healthy",
  "Powdery",
  "Rust"
};

// =====================================================
// TFLITE
// =====================================================

const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;

TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

uint8_t* tensorArena = nullptr;

constexpr size_t TENSOR_ARENA_SIZE =
  2400 * 1024;

// =====================================================
// RGB BUFFER
// =====================================================

constexpr int CAMERA_WIDTH = 240;
constexpr int CAMERA_HEIGHT = 240;

constexpr size_t RGB_BUFFER_SIZE =
  CAMERA_WIDTH *
  CAMERA_HEIGHT *
  3;

uint8_t* rgbBuffer = nullptr;

// =====================================================
// SCAN COUNTER
// =====================================================

uint32_t scanCounter = 0;
volatile bool captureRequested = false;

// =====================================================
// ESP-NOW PACKET TYPE
// =====================================================

#define PACKET_RESULT 1
#define PACKET_IMAGE_START 2
#define PACKET_IMAGE_CHUNK 3
#define PACKET_IMAGE_END 4
#define PACKET_CAPTURE_REQUEST 5

// Channel dari ESP32 utama berdasarkan log perangkat. ESP-NOW hanya bekerja
// jika kedua node memakai channel yang sama.
constexpr uint8_t ESP_NOW_CHANNEL = 6;

// =====================================================
// AI RESULT PACKET
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
// IMAGE CHUNK
// =====================================================

// ESP-NOW payload dibatasi.
// Kita pakai 200 byte agar aman.

#define IMAGE_CHUNK_SIZE 200

typedef struct __attribute__((packed)) {

  uint8_t packetType;

  uint32_t scanNumber;

  uint16_t chunkIndex;

  uint16_t dataLength;

  uint8_t data[IMAGE_CHUNK_SIZE];

} ImageChunkPacket;

// =====================================================
// IMAGE END
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
  Serial.println("CAMERA INIT");
  Serial.println("==============================");

  camera_config_t config = {};

  config.ledc_channel =
    LEDC_CHANNEL_0;

  config.ledc_timer =
    LEDC_TIMER_0;

  config.pin_d0 =
    Y2_GPIO_NUM;

  config.pin_d1 =
    Y3_GPIO_NUM;

  config.pin_d2 =
    Y4_GPIO_NUM;

  config.pin_d3 =
    Y5_GPIO_NUM;

  config.pin_d4 =
    Y6_GPIO_NUM;

  config.pin_d5 =
    Y7_GPIO_NUM;

  config.pin_d6 =
    Y8_GPIO_NUM;

  config.pin_d7 =
    Y9_GPIO_NUM;

  config.pin_xclk =
    XCLK_GPIO_NUM;

  config.pin_pclk =
    PCLK_GPIO_NUM;

  config.pin_vsync =
    VSYNC_GPIO_NUM;

  config.pin_href =
    HREF_GPIO_NUM;

  config.pin_sccb_sda =
    SIOD_GPIO_NUM;

  config.pin_sccb_scl =
    SIOC_GPIO_NUM;

  config.pin_pwdn =
    PWDN_GPIO_NUM;

  config.pin_reset =
    RESET_GPIO_NUM;

  config.xclk_freq_hz =
    20000000;

  // JPEG agar mudah dikirim
  config.pixel_format =
    PIXFORMAT_JPEG;

  config.frame_size =
    FRAMESIZE_240X240;

  // kualitas sedang supaya ukuran kecil
  config.jpeg_quality =
    14;

  config.fb_count =
    1;

  if (psramFound()) {

    config.fb_location =
      CAMERA_FB_IN_PSRAM;

  } else {

    config.fb_location =
      CAMERA_FB_IN_DRAM;
  }

  config.grab_mode =
    CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err =
    esp_camera_init(
      &config
    );

  if (
    err != ESP_OK
  ) {

    Serial.print(
      "[CAM] Init gagal: 0x"
    );

    Serial.println(
      err,
      HEX
    );

    return false;
  }

  Serial.println(
    "[CAM] READY"
  );

  return true;
}

// =====================================================
// INIT AI
// =====================================================

bool initModel() {

  Serial.println();
  Serial.println("==============================");
  Serial.println("AI MODEL INIT");
  Serial.println("==============================");

  if (!psramFound()) {

    Serial.println(
      "[AI] PSRAM tidak ditemukan!"
    );

    return false;
  }

  Serial.print(
    "[AI] Total PSRAM : "
  );

  Serial.print(
    ESP.getPsramSize() / 1024
  );

  Serial.println(
    " KB"
  );

  Serial.print(
    "[AI] Free PSRAM : "
  );

  Serial.print(
    ESP.getFreePsram() / 1024
  );

  Serial.println(
    " KB"
  );

  model =
    tflite::GetModel(
      AI_VISION_INT8_tflite
    );

  if (
    model->version()
    !=
    TFLITE_SCHEMA_VERSION
  ) {

    Serial.println(
      "[AI] Schema tidak cocok!"
    );

    return false;
  }

  static
  tflite::MicroMutableOpResolver<8>
  resolver;

  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddReshape();
  resolver.AddMean();
  resolver.AddQuantize();
  resolver.AddDequantize();

  tensorArena =
    (uint8_t*)
    ps_malloc(
      TENSOR_ARENA_SIZE
    );

  if (
    tensorArena ==
    nullptr
  ) {

    Serial.println(
      "[AI] Tensor Arena gagal!"
    );

    return false;
  }

  static
  tflite::MicroInterpreter
  staticInterpreter(
    model,
    resolver,
    tensorArena,
    TENSOR_ARENA_SIZE
  );

  interpreter =
    &staticInterpreter;

  Serial.println(
    "[AI] AllocateTensors..."
  );

  if (
    interpreter->AllocateTensors()
    !=
    kTfLiteOk
  ) {

    Serial.println(
      "[AI] AllocateTensors FAILED"
    );

    return false;
  }

  input =
    interpreter->input(0);

  output =
    interpreter->output(0);

  rgbBuffer =
    (uint8_t*)
    ps_malloc(
      RGB_BUFFER_SIZE
    );

  if (
    rgbBuffer ==
    nullptr
  ) {

    Serial.println(
      "[AI] RGB buffer gagal!"
    );

    return false;
  }

  Serial.println(
    "[AI] MODEL READY"
  );

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

  WiFi.mode(
    WIFI_STA
  );

  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  delay(
    300
  );

  Serial.print(
    "[ESP-NOW] MAC CAM : "
  );

  Serial.println(
    WiFi.macAddress()
  );

  Serial.println(
    "[ESP-NOW] Target : 8C:AA:B5:37:71:18"
  );

  if (
    esp_now_init()
    !=
    ESP_OK
  ) {

    Serial.println(
      "[ESP-NOW] Init gagal"
    );

    return false;
  }

  esp_now_peer_info_t peerInfo;

  memset(
    &peerInfo,
    0,
    sizeof(peerInfo)
  );

  memcpy(
    peerInfo.peer_addr,
    receiverMAC,
    6
  );

  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = false;

  if (
    !esp_now_is_peer_exist(
      receiverMAC
    )
  ) {

    if (
      esp_now_add_peer(
        &peerInfo
      )
      !=
      ESP_OK
    ) {

      Serial.println(
        "[ESP-NOW] Add peer gagal"
      );

      return false;
    }
  }

  esp_now_register_recv_cb(onEspNowReceive);

  Serial.println(
    "[ESP-NOW] READY"
  );

  return true;
}

// =====================================================
// JPEG -> RGB -> AI INPUT
// =====================================================

bool fillInputTensor(
  camera_fb_t* fb
) {

  Serial.println(
    "[AI] Decode JPEG..."
  );

  bool ok =
    fmt2rgb888(
      fb->buf,
      fb->len,
      PIXFORMAT_JPEG,
      rgbBuffer
    );

  if (!ok) {

    Serial.println(
      "[AI] Decode JPEG gagal"
    );

    return false;
  }

  int dstH =
    input->dims->data[1];

  int dstW =
    input->dims->data[2];

  int dstC =
    input->dims->data[3];

  if (
    dstC != 3
  ) {

    Serial.println(
      "[AI] Input bukan RGB"
    );

    return false;
  }

  float scale =
    input->params.scale;

  int zero =
    input->params.zero_point;

  int index = 0;

  for (
    int y = 0;
    y < dstH;
    y++
  ) {

    int srcY =
      y *
      CAMERA_HEIGHT /
      dstH;

    for (
      int x = 0;
      x < dstW;
      x++
    ) {

      int srcX =
        x *
        CAMERA_WIDTH /
        dstW;

      int pos =
        (
          srcY *
          CAMERA_WIDTH
          +
          srcX
        )
        *
        3;

      uint8_t r =
        rgbBuffer[pos];

      uint8_t g =
        rgbBuffer[pos + 1];

      uint8_t b =
        rgbBuffer[pos + 2];

      if (
        input->type ==
        kTfLiteInt8
      ) {

        int qr =
          round(
            (float)r /
            scale
          )
          +
          zero;

        int qg =
          round(
            (float)g /
            scale
          )
          +
          zero;

        int qb =
          round(
            (float)b /
            scale
          )
          +
          zero;

        input->data.int8[index++] =
          (int8_t)
          constrain(
            qr,
            -128,
            127
          );

        input->data.int8[index++] =
          (int8_t)
          constrain(
            qg,
            -128,
            127
          );

        input->data.int8[index++] =
          (int8_t)
          constrain(
            qb,
            -128,
            127
          );
      }

      else if (
        input->type ==
        kTfLiteUInt8
      ) {

        input->data.uint8[index++] = r;
        input->data.uint8[index++] = g;
        input->data.uint8[index++] = b;
      }

      else if (
        input->type ==
        kTfLiteFloat32
      ) {

        input->data.f[index++] =
          (float)r;

        input->data.f[index++] =
          (float)g;

        input->data.f[index++] =
          (float)b;
      }

      else {

        Serial.println(
          "[AI] Input type tidak didukung"
        );

        return false;
      }
    }
  }

  return true;
}

// =====================================================
// GET OUTPUT
// =====================================================

float getOutputValue(
  int index
) {

  if (
    output->type ==
    kTfLiteInt8
  ) {

    int8_t raw =
      output->data.int8[
        index
      ];

    return
      (
        (
          float
        )
        raw
        -
        output
        ->params
        .zero_point
      )
      *
      output
      ->params
      .scale;
  }

  if (
    output->type ==
    kTfLiteUInt8
  ) {

    uint8_t raw =
      output
      ->data
      .uint8[
        index
      ];

    return
      (
        (
          float
        )
        raw
        -
        output
        ->params
        .zero_point
      )
      *
      output
      ->params
      .scale;
  }

  if (
    output->type ==
    kTfLiteFloat32
  ) {

    return
      output->data.f[
        index
      ];
  }

  return 0.0;
}

// =====================================================
// SEND AI RESULT
// =====================================================

void sendAIResult(
  int bestIndex,
  float probability[3],
  uint32_t scanNumber,
  uint32_t imageSize
) {

  VisionResultPacket packet;

  memset(
    &packet,
    0,
    sizeof(packet)
  );

  packet.packetType =
    PACKET_RESULT;

  packet.scanNumber =
    scanNumber;

  strncpy(
    packet.visionClass,
    CLASS_NAMES[
      bestIndex
    ],
    sizeof(
      packet.visionClass
    ) - 1
  );

  packet.confidence =
    probability[
      bestIndex
    ]
    *
    100.0;

  packet.healthy =
    probability[0]
    *
    100.0;

  packet.powdery =
    probability[1]
    *
    100.0;

  packet.rust =
    probability[2]
    *
    100.0;

  packet.imageSize =
    imageSize;

  esp_err_t result =
    esp_now_send(
      receiverMAC,
      (uint8_t*)
      &packet,
      sizeof(packet)
    );

  if (
    result ==
    ESP_OK
  ) {

    Serial.println(
      "[ESP-NOW] AI RESULT SENT"
    );

  } else {

    Serial.print(
      "[ESP-NOW] Result send error: "
    );

    Serial.println(
      result
    );
  }
}

// =====================================================
// SEND JPEG
// =====================================================

void sendImageESPNow(
  camera_fb_t* fb,
  uint32_t scanNumber
) {

  if (
    fb == nullptr
    ||
    fb->len == 0
  ) {

    return;
  }

  uint32_t imageSize =
    fb->len;

  uint16_t totalChunks =
    (
      imageSize
      +
      IMAGE_CHUNK_SIZE
      -
      1
    )
    /
    IMAGE_CHUNK_SIZE;

  Serial.println();
  Serial.println("==============================");
  Serial.println("SEND JPEG VIA ESP-NOW");
  Serial.println("==============================");

  Serial.print(
    "[IMAGE] Size : "
  );

  Serial.print(
    imageSize
  );

  Serial.println(
    " bytes"
  );

  Serial.print(
    "[IMAGE] Chunks : "
  );

  Serial.println(
    totalChunks
  );

  // =================================================
  // START PACKET
  // =================================================

  ImageStartPacket startPacket;

  startPacket.packetType =
    PACKET_IMAGE_START;

  startPacket.scanNumber =
    scanNumber;

  startPacket.imageSize =
    imageSize;

  startPacket.totalChunks =
    totalChunks;

  esp_now_send(
    receiverMAC,
    (uint8_t*)
    &startPacket,
    sizeof(startPacket)
  );

  delay(
    20
  );

  // =================================================
  // CHUNKS
  // =================================================

  for (
    uint16_t i = 0;
    i < totalChunks;
    i++
  ) {

    ImageChunkPacket chunkPacket;

    chunkPacket.packetType =
      PACKET_IMAGE_CHUNK;

    chunkPacket.scanNumber =
      scanNumber;

    chunkPacket.chunkIndex =
      i;

    uint32_t offset =
      i *
      IMAGE_CHUNK_SIZE;

    uint16_t remaining =
      imageSize
      -
      offset;

    uint16_t chunkLength =
      remaining >
      IMAGE_CHUNK_SIZE
      ?
      IMAGE_CHUNK_SIZE
      :
      remaining;

    chunkPacket.dataLength =
      chunkLength;

    memcpy(
      chunkPacket.data,
      fb->buf + offset,
      chunkLength
    );

    esp_err_t result =
      esp_now_send(
        receiverMAC,
        (uint8_t*)
        &chunkPacket,
        sizeof(chunkPacket)
      );

    if (
      result !=
      ESP_OK
    ) {

      Serial.print(
        "[IMAGE] Chunk error : "
      );

      Serial.println(
        i
      );
    }

    // penting agar radio tidak kebanjiran
    delay(
      15
    );

    if (
      i % 10 ==
      0
    ) {

      Serial.print(
        "[IMAGE] Sending "
      );

      Serial.print(
        i
      );

      Serial.print(
        "/"
      );

      Serial.println(
        totalChunks
      );
    }
  }

  // =================================================
  // END PACKET
  // =================================================

  ImageEndPacket endPacket;

  endPacket.packetType =
    PACKET_IMAGE_END;

  endPacket.scanNumber =
    scanNumber;

  endPacket.totalChunks =
    totalChunks;

  esp_now_send(
    receiverMAC,
    (uint8_t*)
    &endPacket,
    sizeof(endPacket)
  );

  Serial.println(
    "[IMAGE] SEND FINISHED"
  );
}

// =====================================================
// RUN SCAN
// =====================================================

void runAI() {

  scanCounter++;

  Serial.println();
  Serial.println("==============================");

  Serial.print(
    "SCAN #"
  );

  Serial.println(
    scanCounter
  );

  Serial.println("==============================");

  // =================================================
  // FLASH
  // =================================================

  digitalWrite(
    FLASH_LED_PIN,
    HIGH
  );

  Serial.println(
    "[FLASH] ON"
  );

  delay(
    500
  );

  // =================================================
  // CAPTURE
  // =================================================

  camera_fb_t* fb =
    esp_camera_fb_get();

  digitalWrite(
    FLASH_LED_PIN,
    LOW
  );

  Serial.println(
    "[FLASH] OFF"
  );

  if (
    !fb
  ) {

    Serial.println(
      "[CAM] Capture gagal"
    );

    return;
  }

  Serial.print(
    "[CAM] JPEG Size : "
  );

  Serial.print(
    fb->len
  );

  Serial.println(
    " bytes"
  );

  // =================================================
  // AI PREPROCESS
  // =================================================

  Serial.println(
    "[AI] Preprocessing..."
  );

  if (
    !fillInputTensor(
      fb
    )
  ) {

    Serial.println(
      "[AI] Preprocess gagal"
    );

    esp_camera_fb_return(
      fb
    );

    return;
  }

  // =================================================
  // INFERENCE
  // =================================================

  Serial.println(
    "[AI] Running inference..."
  );

  unsigned long start =
    millis();

  TfLiteStatus status =
    interpreter->Invoke();

  unsigned long duration =
    millis()
    -
    start;

  if (
    status !=
    kTfLiteOk
  ) {

    Serial.println(
      "[AI] Invoke gagal"
    );

    esp_camera_fb_return(
      fb
    );

    return;
  }

  float probability[3];

  probability[0] =
    getOutputValue(0);

  probability[1] =
    getOutputValue(1);

  probability[2] =
    getOutputValue(2);

  int bestIndex =
    0;

  for (
    int i = 1;
    i < 3;
    i++
  ) {

    if (
      probability[i]
      >
      probability[
        bestIndex
      ]
    ) {

      bestIndex =
        i;
    }
  }

  float confidence =
    probability[
      bestIndex
    ]
    *
    100.0;

  // =================================================
  // RESULT
  // =================================================

  Serial.println();
  Serial.println("==============================");
  Serial.println("AI VISION RESULT");
  Serial.println("==============================");

  Serial.print("Healthy : ");
  Serial.print(
    probability[0]
    * 100.0,
    2
  );
  Serial.println("%");

  Serial.print("Powdery : ");
  Serial.print(
    probability[1]
    * 100.0,
    2
  );
  Serial.println("%");

  Serial.print("Rust    : ");
  Serial.print(
    probability[2]
    * 100.0,
    2
  );
  Serial.println("%");

  Serial.println();

  Serial.print(
    "Prediction : "
  );

  Serial.println(
    CLASS_NAMES[
      bestIndex
    ]
  );

  Serial.print(
    "Confidence : "
  );

  Serial.print(
    confidence,
    2
  );

  Serial.println(
    "%"
  );

  Serial.print(
    "Inference Time : "
  );

  Serial.print(
    duration
  );

  Serial.println(
    " ms"
  );

  Serial.println("==============================");

  // =================================================
  // SEND RESULT FIRST
  // =================================================

  sendAIResult(
    bestIndex,
    probability,
    scanCounter,
    fb->len
  );

  delay(
    100
  );

  // =================================================
  // SEND IMAGE
  // =================================================

  sendImageESPNow(
    fb,
    scanCounter
  );

  // =================================================
  // RELEASE FRAME
  // =================================================

  esp_camera_fb_return(
    fb
  );

  Serial.println();
  Serial.println(
    "[SYSTEM] SCAN SELESAI"
  );
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(
    2000
  );

  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP32-CAM AI + ESP-NOW IMAGE");
  Serial.println("==============================");

  // =================================================
  // FLASH
  // =================================================

  pinMode(
    FLASH_LED_PIN,
    OUTPUT
  );

  digitalWrite(
    FLASH_LED_PIN,
    LOW
  );

  // =================================================
  // CAMERA
  // =================================================

  if (
    !initCamera()
  ) {

    Serial.println(
      "[SYSTEM] CAMERA FAILED"
    );

    while (
      true
    ) {

      delay(
        1000
      );
    }
  }

  // =================================================
  // AI
  // =================================================

  if (
    !initModel()
  ) {

    Serial.println(
      "[SYSTEM] AI FAILED"
    );

    while (
      true
    ) {

      delay(
        1000
      );
    }
  }

  // =================================================
  // ESP-NOW
  // =================================================

  if (
    !initESPNow()
  ) {

    Serial.println(
      "[SYSTEM] ESP-NOW FAILED"
    );

    while (
      true
    ) {

      delay(
        1000
      );
    }
  }

  Serial.println();
  Serial.println("==============================");
  Serial.println("SYSTEM READY");
  Serial.println("==============================");

  Serial.println(
    "Ketik A lalu Send"
  );

  Serial.println(
    "untuk capture + AI + kirim gambar."
  );
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  if (
    Serial.available()
  ) {

    char cmd =
      Serial.read();

    if (
      cmd == 'A'
      ||
      cmd == 'a'
    ) {

      runAI();
    }
  }

  if (captureRequested) {
    captureRequested = false;
    runAI();
  }

  delay(
    20
  );
}
