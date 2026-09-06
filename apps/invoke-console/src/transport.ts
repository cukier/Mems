// Transport contract shared by the real Web Bluetooth client and the simulator,
// so the UI never branches on which one is live.

import type { Gesture, QuestionDispatch } from './protocol';

export type LogLevel = 'info' | 'ok' | 'warn' | 'error' | 'rx' | 'tx';

export interface TransportEvents {
  onLog: (level: LogLevel, msg: string) => void;
  onGesture: (g: Gesture, raw: string) => void;
  onStateChange: (state: TransportState) => void;
}

export type TransportState = 'idle' | 'connecting' | 'connected' | 'error';

export interface Transport {
  readonly kind: 'ble' | 'sim';
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  sendQuestion(dispatch: QuestionDispatch): Promise<void>;
}
