import { useEffect, useMemo, useState } from 'react';
import { ChevronDown, ChevronRight, Loader2 } from 'lucide-react';
import { api } from '@/api/client';
import ResultsPanel from '@/components/live/ResultsPanel';
import type { Round } from '@/types';

export default function History() {
  const [rounds, setRounds] = useState<Round[] | null>(null);
  const [open, setOpen] = useState<string | null>(null);

  useEffect(() => {
    api.entities.Round.list('-created_date', 200).then(setRounds);
  }, []);

  if (rounds === null) {
    return (
      <div className="flex justify-center py-20">
        <Loader2 className="w-5 h-5 animate-spin text-muted-foreground" />
      </div>
    );
  }

  return (
    <div>
      <h1 className="text-3xl font-light tracking-tight">Histórico</h1>
      <p className="text-sm text-muted-foreground mt-1">Rodadas enviadas e o que voltou.</p>

      {rounds.length === 0 ? (
        <div className="text-center py-20 border border-dashed border-border rounded-2xl mt-8">
          <p className="text-muted-foreground text-sm">Nenhuma rodada ainda.</p>
        </div>
      ) : (
        <div className="mt-8 space-y-2">
          {rounds.map((r) => (
            <Row key={r.id} round={r} open={open === r.id} onToggle={() => setOpen(open === r.id ? null : r.id)} />
          ))}
        </div>
      )}
    </div>
  );
}

function Row({ round, open, onToggle }: { round: Round; open: boolean; onToggle: () => void }) {
  const { total, hits, rate } = useMemo(() => {
    const t = round.results?.length ?? 0;
    const h = round.results?.filter((x) => x.is_correct).length ?? 0;
    return { total: t, hits: h, rate: t ? Math.round((h / t) * 100) : 0 };
  }, [round.results]);

  return (
    <div className="rounded-2xl border border-border bg-card">
      <button
        onClick={onToggle}
        className="w-full flex items-center gap-3 px-5 py-3.5 text-left"
      >
        {open ? (
          <ChevronDown className="w-4 h-4 text-muted-foreground shrink-0" />
        ) : (
          <ChevronRight className="w-4 h-4 text-muted-foreground shrink-0" />
        )}
        <div className="min-w-0 flex-1">
          <p className="text-sm truncate">{round.statement || '(sem enunciado)'}</p>
          <p className="text-[11px] text-muted-foreground">
            {new Date(round.created_date).toLocaleString('pt-BR')} · {total} respostas
            {round.correct ? ` · ${round.correct} correta` : ''}
          </p>
        </div>
        {round.correct && total > 0 && (
          <span className="text-sm font-light tabular-nums shrink-0">
            {hits}/{total} · {rate}%
          </span>
        )}
      </button>
      {open && (
        <div className="px-5 pb-5 pt-1 border-t border-border">
          <div className="grid grid-cols-2 gap-x-4 gap-y-1 text-xs text-muted-foreground my-3">
            <span>A · cima: {round.a}</span>
            <span>B · baixo: {round.b}</span>
            <span>C · esquerda: {round.c}</span>
            <span>D · direita: {round.d}</span>
          </div>
          <ResultsPanel results={round.results ?? []} correct={round.correct} />
        </div>
      )}
    </div>
  );
}
