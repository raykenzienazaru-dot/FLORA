import React, { useState } from 'react';
import { TelemetryRecord } from '../../types/dashboard';
import { fmt, formatTime } from '../../utils/formatters';
import { sendDeviceCommand } from '../../services/api';

interface CameraCapturePanelProps {
  latest: TelemetryRecord | null;
}

export const CameraCapturePanel: React.FC<CameraCapturePanelProps> = ({ latest }) => {
  const [isCapturing, setIsCapturing] = useState(false);
  const [captureFeedback, setCaptureFeedback] = useState<string | null>(null);

  const handleCapture = async () => {
    try {
      setIsCapturing(true);
      setCaptureFeedback('Mengirim sinyal foto ke ESP32-CAM...');

      // Kirimkan sinyal hardware 'C' ke device via API / MQTT
      const res = await sendDeviceCommand('C');
      setCaptureFeedback(res.message || 'Perintah Ambil Foto Terkirim');
    } catch (err: any) {
      setCaptureFeedback(err?.message || 'Gagal mengirim perintah');
    } finally {
      setTimeout(() => {
        setIsCapturing(false);
        setCaptureFeedback(null);
      }, 3500);
    }
  };

  const captureUrl = latest?.image_url || latest?.image_path || null;
  const isVisionConnected = Boolean(latest?.vision_connected);

  const captureTime = latest?.image_timestamp
    ? formatTime(latest.image_timestamp)
    : latest?.timestamp
      ? formatTime(latest.timestamp)
      : 'Standby / Menunggu Foto';

  const healthyProb = Number(latest?.vision_healthy ?? 0);
  const powderyProb = Number(latest?.vision_powdery ?? 0);
  const rustProb = Number(latest?.vision_rust ?? 0);
  const dominantProb = Number(latest?.vision_confidence ?? Math.max(healthyProb, powderyProb, rustProb));

  const rawPred = (latest?.vision_prediction || '').trim();
  const prediction = rawPred && rawPred !== 'Unknown' && rawPred !== 'StreamReady' && rawPred !== 'Awaiting inference'
    ? rawPred
    : dominantProb > 0
      ? (rustProb >= powderyProb && rustProb >= healthyProb ? 'Rust' : powderyProb >= rustProb && powderyProb >= healthyProb ? 'Powdery' : 'Healthy')
      : 'Menunggu Inferensi';

  return (
    <section id="cameraCapture" className="flora-card p-6 mt-8 shadow-xs">
      <div className="flex flex-wrap justify-between items-start gap-3 mb-4">
        <div>
          <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
            Camera Subsystem (ESP32-CAM OV2640 + Railway AI Vision)
          </span>
          <h2 className="text-lg font-bold text-[#1B2408] font-display mt-0.5">
            Leaf Canopy Camera Stream
          </h2>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={handleCapture}
            disabled={isCapturing}
            className="text-xs font-semibold px-3 py-1.5 rounded-full bg-[#1E2805] hover:bg-[#2C3B0E] text-white flex items-center gap-1.5 transition-all shadow-xs cursor-pointer disabled:opacity-50"
            title="Kirim sinyal ke ESP32-CAM untuk mengambil foto kanopi daun secara riil"
          >
            <span>{isCapturing ? '⏳' : '📸'}</span>
            <span>{captureFeedback || (isCapturing ? 'Memproses...' : 'Ambil Foto Sekarang')}</span>
          </button>
          <span
            className={`text-xs font-semibold px-2.5 py-1.5 rounded-full border flex items-center gap-1.5 ${
              captureUrl
                ? 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]'
                : 'bg-[#F4F7F2] text-[#617253] border-[#E4EBE0]'
            }`}
          >
            <span className={`w-1.5 h-1.5 rounded-full ${captureUrl ? 'bg-[#597C00]' : 'bg-[#617253]'}`} />
            {captureUrl ? 'Optical Frame Live' : 'Camera Standby'}
          </span>
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5 items-stretch">
        {/* Optical Frame / Preview Area */}
        <div className="lg:col-span-2 relative min-h-[320px] bg-[#F4F7F2] border border-[#E4EBE0] rounded-2xl overflow-hidden flex flex-col items-center justify-center text-center p-6">
          {captureUrl ? (
            <>
              <img
                src={captureUrl}
                alt="Leaf canopy capture from ESP32-CAM"
                className="w-full h-full object-cover absolute inset-0"
              />
              {/* Badges hasil Healthy, Powdery, Rust yang muncul langsung di atas frame foto */}
              <div className="absolute right-3 bottom-3 flex flex-wrap items-center justify-end gap-1.5 z-10 pointer-events-none">
                <span className="px-2.5 py-1 rounded-lg bg-[#1E2805]/90 text-white text-[10px] font-bold backdrop-blur-sm border border-[#2C3B0E] flex items-center gap-1.5 shadow-xs">
                  <span className="w-2 h-2 rounded-full bg-[#4ADE80]" />
                  Healthy: {fmt(healthyProb)}%
                </span>
                <span className="px-2.5 py-1 rounded-lg bg-[#1E2805]/90 text-white text-[10px] font-bold backdrop-blur-sm border border-[#2C3B0E] flex items-center gap-1.5 shadow-xs">
                  <span className="w-2 h-2 rounded-full bg-[#FB923C]" />
                  Powdery: {fmt(powderyProb)}%
                </span>
                <span className="px-2.5 py-1 rounded-lg bg-[#1E2805]/90 text-white text-[10px] font-bold backdrop-blur-sm border border-[#2C3B0E] flex items-center gap-1.5 shadow-xs">
                  <span className="w-2 h-2 rounded-full bg-[#F87171]" />
                  Rust: {fmt(rustProb)}%
                </span>
              </div>
            </>
          ) : (
            <div className="max-w-md p-4 flex flex-col items-center">
              <div className="w-12 h-12 rounded-2xl bg-white border border-[#E4EBE0] text-[#597C00] flex items-center justify-center mb-3.5 shadow-xs">
                <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" />
                  <circle cx="12" cy="13" r="4" />
                </svg>
              </div>
              <h3 className="text-sm font-bold text-[#1B2408] font-display uppercase tracking-wide mb-1">
                Kamera Standby (Tanpa Data Palsu)
              </h3>
              <p className="text-xs text-[#617253] leading-relaxed m-0 max-w-sm">
                Menunggu foto riil dari modul kamera ESP32-CAM. Klik tombol <strong>&apos;Ambil Foto Sekarang&apos;</strong> atau sentuh Limit Switch pada rel tanaman untuk memicu jepretan foto.
              </p>
              <div className="mt-3 px-2.5 py-1 rounded-md bg-white border border-[#E4EBE0] text-[10px] text-[#617253] font-medium">
                {isVisionConnected ? 'ESP-NOW & TFLite Cloud Backend aktif' : 'ESP32-CAM siap menerima pemicu foto'}
              </div>
            </div>
          )}

          <div className="absolute left-3 bottom-3 px-2.5 py-1 rounded-lg bg-[#1E2805]/90 text-[#F0F4E8] text-[10px] font-medium backdrop-blur-sm pointer-events-none border border-[#2C3B0E] z-10">
            ESP32-CAM Node · ESP-NOW Transport · Railway AI
          </div>
        </div>

        {/* Inference & Telemetry Metadata */}
        <div className="flex flex-col justify-between gap-3">
          <div className="space-y-2.5">
            <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3.5">
              <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                Waktu Foto Terakhir
              </span>
              <span className="text-sm font-bold text-[#1B2408] font-tabular block mt-1">
                {captureTime}
              </span>
            </div>

            <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3.5 flex justify-between items-center">
              <div>
                <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                  Klasifikasi Citra Daun
                </span>
                <span className="text-sm font-bold text-[#1B2408] block mt-0.5 capitalize font-display">
                  {prediction}
                </span>
              </div>
              <div className="text-right">
                <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                  Tingkat Keyakinan
                </span>
                <span className="text-sm font-bold text-[#597C00] font-tabular block mt-0.5">
                  {dominantProb > 0 ? `${fmt(dominantProb)}%` : '—'}
                </span>
              </div>
            </div>

            {/* Classification Probability Breakdown (Healthy, Powdery, Rust) */}
            <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3.5 space-y-2.5">
              <div className="flex justify-between items-center mb-0.5">
                <span className="text-[10px] font-bold text-[#617253] uppercase tracking-wider block">
                  Probabilitas Deteksi Riil (Railway AI)
                </span>
                <span className={`text-[10px] font-semibold px-2 py-0.5 rounded-md border ${captureUrl || dominantProb > 0 ? 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]' : 'bg-white text-[#617253] border-[#E4EBE0]'}`}>
                  {dominantProb > 0 ? 'Data Riil' : 'Menunggu Inferensi'}
                </span>
              </div>

              {/* Healthy Bar */}
              <div>
                <div className="flex justify-between text-xs font-semibold text-[#1B2408] mb-1">
                  <span className="flex items-center gap-1.5">
                    <span className="w-2 h-2 rounded-full bg-[#367C29]" />
                    Healthy (Sehat)
                  </span>
                  <span className="font-tabular text-[#22531A] font-bold">
                    {dominantProb > 0 ? `${fmt(healthyProb)}%` : '—%'}
                  </span>
                </div>
                <div className="h-2 rounded-full bg-[#E4EBE0] overflow-hidden">
                  <div
                    className="h-full rounded-full transition-all duration-700 ease-out bg-[#367C29]"
                    style={{ width: `${Math.min(100, Math.max(0, healthyProb))}%` }}
                  />
                </div>
              </div>

              {/* Powdery Mildew Bar */}
              <div>
                <div className="flex justify-between text-xs font-semibold text-[#1B2408] mb-1">
                  <span className="flex items-center gap-1.5">
                    <span className="w-2 h-2 rounded-full bg-[#EA580C]" />
                    Powdery (Embun Tepung)
                  </span>
                  <span className="font-tabular text-[#9A3412] font-bold">
                    {dominantProb > 0 ? `${fmt(powderyProb)}%` : '—%'}
                  </span>
                </div>
                <div className="h-2 rounded-full bg-[#E4EBE0] overflow-hidden">
                  <div
                    className="h-full rounded-full transition-all duration-700 ease-out bg-[#EA580C]"
                    style={{ width: `${Math.min(100, Math.max(0, powderyProb))}%` }}
                  />
                </div>
              </div>

              {/* Rust Bar */}
              <div>
                <div className="flex justify-between text-xs font-semibold text-[#1B2408] mb-1">
                  <span className="flex items-center gap-1.5">
                    <span className="w-2 h-2 rounded-full bg-[#B43E1F]" />
                    Rust (Karat Daun)
                  </span>
                  <span className="font-tabular text-[#7C2D12] font-bold">
                    {dominantProb > 0 ? `${fmt(rustProb)}%` : '—%'}
                  </span>
                </div>
                <div className="h-2 rounded-full bg-[#E4EBE0] overflow-hidden">
                  <div
                    className="h-full rounded-full transition-all duration-700 ease-out bg-[#B43E1F]"
                    style={{ width: `${Math.min(100, Math.max(0, rustProb))}%` }}
                  />
                </div>
              </div>
            </div>
          </div>

          <div className="bg-[#F4F7F2] border border-[#E4EBE0] p-3 rounded-xl">
            <p className="text-[11px] text-[#617253] m-0 leading-relaxed">
              Citra kanopi daun asli dari kamera dan klasifikasi model AI Railway ditransmisikan tanpa data dummy.
            </p>
          </div>
        </div>
      </div>
    </section>
  );
};
