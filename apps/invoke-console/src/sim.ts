// Simulator transport: exercises the whole UI with no hardware. It accepts a
// question dispatch, waits out the countdown, then emits one random gesture per
// band listed in `bands` (plus the occasional miss), so the gesture list and
// scoring can be developed offline.

import { DIRECTIONS, type Direction, type QuestionDispatch } from './protocol';
import type { Transport, TransportEvents } from './transport';

export class SimTransport implements Transport {
  readonly kind = 'sim' as const;
  private timers = new Set<ReturnType<typeof setTimeout>>();

  constructor(private readonly ev: TransportEvents) {}

  async connect(): Promise<void> {
    this.ev.onStateChange('connecting');
    this.ev.onLog('info', 'Simulador: conectando…');
    await this.wait(300);
    this.ev.onLog('ok', 'Simulador conectado (sem hardware).');
    this.ev.onStateChange('connected');
  }

  async disconnect(): Promise<void> {
    this.timers.forEach(clearTimeout);
    this.timers.clear();
    this.ev.onLog('info', 'Simulador desconectado.');
    this.ev.onStateChange('idle');
  }

  async sendQuestion(dispatch: QuestionDispatch): Promise<void> {
    this.ev.onLog('tx', JSON.stringify(dispatch));
    const delayBase = dispatch.cd * 1000;
    for (const band of dispatch.bands) {
      // ~15% of bands "miss" the capture window
      if (Math.random() < 0.15) continue;
      const jitter = 400 + Math.random() * 3000;
      this.schedule(delayBase + jitter, () => {
        const dir = DIRECTIONS[Math.floor(Math.random() * DIRECTIONS.length)] as Direction;
        const raw = JSON.stringify({ b: band, d: dir });
        this.ev.onLog('rx', raw);
        this.ev.onGesture({ band, dir }, raw);
      });
    }
  }

  private schedule(ms: number, fn: () => void) {
    const t = setTimeout(() => {
      this.timers.delete(t);
      fn();
    }, ms);
    this.timers.add(t);
  }

  private wait(ms: number) {
    return new Promise((r) => this.schedule(ms, () => r(undefined)));
  }
}
