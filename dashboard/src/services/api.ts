import {
  DashboardState,
  TelemetryRecord,
  WateringEvent,
  DeviceCommand,
  DeviceControlResponse,
} from '../types/dashboard';
import { DEFAULT_THRESHOLDS } from '../utils/sensorRules';
import { deviceMqtt } from './deviceMqtt';
import { readLocalHistory, summarizeLocalHistory } from './localHistory';

const WATERING_STORAGE_KEY = 'flora-watering-events';

const emptySummary = {
  readings: 0,
  temperature: { average: 0, minimum: 0, maximum: 0 },
  humidity: { average: 0, minimum: 0, maximum: 0 },
  soil: { average: 0, minimum: 0, maximum: 0 },
  vision: { healthy: { average: 0, minimum: 0, maximum: 0 }, powdery: { average: 0, minimum: 0, maximum: 0 }, rust: { average: 0, minimum: 0, maximum: 0 }, counts: { Healthy: 0, Powdery: 0, Rust: 0 }, dominant: 'Unknown' },
  environmental: { counts: { Low: 0, Moderate: 0, High: 0 }, dominant: 'Unknown' },
  watering: { dry_events: 0, wet_events: 0, recommendations: 0, events: 0 },
  trends: { soil: 'INSUFFICIENT_DATA', rust: 'INSUFFICIENT_DATA', powdery: 'INSUFFICIENT_DATA' },
} as DashboardState['summary'];

export async function getDashboardState(): Promise<DashboardState> {
  const events = JSON.parse(localStorage.getItem(WATERING_STORAGE_KEY) || '[]') as WateringEvent[];
  const history = readLocalHistory();
  return { latest: history.at(-1) || null, history, summary: history.length ? summarizeLocalHistory(history, events.length) : emptySummary, mqtt: 'DISCONNECTED', lastTelemetryAt: history.at(-1)?.timestamp || null, config: DEFAULT_THRESHOLDS, wateringEvents: events };
}

export async function createWateringEvent(note: string = 'Recorded from dashboard'): Promise<WateringEvent> {
  const event = { id: Date.now(), timestamp: new Date().toISOString(), soil_before: null, note };
  const events = JSON.parse(localStorage.getItem(WATERING_STORAGE_KEY) || '[]') as WateringEvent[];
  localStorage.setItem(WATERING_STORAGE_KEY, JSON.stringify([...events, event].slice(-20)));
  return event;
}

export async function sendDemoData(payload: Partial<TelemetryRecord>): Promise<TelemetryRecord> {
  const response = await fetch('/api/demo', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    throw new Error(`Failed to send demo data: ${response.status} ${response.statusText}`);
  }
  return response.json();
}

export async function sendDeviceCommand(command: DeviceCommand): Promise<DeviceControlResponse> {
  await deviceMqtt.publish(command);
  const message = command === 'C' ? 'Camera capture request sent' : `Command ${command} sent to L298N`;
  return { success: true, command, message, mqtt: 'CONNECTED' };
}
