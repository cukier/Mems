import CrudPage from '@/components/crud/CrudPage';
import type { CrudField } from '@/components/crud/types';
import useOptions from '@/hooks/useOptions';
import type { SchoolClass } from '@/types';

const PERIODS = ['Manhã', 'Tarde', 'Noite', 'Integral'].map((p) => ({ value: p, label: p }));

export default function Classes() {
  const schools = useOptions('School', (s) => s.name);
  const fields: CrudField[] = [
    { name: 'name', label: 'Nome da turma', required: true },
    { name: 'number', label: 'Número da turma' },
    { name: 'period', label: 'Período', type: 'select', options: PERIODS },
    { name: 'grade', label: 'Série / ano' },
    { name: 'year', label: 'Ano letivo', type: 'number' },
    { name: 'school_id', label: 'Escola', type: 'select', options: schools },
  ];
  return (
    <CrudPage<SchoolClass>
      entity="SchoolClass"
      title="Turmas"
      subtitle="Turmas, números e períodos"
      fields={fields}
      primary={(c) => `${c.name}${c.number ? ` · nº ${c.number}` : ''}`}
      secondary={(c) => [c.period, c.grade, c.year].filter(Boolean).join(' · ')}
    />
  );
}
