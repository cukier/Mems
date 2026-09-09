import { useEffect, useMemo, useState } from 'react';
import { AlertTriangle, Loader2, TrendingUp } from 'lucide-react';
import {
  Bar,
  BarChart,
  CartesianGrid,
  Cell,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { api } from '@/api/client';
import type { Question, QuizSession, SchoolClass } from '@/types';

const COLORS = { good: '#34d399', mid: '#fbbf24', low: '#f87171' };

function colorFor(v: number): string {
  if (v >= 70) return COLORS.good;
  if (v >= 50) return COLORS.mid;
  return COLORS.low;
}

interface Agg {
  label: string;
  total: number;
  hits: number;
  avg: number;
}

export default function Performance() {
  const [sessions, setSessions] = useState<QuizSession[] | null>(null);
  const [classes, setClasses] = useState<SchoolClass[]>([]);
  const [questions, setQuestions] = useState<Question[]>([]);

  useEffect(() => {
    (async () => {
      setSessions(await api.entities.QuizSession.list('-created_date', 200));
      setClasses(await api.entities.SchoolClass.list());
      setQuestions(await api.entities.Question.list());
    })();
  }, []);

  const { byClassPeriod, bySubject, weakTopics } = useMemo(() => {
    if (!sessions) {
      return { byClassPeriod: [] as Agg[], bySubject: [] as Agg[], weakTopics: [] as Agg[] };
    }

    const classMap = Object.fromEntries(classes.map((c) => [c.id, c]));
    const qMap = Object.fromEntries(questions.map((q) => [q.id, q]));

    const aggClass: Record<string, Omit<Agg, 'avg'>> = {};
    sessions.forEach((s) => {
      const cls = s.class_id ? classMap[s.class_id] : undefined;
      const period = cls?.period || (s.question_id ? qMap[s.question_id]?.period : '') || 'Manhã';
      const label = cls ? `${cls.name} (${period})` : 'Sem turma';
      const total = (s.results || []).length;
      const hits = (s.results || []).filter((r) => r.is_correct).length;
      if (!aggClass[label]) aggClass[label] = { label, total: 0, hits: 0 };
      aggClass[label].total += total;
      aggClass[label].hits += hits;
    });
    const byClassPeriod: Agg[] = Object.values(aggClass)
      .map((a) => ({ ...a, avg: a.total ? Math.round((a.hits / a.total) * 100) : 0 }))
      .sort((a, b) => b.avg - a.avg);

    const aggSubj: Record<string, Omit<Agg, 'avg'>> = {};
    sessions.forEach((s) => {
      const subj = (s.question_id ? qMap[s.question_id]?.subject : '') || 'Outros';
      const total = (s.results || []).length;
      const hits = (s.results || []).filter((r) => r.is_correct).length;
      if (!aggSubj[subj]) aggSubj[subj] = { label: subj, total: 0, hits: 0 };
      aggSubj[subj].total += total;
      aggSubj[subj].hits += hits;
    });
    const bySubject: Agg[] = Object.values(aggSubj)
      .map((a) => ({ ...a, avg: a.total ? Math.round((a.hits / a.total) * 100) : 0 }))
      .sort((a, b) => a.avg - b.avg);

    const weakTopics = bySubject.filter((s) => s.avg < 60);

    return { byClassPeriod, bySubject, weakTopics };
  }, [sessions, classes, questions]);

  const overall = byClassPeriod.length
    ? Math.round(byClassPeriod.reduce((s, c) => s + c.avg, 0) / byClassPeriod.length)
    : 0;

  return (
    <div>
      <h1 className="text-3xl font-light tracking-tight">Desempenho</h1>
      <p className="text-sm text-muted-foreground mt-1">
        Média das turmas por período e temas que precisam de reforço.
      </p>

      {sessions === null ? (
        <div className="flex justify-center py-20">
          <Loader2 className="w-5 h-5 animate-spin text-muted-foreground" />
        </div>
      ) : sessions.length === 0 ? (
        <div className="text-center py-20 border border-dashed border-border rounded-2xl">
          <p className="text-muted-foreground text-sm">
            Nenhuma sessão realizada ainda. Inicie uma sessão ao vivo para gerar dados.
          </p>
        </div>
      ) : (
        <>
          <div className="grid sm:grid-cols-3 gap-3 mt-8">
            <Stat label="Média geral" value={`${overall}%`} icon={TrendingUp} tone="text-amber-400" />
            <Stat
              label="Turmas avaliadas"
              value={byClassPeriod.length}
              icon={TrendingUp}
              tone="text-sky-400"
            />
            <Stat
              label="Temas em reforço"
              value={weakTopics.length}
              icon={AlertTriangle}
              tone="text-red-400"
            />
          </div>

          <ChartCard
            title="Desempenho médio das turmas por período"
            subtitle="Acertos (%) por turma"
          >
            <ResponsiveContainer width="100%" height={Math.max(220, byClassPeriod.length * 44)}>
              <BarChart data={byClassPeriod} layout="vertical" margin={{ left: 20, right: 24 }}>
                <CartesianGrid horizontal={false} stroke="hsl(var(--border))" />
                <XAxis
                  type="number"
                  domain={[0, 100]}
                  tick={{ fill: '#737373', fontSize: 11 }}
                  stroke="hsl(var(--border))"
                />
                <YAxis
                  type="category"
                  dataKey="label"
                  tick={{ fill: '#a3a3a3', fontSize: 11 }}
                  width={140}
                  stroke="hsl(var(--border))"
                />
                <Tooltip
                  cursor={{ fill: 'hsl(var(--muted))' }}
                  contentStyle={{
                    background: 'hsl(var(--popover))',
                    border: '1px solid hsl(var(--border))',
                    borderRadius: 12,
                    fontSize: 12,
                  }}
                  labelStyle={{ color: 'hsl(var(--foreground))' }}
                  formatter={(v: number, n: string) => [n === 'avg' ? `${v}%` : v, 'Acerto']}
                />
                <Bar dataKey="avg" radius={[0, 6, 6, 0]} barSize={26}>
                  {byClassPeriod.map((d, i) => (
                    <Cell key={i} fill={colorFor(d.avg)} />
                  ))}
                </Bar>
              </BarChart>
            </ResponsiveContainer>
          </ChartCard>

          <div className="grid lg:grid-cols-2 gap-4 mt-4">
            <ChartCard
              title="Desempenho por disciplina"
              subtitle="Identifique temas com baixo aproveitamento"
            >
              <ResponsiveContainer width="100%" height={Math.max(200, bySubject.length * 44)}>
                <BarChart data={bySubject} layout="vertical" margin={{ left: 20, right: 24 }}>
                  <CartesianGrid horizontal={false} stroke="hsl(var(--border))" />
                  <XAxis
                    type="number"
                    domain={[0, 100]}
                    tick={{ fill: '#737373', fontSize: 11 }}
                    stroke="hsl(var(--border))"
                  />
                  <YAxis
                    type="category"
                    dataKey="label"
                    tick={{ fill: '#a3a3a3', fontSize: 11 }}
                    width={110}
                    stroke="hsl(var(--border))"
                  />
                  <Tooltip
                    cursor={{ fill: 'hsl(var(--muted))' }}
                    contentStyle={{
                      background: 'hsl(var(--popover))',
                      border: '1px solid hsl(var(--border))',
                      borderRadius: 12,
                      fontSize: 12,
                    }}
                    formatter={(v: number) => [`${v}%`, 'Acerto']}
                  />
                  <Bar dataKey="avg" radius={[0, 6, 6, 0]} barSize={22}>
                    {bySubject.map((d, i) => (
                      <Cell key={i} fill={colorFor(d.avg)} />
                    ))}
                  </Bar>
                </BarChart>
              </ResponsiveContainer>
            </ChartCard>

            <ChartCard
              title="Temas que exigem reforço"
              subtitle="Disciplinas com média abaixo de 60%"
            >
              {weakTopics.length === 0 ? (
                <div className="flex flex-col items-center justify-center h-full py-10 text-center">
                  <TrendingUp className="w-6 h-6 text-emerald-400" />
                  <p className="text-sm text-muted-foreground mt-2">
                    Nenhum tema crítico no momento. Bom trabalho!
                  </p>
                </div>
              ) : (
                <ul className="space-y-2.5">
                  {weakTopics.map((t) => (
                    <li
                      key={t.label}
                      className="flex items-center gap-3 rounded-xl bg-card border border-border px-4 py-3"
                    >
                      <span
                        className="w-12 text-center text-lg font-light"
                        style={{ color: colorFor(t.avg) }}
                      >
                        {t.avg}%
                      </span>
                      <div className="flex-1 min-w-0">
                        <p className="text-sm truncate">{t.label}</p>
                        <p className="text-xs text-muted-foreground">
                          {t.hits}/{t.total} acertos
                        </p>
                      </div>
                      <div className="h-1.5 w-24 rounded-full bg-muted overflow-hidden">
                        <div
                          className="h-full rounded-full"
                          style={{ width: `${t.avg}%`, background: colorFor(t.avg) }}
                        />
                      </div>
                    </li>
                  ))}
                </ul>
              )}
            </ChartCard>
          </div>
        </>
      )}
    </div>
  );
}

function Stat({
  label,
  value,
  icon: Icon,
  tone,
}: {
  label: string;
  value: React.ReactNode;
  icon: typeof TrendingUp;
  tone: string;
}) {
  return (
    <div className="rounded-2xl border border-border bg-card p-5">
      <Icon className={`w-4 h-4 ${tone}`} />
      <p className="text-3xl font-light mt-4">{value}</p>
      <p className="text-xs text-muted-foreground mt-1">{label}</p>
    </div>
  );
}

function ChartCard({
  title,
  subtitle,
  children,
}: {
  title: string;
  subtitle: string;
  children: React.ReactNode;
}) {
  return (
    <div className="rounded-2xl border border-border bg-card p-5 mt-4">
      <p className="text-sm font-medium">{title}</p>
      <p className="text-xs text-muted-foreground mt-0.5 mb-4">{subtitle}</p>
      {children}
    </div>
  );
}
