// INVOKE Band BLE protocol — app <-> proxy node.
// Mirrors docs/INVOKE_BLE_ESPECIFICACAO.md (§2) and the firmware in
// src/Mems/main/invoke_ble.c. Keep the three UUIDs byte-for-byte in sync.

// Nordic UART Service. The node advertises this UUID (incomplete 128-bit list)
// and serves it as a primary GATT service.
export const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
// RX: app -> node. WRITE / WRITE_NO_RSP. Carries the question-dispatch JSON.
export const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
// TX: node -> app. NOTIFY. Carries one gesture per notification.
export const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

export type Direction = 'up' | 'down' | 'left' | 'right';
// Ordered to match the fixed direction<->letter map / capture-screen layout
// (spec §2.1, §3.3): up=A, left=B, right=C, down=D — reading order top, left,
// right, bottom.
export const DIRECTIONS: Direction[] = ['up', 'left', 'right', 'down'];
export const DIR_TO_LETTER: Record<Direction, string> = {
  up: 'A',
  left: 'B',
  right: 'C',
  down: 'D',
};

// §2.1 app -> node. The answer key never goes on the air — only `bands`, `cd`,
// and the display hints (`s`, `o`).
export interface QuestionDispatch {
  t: 'q';
  bands: string[]; // band numbers 1..64; only these bands run the round
  cd: number; // legacy — firmware ignores it for gameplay; kept only because
              // it's part of the mesh dedupe key (bitmap ^ cd), see buildDispatch
  to: number; // the time the student has to answer (teacher-set; spec §3.3's only phase)
  s?: string; // statement, for the node's OLED
  o?: Partial<Record<Direction, string>>; // option text per gesture direction
}

// §2.2 node -> app. `b` may be absent when the band did not identify itself.
export interface Gesture {
  band: string | null;
  dir: Direction;
}

export interface QuestionInput {
  statement: string;
  answerSecs: number; // the time the student has to answer (spec §3.3's only phase)
  bandNumbers: string[];
  options: Record<Direction, string>; // text per direction ('' = unused)
  correct: Direction | null; // local only — never sent
}

export function buildDispatch(q: QuestionInput): QuestionDispatch {
  const o: Partial<Record<Direction, string>> = {};
  for (const dir of DIRECTIONS) {
    const text = q.options[dir]?.trim();
    if (text) o[dir] = text;
  }
  return {
    t: 'q',
    bands: q.bandNumbers.map(String),
    // cd is legacy — the firmware doesn't run a separate countdown phase (see
    // the doc comment on QuestionDispatch.cd) — but it's still part of the
    // mesh dedupe key (bitmap ^ cd), so it needs to vary between questions
    // rather than collide if two distinct questions to the same bands land
    // within the 3s dedupe window. Not user-facing; just a random byte.
    cd: Math.floor(Math.random() * 256),
    to: q.answerSecs,
    s: q.statement || undefined,
    o: Object.keys(o).length ? o : undefined,
  };
}

// Parse a TX notification. Canonical form is JSON `{"b":"3","d":"up"}`; the
// firmware's older text forms ("3:up", "up") are still accepted.
export function parseGesture(raw: string): Gesture | null {
  const text = raw.trim().toLowerCase();
  if (!text) return null;

  let band: string | null = null;
  let dir = text;

  if (text.startsWith('{')) {
    try {
      const j = JSON.parse(text) as { b?: unknown; d?: unknown };
      band = j.b != null ? String(j.b) : null;
      dir = String(j.d ?? '');
    } catch {
      return null;
    }
  } else if (text.includes(':')) {
    const [b, d] = text.split(':');
    band = b || null;
    dir = d ?? '';
  }

  return isDirection(dir) ? { band, dir } : null;
}

export function isDirection(v: string): v is Direction {
  return (DIRECTIONS as string[]).includes(v);
}

// Fixed map — up=A, left=B, right=C, down=D — same as the firmware's
// dir_to_letter() and the capture-screen layout.
export function directionToLetter(dir: Direction): string {
  return DIR_TO_LETTER[dir];
}
