import React from 'react';
import { TelemetryRecord } from '../../types/dashboard';
import { fmt } from '../../utils/formatters';
import { explainEnvironmentalRisk } from '../../utils/sensorRules';

interface EnvironmentalAiRiskPanelProps {
  latest: TelemetryRecord | null;
}

export const EnvironmentalAiRiskPanel: React.FC<EnvironmentalAiRiskPanelProps> = ({ latest }) => {
  const risk = latest?.sensor_risk ? String(latest.sensor_risk).toUpperCase() : 'UNKNOWN';
  const confidence = latest?.sensor_confidence !== undefined ? fmt(latest.sensor_confidence) : null;

  // Real probabilities from on-device model telemetry (only if provided)
  const hasHighProb = latest?.high_probability !== undefined && latest.high_probability !== null;
  const hasModProb = latest?.moderate_probability !== undefined && latest.moderate_probability !== null;
  const hasLowProb = latest?.low_probability !== undefined && latest.low_probability !== null;
  const hasProbabilities = hasHighProb || hasModProb || hasLowProb;

  const getRiskBadge = (r: string) => {
    switch (r) {
      case 'HIGH':
        return {
          label: 'HIGH RISK',
          style: 'bg-[#FEEAEA] text-[#961C1C] border-[#FCCECE]',
          dot: 'bg-[#DC2626]',
        };
      case 'MODERATE':
        return {
          label: 'MODERATE RISK',
          style: 'bg-[#FEF7E8] text-[#8A570C] border-[#FDE3B5]',
          dot: 'bg-[#D97706]',
        };
      case 'LOW':
        return {
          label: 'LOW RISK',
          style: 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]',
          dot: 'bg-[#367C29]',
        };
      default:
        return {
          label: 'AWAITING INFERENCE',
          style: 'bg-[#F4F7F2] text-[#617253] border-[#E4EBE0]',
          dot: 'bg-[#617253]',
        };
    }
  };

  const riskBadge = getRiskBadge(risk);
  const { reasons, actions } = explainEnvironmentalRisk(latest);

  return (
    <section className="flora-card p-6 flex flex-col justify-between shadow-xs">
      <div>
        {/* Header */}
        <div className="flex justify-between items-start mb-4">
          <div>
            <div className="flex items-center gap-2">
              <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
                Software AI Microclimate Engine
              </span>
              <span className="text-[9px] font-semibold bg-[#EAF4E8] text-[#22531A] px-2 py-0.5 rounded-full border border-[#C4E1BF]">
                0% HW Load
              </span>
            </div>
            <h2 className="text-base font-bold text-[#1B2408] font-display mt-0.5">
              Microclimate Disease Risk Analysis
            </h2>
          </div>
          <span className={`text-xs font-bold px-3 py-1 rounded-full border flex items-center gap-1.5 ${riskBadge.style}`}>
            <span className={`w-1.5 h-1.5 rounded-full ${riskBadge.dot}`} />
            {riskBadge.label}
          </span>
        </div>

        {/* Primary Risk Summary Card */}
        <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-4 my-2 flex items-center justify-between">
          <div>
            <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
              Assessed Environmental Risk
            </span>
            <span className="text-lg font-bold text-[#1B2408] font-display block mt-0.5">
              {risk === 'UNKNOWN' ? 'Awaiting Inference' : `${risk} Risk Level`}
            </span>
          </div>

          <div className="text-right">
            <span className="text-[10px] font-semibold text-[#617253] uppercase tracking-wider block">
              Model Confidence
            </span>
            <span className="text-base font-bold font-tabular text-[#597C00] block mt-0.5">
              {confidence ? `${confidence}%` : '—%'}
            </span>
          </div>
        </div>

        {/* Model Probabilities Distribution (Real data only) */}
        {hasProbabilities && (
          <div className="grid grid-cols-3 gap-2.5 my-3">
            <div className="bg-white p-3 rounded-xl border border-[#E4EBE0] shadow-xs">
              <span className="text-[10px] font-semibold text-[#617253] uppercase block">
                High Risk
              </span>
              <span className="text-base font-bold font-tabular text-[#961C1C] block mt-0.5">
                {hasHighProb ? `${fmt(latest!.high_probability)}%` : '—'}
              </span>
            </div>
            <div className="bg-white p-3 rounded-xl border border-[#E4EBE0] shadow-xs">
              <span className="text-[10px] font-semibold text-[#617253] uppercase block">
                Moderate Risk
              </span>
              <span className="text-base font-bold font-tabular text-[#8A570C] block mt-0.5">
                {hasModProb ? `${fmt(latest!.moderate_probability)}%` : '—'}
              </span>
            </div>
            <div className="bg-white p-3 rounded-xl border border-[#E4EBE0] shadow-xs">
              <span className="text-[10px] font-semibold text-[#617253] uppercase block">
                Low Risk
              </span>
              <span className="text-base font-bold font-tabular text-[#22531A] block mt-0.5">
                {hasLowProb ? `${fmt(latest!.low_probability)}%` : '—'}
              </span>
            </div>
          </div>
        )}

        {/* Section: Why this result? (Rule-based Telemetry Evidence) */}
        <div className="mt-4 pt-3.5 border-t border-[#E4EBE0]">
          <h3 className="text-xs font-bold text-[#1B2408] mb-2 flex items-center gap-1.5 font-display uppercase tracking-wider">
            <span className="w-1.5 h-1.5 rounded-full bg-[#597C00]" />
            Why This Result?
          </h3>

          {latest ? (
            <ul className="text-xs text-[#617253] space-y-1.5 pl-4 list-disc leading-relaxed">
              {reasons.map((r, i) => (
                <li key={i}>{r}</li>
              ))}
            </ul>
          ) : (
            <p className="text-xs text-[#617253] italic">
              Not enough data to explain this result.
            </p>
          )}
        </div>

        {/* Section: What to do (Actionable Steps) */}
        <div className="mt-3.5 pt-3 border-t border-[#E4EBE0]">
          <h3 className="text-xs font-bold text-[#1B2408] mb-2 flex items-center gap-1.5 font-display uppercase tracking-wider">
            <span className="w-1.5 h-1.5 rounded-full bg-[#9DB312]" />
            What To Do:
          </h3>

          <ul className="text-xs text-[#1B2408] space-y-1 pl-4 list-disc font-medium leading-relaxed">
            {actions.map((act, i) => (
              <li key={i}>{act}</li>
            ))}
          </ul>
        </div>
      </div>

      {/* Scientific Context Disclaimer */}
      <div className="mt-4 pt-3 border-t border-[#E4EBE0]">
        <p className="text-[11px] text-[#617253] leading-relaxed m-0">
          Evaluasi risiko mikroklimat dihitung oleh model Neural Network <code className="text-[#597C00] font-mono text-[10px]">grenvis_sensor_model</code> (fitur input: suhu, kelembapan udara, kelembapan tanah) sebagai sistem pendukung keputusan agronomis.
        </p>
      </div>
    </section>
  );
};
