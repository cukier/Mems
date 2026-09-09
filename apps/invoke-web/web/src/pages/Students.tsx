import CrudPage from '@/components/crud/CrudPage';
import DeviceScanner from '@/components/live/DeviceScanner';
import type { CrudField } from '@/components/crud/types';
import useOptions from '@/hooks/useOptions';
import type { Student } from '@/types';

export default function Students() {
  const classes = useOptions('SchoolClass', (c) => `${c.name}${c.period ? ` (${c.period})` : ''}`);
  const bands = useOptions('Band', (b) => `Pulseira ${b.number}`);
  const fields: CrudField[] = [
    { name: 'name', label: 'Nome do aluno', required: true },
    { name: 'registration', label: 'Matrícula' },
    { name: 'birth_date', label: 'Data de nascimento', type: 'date' },
    { name: 'guardian', label: 'Responsável' },
    { name: 'phone', label: 'Telefone' },
    { name: 'class_id', label: 'Turma', type: 'select', options: classes },
    {
      name: 'band_number',
      label: 'Número da pulseira',
      type: 'select',
      options: bands.map((b) => ({
        value: b.label.replace('Pulseira ', ''),
        label: b.label,
      })),
    },
  ];
  return (
    <div className="space-y-6">
      <DeviceScanner />
      <CrudPage<Student>
        entity="Student"
        title="Alunos"
        subtitle="Alunos, turmas e pulseiras"
        fields={fields}
        primary={(s) => s.name}
        secondary={(s) =>
          [s.registration && `Mat. ${s.registration}`, s.band_number && `Pulseira ${s.band_number}`]
            .filter(Boolean)
            .join(' · ')
        }
      />
    </div>
  );
}
