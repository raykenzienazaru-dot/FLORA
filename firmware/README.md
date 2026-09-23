# GRENVIS hardware phase 1–4

Repositori ini berisi dua Arduino sketch:

- `esp32_main/esp32_main.ino`: DHT22, simulasi soil, model sensor, ESP-NOW receiver, MQTT TLS, JSON telemetry, timeout vision.
- `esp32_cam_test/esp32_cam_test.ino`: ESP-NOW test sender 10% Healthy, 20% Powdery, 70% Rust setiap 5 detik.

## Library

Pasang library Arduino `DHT sensor library`, `PubSubClient`, dan `ArduTFLite` beserta dependensinya. Gunakan board package ESP32 Arduino Core 3.x. ESP-NOW dan WiFi berasal dari board package ESP32.

## Persiapan

1. Isi SSID/password pada kedua `config.h`.
2. Salin **model asli** `C:\Users\KOMAGATRA\Downloads\grenvis_sensor_model.h` menjadi `esp32_main/grenvis_sensor_model.h`. Jangan memakai file `.example` sebagai model.
3. Header yang ditemukan menyediakan `grenvis_sensor_model_tflite` dan `grenvis_sensor_model_tflite_len`; sketch sudah memakai kedua simbol tersebut. Jika memakai model lain, ubah macro `GRENVIS_MODEL_DATA` dan `GRENVIS_MODEL_LEN`, bukan scaler atau urutan output.
4. Upload ESP32 Main lebih dahulu dan catat `ESP32 MAC` serta `WiFi Channel`.
5. Ubah MAC, misalnya `AA:BB:CC:11:22:33`, menjadi `{0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33}` di `esp32_cam_test/config.h`.
6. Hubungkan kedua board ke SSID yang sama. Channel mengikuti access point secara otomatis.
7. Upload sketch ESP32-CAM dan buka kedua Serial Monitor pada 115200 baud.

## Uji bertahap

1. Main harus mencetak IP, channel, `ESP-NOW: READY`, dan status model.
2. Pastikan DHT menghasilkan nilai valid dan inference mencetak probabilitas dalam urutan High, Low, Moderate.
3. CAM harus mencetak `ESP-NOW SEND SUCCESS`; Main harus mencetak `Vision RX` sekitar setiap 5 detik.
4. Subscribe `grenvis/sensor/data` dan `grenvis/sensor/status` pada EMQX. Status `online` bersifat retained dan LWT `offline` dipasang.
5. Matikan CAM selama lebih dari 15 detik. Payload berikutnya harus mengirim `vision_connected: false`, probabilitas vision nol, dan prediction `STALE`.

`secureClient.setInsecure()` hanya sesuai prototype. Production harus menggunakan CA certificate melalui `setCACert()`.

## Firmware production FLORA

Gunakan `esp32_main/esp32_main.ino` pada ESP32 utama (MAC `8C:AA:B5:37:71:18`) dan `esp32_cam/esp32_cam.ino` pada AI Thinker ESP32-CAM (MAC `F8:B3:B7:A6:F3:9C`). Kedua perangkat harus berada pada Wi-Fi channel **1**. MAC target utama sudah terpasang di sketch CAM sebagai `{ 0x8C, 0xAA, 0xB5, 0x37, 0x71, 0x18 }`.

Kontrol dashboard memakai `grenvis/device/control`: `L` (kiri), `R` (kanan), `S` (stop), dan `C` (ambil foto). Main meneruskan `C` ke CAM lewat ESP-NOW; JPEG yang selesai diterima dipublikasikan sebagai biner ke `grenvis/vision/image`, kemudian ditampilkan oleh dashboard. Telemetri dan inferensi tetap ada pada `grenvis/sensor/data` serta `grenvis/vision/data`.

Wiring main: DHT22 GPIO 19, soil GPIO 34, L298N IN1 GPIO 16 / IN2 GPIO 17, limit kiri GPIO 32 dan limit kanan GPIO 33.
