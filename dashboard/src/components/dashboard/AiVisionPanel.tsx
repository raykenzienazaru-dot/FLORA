import React from 'react';
import { TelemetryRecord } from '../../types/dashboard';
import { fmt } from '../../utils/formatters';
import { explainVisionClassification } from '../../utils/sensorRules';

interface AiVisionPanelProps {
  latest: TelemetryRecord | null;
}

export const AiVisionPanel: React.FC<AiVisionPanelProps> = ({ latest }) => {
  const isConnected = Boolean(latest?.vision_connected);
  const prediction = latest?.vision_prediction || 'Awaiting inference';
  const { interpretation, why, whatToDo } = explainVisionClassification(latest);

  const bars = [
    {
      label: 'Healthy Foliage Pattern',
      value: latest?.vision_healthy !== undefined ? Number(latest.vision_healthy) : null,
      color: '#367C29',
      bgBar: '#EAF4E8',
    },
    {
      label: 'Powdery Mildew Indication',
      value: latest?.vision_powdery !== undefined ? Number(latest.vision_powdery) : null,
      color: '#EA580C',
      bgBar: '#FFF3E6',
    },
    {
      label: 'Rust Indication',
      value: latest?.vision_rust !== undefined ? Number(latest.vision_rust) : null,
      color: '#B43E1F',
      bgBar: '#FAF0EB',
    },
  ];

  return (
    <section className="flora-card p-6 flex flex-col justify-between shadow-xs">
      <div>
        {/* Header */}
        <div className="flex justify-between items-start mb-4">
          <div>
            <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
              Software AI Vision Engine · Camera Stream
            </span>
            <h2 className="text-base font-bold text-[#1B2408] font-display mt-0.5">
              AI Vision Foliage Analysis
            </h2>
          </div>
          <span
            className={`text-xs font-semibold px-2.5 py-1 rounded-full border flex items-center gap-1.5 ${isConnected
                ? 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]'
                : 'bg-[#F4F7F2] text-[#617253] border-[#E4EBE0]'
              }`}
          >
            <span className={`w-1.5 h-1.5 rounded-full ${isConnected ? 'bg-[#597C00]' : 'bg-[#617253]'}`} />
            {isConnected ? 'ESP-NOW Active' : 'ESP-NOW Standby'}
          </span>
        </div>

        {/* Prediction Summary Box */}
        <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-4 mb-4 flex justify-between items-center">
          <div>
            <span className="text-[10px] uppercase font-bold text-[#617253] tracking-wider block">
              Dominant Pattern Classification
            </span>
            <span className="text-base font-bold text-[#1B2408] font-display capitalize block mt-0.5">
              {prediction}
            </span>
          </div>
          <span className="text-[11px] font-bold text-[#597C00] bg-white px-2.5 py-1 rounded-lg border border-[#E4EBE0] shadow-xs">
            ESP-NOW Test Stream
          </span>
        </div>

        {/* Probability Bars (Real data only) */}
        <div className="space-y-3.5 my-3">
          {bars.map((bar) => {
            const hasVal = bar.value !== null && !isNaN(bar.value);
            const valNum = hasVal ? Math.min(100, Math.max(0, bar.value!)) : 0;
            return (
              <div key={bar.label}>
                <div className="flex justify-between text-xs font-semibold text-[#1B2408] mb-1.5">
                  <span>{bar.label}</span>
                  <span className="font-tabular text-[#617253]">
                    {hasVal ? `${fmt(valNum)}%` : '—%'}
                  </span>
                </div>
                <div className="h-2.5 rounded-full bg-[#E4EBE0] overflow-hidden">
                  <div
                    className="h-full rounded-full transition-all duration-500"
                    style={{
                      width: `${valNum}%`,
                      backgroundColor: bar.color,
                    }}
                  />
                </div>
              </div>
            );
          })}
        </div>

        {/* Contextual Interpretation & Clinical Action Guidance */}
        <div className="mt-4 pt-3.5 border-t border-[#E4EBE0] space-y-2.5">
          <div>
            <span className="text-[10px] font-bold uppercase tracking-wider text-[#617253] block">
              Interpretation
            </span>
            <p className="text-xs text-[#1B2408] font-medium mt-0.5 leading-relaxed">
              {interpretation}
            </p>
          </div>

          <div>
            <span className="text-[10px] font-bold uppercase tracking-wider text-[#617253] block">
              Pattern Rationale
            </span>
            <p className="text-xs text-[#617253] mt-0.5 leading-relaxed">
              {why}
            </p>
          </div>

          <div>
            <span className="text-[10px] font-bold uppercase tracking-wider text-[#597C00] block">
              Recommended Action
            </span>
            <p className="text-xs text-[#1B2408] font-medium mt-0.5 leading-relaxed">
              {whatToDo}
            </p>
          </div>
        </div>
      </div>

      {/* Non-diagnostic Agronomic Disclaimer */}
      <div className="mt-4 pt-3 border-t border-[#E4EBE0]">
        <p className="text-[11px] text-[#617253] leading-relaxed m-0">
          Klasifikasi visual diproses berdasarkan fitur model <code className="text-[#597C00] font-mono text-[10px]">AI_VISION_INT8</code> (fitur kelas: Healthy, Powdery Mildew, Leaf Rust) untuk pemantauan dini kesehatan kanopi daun.
        </p>
      </div>
    </section>
  );
};
