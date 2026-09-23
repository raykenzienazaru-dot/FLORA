import React from 'react';
import { TelemetryRecord } from '../../types/dashboard';
import { fmt, formatTime } from '../../utils/formatters';

interface CameraCapturePanelProps {
  latest: TelemetryRecord | null;
}

export const CameraCapturePanel: React.FC<CameraCapturePanelProps> = ({ latest }) => {
  const captureUrl = latest?.image_url || latest?.image_path || null;
  const isVisionConnected = Boolean(latest?.vision_connected);

  const captureTime = latest?.image_timestamp
    ? formatTime(latest.image_timestamp)
    : latest?.timestamp
      ? formatTime(latest.timestamp)
      : 'Awaiting first capture';

  const prediction = latest?.vision_prediction || 'Awaiting inference';
  const dominantProb = Math.max(
    Number(latest?.vision_healthy || 0),
    Number(latest?.vision_powdery || 0),
    Number(latest?.vision_rust || 0)
  );

  return (
    <section id="cameraCapture" className="flora-card p-6 mt-8 shadow-xs">
      <div className="flex justify-between items-start mb-4">
        <div>
          <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
            Camera Subsystem
          </span>
          <h2 className="text-lg font-bold text-[#1B2408] font-display mt-0.5">
            Leaf Canopy Camera Stream
          </h2>
        </div>
        <span
          className={`text-xs font-semibold px-2.5 py-1 rounded-full border flex items-center gap-1.5 ${captureUrl
              ? 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]'
              : 'bg-[#F4F7F2] text-[#617253] border-[#E4EBE0]'
            }`}
        >
          <span className={`w-1.5 h-1.5 rounded-full ${captureUrl ? 'bg-[#597C00]' : 'bg-[#617253]'}`} />
          {captureUrl ? 'Optical Frame Uploaded' : 'Telemetry Mode (No Frame)'}
        </span>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5 items-stretch">
        {/* Optical Frame / Preview Area */}
        <div className="lg:col-span-2 relative min-h-[290px] bg-[#F4F7F2] border border-[#E4EBE0] rounded-2xl overflow-hidden flex flex-col items-center justify-center text-center p-6">
          {captureUrl ? (
            <img
              src={captureUrl}
              alt="Leaf canopy capture from ESP32-CAM"
              className="w-full h-full object-cover absolute inset-0"
            />
          ) : (
            <div className="max-w-md p-4 flex flex-col items-center">
              <div className="w-12 h-12 rounded-2xl bg-white border border-[#E4EBE0] text-[#597C00] flex items-center justify-center mb-3.5 shadow-xs">
                <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" />
                  <circle cx="12" cy="13" r="4" />
                </svg>
              </div>
              <h3 className="text-sm font-bold text-[#1B2408] font-display uppercase tracking-wide mb-1">
                No Optical Frame Uploaded Yet
              </h3>
              <p className="text-xs text-[#617253] leading-relaxed m-0 max-w-sm">
                Node ESP32-CAM mengirimkan telemetri klasifikasi via ESP-NOW. Pratinjau gambar akan tampil otomatis bila frame optik diunggah.
              </p>
              <div className="mt-3 px-2.5 py-1 rounded-md bg-white border border-[#E4EBE0] text-[10px] text-[#617253] font-medium">
                {isVisionConnected ? 'ESP-NOW inference stream active' : 'ESP32-CAM node in standby'}
              </div>
            </div>
          )}

          <div className="absolute left-3 bottom-3 px-2.5 py-1 rounded-lg bg-[#1E2805]/90 text-[#F0F4E8] text-[10px] font-medium backdrop-blur-sm pointer-events-none border border-[#2C3B0E]">
            ESP32-CAM Node · ESP-NOW Transport
          </div>
        </div>

        {/* Inference & Telemetry Metadata */}
        <div className="flex flex-col justify-between gap-3">
          <div className="space-y-2.5">
            <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3.5">
              <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                Last Optical Scan Time
              </span>
              <span className="text-sm font-bold text-[#1B2408] font-tabular block mt-1">
                {captureTime}
              </span>
            </div>

            <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3.5">
              <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                Detected Leaf Pattern
              </span>
              <span className="text-sm font-bold text-[#1B2408] block mt-1 capitalize font-display">
                {prediction}
              </span>
            </div>

            <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3.5">
              <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                Pattern Confidence
              </span>
              <span className="text-sm font-bold text-[#1B2408] font-tabular block mt-1">
                {dominantProb > 0 ? `${fmt(dominantProb)}%` : '—'}
              </span>
            </div>
          </div>

          <div className="bg-[#F4F7F2] border border-[#E4EBE0] p-3.5 rounded-xl">
            <p className="text-[11px] text-[#617253] m-0 leading-relaxed">
              Citra kanopi daun dan klasifikasi model TFLite ditransmisikan secara nirkabel untuk pemantauan dini kesehatan daun.
            </p>
          </div>
        </div>
      </div>
    </section>
  );
};
