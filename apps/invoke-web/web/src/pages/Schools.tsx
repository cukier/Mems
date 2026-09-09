import CrudPage from '@/components/crud/CrudPage';
import type { CrudField } from '@/components/crud/types';
import type { School } from '@/types';

const fields: CrudField[] = [
  { name: 'name', label: 'Nome da escola', required: true },
  { name: 'cnpj', label: 'CNPJ' },
  { name: 'director', label: 'Diretor(a)' },
  { name: 'phone', label: 'Telefone' },
  { name: 'email', label: 'E-mail' },
  { name: 'city', label: 'Cidade' },
  { name: 'state', label: 'Estado' },
  { name: 'address', label: 'Endereço', type: 'textarea' },
];

export default function Schools() {
  return (
    <CrudPage<School>
      entity="School"
      title="Escolas"
      subtitle="Cadastro das instituições de ensino"
      fields={fields}
      primary={(s) => s.name}
      secondary={(s) => [s.city, s.state, s.phone].filter(Boolean).join(' · ')}
    />
  );
}
