import CrudPage from '@/components/crud/CrudPage';
import type { CrudField } from '@/components/crud/types';
import type { Question } from '@/types';

const fields: CrudField[] = [
  { name: 'subject', label: 'Disciplina' },
  { name: 'statement', label: 'Enunciado', type: 'textarea', required: true },
  { name: 'a', label: 'Resposta A (cima)' },
  { name: 'b', label: 'Resposta B (baixo)' },
  { name: 'c', label: 'Resposta C (esquerda)' },
  { name: 'd', label: 'Resposta D (direita)' },
  {
    name: 'correct',
    label: 'Resposta correta',
    type: 'select',
    options: ['A', 'B', 'C', 'D'].map((x) => ({ value: x, label: x })),
  },
];

export default function Questions() {
  return (
    <CrudPage<Question>
      entity="Question"
      title="Banco de perguntas"
      subtitle="Perguntas reutilizáveis — A cima, B baixo, C esquerda, D direita"
      fields={fields}
      primary={(q) => q.statement}
      secondary={(q) =>
        [q.subject, q.correct && `Correta: ${q.correct}`].filter(Boolean).join(' · ')
      }
    />
  );
}
