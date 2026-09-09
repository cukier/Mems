// Simulator transport: exercises the whole UI with no hardware. It accepts a
// question dispatch, waits out the countdown, then emits one random answer per
// band (plus the occasional miss) so the board and scoring can be developed
// offline.

import { LETTERS, type Letter, type QuestionDispatch } from './protocol';
import type { Transport, TransportEvents } from './transport';

const SIM_BANDS = [1, 2, 3, 4, 5, 6, 7, 8];

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
    const bands = dispatch.bands?.length ? dispatch.bands : SIM_BANDS;
    const openAt = dispatch.cd * 1000;
    for (const band of bands) {
      if (Math.random() < 0.12) continue; // a few bands never answer
      const jitter = 400 + Math.random() * dispatch.to * 1000;
      this.schedule(openAt + jitter, () => {
        const ans =
          Math.random() < 0.1
            ? ''
            : (LETTERS[Math.floor(Math.random() * 4)] as Letter);
        const raw = JSON.stringify({ t: 'a', rid: dispatch.rid, n: band, ans });
        this.ev.onLog('rx', raw);
        this.ev.onAnswer({ band, ans }, raw);
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
