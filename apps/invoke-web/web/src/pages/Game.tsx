import { useEffect, useMemo, useRef, useState } from 'react';
import { Crown, Gamepad2, Loader2, Send, Trophy, Zap } from 'lucide-react';
import { api } from '@/api/client';
import WatchPreview, { type Phase } from '@/components/live/WatchPreview';
import { Button } from '@/components/ui/button';
import { CAPTURE_MS, connectGestureHub, directionToLetter } from '@/lib/ble';
import type { Question, QuizResult, QuizSession, SchoolClass, Student } from '@/types';

const PTS_CORRECT = 10;
const PTS_PARTICIPATION = 2;

interface RankRow {
  class_id: string;
  points: number;
  hits: number;
  total: number;
  rounds: number;
  name: string;
  period: string;
}

export default function Game() {
  const [sessions, setSessions] = useState<QuizSession[]>([]);
  const [classes, setClasses] = useState<SchoolClass[]>([]);
  const [questions, setQuestions] = useState<Question[]>([]);
  const [classId, setClassId] = useState('');
  const [questionId, setQuestionId] = useState('');
  const [phase, setPhase] = useState<Phase>('idle');
  const [countdown, setCountdown] = useState(5);
  const [lastRound, setLastRound] = useState<{ hits: number; total: number; gained: number } | null>(
    null,
  );
  const [pulse, setPulse] = useState<number | null>(null);
  const pulseTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const load = async () => {
    setSessions(await api.entities.QuizSession.list('-created_date', 200));
  };

  useEffect(() => {
    (async () => {
      setClasses(await api.entities.SchoolClass.list());
      setQuestions(await api.entities.Question.list('-created_date'));
      await load();
    })();
    const unsub = api.entities.QuizSession.subscribe(() => {
      load();
    });
    return unsub;
  }, []);

  const classMap = useMemo(
    () => Object.fromEntries(classes.map((c) => [c.id, c])),
    [classes],
  );
  const question = questions.find((q) => q.id === questionId);

  const ranking = useMemo<RankRow[]>(() => {
    const agg: Record<string, Omit<RankRow, 'name' | 'period'>> = {};
    sessions.forEach((s) => {
      if (!s.class_id) return;
      const total = (s.results || []).length;
      const hits = (s.results || []).filter((r) => r.is_correct).length;
      if (!agg[s.class_id]) {
        agg[s.class_id] = { class_id: s.class_id, points: 0, hits: 0, total: 0, rounds: 0 };
      }
      agg[s.class_id].points += hits * PTS_CORRECT + total * PTS_PARTICIPATION;
      agg[s.class_id].hits += hits;
      agg[s.class_id].total += total;
      agg[s.class_id].rounds += 1;
    });
    return Object.values(agg)
      .map((a) => ({
        ...a,
        name: classMap[a.class_id]?.name || 'Sem turma',
        period: classMap[a.class_id]?.period || '',
      }))
      .sort((a, b) => b.points - a.points);
  }, [sessions, classMap]);

  const maxPoints = ranking[0]?.points || 1;

  const flash = (pts: number) => {
    setPulse(pts);
    if (pulseTimer.current) clearTimeout(pulseTimer.current);
    pulseTimer.current = setTimeout(() => setPulse(null), 1400);
  };

  const start = async () => {
    if (!question || !classId) return;
    setPhase('countdown');
    for (let n = 5; n >= 1; n--) {
      setCountdown(n);
      await new Promise((r) => setTimeout(r, 1000));
    }

    const gestures: Record<string, string> = {};
    let stopHub: () => Promise<void> = async () => {};
    try {
      stopHub = await connectGestureHub(
        ({ band, dir }) => {
          if (dir) gestures[band || ''] = dir;
        },
        () => {},
      );
    } catch {
      /* BLE unavailable or teacher cancelled: empty capture */
    }

    setPhase('countdown');
    for (let n = 5; n >= 1; n--) {
      setCountdown(n);
      await new Promise((r) => setTimeout(r, 1000));
    }
    setPhase('go');

    const students: Student[] = await api.entities.Student.filter({ class_id: classId });

    await new Promise((r) => setTimeout(r, CAPTURE_MS));
    try {
      await stopHub();
    } catch {
      /* ignore */
    }

    const captured: QuizResult[] = students.map((s) => {
      const dir = gestures[s.band_number || ''];
      const letter = dir ? directionToLetter(question, dir) : null;
      return {
        student_id: s.id,
        student_name: s.name,
        band_number: s.band_number,
        answer: letter,
        direction: dir,
        is_correct: letter != null && letter === question.correct,
      };
    });

    await api.entities.QuizSession.create({
      class_id: classId,
      question_id: question.id,
      statement: question.statement,
      correct: question.correct,
      results: captured,
    });

    const hits = captured.filter((r) => r.is_correct).length;
    const gained = hits * PTS_CORRECT + captured.length * PTS_PARTICIPATION;
    setLastRound({ hits, total: captured.length, gained });
    flash(gained);
    setPhase('idle');
  };

  const running = phase !== 'idle';

  return (
    <div>
      <div className="flex items-center gap-2 text-amber-400/80">
        <Gamepad2 className="w-4 h-4" />
        <p className="text-[11px] uppercase tracking-[0.3em]">Modo competitivo</p>
      </div>
      <h1 className="text-3xl font-light tracking-tight mt-2">Jogo das turmas</h1>
      <p className="text-sm text-muted-foreground mt-1">
        Acertos valem {PTS_CORRECT} pts e participação {PTS_PARTICIPATION} pts. Placar atualiza em
        tempo real.
      </p>

      <div className="grid lg:grid-cols-2 gap-6 mt-8">
        <div className="space-y-4">
          <div className="rounded-2xl border border-border bg-card p-5 space-y-4">
            <div>
              <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                Turma
              </label>
              <select
                value={classId}
                onChange={(e) => setClassId(e.target.value)}
                className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm text-foreground focus:outline-none focus:border-amber-400/60"
              >
                <option value="">Selecione a turma</option>
                {classes.map((c) => (
                  <option key={c.id} value={c.id}>
                    {c.name} {c.period ? `(${c.period})` : ''}
                  </option>
                ))}
              </select>
            </div>
            <div>
              <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                Questão
              </label>
              <select
                value={questionId}
                onChange={(e) => setQuestionId(e.target.value)}
                className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm text-foreground focus:outline-none focus:border-amber-400/60"
              >
                <option value="">Selecione</option>
                {questions.map((q) => (
                  <option key={q.id} value={q.id}>
                    {q.statement.slice(0, 60)}
                  </option>
                ))}
              </select>
            </div>
            <Button
              onClick={start}
              disabled={!question || !classId || running}
              className="w-full bg-amber-400 text-black hover:bg-amber-300 h-11 rounded-xl"
            >
              {running ? (
                <Loader2 className="w-4 h-4 animate-spin mr-2" />
              ) : (
                <Send className="w-4 h-4 mr-2" />
              )}
              {running ? 'Em andamento...' : 'Disparar rodada'}
            </Button>
            {lastRound && (
              <p className="text-xs text-muted-foreground text-center">
                Última rodada: {lastRound.hits}/{lastRound.total} acertos ·{' '}
                <span className="text-amber-400 font-medium">+{lastRound.gained} pts</span>
              </p>
            )}
          </div>

          <div className="rounded-2xl border border-border bg-card p-5">
            <div className="flex items-center gap-2 mb-4">
              <Trophy className="w-4 h-4 text-amber-400" />
              <p className="text-sm font-medium">Placar ao vivo</p>
              {pulse !== null && (
                <span className="ml-auto text-xs font-semibold text-amber-400 animate-pulse">
                  +{pulse} pts
                </span>
              )}
            </div>
            {ranking.length === 0 ? (
              <p className="text-sm text-muted-foreground text-center py-6">
                Nenhuma rodada disputada ainda.
              </p>
            ) : (
              <ul className="space-y-3">
                {ranking.map((r, i) => (
                  <li key={r.class_id} className="flex items-center gap-3">
                    <span
                      className={`w-6 text-center ${
                        i === 0 ? 'text-amber-400' : 'text-muted-foreground'
                      }`}
                    >
                      {i === 0 ? <Crown className="w-4 h-4 mx-auto" /> : i + 1}
                    </span>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-baseline justify-between">
                        <p className="text-sm truncate">{r.name}</p>
                        <p className="text-sm font-light tabular-nums">{r.points} pts</p>
                      </div>
                      <div className="h-1.5 mt-1.5 rounded-full bg-muted overflow-hidden">
                        <div
                          className={`h-full rounded-full transition-all duration-700 ${
                            i === 0 ? 'bg-amber-400' : 'bg-amber-400/50'
                          }`}
                          style={{ width: `${Math.max(6, (r.points / maxPoints) * 100)}%` }}
                        />
                      </div>
                      <p className="text-[11px] text-muted-foreground mt-1">
                        {r.rounds} rodadas · {r.hits} acertos
                      </p>
                    </div>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>

        <div>
          <WatchPreview question={question} phase={phase} countdown={countdown} />
          {phase === 'go' && (
            <div className="flex items-center justify-center gap-2 mt-4 text-amber-400">
              <Zap className="w-4 h-4" />
              <p className="text-xs uppercase tracking-[0.2em]">Capturando movimentos...</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
