// INVOKE Band BLE protocol v2 — teacher app <-> proxy band.
// Mirrors docs/INVOKE_BLE_ESPECIFICACAO.md (§2) and firmware/main/invoke_ble.c.
// Keep the three UUIDs and the JSON shapes in sync.

export const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // app -> proxy, write
export const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // proxy -> app, notify

export type Letter = 'A' | 'B' | 'C' | 'D';
export const LETTERS: Letter[] = ['A', 'B', 'C', 'D'];
export const DIR_LABEL: Record<Letter, string> = {
  A: 'cima',
  B: 'baixo',
  C: 'esquerda',
  D: 'direita',
};

// §2.1 app -> proxy. The answer key is never sent.
export interface QuestionDispatch {
  t: 'q';
  rid: number;
  cd: number; // countdown seconds before answering opens
  to: number; // answer window seconds
  s: string;
  a: string;
  b: string;
  c: string;
  d: string;
  bands?: number[];
}

// §2.2 proxy -> app.
export interface Answer {
  band: number;
  ans: Letter | ''; // '' = the band locked in no answer
}

export interface QuestionInput {
  statement: string;
  countdown: number;
  timeout: number;
  options: Record<Letter, string>;
  correct: Letter | null; // local only — never sent
  bandNumbers: number[]; // empty = all
}

export function buildDispatch(q: QuestionInput): QuestionDispatch {
  return {
    t: 'q',
    rid: Date.now() % 60000,
    cd: Math.max(0, Math.round(q.countdown)),
    to: Math.max(1, Math.round(q.timeout)),
    s: q.statement,
    a: q.options.A,
    b: q.options.B,
    c: q.options.C,
    d: q.options.D,
    ...(q.bandNumbers.length ? { bands: q.bandNumbers } : {}),
  };
}

export function isLetter(v: string): v is Letter {
  return (LETTERS as string[]).includes(v);
}

// Parse a TX notification: JSON `{"t":"a","rid":1,"n":3,"ans":"C"}`.
export function parseAnswer(raw: string): Answer | null {
  const text = raw.trim();
  if (!text.startsWith('{')) return null;
  try {
    const j = JSON.parse(text) as { t?: unknown; n?: unknown; ans?: unknown };
    if (j.t !== 'a') return null;
    const band = Number(j.n);
    if (!Number.isFinite(band)) return null;
    const ans = String(j.ans ?? '').toUpperCase();
    return { band, ans: isLetter(ans) ? ans : '' };
  } catch {
    return null;
  }
}
