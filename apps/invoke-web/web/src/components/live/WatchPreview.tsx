import CrossArrows from '@/components/live/CrossArrows';

export type Phase = 'idle' | 'countdown' | 'result';

export interface PreviewQuestion {
  statement: string;
  a: string;
  b: string;
  c: string;
  d: string;
  correct?: string;
}

const OPT_KEY = { A: 'a', B: 'b', C: 'c', D: 'd' } as const;

export default function WatchPreview({
  phase,
  secondsLeft = 0,
  question,
  selected,
  bandNumber,
}: {
  phase: Phase;
  secondsLeft?: number;
  question?: PreviewQuestion;
  selected?: string;
  bandNumber?: number | null;
}) {
  const sel = String(selected ?? '').toUpperCase();
  const selText =
    question && sel in OPT_KEY ? question[OPT_KEY[sel as keyof typeof OPT_KEY]] : '';

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
            {bandNumber != null && (
              <p className="text-[11px] text-neutral-400 mt-1">
                Pulseira #{String(bandNumber).padStart(2, '0')}
              </p>
            )}
            <p className="text-[9px] uppercase tracking-[0.2em] text-neutral-600 mt-3">
              Aguardando pergunta
            </p>
          </div>
        ) : phase === 'countdown' ? (
          <div className="flex flex-col items-center">
            {question?.statement ? (
              <p className="text-[11px] leading-snug text-neutral-200 text-center line-clamp-3 mb-2">
                {question.statement}
              </p>
            ) : null}
            <p
              className="text-6xl font-light text-amber-400"
              style={{ textShadow: '0 0 18px rgba(255,179,71,0.8)' }}
            >
              {secondsLeft}
            </p>
            <div className="mt-2 flex justify-center">
              <CrossArrows active={sel} />
            </div>
          </div>
        ) : (
          <div className="flex flex-col items-center text-center">
            <p className="text-[10px] uppercase tracking-[0.3em] text-neutral-500">Sua resposta</p>
            <p className="text-6xl font-light text-emerald-400 mt-3">{sel || '—'}</p>
            {selText ? (
              <p className="text-sm text-neutral-100 mt-3">{selText}</p>
            ) : (
              <p className="text-xs text-neutral-500 mt-3">Sem resposta</p>
            )}
          </div>
        )}
      </div>
      <p className="text-center text-[10px] uppercase tracking-[0.25em] text-muted-foreground mt-3">
        Visão da pulseira
      </p>
    </div>
  );
}
