import { Check, X } from 'lucide-react';
import type { RoundResult } from '@/types';

export default function ResultsPanel({
  results,
  correct,
}: {
  results: RoundResult[];
  correct?: string;
}) {
  if (!results?.length) {
    return <p className="text-sm text-muted-foreground py-3">Nenhuma resposta.</p>;
  }
  const hits = results.filter((r) => r.is_correct).length;
  return (
    <div>
      <p className="text-xs text-muted-foreground mb-3">
        {hits}/{results.length} corretas{correct ? ` · gabarito ${correct}` : ''}
      </p>
      <div className="grid grid-cols-4 sm:grid-cols-6 gap-2">
        {[...results]
          .sort((a, b) => a.band - b.band)
          .map((r) => {
            const ok = correct && r.is_correct;
            const bad = correct && !r.is_correct;
            return (
              <div
                key={r.band}
                className={`rounded-lg border p-2 text-center ${
                  ok
                    ? 'border-emerald-400/40 bg-emerald-400/10'
                    : bad
                      ? 'border-red-400/30 bg-red-400/5'
                      : 'border-border'
                }`}
              >
                <p className="text-[10px] text-muted-foreground">
                  #{String(r.band).padStart(2, '0')}
                </p>
                <p className="text-lg font-light">{r.answer || '—'}</p>
                {correct && (
                  <div className="flex justify-center">
                    {ok ? (
                      <Check className="w-3 h-3 text-emerald-400" />
                    ) : (
                      <X className="w-3 h-3 text-red-400" />
                    )}
                  </div>
                )}
              </div>
            );
          })}
      </div>
    </div>
  );
}
