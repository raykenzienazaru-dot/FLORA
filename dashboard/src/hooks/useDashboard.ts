import { useState, useEffect, useCallback, useRef, useMemo } from 'react';
import {
  DashboardState,
  WebSocketMessage,
  RealtimeSystemState,
  SystemStatusInfo,
  WateringEvent,
} from '../types/dashboard';
import { getDashboardState, createWateringEvent } from '../services/api';
import { evaluateSystemAlert } from '../utils/sensorRules';
import { useWebSocket } from './useWebSocket';
import { deviceMqtt } from '../services/deviceMqtt';
import { appendLocalTelemetry, mergeLatestLocalTelemetry, summarizeLocalHistory } from '../services/localHistory';

interface UseDashboardReturn {
  data: DashboardState | null;
  loading: boolean;
  error: string | null;
  systemState: string;
  systemStatus: SystemStatusInfo;
  wsStatus: 'connecting' | 'connected' | 'disconnected';
  refresh: () => Promise<void>;
  recordWatering: (note?: string) => Promise<WateringEvent | undefined>;
}

export function useDashboard(onToast?: (msg: string) => void): UseDashboardReturn {
  const [data, setData] = useState<DashboardState | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState<number>(Date.now());
  const prevSeverityRef = useRef<string | null>(null);

  // 1-second natural clock tick for telemetry age & status calculation
  useEffect(() => {
    const timer = setInterval(() => {
      setNow(Date.now());
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  const loadData = useCallback(async () => {
    try {
      const state = await getDashboardState();
      setData(state);
      setError(null);
      if (state.latest) {
        const currentAlert = evaluateSystemAlert(state.latest, state.mqtt);
        prevSeverityRef.current = currentAlert.severity;
      }
    } catch (err: any) {
      setError(err?.message || 'Backend tidak dapat dihubungi');
      if (onToast) {
        onToast('Backend tidak dapat dihubungi');
      }
    } finally {
      setLoading(false);
    }
  }, [onToast]);

  const handleMqttMessage = useCallback(
    (msg: WebSocketMessage) => {
      if (!msg) return;

      if (msg.type === 'telemetry' && msg.data && msg.data.timestamp) {
        setData((prev) => {
          if (!prev) return prev;

          // Check for condition / severity change
          const newAlert = evaluateSystemAlert(msg.data, prev.mqtt);
          if (
            prevSeverityRef.current &&
            prevSeverityRef.current !== newAlert.severity &&
            newAlert.severity !== 'AWAITING'
          ) {
            if (onToast) {
              onToast(`Kondisi berubah: ${newAlert.title}`);
            }
          }
          prevSeverityRef.current = newAlert.severity;

          const history = appendLocalTelemetry(msg.data);
          return {
            ...prev,
            latest: msg.data,
            history,
            summary: summarizeLocalHistory(history, prev.wateringEvents.length),
            lastTelemetryAt: msg.data.timestamp,
          };
        });
      } else if (msg.type === 'mqtt' && msg.data) {
        setData((prev) => (prev ? { ...prev, mqtt: msg.data } : prev));
      } else if (msg.type === 'vision') {
        setData((prev) => {
          if (!prev?.latest) return prev;
          const latest = { ...prev.latest, ...msg.data, vision_connected: true, timestamp: prev.latest.timestamp };
          const history = mergeLatestLocalTelemetry(latest);
          return { ...prev, latest, history, summary: summarizeLocalHistory(history, prev.wateringEvents.length) };
        });
      } else if (msg.type === 'image') {
        setData((prev) => prev?.latest ? {
          ...prev,
          latest: { ...prev.latest, ...msg.data },
        } : prev);
      } else if (msg.type === 'watering') {
        loadData();
      }
    },
    [loadData, onToast]
  );

  const { wsStatus: liveWsStatus } = useWebSocket({
    onMessage: handleMqttMessage,
  });

  const [deviceWsStatus, setDeviceWsStatus] = useState<'connecting' | 'connected' | 'disconnected'>('connecting');

  useEffect(() => {
    if (liveWsStatus === 'connected') return;

    deviceMqtt.start();
    return deviceMqtt.subscribe((message) => {
      if (message.type === 'mqtt') {
        setDeviceWsStatus(
          message.data === 'CONNECTED'
            ? 'connected'
            : message.data === 'RECONNECTING'
            ? 'connecting'
            : 'disconnected'
        );
      }
      handleMqttMessage(message);
    });
  }, [handleMqttMessage, liveWsStatus]);

  const wsStatus: 'connecting' | 'connected' | 'disconnected' =
    liveWsStatus === 'connected' || deviceWsStatus === 'connected'
      ? 'connected'
      : liveWsStatus === 'connecting' || deviceWsStatus === 'connecting'
      ? 'connecting'
      : 'disconnected';

  const channelStatusText = wsStatus === 'connected' ? 'MQTT channel ready' : 'Connecting to MQTT';

  // Centralized Realtime System State Evaluation (Rules 1-6)
  const systemStatus: SystemStatusInfo = useMemo(() => {
    const timestamp = data?.latest?.timestamp;
    const parsedTime = timestamp ? Date.parse(timestamp) : NaN;
    const hasValidTimestamp = !isNaN(parsedTime);
    const secondsAgo = hasValidTimestamp ? Math.max(0, Math.floor((now - parsedTime) / 1000)) : null;

    let lastUpdateText: string | null = null;
    if (secondsAgo !== null) {
      if (secondsAgo === 0) {
        lastUpdateText = 'just now';
      } else if (secondsAgo === 1) {
        lastUpdateText = '1 second ago';
      } else if (secondsAgo < 60) {
        lastUpdateText = `${secondsAgo} seconds ago`;
      } else if (secondsAgo < 3600) {
        const m = Math.floor(secondsAgo / 60);
        lastUpdateText = `${m}m ago`;
      } else {
        const h = Math.floor(secondsAgo / 3600);
        lastUpdateText = `${h}h ago`;
      }
    }

    // 1. ERROR: Request or connection error occurred
    if (error && !data) {
      return {
        state: 'ERROR' as RealtimeSystemState,
        label: 'ERROR',
        badgeText: 'Connection Error',
        description: error || 'A request or connection error occurred.',
        lastUpdateText,
        secondsAgo,
        indicatorColor: 'bg-[#DC2626]',
        reconnectMessage: 'Check backend connection.',
        isLive: false,
        isStale: false,
      };
    }

    // 2. DISCONNECTED: WebSocket is closed, unavailable, or connection failed
    if (wsStatus === 'disconnected') {
      return {
        state: 'DISCONNECTED' as RealtimeSystemState,
        label: 'DISCONNECTED',
        badgeText: 'Disconnected',
        description: 'Trying to reconnect...',
        lastUpdateText,
        secondsAgo,
        indicatorColor: 'bg-[#DC2626]',
        reconnectMessage: 'Trying to reconnect...',
        isLive: false,
        isStale: true,
      };
    }

    // 3. LOADING: Dashboard has not established a usable connection yet
    if (loading && !data) {
      return {
        state: 'LOADING' as RealtimeSystemState,
        label: 'LOADING',
        badgeText: 'Connecting',
        description: 'Connecting to FLORA...',
        lastUpdateText: null,
        secondsAgo: null,
        indicatorColor: 'bg-[#D97706] animate-pulse',
        reconnectMessage: 'Waiting for FLORA...',
        isLive: false,
        isStale: false,
      };
    }

    // 4. CONNECTED: WebSocket is open, but no telemetry packet has been received yet
    if (wsStatus === 'connected' && (!data?.latest || !hasValidTimestamp)) {
      return {
        state: 'CONNECTED' as RealtimeSystemState,
        label: 'CONNECTED',
        badgeText: 'Connected',
        description: 'Connection established. Waiting for telemetry.',
        lastUpdateText: null,
        secondsAgo: null,
        indicatorColor: 'bg-[#D97706]',
        reconnectMessage: 'Waiting for telemetry.',
        isLive: false,
        isStale: false,
      };
    }

    // 5. LIVE: Valid telemetry packet received recently (<= 15 seconds)
    if (hasValidTimestamp && secondsAgo !== null && secondsAgo <= 15) {
      return {
        state: 'LIVE' as RealtimeSystemState,
        label: 'LIVE',
        badgeText: 'Live Telemetry',
        description: 'Live telemetry',
        lastUpdateText: `Last update: ${lastUpdateText}`,
        secondsAgo,
        indicatorColor: 'bg-[#597C00] animate-pulse',
        reconnectMessage: null,
        isLive: true,
        isStale: false,
      };
    }

    // 6. STALE: WebSocket may be open, but no telemetry received for > 15 seconds
    if (hasValidTimestamp && secondsAgo !== null && secondsAgo > 15) {
      return {
        state: 'STALE' as RealtimeSystemState,
        label: 'STALE',
        badgeText: 'Telemetry Stale',
        description: `No new sensor data for ${secondsAgo} seconds.`,
        lastUpdateText: `${secondsAgo}s ago`,
        secondsAgo,
        indicatorColor: 'bg-[#D97706]',
        reconnectMessage: null,
        isLive: false,
        isStale: true,
      };
    }

    // Default fallback
    return {
      state: 'LOADING' as RealtimeSystemState,
      label: 'LOADING',
      badgeText: 'Connecting',
      description: channelStatusText || 'Connecting to FLORA...',
      lastUpdateText: null,
      secondsAgo: null,
      indicatorColor: 'bg-[#D97706] animate-pulse',
      reconnectMessage: null,
      isLive: false,
      isStale: false,
    };
  }, [data, loading, error, wsStatus, now, channelStatusText]);

  const recordWatering = useCallback(
    async (note: string = 'Manual watering recorded'): Promise<WateringEvent | undefined> => {
      try {
        const event = await createWateringEvent(note);
        if (onToast) {
          onToast('Manual irrigation recorded — Saved as a manual watering event. No pump command was sent.');
        }
        await loadData();
        return event;
      } catch (err: any) {
        if (onToast) {
          onToast('Gagal mencatat penyiraman manual');
        }
      }
    },
    [loadData, onToast]
  );

  useEffect(() => {
    loadData();
    const interval = setInterval(loadData, 4000);
    return () => clearInterval(interval);
  }, [loadData]);

  return {
    data,
    loading,
    error,
    systemState: systemStatus.label,
    systemStatus,
    wsStatus,
    refresh: loadData,
    recordWatering,
  };
}
