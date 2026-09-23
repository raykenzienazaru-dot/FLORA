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

import { enrichWithSoftwareAi } from './softwareAi';

/** Enrich raw hardware sensor telemetry using Software AI (Neural Network + Condition Protocols). */
function normalizeTelemetry(payload: Record<string, unknown>): TelemetryRecord {
  const enriched = enrichWithSoftwareAi(payload);
  return {
    ...enriched,
    image_url: imageUrl,
    esp32_mac: String(payload.esp32_mac ?? FLORA_DEVICE.mainMac),
    esp32cam_mac: String(payload.esp32cam_mac ?? FLORA_DEVICE.cameraMac),
    wifi_channel: Number(payload.wifi_channel ?? FLORA_DEVICE.wifiChannel),
  };
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
        const raw = message as Record<string, unknown>;
        const pred = String(raw.vision_prediction ?? raw.prediction ?? 'Unknown');
        const condition = pred.toLowerCase().includes('rust')
          ? 'Possible Rust Indication'
          : pred.toLowerCase().includes('powdery')
          ? 'Possible Powdery Mildew Indication'
          : 'Healthy / Stable';

        const visionRecord: Partial<TelemetryRecord> = {
          vision_scan: Number(raw.vision_scan ?? raw.scan ?? 0),
          vision_prediction: pred,
          vision_confidence: Number(raw.vision_confidence ?? raw.confidence ?? 0),
          vision_healthy: Number(raw.vision_healthy ?? raw.healthy ?? 0),
          vision_powdery: Number(raw.vision_powdery ?? raw.powdery ?? 0),
          vision_rust: Number(raw.vision_rust ?? raw.rust ?? 0),
          vision_connected: true,
          condition: {
            title: condition,
            description: `Hasil inferensi AI Vision: ${pred}`,
            factors: [],
            actions: [],
            priority: pred.toLowerCase() === 'healthy' ? 'LOW' : 'MEDIUM',
            recommended_inspection: 'Lakukan pemeriksaan visual daun secara berkala.',
          },
        };
        emit({ type: 'vision', data: visionRecord });
        return;
      }
      emit({ type: 'telemetry', data: normalizeTelemetry(message) });
    });
  },
  subscribe(listener: Listener) {
    listeners.add(listener);
    if (mqttStatus === 'CONNECTED') {
      listener({ type: 'mqtt', data: mqttStatus });
    }
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
