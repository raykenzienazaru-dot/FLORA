# FLORA Dashboard

**Field & Leaf Observation Robotic Assistant**

Dashboard ini adalah situs statis Vite yang terhubung langsung ke EMQX melalui MQTT-over-WSS. Tidak memerlukan Express, WebSocket server, maupun file `.env`; konfigurasi perangkat dan MQTT berada di `src/config/device.ts`.

## Menjalankan

```powershell
cd dashboard
npm install
npm run dev
```

Buka `http://localhost:5173`. Dashboard menampilkan telemetri dari `grenvis/sensor/data`, status AI vision dari `grenvis/vision/data`, dan JPEG biner dari `grenvis/vision/image`.

Tombol `Move Left`, `Stop Motor`, dan `Move Right` menerbitkan `L`, `S`, serta `R` ke `grenvis/device/control`. Tombol `Capture Photo` menerbitkan `C`; ESP32 utama meneruskannya ke ESP32-CAM lewat ESP-NOW.

## Deploy Vercel

Import folder `dashboard/` sebagai root directory proyek Vercel. `vercel.json` sudah mengarahkan hasil build ke `dist/`; tidak ada environment variable yang perlu diisi.

Threshold pada `.env` wajib dikalibrasi sesuai spesies, media tanam, dan sensor. Sistem hanya memberikan rekomendasi; tidak melakukan diagnosis penyakit atau penyiraman otomatis.
