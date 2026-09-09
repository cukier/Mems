import DirectionArrow from '@/components/DirectionArrow';
import type { Direction, Question } from '@/types';

const LETTERS = ['A', 'B', 'C', 'D'] as const;

export type Phase = 'idle' | 'countdown' | 'go';

export default function WatchPreview({
  question,
  phase,
  countdown,
}: {
  question?: Question;
  phase: Phase;
  countdown: number;
}) {
  return (
    <div className="mx-auto w-full max-w-[280px]">
      <div className="rounded-[42px] border-[6px] border-neutral-800 bg-black p-5 shadow-2xl aspect-[9/11] flex flex-col justify-center">
        {phase === 'countdown' ? (
          <div className="text-center">
            <p className="text-[10px] uppercase tracking-[0.3em] text-neutral-500">Prepare-se</p>
            <p className="text-7xl font-light text-amber-400 mt-3">{countdown}</p>
          </div>
        ) : phase === 'go' ? (
          <p className="text-center text-5xl font-bold tracking-widest text-emerald-400 animate-pulse">
            VÁ
          </p>
        ) : (
          <div>
            <p className="text-[13px] leading-snug text-neutral-100 line-clamp-3">
              {question?.statement || 'Selecione uma questão'}
            </p>
            <div className="mt-4 space-y-1.5">
              {LETTERS.map((l) => {
                const key = l.toLowerCase() as 'a' | 'b' | 'c' | 'd';
                const text = question?.[`answer_${key}`];
                const dir = (question?.[`answer_${key}_dir`] ?? 'up') as Direction;
                if (!text) return null;
                return (
                  <div key={l} className="flex items-center gap-2 rounded-lg bg-white/5 px-2 py-1.5">
                    <DirectionArrow dir={dir} className="w-3.5 h-3.5 text-amber-400 shrink-0" />
                    <span className="text-[11px] text-neutral-300 truncate">
                      {l}. {text}
                    </span>
                  </div>
                );
              })}
            </div>
          </div>
        )}
      </div>
      <p className="text-center text-[10px] uppercase tracking-[0.25em] text-muted-foreground mt-3">
        Visão da pulseira
      </p>
    </div>
  );
}
