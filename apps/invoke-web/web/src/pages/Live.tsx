import { useEffect, useMemo, useRef, useState } from 'react';
import { Check, Loader2, Plug, PlugZap, Send, X } from 'lucide-react';
import { api } from '@/api/client';
import WatchPreview from '@/components/live/WatchPreview';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Textarea } from '@/components/ui/textarea';
import {
  bleSupported,
  buildDispatch,
  connectMeshNode,
  type AnswerLetter,
} from '@/lib/ble';
import type { Question, RoundResult } from '@/types';

type Phase = 'idle' | 'countdown' | 'answer' | 'result';
const TIMEOUTS = [5, 8, 10, 15, 20, 30, 45, 60];
const LETTERS: AnswerLetter[] = ['A', 'B', 'C', 'D'];

interface Compose {
  statement: string;
  a: string;
  b: string;
  c: string;
  d: string;
  correct: AnswerLetter | '';
  cd: number;
  to: number;
}

const EMPTY: Compose = {
  statement: '',
  a: '',
  b: '',
  c: '',
  d: '',
  correct: '',
  cd: 3,
  to: 15,
};

export default function Live() {
  const [bank, setBank] = useState<Question[]>([]);
  const [pick, setPick] = useState('');
  const [c, setC] = useState<Compose>(EMPTY);

  const [phase, setPhase] = useState<Phase>('idle');
  const [secsLeft, setSecsLeft] = useState(0);
  const [answers, setAnswers] = useState<Record<number, AnswerLetter | ''>>({});

  const [hubConnected, setHubConnected] = useState(false);
  const [hubBusy, setHubBusy] = useState(false);
  const [err, setErr] = useState('');

  const stopHub = useRef<() => Promise<void>>(async () => {});
  const sendHub = useRef<((o: unknown) => Promise<void>) | null>(null);
  const answersRef = useRef<Record<number, AnswerLetter | ''>>({});

  useEffect(() => {
    api.entities.Question.list('-created_date').then(setBank);
    return () => {
      stopHub.current().catch(() => {});
    };
  }, []);

  const set = <K extends keyof Compose>(k: K, v: Compose[K]) =>
    setC((p) => ({ ...p, [k]: v }));

  const loadFromBank = (id: string) => {
    setPick(id);
    const q = bank.find((x) => x.id === id);
    if (q) {
      setC({
        statement: q.statement,
        a: q.a ?? '',
        b: q.b ?? '',
        c: q.c ?? '',
        d: q.d ?? '',
        correct: q.correct ?? '',
        cd: c.cd,
        to: c.to,
      });
    }
  };

  const connect = async () => {
    setErr('');
    setHubBusy(true);
    try {
      const { send, stop } = await connectMeshNode(
        ({ band, ans }) => {
          answersRef.current[band] = ans;
          setAnswers({ ...answersRef.current });
        },
        (e) => {
          setHubConnected(false);
          sendHub.current = null;
          setErr(e?.message || 'Nó desconectado.');
        },
      );
      stopHub.current = stop;
      sendHub.current = send;
      setHubConnected(true);
    } catch (e) {
      setErr(e instanceof Error ? e.message : 'Não foi possível conectar ao nó.');
    }
    setHubBusy(false);
  };

  const disconnect = async () => {
    await stopHub.current().catch(() => {});
    stopHub.current = async () => {};
    sendHub.current = null;
    setHubConnected(false);
  };

  const running = phase === 'countdown' || phase === 'answer';
  const canSend =
    hubConnected && !running && c.statement.trim() !== '' && LETTERS.every((l) => c[toKey(l)].trim() !== '');

  const start = async () => {
    if (!canSend) return;
    setErr('');
    answersRef.current = {};
    setAnswers({});

    const rid = Date.now() % 60000;
    try {
      await sendHub.current?.(
        buildDispatch({
          rid,
          statement: c.statement.trim(),
          a: c.a.trim(),
          b: c.b.trim(),
          c: c.c.trim(),
          d: c.d.trim(),
          cd: c.cd,
          to: c.to,
        }),
      );
    } catch (e) {
      setErr(e instanceof Error ? e.message : 'Falha ao enviar a pergunta.');
      return;
    }

    setPhase('countdown');
    for (let n = c.cd; n >= 1; n--) {
      setSecsLeft(n);
      await sleep(1000);
    }
    setPhase('answer');
    for (let n = c.to; n >= 1; n--) {
      setSecsLeft(n);
      await sleep(1000);
    }
    setSecsLeft(0);
    setPhase('result');

    const snapshot = { ...answersRef.current };
    const results: RoundResult[] = Object.entries(snapshot).map(([band, ans]) => ({
      band: Number(band),
      answer: ans,
      is_correct: c.correct !== '' && ans === c.correct,
    }));

    await api.entities.Round.create({
      rid,
      question_id: pick || undefined,
      statement: c.statement.trim(),
      a: c.a.trim(),
      b: c.b.trim(),
      c: c.c.trim(),
      d: c.d.trim(),
      correct: c.correct || undefined,
      cd: c.cd,
      to: c.to,
      results,
    });
  };

  const board = useMemo(
    () =>
      Object.entries(answers)
        .map(([band, ans]) => ({ band: Number(band), ans }))
        .sort((x, y) => x.band - y.band),
    [answers],
  );

  const hits = board.filter((b) => c.correct !== '' && b.ans === c.correct).length;

  return (
    <div>
      <h1 className="text-3xl font-light tracking-tight">Rodada ao vivo</h1>
      <p className="text-sm text-muted-foreground mt-1">
        Componha a pergunta, conecte um nó e envie. As pulseiras respondem pela inclinação do pulso.
      </p>

      <div className="grid lg:grid-cols-2 gap-6 mt-8">
        <div className="space-y-4">
          <div className="rounded-2xl border border-border bg-card p-5 space-y-3">
            {bank.length > 0 && (
              <div>
                <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                  Carregar do banco
                </label>
                <select
                  value={pick}
                  onChange={(e) => loadFromBank(e.target.value)}
                  className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm focus:outline-none focus:border-amber-400/60"
                >
                  <option value="">— nova pergunta —</option>
                  {bank.map((q) => (
                    <option key={q.id} value={q.id}>
                      {q.statement.slice(0, 60)}
                    </option>
                  ))}
                </select>
              </div>
            )}

            <div>
              <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                Enunciado
              </label>
              <Textarea
                value={c.statement}
                onChange={(e) => set('statement', e.target.value)}
                rows={2}
                className="mt-1.5"
              />
            </div>

            <div className="grid grid-cols-2 gap-2">
              {LETTERS.map((l) => (
                <div key={l}>
                  <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                    {l} · {dirLabel(l)}
                  </label>
                  <Input
                    value={c[toKey(l)]}
                    onChange={(e) => set(toKey(l), e.target.value)}
                    className="mt-1.5"
                  />
                </div>
              ))}
            </div>

            <div className="grid grid-cols-3 gap-2">
              <div>
                <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                  Correta
                </label>
                <select
                  value={c.correct}
                  onChange={(e) => set('correct', e.target.value as AnswerLetter | '')}
                  className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm focus:outline-none focus:border-amber-400/60"
                >
                  <option value="">—</option>
                  {LETTERS.map((l) => (
                    <option key={l} value={l}>
                      {l}
                    </option>
                  ))}
                </select>
              </div>
              <div>
                <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                  Contagem
                </label>
                <select
                  value={c.cd}
                  onChange={(e) => set('cd', Number(e.target.value))}
                  className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm focus:outline-none focus:border-amber-400/60"
                >
                  {[0, 1, 2, 3, 4, 5].map((n) => (
                    <option key={n} value={n}>
                      {n}s
                    </option>
                  ))}
                </select>
              </div>
              <div>
                <label className="text-[11px] uppercase tracking-wider text-muted-foreground">
                  Tempo
                </label>
                <select
                  value={c.to}
                  onChange={(e) => set('to', Number(e.target.value))}
                  className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm focus:outline-none focus:border-amber-400/60"
                >
                  {TIMEOUTS.map((n) => (
                    <option key={n} value={n}>
                      {n}s
                    </option>
                  ))}
                </select>
              </div>
            </div>

            <div className="flex items-center justify-between gap-3 rounded-lg border border-border bg-background/40 px-3 py-2.5">
              <div className="flex items-center gap-2 min-w-0">
                <span
                  className={`w-2 h-2 rounded-full shrink-0 ${
                    hubConnected ? 'bg-emerald-400' : 'bg-muted-foreground/40'
                  }`}
                />
                <p className="text-xs font-medium truncate">
                  {hubConnected ? 'Nó conectado' : 'Nó não conectado'}
                </p>
              </div>
              {hubConnected ? (
                <Button onClick={disconnect} variant="outline" size="sm" className="shrink-0">
                  <PlugZap className="w-3.5 h-3.5 mr-1" /> Desconectar
                </Button>
              ) : (
                <Button
                  onClick={connect}
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
              disabled={!canSend}
              className="w-full bg-amber-400 text-black hover:bg-amber-300 h-11 rounded-xl"
            >
              {running ? (
                <Loader2 className="w-4 h-4 animate-spin mr-2" />
              ) : (
                <Send className="w-4 h-4 mr-2" />
              )}
              {phase === 'countdown'
                ? `Contagem ${secsLeft}...`
                : phase === 'answer'
                  ? `Respondendo ${secsLeft}s`
                  : 'Enviar para as pulseiras'}
            </Button>
            {err && <p className="text-xs text-red-400 -mt-1">{err}</p>}
          </div>

          <div className="rounded-2xl border border-border bg-card p-5">
            <div className="flex items-baseline justify-between">
              <p className="text-sm font-medium">Respostas</p>
              <p className="text-xs text-muted-foreground">
                {board.length} pulseira{board.length === 1 ? '' : 's'}
                {c.correct !== '' && board.length > 0 ? ` · ${hits} corretas` : ''}
              </p>
            </div>
            {board.length === 0 ? (
              <p className="text-sm text-muted-foreground text-center py-6">
                Nenhuma resposta ainda.
              </p>
            ) : (
              <div className="grid grid-cols-3 sm:grid-cols-4 gap-2 mt-4">
                {board.map(({ band, ans }) => {
                  const ok = c.correct !== '' && ans === c.correct;
                  const bad = c.correct !== '' && ans !== c.correct;
                  return (
                    <div
                      key={band}
                      className={`rounded-xl border p-3 text-center ${
                        ok
                          ? 'border-emerald-400/40 bg-emerald-400/10'
                          : bad
                            ? 'border-red-400/30 bg-red-400/5'
                            : 'border-border bg-background/40'
                      }`}
                    >
                      <p className="text-[11px] text-muted-foreground">#{String(band).padStart(2, '0')}</p>
                      <p className="text-2xl font-light mt-1">{ans || '—'}</p>
                      {c.correct !== '' && (
                        <div className="mt-1 flex justify-center">
                          {ok ? (
                            <Check className="w-3.5 h-3.5 text-emerald-400" />
                          ) : (
                            <X className="w-3.5 h-3.5 text-red-400" />
                          )}
                        </div>
                      )}
                    </div>
                  );
                })}
              </div>
            )}
          </div>
        </div>

        <div>
          <WatchPreview
            phase={phase === 'answer' ? 'countdown' : phase === 'result' ? 'idle' : phase}
            secondsLeft={secsLeft}
            question={{ statement: c.statement, a: c.a, b: c.b, c: c.c, d: c.d, correct: c.correct || undefined }}
          />
        </div>
      </div>
    </div>
  );
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const toKey = (l: AnswerLetter) => l.toLowerCase() as 'a' | 'b' | 'c' | 'd';
const dirLabel = (l: AnswerLetter) =>
  ({ A: 'cima', B: 'baixo', C: 'esquerda', D: 'direita' })[l];
