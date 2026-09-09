import CrossArrows, { CrossArrow } from '@/components/live/CrossArrows';
import type { Question } from '@/types';

const LETTERS = ['A', 'B', 'C', 'D'] as const;

// Clock position for each countdown number (clockwise).
const CLOCK: Record<number, string> = { 5: 'up', 4: 'right', 3: 'down', 2: 'left', 1: 'up' };

export type Phase = 'idle' | 'countdown' | 'go' | 'result';

export default function WatchPreview({
  question,
  phase,
  countdown,
  bandNumber,
  answerDir,
}: {
  question?: Question;
  phase: Phase;
  countdown: number;
  bandNumber?: string | null;
  answerDir?: string | null;
}) {
  const q = question as unknown as Record<string, unknown> | undefined;
  const activeDir = phase === 'countdown' ? CLOCK[countdown] || 'up' : answerDir || undefined;

  // letter/text of the chosen answer (result phase)
  const dir = String(answerDir || '').toLowerCase();
  const hit =
    question && dir
      ? LETTERS.find(
          (l) => String(q?.[`answer_${l.toLowerCase()}_dir`] ?? '').toLowerCase() === dir,
        )
      : undefined;
  const answerText = hit ? String(q?.[`answer_${hit.toLowerCase()}`] ?? '') : 'Sem resposta';

  return (
    <div className="mx-auto w-full max-w-[280px]">
      <div className="rounded-[42px] border-[6px] border-neutral-800 bg-black p-5 shadow-2xl aspect-[9/11] flex flex-col justify-center">
        {phase === 'idle' ? (
          <div className="text-center flex flex-col items-center gap-2">
            <div className="flex items-center gap-1.5">
              <span
                className="h-2 w-2 rounded-full bg-emerald-400 animate-pulse"
                style={{ boxShadow: '0 0 8px rgba(52,211,153,0.85)' }}
              />
              <span className="text-[9px] uppercase tracking-[0.3em] text-neutral-500">Pronta</span>
            </div>
            <p
              className="text-3xl font-bold tracking-[0.2em] text-neutral-100 mt-1"
              style={{ textShadow: '0 0 12px rgba(255,179,71,0.5)' }}
            >
              INVOKE
            </p>
            {bandNumber ? (
              <p className="text-[11px] text-neutral-400 mt-1">
                Pulseira #{String(bandNumber).padStart(2, '0')}
              </p>
            ) : null}
            <p className="text-[9px] uppercase tracking-[0.2em] text-neutral-600 mt-3">
              Aguardando pergunta
            </p>
          </div>
        ) : phase === 'countdown' ? (
          <div className="flex flex-col items-center">
            <p
              className="text-7xl font-light text-amber-400"
              style={{ textShadow: '0 0 18px rgba(255,179,71,0.8)' }}
            >
              {countdown}
            </p>
            <div className="mt-3 flex justify-center">
              <CrossArrows question={question} activeDir={activeDir} />
            </div>
          </div>
        ) : phase === 'go' ? (
          <p className="text-center text-5xl font-bold tracking-widest text-emerald-400 animate-pulse">
            VÁ
          </p>
        ) : phase === 'result' ? (
          <div className="flex flex-col items-center text-center">
            <p className="text-[10px] uppercase tracking-[0.3em] text-neutral-500">Sua resposta foi</p>
            <div className="w-24 h-24 mt-3">
              <CrossArrow dir={answerDir || 'up'} letter={hit || '?'} active />
            </div>
            <p className="text-base font-medium text-neutral-100 mt-3">{answerText}</p>
            <p className="text-[10px] uppercase tracking-[0.25em] text-neutral-600 mt-2">
              {question?.correct ? `Gabarito: ${question.correct}` : ''}
            </p>
          </div>
        ) : null}
      </div>
      <p className="text-center text-[10px] uppercase tracking-[0.25em] text-muted-foreground mt-3">
        Visão da pulseira
      </p>
    </div>
  );
}
