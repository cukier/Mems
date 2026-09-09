import CrudPage from '@/components/crud/CrudPage';
import { DIRECTIONS } from '@/components/DirectionArrow';
import type { CrudField } from '@/components/crud/types';
import type { Question } from '@/types';

const PERIODS = ['Manhã', 'Tarde', 'Noite', 'Integral'].map((p) => ({ value: p, label: p }));

const fields: CrudField[] = [
  { name: 'subject', label: 'Disciplina' },
  { name: 'period', label: 'Período das questões', type: 'select', options: PERIODS },
  {
    name: 'difficulty',
    label: 'Dificuldade',
    type: 'select',
    options: ['Fácil', 'Média', 'Difícil'].map((d) => ({ value: d, label: d })),
  },
  { name: 'statement', label: 'Questão', type: 'textarea', required: true },
  { name: 'answer_a', label: 'Resposta A' },
  { name: 'answer_a_dir', label: 'Movimento da resposta A', type: 'select', options: DIRECTIONS },
  { name: 'answer_b', label: 'Resposta B' },
  { name: 'answer_b_dir', label: 'Movimento da resposta B', type: 'select', options: DIRECTIONS },
  { name: 'answer_c', label: 'Resposta C' },
  { name: 'answer_c_dir', label: 'Movimento da resposta C', type: 'select', options: DIRECTIONS },
  { name: 'answer_d', label: 'Resposta D' },
  { name: 'answer_d_dir', label: 'Movimento da resposta D', type: 'select', options: DIRECTIONS },
  {
    name: 'correct',
    label: 'Resposta correta',
    type: 'select',
    options: ['A', 'B', 'C', 'D'].map((c) => ({ value: c, label: c })),
  },
];

export default function Questions() {
  return (
    <CrudPage<Question>
      entity="Question"
      title="Banco de Questões"
      subtitle="Questões com respostas e movimentos da pulseira"
      fields={fields}
      primary={(q) => q.statement}
      secondary={(q) =>
        [q.subject, q.period, q.difficulty, q.correct && `Correta: ${q.correct}`]
          .filter(Boolean)
          .join(' · ')
      }
    />
  );
}
