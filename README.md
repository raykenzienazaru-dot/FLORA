# FLORA Smart Plant Monitoring System

FLORA adalah prototype pemantauan tanaman yang menggabungkan ESP32, ESP32-CAM, sensor lingkungan, AI, ESP-NOW, MQTT, dan dashboard web.

## Struktur project

- `dashboard/` — dashboard Vite statis dengan MQTT-over-WSS langsung untuk monitoring, kontrol L298N, dan pratinjau JPEG ESP32-CAM.
- `firmware/esp32_main/` — pembacaan sensor, environmental ML, penerima ESP-NOW, L298N, penerima kontrol MQTT, dan publisher telemetry/JPEG MQTT.
- `firmware/esp32_cam/` — inferensi AI daun, pengirim JPEG ESP-NOW, dan penerima permintaan capture dari ESP32 utama.
- `.md` — spesifikasi lengkap, arsitektur, tahapan implementasi, serta daftar fitur yang belum aktif.

## Menjalankan dashboard

```powershell
cd dashboard
Copy-Item .env.example .env
npm install
npm start
```

Buka `http://localhost:5173`. Konfigurasi perangkat dan MQTT berada di `dashboard/src/config/device.ts`, sehingga dashboard dapat dideploy sebagai static site Vercel tanpa `.env`.

## Status saat ini

Dashboard, API, WebSocket, penyimpanan history lokal, decision support, smart watering recommendation, ringkasan harian, dan tren sudah tersedia. Integrasi AI Vision asli, kamera/foto, soil sensor fisik, database production, notifikasi eksternal, penyiraman otomatis, autentikasi, export laporan, serta deployment production belum aktif.

Daftar status yang lebih lengkap tersedia pada bagian **50. Status Implementasi dan Fitur yang Belum Aktif** di file [`.md`](.md).

## Keamanan

Salin file konfigurasi contoh menjadi file konfigurasi lokal. File `.env`, `config.h`, data history, dan model lokal tidak diikutkan ke Git. Jangan memasukkan kredensial asli ke file `*.example`.

## Catatan

Hasil AI merupakan indikasi pendukung dan bukan diagnosis penyakit tanaman yang pasti. Threshold sensor harus dikalibrasi sesuai spesies, media tanam, dan perangkat fisik.
