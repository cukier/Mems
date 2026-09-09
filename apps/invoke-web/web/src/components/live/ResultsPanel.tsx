import { Check, X } from 'lucide-react';
import DirectionArrow from '@/components/DirectionArrow';
import type { QuizResult } from '@/types';

export default function ResultsPanel({
  results,
  correct,
}: {
  results: QuizResult[];
  correct?: string;
}) {
  if (!results?.length) return null;
  const hits = results.filter((r) => r.is_correct).length;
  return (
    <div className="rounded-2xl border border-border bg-card p-5">
      <div className="flex items-baseline justify-between">
        <p className="text-sm font-medium">Respostas recebidas</p>
        <p className="text-xs text-muted-foreground">
          {hits}/{results.length} corretas · gabarito {correct}
        </p>
      </div>
      <ul className="mt-4 divide-y divide-border">
        {results.map((r, i) => (
          <li key={i} className="flex items-center gap-3 py-2.5">
            {r.is_correct ? (
              <Check className="w-4 h-4 text-emerald-400" />
            ) : (
              <X className="w-4 h-4 text-red-400" />
            )}
            <span className="text-sm truncate flex-1">{r.student_name}</span>
            <span className="text-xs text-muted-foreground">Pulseira {r.band_number || '—'}</span>
            <span className="flex items-center gap-1 text-xs text-amber-300">
              <DirectionArrow dir={r.direction} className="w-3.5 h-3.5" /> {r.answer}
            </span>
          </li>
        ))}
      </ul>
    </div>
  );
}
