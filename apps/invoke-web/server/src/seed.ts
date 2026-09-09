import { randomUUID } from 'node:crypto';
import { db } from './db.js';

// Minimal demo data so the app isn't a blank slate on first boot and the Live
// session has a class with band-linked students to address. Runs only when the
// records table is empty (and SEED !== '0').
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
    // stagger created_date so "-created_date" ordering is stable and readable
    const ts = new Date(now.getTime() + tick++ * 1000).toISOString();
    insert.run(entity, id, JSON.stringify(data), ts, ts);
    return id;
  };

  const schoolId = put('School', {
    name: 'Escola Municipal Machado de Assis',
    city: 'São Paulo',
    state: 'SP',
    director: 'Marina Alves',
    phone: '(11) 5555-0100',
  });

  const classId = put('SchoolClass', {
    name: '9º A',
    number: '901',
    period: 'Manhã',
    grade: '9º ano',
    year: 2026,
    school_id: schoolId,
  });

  put('Teacher', {
    name: 'Paulo Ribeiro',
    subject: 'Ciências',
    email: 'paulo.ribeiro@exemplo.edu.br',
    school_id: schoolId,
    class_id: classId,
  });

  const students = [
    ['Ana Souza', '1'],
    ['Bruno Lima', '2'],
    ['Carla Nunes', '3'],
    ['Diego Prado', '4'],
  ];
  students.forEach(([name, band], i) => {
    put('Student', {
      name,
      registration: `2026${String(i + 1).padStart(3, '0')}`,
      class_id: classId,
      band_number: band,
    });
    put('Band', {
      number: band,
      device_code: `INVOKE-0${band}`,
      status: 'Ativa',
      firmware: 'esp32c3-1.0',
    });
  });

  put('Question', {
    subject: 'Ciências',
    period: 'Manhã',
    difficulty: 'Fácil',
    statement: 'Qual é o planeta mais próximo do Sol?',
    answer_a: 'Vênus',
    answer_a_dir: 'up',
    answer_b: 'Mercúrio',
    answer_b_dir: 'down',
    answer_c: 'Terra',
    answer_c_dir: 'left',
    answer_d: 'Marte',
    answer_d_dir: 'right',
    correct: 'B',
  });

  put('Question', {
    subject: 'Matemática',
    period: 'Manhã',
    difficulty: 'Média',
    statement: 'Quanto é 12 × 8?',
    answer_a: '84',
    answer_a_dir: 'up',
    answer_b: '96',
    answer_b_dir: 'down',
    answer_c: '108',
    answer_c_dir: 'left',
    answer_d: '112',
    answer_d_dir: 'right',
    correct: 'B',
  });

  return true;
}
