import WatchPreview, { type Phase } from '@/components/live/WatchPreview';
import type { Question } from '@/types';

// Sample question, only to illustrate the "question" phase in the showcase.
const SAMPLE = {
  id: 'sample',
  created_date: '',
  updated_date: '',
  statement: 'Qual é a capital do Brasil?',
  answer_a: 'São Paulo',
  answer_a_dir: 'up',
  answer_b: 'Rio de Janeiro',
  answer_b_dir: 'down',
  answer_c: 'Brasília',
  answer_c_dir: 'left',
  answer_d: 'Salvador',
  answer_d_dir: 'right',
  correct: 'C',
} as unknown as Question;

const PHASES: { key: Phase; label: string; desc: string; answerDir?: string }[] = [
  { key: 'idle', label: 'Standby', desc: 'Nó conectado, aguardando pergunta' },
  { key: 'countdown', label: 'Contagem', desc: '5 → 1 — aluno responde durante a contagem' },
  { key: 'result', label: 'Resultado', desc: 'Mostra a resposta do aluno', answerDir: 'left' },
];

export default function Showcase() {
  return (
    <div>
      <h1 className="text-3xl font-light tracking-tight">Telas da pulseira</h1>
      <p className="text-sm text-muted-foreground mt-1">
        As fases do display do wearable, do standby à captura do gesto.
      </p>

      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-8 mt-10">
        {PHASES.map((p) => (
          <div key={p.key} className="flex flex-col items-center">
            <WatchPreview
              question={SAMPLE}
              phase={p.key}
              countdown={p.key === 'countdown' ? 3 : 5}
              bandNumber={p.key === 'idle' ? '03' : null}
              answerDir={p.answerDir}
            />
            <div className="mt-4 text-center">
              <p className="text-sm font-medium">{p.label}</p>
              <p className="text-[11px] text-muted-foreground mt-0.5">{p.desc}</p>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
