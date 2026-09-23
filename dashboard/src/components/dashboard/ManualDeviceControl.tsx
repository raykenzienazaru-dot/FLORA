import React from 'react';
import { useDeviceControl } from '../../hooks/useDeviceControl';
import { DeviceMovementStatus } from '../../types/dashboard';

interface ManualDeviceControlProps {
  onToast?: (message: string) => void;
  limitLeft?: boolean | null;
  limitRight?: boolean | null;
}

export const ManualDeviceControl: React.FC<ManualDeviceControlProps> = ({
  onToast,
  limitLeft,
  limitRight,
}) => {
  const {
    movementStatus,
    isSending,
    lastCommand,
    sendCommand,
    isLeftBlocked,
    isRightBlocked,
  } = useDeviceControl({ onToast, limitLeft, limitRight });

  const getStatusBadge = (status: DeviceMovementStatus) => {
    switch (status) {
      case 'left_command_sent':
        return {
          text: 'SIGNAL SENT: MOVE LEFT',
          classes: 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]',
          dot: 'bg-[#367C29]',
        };
      case 'right_command_sent':
        return {
          text: 'SIGNAL SENT: MOVE RIGHT',
          classes: 'bg-[#EAF4E8] text-[#22531A] border-[#C4E1BF]',
          dot: 'bg-[#367C29]',
        };
      case 'stop_command_sent':
        return {
          text: 'STOP COMMAND SENT',
          classes: 'bg-[#FEF7E8] text-[#8A570C] border-[#FDE3B5]',
          dot: 'bg-[#D97706]',
        };
      case 'capture_command_sent':
        return {
          text: 'CAMERA CAPTURE REQUEST SENT',
          classes: 'bg-[#E0F2FE] text-[#075985] border-[#BAE6FD]',
          dot: 'bg-[#0284C7]',
        };
      case 'sending':
        return {
          text: 'DISPATCHING TO MQTT...',
          classes: 'bg-[#E0F2FE] text-[#075985] border-[#BAE6FD]',
          dot: 'bg-[#0284C7]',
        };
      case 'error':
        return {
          text: 'DISPATCH ERROR',
          classes: 'bg-[#FEEAEA] text-[#961C1C] border-[#FCCECE]',
          dot: 'bg-[#DC2626]',
        };
      default:
        return {
          text: 'CONTROLLER READY (NO MOTOR FEEDBACK)',
          classes: 'bg-[#F4F7F2] text-[#597C00] border-[#E4EBE0]',
          dot: 'bg-[#597C00]',
        };
    }
  };

  const badge = getStatusBadge(movementStatus);

  const getLimitLabel = (val: boolean | null | undefined) => {
    if (val === true) return { text: 'ACTIVE', color: 'text-[#DC2626]' };
    if (val === false) return { text: 'CLEAR', color: 'text-[#367C29]' };
    return { text: 'UNKNOWN', color: 'text-[#617253]' };
  };

  const leftInfo = getLimitLabel(limitLeft);
  const rightInfo = getLimitLabel(limitRight);

  return (
    <section id="device-control" className="flora-card p-6 mt-8 shadow-xs">
      <div className="flex justify-between items-start mb-2 flex-wrap gap-2">
        <div>
          <span className="text-[10px] font-bold text-[#597C00] uppercase tracking-widest block">
            Actuator Subsystem
          </span>
          <h2 className="text-lg font-bold text-[#1B2408] font-display">
            Scanner Carriage Manual Control
          </h2>
        </div>
        <span
          className={`text-xs font-semibold px-3 py-1 rounded-full border flex items-center gap-1.5 ${badge.classes}`}
        >
          <span className={`w-2 h-2 rounded-full ${badge.dot}`} />
          {badge.text}
        </span>
      </div>

      <p className="text-xs text-[#617253] mb-5 leading-relaxed max-w-2xl">
        Kirimkan sinyal perintah gerak carriage scanner optik melalui broker MQTT. Status mencerminkan sinyal perintah yang diterbitkan (umpan balik posisi fisik motor tidak dilaporkan oleh perangkat keras).
      </p>

      {/* Control Buttons Trio */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-3.5">
        {/* Move Left */}
        <button
          onClick={() => sendCommand('L')}
          disabled={isSending || isLeftBlocked}
          className={`py-3 px-4 rounded-xl font-bold text-xs transition-all flex items-center justify-center gap-2 border shadow-xs ${
            movementStatus === 'left_command_sent'
              ? 'bg-[#1E2805] text-white border-[#1E2805] ring-2 ring-[#9DB312]'
              : isLeftBlocked
              ? 'bg-[#F4F7F2] text-[#617253]/60 border-[#E4EBE0] cursor-not-allowed'
              : 'bg-white hover:bg-[#F4F7F2] text-[#1B2408] border-[#E4EBE0] cursor-pointer'
          } disabled:opacity-50 disabled:cursor-not-allowed`}
          title={isLeftBlocked ? 'Left Limit Switch Active - Left motion blocked' : 'Send move left command'}
        >
          <span>◀</span>
          <span>Move Left</span>
          {isLeftBlocked && (
            <span className="text-[10px] font-semibold bg-[#FEEAEA] text-[#961C1C] px-1.5 py-0.5 rounded border border-[#FCCECE]">
              Limit
            </span>
          )}
        </button>

        {/* STOP (Emergency / Halting) */}
        <button
          onClick={() => sendCommand('S')}
          disabled={isSending}
          className="py-3 px-4 rounded-xl font-bold text-xs transition-all flex items-center justify-center gap-2 bg-[#DC2626] hover:bg-[#B91C1C] text-white border border-[#B91C1C] shadow-xs disabled:opacity-50 cursor-pointer"
          title="Send stop motor command immediately"
        >
          <span className="text-sm">■</span>
          <span>STOP MOTOR</span>
        </button>

        {/* Move Right */}
        <button
          onClick={() => sendCommand('R')}
          disabled={isSending || isRightBlocked}
          className={`py-3 px-4 rounded-xl font-bold text-xs transition-all flex items-center justify-center gap-2 border shadow-xs ${
            movementStatus === 'right_command_sent'
              ? 'bg-[#1E2805] text-white border-[#1E2805] ring-2 ring-[#9DB312]'
              : isRightBlocked
              ? 'bg-[#F4F7F2] text-[#617253]/60 border-[#E4EBE0] cursor-not-allowed'
              : 'bg-white hover:bg-[#F4F7F2] text-[#1B2408] border-[#E4EBE0] cursor-pointer'
          } disabled:opacity-50 disabled:cursor-not-allowed`}
          title={isRightBlocked ? 'Right Limit Switch Active - Right motion blocked' : 'Send move right command'}
        >
          <span>Move Right</span>
          <span>▶</span>
          {isRightBlocked && (
            <span className="text-[10px] font-semibold bg-[#FEEAEA] text-[#961C1C] px-1.5 py-0.5 rounded border border-[#FCCECE]">
              Limit
            </span>
          )}
        </button>

        <button
          onClick={() => sendCommand('C')}
          disabled={isSending}
          className="py-3 px-4 rounded-xl font-bold text-xs transition-all flex items-center justify-center gap-2 border shadow-xs bg-[#E0F2FE] hover:bg-[#BAE6FD] text-[#075985] border-[#BAE6FD] disabled:opacity-50 cursor-pointer"
          title="Request a new ESP32-CAM scan and photo"
        >
          <span>◉</span>
          <span>Capture Photo</span>
        </button>
      </div>

      {/* Hardware Telemetry & Safety Status */}
      <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 mt-4">
        <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3 flex items-center justify-between">
          <span className="text-[10px] font-semibold text-[#617253] uppercase tracking-wider">
            Command Dispatch
          </span>
          <span className="text-xs font-bold text-[#1B2408] capitalize">
            {movementStatus.replace(/_/g, ' ')}
          </span>
        </div>

        <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3 flex items-center justify-between">
          <span className="text-[10px] font-semibold text-[#617253] uppercase tracking-wider">
            Last Command Sent
          </span>
          <span className="text-xs font-bold font-tabular text-[#1B2408]">
            {lastCommand ? `Cmd '${lastCommand}'` : 'None'}
          </span>
        </div>

        <div className="bg-[#F4F7F2] border border-[#E4EBE0] rounded-xl p-3 flex items-center justify-between">
          <span className="text-[10px] font-semibold text-[#617253] uppercase tracking-wider">
            Limit Switches
          </span>
          <div className="flex items-center gap-2 text-xs font-bold font-tabular">
            <span className={leftInfo.color}>
              L: {leftInfo.text}
            </span>
            <span className="text-[#617253]/40">|</span>
            <span className={rightInfo.color}>
              R: {rightInfo.text}
            </span>
          </div>
        </div>
      </div>

      <div className="mt-4 pt-3 border-t border-[#E4EBE0]">
        <p className="text-[11px] text-[#617253] m-0 leading-relaxed">
          Perintah <code className="bg-[#F4F7F2] px-1.5 py-0.5 rounded border border-[#E4EBE0] font-mono text-[10px] text-[#1B2408]">L/R/S</code> menggerakkan L298N; <code className="bg-[#F4F7F2] px-1.5 py-0.5 rounded border border-[#E4EBE0] font-mono text-[10px] text-[#1B2408]">C</code> meminta ESP32-CAM mengambil foto melalui MQTT → ESP-NOW.
        </p>
      </div>
    </section>
  );
};
