import { useEffect, useRef, useState } from 'react';
import { Loader2, Plug, PlugZap, Send } from 'lucide-react';
import { api } from '@/api/client';
import ResultsPanel from '@/components/live/ResultsPanel';
import { Button } from '@/components/ui/button';
import { bleSupported, buildDispatch, connectMeshNode, directionToLetter } from '@/lib/ble';
import type { Question, QuizResult, SchoolClass, Student } from '@/types';

type Phase = 'idle' | 'countdown' | 'result';

const TIMEOUTS = [5, 8, 10, 15, 20, 30, 45, 60];

export default function Live() {
  const [classes, setClasses] = useState<SchoolClass[]>([]);
  const [questions, setQuestions] = useState<Question[]>([]);
  const [classId, setClassId] = useState('');
  const [questionId, setQuestionId] = useState('');
  const [phase, setPhase] = useState<Phase>('idle');
  const [countdown, setCountdown] = useState(5);
  const [results, setResults] = useState<QuizResult[]>([]);
  const [hubConnected, setHubConnected] = useState(false);
  const [hubBusy, setHubBusy] = useState(false);
  const [hubError, setHubError] = useState('');
  const [timeoutSec, setTimeoutSec] = useState(8);

  const stopHubRef = useRef<() => Promise<void>>(async () => {});
  const sendRef = useRef<((obj: unknown) => Promise<void>) | null>(null);
  const gesturesRef = useRef<Record<string, string>>({});

  useEffect(() => {
    (async () => {
      setClasses(await api.entities.SchoolClass.list());
      setQuestions(await api.entities.Question.list('-created_date'));
    })();
    return () => {
      try {
        stopHubRef.current();
      } catch {
        /* ignore */
      }
    };
  }, []);

  const question = questions.find((q) => q.id === questionId);

  const connectHub = async () => {
    setHubError('');
    setHubBusy(true);
    try {
      const { send, stop } = await connectMeshNode(
        ({ band, dir }) => {
          if (dir) gesturesRef.current[band || ''] = dir;
        },
        (err) => {
          setHubConnected(false);
          sendRef.current = null;
          setHubError(err?.message || 'Nó da rede mesh desconectado.');
        },
      );
      stopHubRef.current = stop;
      sendRef.current = send;
      setHubConnected(true);
    } catch (e) {
      setHubError(e instanceof Error ? e.message : 'Não foi possível conectar ao nó da rede mesh.');
      setHubConnected(false);
    }
    setHubBusy(false);
  };

  const disconnectHub = async () => {
    try {
      await stopHubRef.current();
    } catch {
      /* ignore */
    }
    stopHubRef.current = async () => {};
    sendRef.current = null;
    setHubConnected(false);
    gesturesRef.current = {};
  };

  const start = async () => {
    if (!question) return;
    if (!classId) {
      setHubError('Selecione a turma — a questão é disparada só para ela.');
      return;
    }
    if (!hubConnected) {
      setHubError('Conecte o nó da rede mesh primeiro.');
      return;
    }
    setResults([]);
    setHubError('');

    const students: Student[] = await api.entities.Student.filter({ class_id: classId });
    const bandNumbers = [
      ...new Set(students.map((s) => String(s.band_number || '').trim()).filter(Boolean)),
    ];
    if (!bandNumbers.length) {
      setHubError('Nenhum aluno desta turma tem pulseira vinculada.');
      return;
    }

    try {
      await sendRef.current?.(buildDispatch(question, bandNumbers, 5, timeoutSec));
    } catch (e) {
      setHubError(e instanceof Error ? e.message : 'Falha ao disparar a questão no mesh.');
      return;
    }

    gesturesRef.current = {};

    setPhase('countdown');
    for (let n = 5; n >= 1; n--) {
      setCountdown(n);
      await new Promise((r) => setTimeout(r, 1000));
    }

    // gesture-capture window — the countdown screen stays visible
    await new Promise((r) => setTimeout(r, timeoutSec * 1000));

    const snapshot = { ...gesturesRef.current };
    const captured: QuizResult[] = students.map((s) => {
      const dir = snapshot[String(s.band_number || '').trim()];
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

    setResults(captured);
    setPhase('result');

    if (captured.length) {
      await api.entities.QuizSession.create({
        class_id: classId,
        question_id: question.id,
        statement: question.statement,
        correct: question.correct,
        results: captured,
      });
    }
  };

  const running = phase === 'countdown';

  return (
    <div>
      <h1 className="text-3xl font-light tracking-tight">Sessão ao vivo</h1>
      <p className="text-sm text-muted-foreground mt-1">
        Envie a questão e capture as respostas pelo movimento das pulseiras.
      </p>

      <div className="max-w-2xl space-y-4 mt-8">
        <div className="rounded-2xl border border-border bg-card p-5 space-y-4">
          <div>
            <label className="text-[11px] uppercase tracking-wider text-muted-foreground">Turma</label>
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
          <div>
            <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
              Tempo de resposta
            </label>
            <select
              value={timeoutSec}
              onChange={(e) => setTimeoutSec(Number(e.target.value))}
              className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm text-foreground focus:outline-none focus:border-amber-400/60"
            >
              {TIMEOUTS.map((s) => (
                <option key={s} value={s}>
                  {s}s
                </option>
              ))}
            </select>
          </div>

          {/* Mesh-node connection — done once, reused for every question */}
          <div className="flex items-center justify-between gap-3 rounded-lg border border-border bg-background/40 px-3 py-2.5">
            <div className="flex items-center gap-2 min-w-0">
              <span
                className={`w-2 h-2 rounded-full shrink-0 ${
                  hubConnected ? 'bg-emerald-400' : 'bg-muted-foreground/40'
                }`}
              />
              <div className="min-w-0">
                <p className="text-xs font-medium truncate">Rede mesh BLE</p>
                <p className="text-[11px] text-muted-foreground truncate">
                  {hubConnected
                    ? 'Nó conectado — as pulseiras se retransmitem'
                    : 'Nó não conectado'}
                </p>
              </div>
            </div>
            {hubConnected ? (
              <Button onClick={disconnectHub} variant="outline" size="sm" className="shrink-0">
                <PlugZap className="w-3.5 h-3.5 mr-1" /> Desconectar
              </Button>
            ) : (
              <Button
                onClick={connectHub}
                disabled={hubBusy || !bleSupported()}
                variant="outline"
                size="sm"
                className="border-amber-400/40 text-amber-300 hover:bg-amber-400/10 shrink-0"
              >
                {hubBusy ? (
                  <Loader2 className="w-3.5 h-3.5 animate-spin mr-1" />
                ) : (
                  <Plug className="w-3.5 h-3.5 mr-1" />
                )}
                {hubBusy ? 'Conectando...' : 'Conectar nó'}
              </Button>
            )}
          </div>

          <Button
            onClick={start}
            disabled={!question || !classId || running || !hubConnected}
            className="w-full bg-amber-400 text-black hover:bg-amber-300 h-11 rounded-xl"
          >
            {running ? (
              <Loader2 className="w-4 h-4 animate-spin mr-2" />
            ) : (
              <Send className="w-4 h-4 mr-2" />
            )}
            {running ? 'Em andamento...' : 'Enviar para as pulseiras'}
          </Button>
          {phase === 'countdown' && (
            <p className="text-xs text-center text-muted-foreground -mt-1">
              Contagem {countdown} · janela de resposta {timeoutSec}s
            </p>
          )}
          {hubError && <p className="text-xs text-red-400 -mt-1">{hubError}</p>}
        </div>

        <ResultsPanel results={results} correct={question?.correct} />
      </div>
    </div>
  );
}
