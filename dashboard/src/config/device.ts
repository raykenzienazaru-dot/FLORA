/**
 * Konfigurasi perangkat FLORA yang dibundel bersama dashboard statis.
 * Broker memakai WebSocket TLS agar Vercel dapat terhubung langsung dari
 * browser tanpa Express server, WebSocket server, maupun file .env.
 */
export const FLORA_DEVICE = {
  mainMac: '8C:AA:B5:37:71:18',
  cameraMac: 'F8:B3:B7:A6:F3:9C',
  wifiChannel: 6,
  aiVision: {
    baseUrl: 'https://web-production-e0039.up.railway.app',
    predictUrl: 'https://web-production-e0039.up.railway.app/predict',
    docsUrl: 'https://web-production-e0039.up.railway.app/docs',
    healthUrl: 'https://web-production-e0039.up.railway.app/health',
  },
  mqtt: {
    url: 'wss://m2da914a.ala.eu-central-1.emqxsl.com:8084/mqtt',
    username: 'grenvis_esp32',
    password: 'grenvis123',
    topics: {
      telemetry: 'grenvis/sensor/data',
      status: 'grenvis/sensor/status',
      vision: 'grenvis/vision/data',
      image: 'grenvis/vision/image',
      control: 'grenvis/device/control',
    },
  },
} as const;
