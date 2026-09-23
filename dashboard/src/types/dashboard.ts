export interface ConditionDetails {
  title: string;
  description: string;
  factors: string[];
  actions: string[];
  priority: 'LOW' | 'MEDIUM' | 'HIGH' | string;
  recommended_inspection: string;
}

export interface TelemetryRecord {
  timestamp: string;
  temperature: number;
  humidity: number;
  soil_moisture: number;
  soil_raw?: number;
  sensor_risk?: 'Low' | 'Moderate' | 'High' | string;
  sensor_confidence?: number;
  high_probability?: number;
  low_probability?: number;
  moderate_probability?: number;
  vision_connected?: boolean;
  vision_healthy: number;
  vision_powdery: number;
  vision_rust: number;
  vision_prediction?: string;
  image_url?: string | null;
  image_path?: string | null;
  image_timestamp?: string | null;
  esp32_mac?: string;
  esp32cam_mac?: string;
  wifi_channel?: number;
  uptime_seconds?: number;
  limit_left?: boolean | null;
  limit_right?: boolean | null;
  temperature_status: 'HIGH' | 'NORMAL' | string;
  humidity_status: 'HIGH' | 'LOW' | 'NORMAL' | string;
  soil_status: 'DRY' | 'WET' | 'NORMAL' | string;
  dry_since: string | null;
  dry_duration_minutes: number;
  consecutive_dry_readings: number;
  watering_status: 'NO_WATERING' | 'TOO_WET' | 'WATERING_RECOMMENDED' | 'URGENT_CHECK' | 'MONITOR' | string;
  watering_priority: 'LOW' | 'MEDIUM' | 'HIGH' | string;
  watering_description: string;
  next_check_time: string;
  condition: ConditionDetails;
}

export interface MetricStats {
  average: number;
  minimum: number;
  maximum: number;
}

export interface VisionStats {
  healthy: MetricStats;
  powdery: MetricStats;
  rust: MetricStats;
  counts: {
    Healthy: number;
    Powdery: number;
    Rust: number;
  };
  dominant: string;
}

export interface EnvironmentalStats {
  counts: {
    Low: number;
    Moderate: number;
    High: number;
  };
  dominant: string;
}

export interface WateringStats {
  dry_events: number;
  wet_events: number;
  recommendations: number;
  events: number;
}

export interface TrendsSummary {
  soil: 'STABLE' | 'INCREASING' | 'DECREASING' | 'INSUFFICIENT_DATA' | string;
  rust: 'STABLE' | 'INCREASING' | 'DECREASING' | 'INSUFFICIENT_DATA' | string;
  powdery: 'STABLE' | 'INCREASING' | 'DECREASING' | 'INSUFFICIENT_DATA' | string;
}

export interface DashboardSummary {
  readings: number;
  temperature: MetricStats;
  humidity: MetricStats;
  soil: MetricStats;
  vision: VisionStats;
  environmental: EnvironmentalStats;
  watering: WateringStats;
  trends: TrendsSummary;
}

export interface ThresholdConfig {
  soilDry: number;
  soilVeryDry: number;
  soilWet: number;
  tempHigh: number;
  humidityLow: number;
  humidityHigh: number;
  consecutive: number;
}

export interface WateringEvent {
  id: number;
  timestamp: string;
  soil_before: number | null;
  note: string;
}

export type RealtimeSystemState =
  | 'LOADING'
  | 'CONNECTED'
  | 'LIVE'
  | 'STALE'
  | 'DISCONNECTED'
  | 'ERROR';

export interface SystemStatusInfo {
  state: RealtimeSystemState;
  label: string;
  badgeText: string;
  description: string;
  lastUpdateText: string | null;
  secondsAgo: number | null;
  indicatorColor: string;
  reconnectMessage: string | null;
  isLive: boolean;
  isStale: boolean;
}

export type MqttStatus = 'CONNECTED' | 'DISCONNECTED' | 'RECONNECTING' | string;

export interface DashboardState {
  latest: TelemetryRecord | null;
  history: TelemetryRecord[];
  summary: DashboardSummary;
  mqtt: MqttStatus;
  lastTelemetryAt: string | null;
  config: ThresholdConfig;
  wateringEvents: WateringEvent[];
}

export type WebSocketMessage =
  | { type: 'telemetry'; data: TelemetryRecord }
  | { type: 'vision'; data: Partial<TelemetryRecord> }
  | { type: 'image'; data: Pick<TelemetryRecord, 'image_url' | 'image_timestamp'> }
  | { type: 'watering'; data: WateringEvent }
  | { type: 'mqtt'; data: MqttStatus };

export type DeviceCommand = 'L' | 'R' | 'S' | 'C';

export type DeviceMovementStatus =
  | 'idle'
  | 'left_command_sent'
  | 'right_command_sent'
  | 'stop_command_sent'
  | 'capture_command_sent'
  | 'sending'
  | 'error';

export interface DeviceControlResponse {
  success: boolean;
  command?: DeviceCommand;
  message?: string;
  error?: string;
  mqtt?: string;
}
