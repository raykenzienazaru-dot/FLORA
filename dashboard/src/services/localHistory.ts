import { DashboardSummary, TelemetryRecord } from '../types/dashboard';

const HISTORY_KEY = 'flora-local-telemetry-v1';
const MAX_LOCAL_RECORDS = 2_000;

const emptyStats = { average: 0, minimum: 0, maximum: 0 };
const stats = (rows: TelemetryRecord[], field: keyof TelemetryRecord) => {
  const values = rows.map((row) => Number(row[field])).filter(Number.isFinite);
  if (!values.length) return emptyStats;
  return {
    average: Number((values.reduce((total, value) => total + value, 0) / values.length).toFixed(2)),
    minimum: Math.min(...values),
    maximum: Math.max(...values),
  };
};

const dominant = (values: string[], fallback = 'Unknown') => {
  if (!values.length) return fallback;
  const counts = values.reduce<Record<string, number>>((result, value) => {
    result[value] = (result[value] || 0) + 1;
    return result;
  }, {});
  return Object.entries(counts).sort((a, b) => b[1] - a[1])[0]?.[0] || fallback;
};

const direction = (rows: TelemetryRecord[], field: keyof TelemetryRecord) => {
  const values = rows.map((row) => Number(row[field])).filter(Number.isFinite);
  if (values.length < 3) return 'INSUFFICIENT_DATA';
  const slope = (values.at(-1)! - values[0]) / (values.length - 1);
  return Math.abs(slope) < 0.4 ? 'STABLE' : slope > 0 ? 'INCREASING' : 'DECREASING';
};

export function readLocalHistory(): TelemetryRecord[] {
  try {
    const stored = JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]');
    return Array.isArray(stored) ? stored.filter((row): row is TelemetryRecord => Boolean(row?.timestamp)).slice(-MAX_LOCAL_RECORDS) : [];
  } catch {
    return [];
  }
}

export function appendLocalTelemetry(record: TelemetryRecord): TelemetryRecord[] {
  const next = { ...record, image_url: null, image_path: null };
  const history = [...readLocalHistory().filter((row) => row.timestamp !== next.timestamp), next].slice(-MAX_LOCAL_RECORDS);
  localStorage.setItem(HISTORY_KEY, JSON.stringify(history));
  return history;
}

export function mergeLatestLocalTelemetry(update: Partial<TelemetryRecord>): TelemetryRecord[] {
  const history = readLocalHistory();
  if (!history.length) return history;
  history[history.length - 1] = { ...history[history.length - 1], ...update };
  localStorage.setItem(HISTORY_KEY, JSON.stringify(history));
  return history;
}

export function summarizeLocalHistory(history: TelemetryRecord[], wateringEvents = 0): DashboardSummary {
  const daily = history.filter((row) => Date.parse(row.timestamp) >= Date.now() - 24 * 60 * 60 * 1_000);
  const rows = daily.length ? daily : history;
  const visionLabels = rows.map((row) => row.vision_prediction || 'Unknown');
  const riskLabels = rows.map((row) => row.sensor_risk || 'Unknown');
  const dryEvents = rows.filter((row, index) => row.soil_status === 'DRY' && rows[index - 1]?.soil_status !== 'DRY').length;

  return {
    readings: rows.length,
    temperature: stats(rows, 'temperature'),
    humidity: stats(rows, 'humidity'),
    soil: stats(rows, 'soil_moisture'),
    vision: {
      healthy: stats(rows, 'vision_healthy'),
      powdery: stats(rows, 'vision_powdery'),
      rust: stats(rows, 'vision_rust'),
      counts: {
        Healthy: visionLabels.filter((label) => label.toLowerCase() === 'healthy').length,
        Powdery: visionLabels.filter((label) => label.toLowerCase() === 'powdery').length,
        Rust: visionLabels.filter((label) => label.toLowerCase() === 'rust').length,
      },
      dominant: dominant(visionLabels),
    },
    environmental: {
      counts: {
        Low: riskLabels.filter((label) => label.toLowerCase() === 'low').length,
        Moderate: riskLabels.filter((label) => label.toLowerCase() === 'moderate').length,
        High: riskLabels.filter((label) => label.toLowerCase() === 'high').length,
      },
      dominant: dominant(riskLabels),
    },
    watering: { dry_events: dryEvents, wet_events: 0, recommendations: rows.filter((row) => ['WATERING_RECOMMENDED', 'URGENT_CHECK'].includes(row.watering_status)).length, events: wateringEvents },
    trends: { soil: direction(rows, 'soil_moisture'), rust: direction(rows, 'vision_rust'), powdery: direction(rows, 'vision_powdery') },
  };
}
