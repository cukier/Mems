import CrudPage from '@/components/crud/CrudPage';
import type { CrudField } from '@/components/crud/types';
import useOptions from '@/hooks/useOptions';
import type { Teacher } from '@/types';

export default function Teachers() {
  const schools = useOptions('School', (s) => s.name);
  const classes = useOptions('SchoolClass', (c) => `${c.name}${c.period ? ` (${c.period})` : ''}`);
  const fields: CrudField[] = [
    { name: 'name', label: 'Nome', required: true },
    { name: 'cpf', label: 'CPF' },
    { name: 'subject', label: 'Disciplina' },
    { name: 'email', label: 'E-mail' },
    { name: 'phone', label: 'Telefone' },
    { name: 'school_id', label: 'Escola', type: 'select', options: schools },
    { name: 'class_id', label: 'Turma', type: 'select', options: classes },
  ];
  return (
    <CrudPage<Teacher>
      entity="Teacher"
      title="Professores"
      subtitle="Cadastro do corpo docente"
      fields={fields}
      primary={(t) => t.name}
      secondary={(t) => [t.subject, t.email].filter(Boolean).join(' · ')}
    />
  );
}
