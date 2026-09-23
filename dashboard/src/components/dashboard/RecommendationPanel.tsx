import React from 'react';
import { TelemetryRecord } from '../../types/dashboard';
import { evaluatePlantPillars } from '../../utils/sensorRules';

interface RecommendationPanelProps {
  latest: TelemetryRecord | null;
}

export const RecommendationPanel: React.FC<RecommendationPanelProps> = ({ latest }) => {
  const pillars = evaluatePlantPillars(latest);

  const getPriorityBadge = () => {
    if (pillars.soil.status === 'Critically Dry' || pillars.aiRisk.status === 'High') {
      return { label: 'High Priority', classes: 'bg-[#FEEAEA] text-[#961C1C] border-[#FCCECE]' };
    }
    if (pillars.soil.status === 'Needs Water' || pillars.environment.status === 'Warm' || pillars.aiVision.status === 'Rust' || pillars.aiVision.status === 'Powdery') {
      return { label: 'Medium Priority', classes: 'bg-[#FEF7E8] text-[#8A570C] border-[#FDE3B5]' };
    }
    return { label: 'Normal / Routine', classes: 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]' };
  };

  const priority = getPriorityBadge();

  // Contextual actions from Software AI Condition Engine or fallback
  const getActionList = () => {
    if (!latest) return ['Menunggu aliran data telemetri dari sensor ESP32.'];
    if (latest.condition?.actions && latest.condition.actions.length > 0) {
      return latest.condition.actions;
    }

    const actions: string[] = [];
    const s = Number(latest.soil_moisture);
    const t = Number(latest.temperature);
    const v = (latest.vision_prediction || '').toLowerCase();

    if (s < 20) {
      actions.push('Kekeringan Kritis (<20%): Lakukan penyiraman darurat secara bertahap.');
    } else if (s < 30) {
      actions.push('Tanah mulai mengering (<30%): Periksa kelembapan tanah dan jadwalkan penyiraman.');
    } else if (s > 80) {
      actions.push('Tanah jenuh air (>80%): Tunda penyiraman dan periksa drainase pot.');
    }

    if (t >= 35) {
      actions.push('Suhu udara tinggi (≥35°C): Berikan naungan tambahan atau tingkatkan ventilasi.');
    }

    if (v.includes('rust')) {
      actions.push('Indikasi Karat Daun: Pangkas daun bergejala, hindari membasahi daun, gunakan fungisida tembaga.');
    } else if (v.includes('powdery')) {
      actions.push('Indikasi Embun Tepung: Tingkatkan sirkulasi udara, semprotkan fungisida organik baking soda / neem.');
    }

    if (actions.length === 0) {
      actions.push('Kondisi tanaman dan iklim mikro optimal. Pertahankan jadwal pemeliharaan rutin.');
    }

    return actions;
  };

  const actionList = getActionList();

  return (
    <section className="flora-card p-6 flex flex-col justify-between shadow-xs">
      <div>
        {/* Header */}
        <div className="flex justify-between items-start mb-3">
          <div>
            <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
              Agronomic Decision Support
            </span>
            <h2 className="text-base font-bold text-[#1B2408] font-display">
              Treatment &amp; Action Guidance
            </h2>
          </div>
          <span className={`text-xs font-semibold px-2.5 py-1 rounded-full border ${priority.classes}`}>
            {priority.label}
          </span>
        </div>

        {/* Holistic Context Banner */}
        <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-4 my-3">
          <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-wider block mb-1">
            Current Assessment
          </span>
          <p className="text-xs text-[#1B2408] leading-relaxed font-medium m-0">
            {pillars.overallAssessment}
          </p>
        </div>

        {/* Actionable Recommendations Checklist */}
        <div className="my-3">
          <span className="text-xs font-bold text-[#1B2408] block mb-2 font-display uppercase tracking-wider">
            Recommended Action Checklist:
          </span>
          <ul className="text-xs text-[#1B2408] space-y-2 pl-4 list-disc font-medium">
            {actionList.map((act, index) => (
              <li key={index} className="leading-relaxed">{act}</li>
            ))}
          </ul>
        </div>
      </div>

      <div className="mt-4 pt-3 border-t border-[#E4EBE0]">
        <p className="text-[11px] text-[#617253] leading-relaxed m-0">
          Rekomendasi bersifat pendukung keputusan budidaya berbasis telemetri nyata dan disesuaikan dengan observasi lapangan.
        </p>
      </div>
    </section>
  );
};
