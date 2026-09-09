import { randomUUID } from 'node:crypto';
import { db } from './db.js';

// A couple of demo questions so the bank isn't empty on first boot. Runs only
// when the records table is empty (and SEED !== '0').
export function seedIfEmpty(): boolean {
  const { n } = db.prepare('SELECT COUNT(*) AS n FROM records').get() as { n: number };
  if (n > 0 || process.env.SEED === '0') return false;

  const now = new Date();
  const insert = db.prepare(
    'INSERT INTO records (entity, id, data, created_date, updated_date) VALUES (?, ?, ?, ?, ?)',
  );
  let tick = 0;
  const put = (entity: string, data: Record<string, unknown>): string => {
    const id = randomUUID();
    const ts = new Date(now.getTime() + tick++ * 1000).toISOString();
    insert.run(entity, id, JSON.stringify(data), ts, ts);
    return id;
  };

  put('Question', {
    subject: 'Geografia',
    statement: 'Qual é a capital do Brasil?',
    a: 'São Paulo',
    b: 'Rio de Janeiro',
    c: 'Brasília',
    d: 'Salvador',
    correct: 'C',
  });

  put('Question', {
    subject: 'Matemática',
    statement: 'Quanto é 12 × 8?',
    a: '84',
    b: '96',
    c: '108',
    d: '112',
    correct: 'B',
  });

  return true;
}
