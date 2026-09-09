import { useEffect, useMemo, useState } from 'react';
import { Crown, Loader2, Medal, Trophy } from 'lucide-react';
import { api } from '@/api/client';
import type { QuizSession, SchoolClass } from '@/types';

interface RankRow {
  class_id: string;
  total: number;
  hits: number;
  sessions: number;
  name: string;
  period: string;
  grade: string;
  avg: number;
}

export default function Ranking() {
  const [sessions, setSessions] = useState<QuizSession[] | null>(null);
  const [classes, setClasses] = useState<SchoolClass[]>([]);

  useEffect(() => {
    (async () => {
      setSessions(await api.entities.QuizSession.list('-created_date', 50));
      setClasses(await api.entities.SchoolClass.list());
    })();
  }, []);

  const ranking = useMemo<RankRow[]>(() => {
    if (!sessions) return [];
    const classMap = Object.fromEntries(classes.map((c) => [c.id, c]));
    const agg: Record<string, Omit<RankRow, 'name' | 'period' | 'grade' | 'avg'>> = {};
    sessions.forEach((s) => {
      if (!s.class_id) return;
      const total = (s.results || []).length;
      const hits = (s.results || []).filter((r) => r.is_correct).length;
      if (!agg[s.class_id]) {
        agg[s.class_id] = { class_id: s.class_id, total: 0, hits: 0, sessions: 0 };
      }
      agg[s.class_id].total += total;
      agg[s.class_id].hits += hits;
      agg[s.class_id].sessions += 1;
    });
    return Object.values(agg)
      .map((a) => {
        const cls = classMap[a.class_id];
        return {
          ...a,
          name: cls?.name || 'Sem turma',
          period: cls?.period || '',
          grade: cls?.grade || '',
          avg: a.total ? Math.round((a.hits / a.total) * 100) : 0,
        };
      })
      .sort((a, b) => b.avg - a.avg || b.hits - a.hits);
  }, [sessions, classes]);

  const podium = ranking.slice(0, 3);
  const rest = ranking.slice(3);
  const top = ranking[0];

  return (
    <div>
      <h1 className="text-3xl font-light tracking-tight">Ranking das turmas</h1>
      <p className="text-sm text-muted-foreground mt-1">
        Desempenho nas questões recentes — incentive a competição saudável!
      </p>

      {sessions === null ? (
        <div className="flex justify-center py-20">
          <Loader2 className="w-5 h-5 animate-spin text-muted-foreground" />
        </div>
      ) : ranking.length === 0 ? (
        <div className="text-center py-20 border border-dashed border-border rounded-2xl">
          <Trophy className="w-6 h-6 text-amber-400 mx-auto" />
          <p className="text-muted-foreground text-sm mt-3">Nenhuma sessão realizada ainda.</p>
        </div>
      ) : (
        <>
          {podium.length > 0 && (
            <div className="grid grid-cols-3 gap-3 mt-8 items-end">
              {[1, 0, 2].map((idx) => {
                const r = podium[idx];
                if (!r) return <div key={idx} />;
                const heights = ['h-32', 'h-40', 'h-28'];
                const order = idx === 0 ? 'order-2' : idx === 1 ? 'order-1' : 'order-3';
                const Icon = idx === 0 ? Crown : Trophy;
                const tone =
                  idx === 0 ? 'text-amber-400' : idx === 1 ? 'text-neutral-300' : 'text-orange-400';
                return (
                  <div key={r.class_id} className={`${order} flex flex-col items-center`}>
                    <Icon className={`w-6 h-6 ${tone} mb-2`} />
                    <div
                      className={`w-full ${
                        heights[idx === 0 ? 1 : idx === 1 ? 0 : 2]
                      } rounded-t-2xl border border-border bg-gradient-to-b ${
                        idx === 0
                          ? 'from-amber-400/30 to-amber-400/5'
                          : idx === 1
                            ? 'from-neutral-400/20 to-neutral-400/5'
                            : 'from-orange-500/20 to-orange-500/5'
                      } flex flex-col items-center justify-center pt-3`}
                    >
                      <span className="text-xs text-muted-foreground">#{idx + 1}</span>
                      <span className="text-2xl font-light mt-1">{r.avg}%</span>
                    </div>
                    <p className="text-sm mt-2 text-center truncate w-full">{r.name}</p>
                    <p className="text-[11px] text-muted-foreground">{r.period}</p>
                  </div>
                );
              })}
            </div>
          )}

          {top && (
            <div className="mt-6 rounded-2xl bg-gradient-to-r from-amber-400/15 to-transparent border border-amber-400/20 p-5 flex items-center gap-3">
              <Crown className="w-5 h-5 text-amber-400" />
              <div>
                <p className="text-xs uppercase tracking-wider text-amber-400/80">Líder atual</p>
                <p className="text-lg font-medium">
                  {top.name} — {top.avg}% de acertos
                </p>
              </div>
            </div>
          )}

          {rest.length > 0 && (
            <div className="mt-4 rounded-2xl border border-border bg-card divide-y divide-border">
              {rest.map((r, i) => (
                <div key={r.class_id} className="flex items-center gap-4 px-5 py-3.5">
                  <span className="w-6 text-center text-sm text-muted-foreground">{i + 4}</span>
                  <Medal className="w-4 h-4 text-neutral-600" />
                  <div className="flex-1 min-w-0">
                    <p className="text-sm truncate">{r.name}</p>
                    <p className="text-xs text-muted-foreground">
                      {[r.period, r.grade, `${r.sessions} sessões`].filter(Boolean).join(' · ')}
                    </p>
                  </div>
                  <span className="text-sm font-light">{r.avg}%</span>
                  <div className="h-1.5 w-24 rounded-full bg-muted overflow-hidden">
                    <div
                      className="h-full rounded-full bg-amber-400"
                      style={{ width: `${r.avg}%` }}
                    />
                  </div>
                </div>
              ))}
            </div>
          )}
        </>
      )}
    </div>
  );
}
