import { useCallback, useMemo, useRef, useState } from 'react';
import { BleTransport, bleSupported } from './ble';
import { SimTransport } from './sim';
import {
  DIR_LABEL,
  LETTERS,
  buildDispatch,
  type Answer,
  type Letter,
  type QuestionInput,
} from './protocol';
import type { LogLevel, Transport, TransportEvents, TransportState } from './transport';

interface LogLine {
  t: number;
  level: LogLevel;
  msg: string;
}

interface AnswerRow {
  t: number;
  band: number;
  ans: Letter | '';
  correct: boolean | null;
}

export function App() {
  const [state, setState] = useState<TransportState>('idle');
  const [useSim, setUseSim] = useState(!bleSupported());
  const [log, setLog] = useState<LogLine[]>([]);
  const [answers, setAnswers] = useState<AnswerRow[]>([]);

  const [bandsText, setBandsText] = useState('');
  const [countdown, setCountdown] = useState(3);
  const [timeout, setTimeoutSec] = useState(15);
  const [statement, setStatement] = useState('Qual é a capital do Brasil?');
  const [options, setOptions] = useState<Record<Letter, string>>({
    A: 'São Paulo',
    B: 'Rio de Janeiro',
    C: 'Brasília',
    D: 'Salvador',
  });
  const [correct, setCorrect] = useState<Letter | null>('C');

  const transportRef = useRef<Transport | null>(null);
  const correctRef = useRef<Letter | null>(correct);
  correctRef.current = correct;

  const addLog = useCallback((level: LogLevel, msg: string) => {
    setLog((prev) => [...prev.slice(-299), { t: Date.now(), level, msg }]);
  }, []);

  const bandNumbers = useMemo(
    () =>
      bandsText
        .split(/[\s,]+/)
        .map((s) => Number(s.trim()))
        .filter((n) => Number.isFinite(n) && n > 0),
    [bandsText],
  );

  const events: TransportEvents = useMemo(
    () => ({
      onLog: addLog,
      onStateChange: setState,
      onAnswer: (a: Answer) => {
        const c = correctRef.current;
        const isCorrect = c ? a.ans === c : null;
        setAnswers((prev) => [
          { t: Date.now(), band: a.band, ans: a.ans, correct: isCorrect },
          ...prev.filter((r) => r.band !== a.band).slice(0, 199),
        ]);
      },
    }),
    [addLog],
  );

  const connect = async () => {
    try {
      const t: Transport = useSim ? new SimTransport(events) : new BleTransport(events);
      transportRef.current = t;
      await t.connect();
    } catch (e) {
      addLog('error', e instanceof Error ? e.message : String(e));
      setState('error');
    }
  };

  const disconnect = async () => {
    await transportRef.current?.disconnect();
    transportRef.current = null;
  };

  const send = async () => {
    const t = transportRef.current;
    if (!t) return;
    const q: QuestionInput = {
      statement,
      countdown,
      timeout,
      options,
      correct,
      bandNumbers,
    };
    setAnswers([]);
    try {
      await t.sendQuestion(buildDispatch(q));
    } catch (e) {
      addLog('error', e instanceof Error ? e.message : String(e));
    }
  };

  const connected = state === 'connected';
  const busy = state === 'connecting';
  const hits = answers.filter((a) => a.correct === true).length;

  return (
    <div className="app">
      <header>
        <h1>
          INVOKE <span>Console</span>
        </h1>
        <label className="sim-toggle">
          <input
            type="checkbox"
            checked={useSim}
            disabled={connected || busy}
            onChange={(e) => setUseSim(e.target.checked)}
          />
          Simulador
        </label>
      </header>

      <section className="card">
        <div className="conn-row">
          <span className={`dot ${state}`} />
          <span className="conn-label">
            {state === 'idle' && 'Desconectado'}
            {state === 'connecting' && 'Conectando…'}
            {state === 'connected' && `Conectado (${transportRef.current?.kind})`}
            {state === 'error' && 'Erro'}
          </span>
          {!connected ? (
            <button onClick={connect} disabled={busy}>
              {useSim ? 'Iniciar simulador' : 'Conectar nó'}
            </button>
          ) : (
            <button className="ghost" onClick={disconnect}>
              Desconectar
            </button>
          )}
        </div>
        {!bleSupported() && !useSim && (
          <p className="hint">
            Web Bluetooth indisponível aqui. Use Chrome/Edge (Android ou desktop) em https:// ou
            localhost, ou ligue o Simulador.
          </p>
        )}
      </section>

      <section className="card">
        <h2>Pergunta</h2>
        <label>
          Enunciado
          <input value={statement} onChange={(e) => setStatement(e.target.value)} />
        </label>
        <div className="options">
          {LETTERS.map((l) => (
            <label key={l} className="option">
              <span className="dir">
                {l} · {DIR_LABEL[l]}
              </span>
              <input
                value={options[l]}
                onChange={(e) => setOptions((o) => ({ ...o, [l]: e.target.value }))}
              />
              <input
                type="radio"
                name="correct"
                checked={correct === l}
                onChange={() => setCorrect(l)}
                title="Resposta correta (local — nunca enviada)"
              />
            </label>
          ))}
        </div>
        <label>
          Contagem regressiva (s)
          <input
            type="number"
            min={0}
            max={10}
            value={countdown}
            onChange={(e) => setCountdown(Math.max(0, Math.min(10, Number(e.target.value) || 0)))}
          />
        </label>
        <label>
          Tempo de resposta (s)
          <input
            type="number"
            min={1}
            max={120}
            value={timeout}
            onChange={(e) => setTimeoutSec(Math.max(1, Math.min(120, Number(e.target.value) || 1)))}
          />
        </label>
        <label>
          Pulseiras (opcional, vazio = todas)
          <input value={bandsText} onChange={(e) => setBandsText(e.target.value)} placeholder="ex: 1,2,3" />
          <small>{bandNumbers.length ? `${bandNumbers.length} pulseira(s)` : 'todas'}</small>
        </label>
        <button className="primary" onClick={send} disabled={!connected}>
          Enviar para as pulseiras
        </button>
      </section>

      <section className="card">
        <h2>
          Respostas <small>{answers.length}{correct ? ` · ${hits} corretas` : ''}</small>
        </h2>
        <div className="gestures">
          {answers.length === 0 && <p className="hint">Nenhuma resposta ainda.</p>}
          {answers
            .slice()
            .sort((a, b) => a.band - b.band)
            .map((a) => (
              <div
                key={a.band}
                className={`gesture ${a.correct === true ? 'right' : a.correct === false ? 'wrong' : ''}`}
              >
                <span className="band">#{String(a.band).padStart(2, '0')}</span>
                <span className="letter">{a.ans || '—'}</span>
                <span className="time">{new Date(a.t).toLocaleTimeString()}</span>
              </div>
            ))}
        </div>
      </section>

      <section className="card log-card">
        <h2>
          Log BLE
          <button className="ghost small" onClick={() => setLog([])}>
            limpar
          </button>
        </h2>
        <div className="log">
          {log.map((l, i) => (
            <div key={i} className={`log-line ${l.level}`}>
              <span className="ts">{new Date(l.t).toLocaleTimeString()}</span>
              <span className="lvl">{l.level}</span>
              <span className="txt">{l.msg}</span>
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}
