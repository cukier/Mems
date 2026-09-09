import { useEffect, useState } from 'react';
import {
  GraduationCap,
  HelpCircle,
  Radio,
  School,
  UserSquare2,
  Users,
  Watch,
} from 'lucide-react';
import { Link } from 'react-router-dom';
import { api } from '@/api/client';
import type { EntityName } from '@/types';

const CARDS: { entity: EntityName; label: string; to: string; icon: typeof School }[] = [
  { entity: 'School', label: 'Escolas', to: '/schools', icon: School },
  { entity: 'SchoolClass', label: 'Turmas', to: '/classes', icon: Users },
  { entity: 'Teacher', label: 'Professores', to: '/teachers', icon: UserSquare2 },
  { entity: 'Student', label: 'Alunos', to: '/students', icon: GraduationCap },
  { entity: 'Question', label: 'Questões', to: '/questions', icon: HelpCircle },
  { entity: 'Band', label: 'Pulseiras', to: '/bands', icon: Watch },
];

export default function Dashboard() {
  const [counts, setCounts] = useState<Record<string, number>>({});

  useEffect(() => {
    (async () => {
      const entries = await Promise.all(
        CARDS.map(async (c) => [c.entity, (await api.entities[c.entity].list()).length] as const),
      );
      setCounts(Object.fromEntries(entries));
    })();
  }, []);

  return (
    <div>
      <p className="text-[11px] uppercase tracking-[0.3em] text-amber-400/80">Sala de aula conectada</p>
      <h1 className="text-4xl sm:text-5xl font-light tracking-tight mt-3">Painel do professor</h1>
      <p className="text-muted-foreground mt-3 text-sm leading-relaxed">
        Envie questões às pulseiras dos alunos e receba as respostas pelo movimento do smart watch.
      </p>

      <Link
        to="/live"
        className="mt-8 flex items-center justify-between rounded-3xl bg-gradient-to-r from-amber-400 to-orange-500 text-black p-6 hover:brightness-110 transition-all duration-300"
      >
        <div>
          <p className="text-xs uppercase tracking-[0.2em] opacity-70">Iniciar</p>
          <p className="text-2xl font-medium mt-1">Sessão ao vivo</p>
        </div>
        <Radio className="w-8 h-8" />
      </Link>

      <div className="grid grid-cols-2 sm:grid-cols-3 gap-3 mt-4">
        {CARDS.map(({ entity, label, to, icon: Icon }) => (
          <Link
            key={entity}
            to={to}
            className="rounded-2xl border border-border bg-card p-5 hover:border-amber-400/30 hover:bg-muted transition-all duration-300"
          >
            <Icon className="w-4 h-4 text-amber-400" />
            <p className="text-3xl font-light mt-4">{counts[entity] ?? '—'}</p>
            <p className="text-xs text-muted-foreground mt-1">{label}</p>
          </Link>
        ))}
      </div>
    </div>
  );
}
