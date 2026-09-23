import React, { useRef, useEffect, useState, useCallback } from 'react';
import { TelemetryRecord, TrendsSummary } from '../../types/dashboard';
import { fmt, formatTime } from '../../utils/formatters';

interface HistoryTrendsSectionProps {
  history: TelemetryRecord[];
  trends?: TrendsSummary;
}

type TimeRange = 'LIVE' | '1H' | '6H' | '24H';
type MetricFilter = 'ALL' | 'TEMP' | 'HUM' | 'SOIL';

export const HistoryTrendsSection: React.FC<HistoryTrendsSectionProps> = ({ history, trends }) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [selectedRange, setSelectedRange] = useState<TimeRange>('24H');
  const [metricFilter, setMetricFilter] = useState<MetricFilter>('ALL');
  const [hoveredPoint, setHoveredPoint] = useState<{
    x: number;
    y: number;
    record: TelemetryRecord;
  } | null>(null);

  // Filter history based on time range
  const getFilteredHistory = useCallback((): TelemetryRecord[] => {
    if (!history || history.length === 0) return [];

    const now = Date.now();
    let cutoff = 0;

    switch (selectedRange) {
      case 'LIVE':
        return history.slice(-30);
      case '1H':
        cutoff = now - 1 * 3600000;
        break;
      case '6H':
        cutoff = now - 6 * 3600000;
        break;
      case '24H':
      default:
        cutoff = now - 24 * 3600000;
        break;
    }

    const filtered = history.filter((r) => Date.parse(r.timestamp) >= cutoff);
    return filtered.length > 0 ? filtered : history.slice(-48);
  }, [history, selectedRange]);

  const activeData = getFilteredHistory();

  const drawChart = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const w = rect.width;
    const h = 260;

    if (w === 0) return;

    canvas.width = w * dpr;
    canvas.height = h * dpr;

    const c = canvas.getContext('2d');
    if (!c) return;

    c.scale(dpr, dpr);
    const pLeft = 46;
    const pRight = 46;
    const pTop = 24;
    const pBottom = 30;
    const chartW = w - pLeft - pRight;
    const chartH = h - pTop - pBottom;

    c.clearRect(0, 0, w, h);

    // Draw horizontal grid lines
    c.strokeStyle = '#EEF2EA';
    c.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = pTop + (chartH * i) / 4;
      c.beginPath();
      c.moveTo(pLeft, y);
      c.lineTo(w - pRight, y);
      c.stroke();

      // Left Y-axis scale: Percentage (0-100%)
      c.fillStyle = '#617253';
      c.font = '10px "JetBrains Mono", monospace';
      c.textAlign = 'right';
      c.fillText(`${100 - i * 25}%`, pLeft - 8, y + 3.5);

      // Right Y-axis scale: Temperature (0-50°C)
      c.fillStyle = '#D97706';
      c.textAlign = 'left';
      c.fillText(`${50 - i * 12.5}°C`, w - pRight + 8, y + 3.5);
    }

    if (!activeData || activeData.length === 0) {
      c.fillStyle = '#617253';
      c.font = '12px "Plus Jakarta Sans", sans-serif';
      c.textAlign = 'center';
      c.fillText('Menunggu rekaman telemetri riwayat sensor...', w / 2, h / 2);
      return;
    }

    // Series Definitions: Temp scaled 0-50°C, Humidity & Soil scaled 0-100%
    const series = [
      {
        field: 'humidity' as const,
        color: '#0284C7',
        show: metricFilter === 'ALL' || metricFilter === 'HUM',
        scaleMax: 100,
      },
      {
        field: 'soil_moisture' as const,
        color: '#597C00',
        show: metricFilter === 'ALL' || metricFilter === 'SOIL',
        scaleMax: 100,
      },
      {
        field: 'temperature' as const,
        color: '#D97706',
        show: metricFilter === 'ALL' || metricFilter === 'TEMP',
        scaleMax: 50,
      },
    ];

    series.forEach(({ field, color, show, scaleMax }) => {
      if (!show) return;

      c.beginPath();
      c.strokeStyle = color;
      c.lineWidth = 2.4;
      c.lineCap = 'round';
      c.lineJoin = 'round';

      activeData.forEach((r, i) => {
        const val = Number(r[field]);
        const clampedVal = Math.min(scaleMax, Math.max(0, isNaN(val) ? 0 : val));
        const x = pLeft + (chartW * i) / Math.max(1, activeData.length - 1);
        const y = pTop + chartH * (1 - clampedVal / scaleMax);

        if (i === 0) {
          c.moveTo(x, y);
        } else {
          c.lineTo(x, y);
        }
      });

      c.stroke();
    });

    // Draw X-axis timestamps (Start, Mid, End)
    if (activeData.length >= 2) {
      c.fillStyle = '#617253';
      c.font = '10px "JetBrains Mono", monospace';
      c.textAlign = 'left';
      c.fillText(formatTime(activeData[0].timestamp), pLeft, h - 8);

      c.textAlign = 'center';
      const midIdx = Math.floor(activeData.length / 2);
      c.fillText(formatTime(activeData[midIdx].timestamp), pLeft + chartW / 2, h - 8);

      c.textAlign = 'right';
      c.fillText(formatTime(activeData[activeData.length - 1].timestamp), w - pRight, h - 8);
    }

    // Draw Crosshair if hovering
    if (hoveredPoint) {
      c.strokeStyle = '#597C00';
      c.lineWidth = 1;
      c.setLineDash([3, 3]);
      c.beginPath();
      c.moveTo(hoveredPoint.x, pTop);
      c.lineTo(hoveredPoint.x, pTop + chartH);
      c.stroke();
      c.setLineDash([]);
    }
  }, [activeData, hoveredPoint, metricFilter]);

  useEffect(() => {
    drawChart();

    const handleResize = () => {
      drawChart();
    };

    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, [drawChart]);

  const handleCanvasMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas || !activeData || activeData.length === 0) return;

    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const pLeft = 46;
    const pRight = 46;
    const chartW = rect.width - pLeft - pRight;

    const relativeX = Math.max(0, Math.min(chartW, x - pLeft));
    const index = Math.round((relativeX / chartW) * (activeData.length - 1));
    const record = activeData[index];

    if (record) {
      setHoveredPoint({
        x: pLeft + (chartW * index) / Math.max(1, activeData.length - 1),
        y: e.clientY - rect.top,
        record,
      });
    }
  };

  const handleCanvasMouseLeave = () => {
    setHoveredPoint(null);
  };

  const trendCards = [
    {
      label: 'Soil Moisture Momentum',
      value: trends?.soil ? trends.soil.replace(/_/g, ' ') : 'Stable',
      statusColor: trends?.soil === 'DECREASING' ? 'text-[#D97706]' : 'text-[#597C00]',
    },
    {
      label: 'Leaf Rust Indication Trend',
      value: trends?.rust ? trends.rust.replace(/_/g, ' ') : 'Stable',
      statusColor: trends?.rust === 'INCREASING' ? 'text-[#B43E1F]' : 'text-[#597C00]',
    },
    {
      label: 'Powdery Mildew Trend',
      value: trends?.powdery ? trends.powdery.replace(/_/g, ' ') : 'Stable',
      statusColor: trends?.powdery === 'INCREASING' ? 'text-[#EA580C]' : 'text-[#597C00]',
    },
  ];

  return (
    <section id="history" className="flora-card p-6 mt-8 shadow-xs">
      {/* Header & Controls */}
      <div className="flex flex-col lg:flex-row justify-between items-start lg:items-center gap-3 mb-4">
        <div>
          <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
            Telemetry Archive
          </span>
          <h2 className="text-lg font-bold text-[#1B2408] font-display mt-0.5">
            Multi-Sensor Historical Trends
          </h2>
        </div>

        {/* Filter and Range Controls */}
        <div className="flex items-center gap-2.5 flex-wrap">
          {/* Sensor Filter Toggle */}
          <div className="flex rounded-xl bg-[#F4F7F2] p-1 border border-[#E4EBE0] text-xs font-semibold">
            <button
              onClick={() => setMetricFilter('ALL')}
              className={`px-2.5 py-1 rounded-lg transition-all ${
                metricFilter === 'ALL'
                  ? 'bg-white text-[#1B2408] shadow-xs font-bold'
                  : 'text-[#617253] hover:text-[#1B2408]'
              }`}
            >
              All
            </button>
            <button
              onClick={() => setMetricFilter('TEMP')}
              className={`px-2.5 py-1 rounded-lg transition-all flex items-center gap-1.5 ${
                metricFilter === 'TEMP'
                  ? 'bg-white text-[#D97706] shadow-xs font-bold'
                  : 'text-[#617253] hover:text-[#D97706]'
              }`}
            >
              <span className="w-2 h-2 rounded-full bg-[#D97706]" />
              <span>Temp (°C)</span>
            </button>
            <button
              onClick={() => setMetricFilter('HUM')}
              className={`px-2.5 py-1 rounded-lg transition-all flex items-center gap-1.5 ${
                metricFilter === 'HUM'
                  ? 'bg-white text-[#0284C7] shadow-xs font-bold'
                  : 'text-[#617253] hover:text-[#0284C7]'
              }`}
            >
              <span className="w-2 h-2 rounded-full bg-[#0284C7]" />
              <span>Humidity (%)</span>
            </button>
            <button
              onClick={() => setMetricFilter('SOIL')}
              className={`px-2.5 py-1 rounded-lg transition-all flex items-center gap-1.5 ${
                metricFilter === 'SOIL'
                  ? 'bg-white text-[#597C00] shadow-xs font-bold'
                  : 'text-[#617253] hover:text-[#597C00]'
              }`}
            >
              <span className="w-2 h-2 rounded-full bg-[#597C00]" />
              <span>Soil (%)</span>
            </button>
          </div>

          {/* Time Range Selector */}
          <div className="flex rounded-xl bg-[#F4F7F2] p-1 border border-[#E4EBE0] text-xs font-semibold">
            {(['LIVE', '1H', '6H', '24H'] as TimeRange[]).map((range) => (
              <button
                key={range}
                onClick={() => setSelectedRange(range)}
                className={`px-3 py-1 rounded-lg transition-all ${
                  selectedRange === range
                    ? 'bg-white text-[#597C00] shadow-xs font-bold'
                    : 'text-[#617253] hover:text-[#1B2408]'
                }`}
              >
                {range}
              </button>
            ))}
          </div>
        </div>
      </div>

      {/* Canvas Chart Area */}
      <div className="relative w-full h-[260px] my-2 bg-white rounded-xl">
        <canvas
          ref={canvasRef}
          onMouseMove={handleCanvasMouseMove}
          onMouseLeave={handleCanvasMouseLeave}
          className="w-full h-[260px] block cursor-crosshair"
        />

        {/* Botanical Tooltip with dual scales */}
        {hoveredPoint && (
          <div
            className="absolute z-20 pointer-events-none bg-[#1E2805] text-white p-3.5 rounded-xl shadow-xl text-xs border border-[#2C3B0E] -translate-x-1/2 -translate-y-full mb-2 min-w-[195px] backdrop-blur-sm"
            style={{ left: hoveredPoint.x, top: Math.max(65, hoveredPoint.y) }}
          >
            <div className="text-[10px] font-bold text-[#9DB312] font-tabular border-b border-[#2C3B0E] pb-1.5 mb-2 flex items-center justify-between">
              <span>Time Recorded</span>
              <span>{formatTime(hoveredPoint.record.timestamp)}</span>
            </div>
            <div className="space-y-1.5 font-tabular">
              <div className="flex justify-between items-center">
                <span className="text-[#F0F4E8] flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-[#D97706]" />
                  Temperature:
                </span>
                <span className="font-bold text-[#FDE68A]">
                  {fmt(hoveredPoint.record.temperature)} °C
                </span>
              </div>
              <div className="pt-1.5 mt-1.5 border-t border-[#2C3B0E] flex justify-between items-center">
                <span className="text-[#C3D883]">Sensor model:</span>
                <span className="font-bold text-white">{hoveredPoint.record.sensor_risk || 'Unknown'}</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-[#C3D883]">ESP32-CAM:</span>
                <span className="font-bold text-white">{hoveredPoint.record.vision_prediction || 'Waiting'}</span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-[#F0F4E8] flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-[#0284C7]" />
                  Air Humidity:
                </span>
                <span className="font-bold text-[#BAE6FD]">
                  {fmt(hoveredPoint.record.humidity)} %
                </span>
              </div>
              <div className="flex justify-between items-center">
                <span className="text-[#F0F4E8] flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-[#597C00]" />
                  Soil Moisture:
                </span>
                <span className="font-bold text-[#C3D883]">
                  {fmt(hoveredPoint.record.soil_moisture)} %
                </span>
              </div>
            </div>
          </div>
        )}
      </div>

      {/* Trend Momentum Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-3 gap-3.5 mt-4 pt-3 border-t border-[#E4EBE0]">
        {trendCards.map((item) => (
          <div key={item.label} className="bg-[#F4F7F2] border border-[#E4EBE0] p-3.5 rounded-xl">
            <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
              {item.label}
            </span>
            <span className={`text-sm font-bold font-display capitalize block mt-1 ${item.statusColor}`}>
              {item.value}
            </span>
          </div>
        ))}
      </div>
    </section>
  );
};
