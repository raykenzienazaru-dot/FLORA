import mqtt, { type MqttClient } from 'mqtt';
import { FLORA_DEVICE } from '../config/device';
import { DeviceCommand, MqttStatus, TelemetryRecord, WebSocketMessage } from '../types/dashboard';

type Listener = (message: WebSocketMessage) => void;

const listeners = new Set<Listener>();
let client: MqttClient | null = null;
let mqttStatus: MqttStatus = 'DISCONNECTED';
let imageUrl: string | null = null;

const emit = (message: WebSocketMessage) => listeners.forEach((listener) => listener(message));
const setStatus = (status: MqttStatus) => {
  mqttStatus = status;
  emit({ type: 'mqtt', data: status });
};

const text = (payload: Uint8Array) => new TextDecoder().decode(payload);

function parseJson(payload: Uint8Array): Record<string, unknown> | null {
  try {
    const value = JSON.parse(text(payload));
    return value && typeof value === 'object' ? value : null;
  } catch {
    return null;
  }
}

/** Fill the UI-only fields that were previously calculated by Express. */
function normalizeTelemetry(payload: Record<string, unknown>): TelemetryRecord {
  const soil = Number(payload.soil_moisture ?? 0);
  const temperature = Number(payload.temperature ?? 0);
  const humidity = Number(payload.humidity ?? 0);
  const prediction = String(payload.vision_prediction ?? 'Awaiting inference');
  const condition = prediction.toLowerCase().includes('rust')
    ? 'Possible Rust Indication'
    : prediction.toLowerCase().includes('powdery')
    ? 'Possible Powdery Mildew Indication'
    : 'Healthy / Stable';
  const priority = soil < 20 || String(payload.sensor_risk).toLowerCase() === 'high' ? 'HIGH' : soil < 30 ? 'MEDIUM' : 'LOW';

  return {
    ...payload,
    timestamp: typeof payload.timestamp === 'string' ? payload.timestamp : new Date().toISOString(),
    temperature,
    humidity,
    soil_moisture: soil,
    vision_healthy: Number(payload.vision_healthy ?? 0),
    vision_powdery: Number(payload.vision_powdery ?? 0),
    vision_rust: Number(payload.vision_rust ?? 0),
    vision_connected: Boolean(payload.vision_connected),
    vision_prediction: prediction,
    image_url: imageUrl,
    esp32_mac: String(payload.esp32_mac ?? FLORA_DEVICE.mainMac),
    esp32cam_mac: String(payload.esp32cam_mac ?? FLORA_DEVICE.cameraMac),
    wifi_channel: Number(payload.wifi_channel ?? FLORA_DEVICE.wifiChannel),
    temperature_status: temperature >= 35 ? 'HIGH' : 'NORMAL',
    humidity_status: humidity > 80 ? 'HIGH' : humidity < 45 ? 'LOW' : 'NORMAL',
    soil_status: soil < 30 ? 'DRY' : soil > 80 ? 'WET' : 'NORMAL',
    dry_since: null,
    dry_duration_minutes: 0,
    consecutive_dry_readings: 0,
    watering_status: soil < 20 ? 'URGENT_CHECK' : soil < 30 ? 'WATERING_RECOMMENDED' : soil > 80 ? 'TOO_WET' : 'NO_WATERING',
    watering_priority: priority,
    watering_description: soil < 20 ? 'Kelembapan tanah sangat rendah; lakukan pemeriksaan dan penyiraman.' : soil < 30 ? 'Kelembapan tanah rendah; pertimbangkan penyiraman.' : soil > 80 ? 'Media tanam terlalu basah; tunda penyiraman.' : 'Kelembapan tanah masih mencukupi.',
    next_check_time: new Date(Date.now() + 30 * 60_000).toISOString(),
    condition: {
      title: condition,
      description: 'Evaluasi dibuat dari telemetri langsung perangkat FLORA.',
      factors: [],
      actions: [],
      priority,
      recommended_inspection: 'Periksa daun dan media tanam secara langsung bila ada peringatan.',
    },
  } as TelemetryRecord;
}

export const deviceMqtt = {
  start() {
    if (client) return;
    client = mqtt.connect(FLORA_DEVICE.mqtt.url, {
      clientId: `flora-web-${Math.random().toString(16).slice(2, 10)}`,
      username: FLORA_DEVICE.mqtt.username,
      password: FLORA_DEVICE.mqtt.password,
      clean: true,
      reconnectPeriod: 5_000,
      connectTimeout: 10_000,
    });

    client.on('connect', () => {
      setStatus('CONNECTED');
      client?.subscribe([
        FLORA_DEVICE.mqtt.topics.telemetry,
        FLORA_DEVICE.mqtt.topics.status,
        FLORA_DEVICE.mqtt.topics.vision,
        FLORA_DEVICE.mqtt.topics.image,
      ], { qos: 1 });
    });
    client.on('reconnect', () => setStatus('RECONNECTING'));
    client.on('offline', () => setStatus('DISCONNECTED'));
    client.on('error', () => setStatus('DISCONNECTED'));
    client.on('message', (topic, payload) => {
      if (topic === FLORA_DEVICE.mqtt.topics.status) return;
      if (topic === FLORA_DEVICE.mqtt.topics.image) {
        if (imageUrl) URL.revokeObjectURL(imageUrl);
        imageUrl = URL.createObjectURL(new Blob([new Uint8Array(payload)], { type: 'image/jpeg' }));
        emit({ type: 'image', data: { image_url: imageUrl, image_timestamp: new Date().toISOString() } });
        return;
      }
      const message = parseJson(payload);
      if (!message) return;
      if (topic === FLORA_DEVICE.mqtt.topics.vision) {
        emit({ type: 'vision', data: message as Partial<TelemetryRecord> });
        return;
      }
      emit({ type: 'telemetry', data: normalizeTelemetry(message) });
    });
  },
  subscribe(listener: Listener) {
    listeners.add(listener);
    listener({ type: 'mqtt', data: mqttStatus });
    return () => { listeners.delete(listener); };
  },
  publish(command: DeviceCommand) {
    this.start();
    return new Promise<void>((resolve, reject) => {
      if (!client?.connected) return reject(new Error('MQTT broker belum tersambung'));
      client.publish(FLORA_DEVICE.mqtt.topics.control, command, { qos: 1 }, (error) => error ? reject(error) : resolve());
    });
  },
};
