import { useState, useCallback } from 'react';
import { DeviceCommand, DeviceMovementStatus } from '../types/dashboard';
import { sendDeviceCommand } from '../services/api';

interface UseDeviceControlOptions {
  onToast?: (message: string) => void;
  limitLeft?: boolean | null;
  limitRight?: boolean | null;
}

export interface UseDeviceControlReturn {
  movementStatus: DeviceMovementStatus;
  isSending: boolean;
  lastCommand: DeviceCommand | null;
  error: string | null;
  sendCommand: (command: DeviceCommand) => Promise<void>;
  isLeftBlocked: boolean;
  isRightBlocked: boolean;
}

export function useDeviceControl({
  onToast,
  limitLeft,
  limitRight,
}: UseDeviceControlOptions = {}): UseDeviceControlReturn {
  const [movementStatus, setMovementStatus] = useState<DeviceMovementStatus>('idle');
  const [isSending, setIsSending] = useState<boolean>(false);
  const [lastCommand, setLastCommand] = useState<DeviceCommand | null>(null);
  const [error, setError] = useState<string | null>(null);

  // Strictly block only if explicitly confirmed active (true)
  const isLeftBlocked = limitLeft === true;
  const isRightBlocked = limitRight === true;

  const sendCommand = useCallback(
    async (command: DeviceCommand) => {
      if (isSending) return; // Prevent rapid duplicate requests

      // Limit switch safety enforcement
      if (command === 'L' && isLeftBlocked) {
        const msg = 'Left Limit Switch active! Movement to the left is blocked.';
        setError(msg);
        if (onToast) onToast(msg);
        return;
      }
      if (command === 'R' && isRightBlocked) {
        const msg = 'Right Limit Switch active! Movement to the right is blocked.';
        setError(msg);
        if (onToast) onToast(msg);
        return;
      }

      setIsSending(true);
      setError(null);
      setMovementStatus('sending');

      try {
        const result = await sendDeviceCommand(command);

        if (command === 'L') {
          setMovementStatus('left_command_sent');
        } else if (command === 'R') {
          setMovementStatus('right_command_sent');
        } else if (command === 'S') {
          setMovementStatus('stop_command_sent');
        } else if (command === 'C') {
          setMovementStatus('capture_command_sent');
        }

        setLastCommand(command);

        if (onToast) {
          onToast(result.message || `Command ${command} dispatched to broker`);
        }
      } catch (err: any) {
        const errorMsg = err?.message || 'Failed to send device command';
        setError(errorMsg);
        setMovementStatus('error');
        if (onToast) {
          onToast(errorMsg);
        }
      } finally {
        setIsSending(false);
      }
    },
    [isSending, isLeftBlocked, isRightBlocked, onToast]
  );

  return {
    movementStatus,
    isSending,
    lastCommand,
    error,
    sendCommand,
    isLeftBlocked,
    isRightBlocked,
  };
}
