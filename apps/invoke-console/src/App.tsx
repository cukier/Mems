import { useCallback, useMemo, useRef, useState } from 'react';
import { BleTransport, bleSupported } from './ble';
import { SimTransport } from './sim';
import {
  DIRECTIONS,
  buildDispatch,
  directionToLetter,
  type Direction,
  type Gesture,
  type QuestionInput,
} from './protocol';
import type { LogLevel, Transport, TransportEvents, TransportState } from './transport';

interface LogLine {
  t: number;
  level: LogLevel;
  msg: string;
}

interface GestureRow {
  t: number;
  band: string | null;
  dir: Direction;
  letter: string | null;
  correct: boolean | null;
}

export function App() {
  const [state, setState] = useState<TransportState>('idle');
  const [useSim, setUseSim] = useState(!bleSupported());
  const [log, setLog] = useState<LogLine[]>([]);
  const [gestures, setGestures] = useState<GestureRow[]>([]);

  const [bandsText, setBandsText] = useState('1,2,3,4,5');
  const [countdown, setCountdown] = useState(5);
  const [statement, setStatement] = useState('Quanto é 12 x 8?');
  const [options, setOptions] = useState<Record<Direction, string>>({
    up: '86',
    down: '96',
    left: '108',
    right: '112',
  });
  const [correct, setCorrect] = useState<Direction | null>('down');

  const transportRef = useRef<Transport | null>(null);
  const questionRef = useRef<QuestionInput | null>(null);

  const addLog = useCallback((level: LogLevel, msg: string) => {
    setLog((prev) => [...prev.slice(-299), { t: Date.now(), level, msg }]);
  }, []);

  const bandNumbers = useMemo(
    () =>
      bandsText
        .split(/[\s,]+/)
        .map((s) => s.trim())
        .filter(Boolean),
    [bandsText],
  );

  const currentQuestion = useCallback(
    (): QuestionInput => ({
      statement,
      countdown,
      bandNumbers,
      options,
      correct,
    }),
    [statement, countdown, bandNumbers, options, correct],
  );

  const events: TransportEvents = useMemo(
    () => ({
      onLog: addLog,
      onStateChange: setState,
      onGesture: (g: Gesture) => {
        const q = questionRef.current;
        const letter = q ? directionToLetter(q, g.dir) : null;
        const isCorrect =
          q && q.correct ? (letter ? letter === directionToLetter(q, q.correct) : false) : null;
        setGestures((prev) => [
          { t: Date.now(), band: g.band, dir: g.dir, letter, correct: isCorrect },
          ...prev.slice(0, 199),
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
    if (!bandNumbers.length) {
      addLog('warn', 'Nenhuma pulseira na lista.');
      return;
    }
    const q = currentQuestion();
    questionRef.current = q;
    setGestures([]);
    try {
      await t.sendQuestion(buildDispatch(q));
    } catch (e) {
      addLog('error', e instanceof Error ? e.message : String(e));
    }
  };

  const connected = state === 'connected';
  const busy = state === 'connecting';

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
        <h2>Questão</h2>
        <label>
          Pulseiras
          <input value={bandsText} onChange={(e) => setBandsText(e.target.value)} />
          <small>{bandNumbers.length} pulseira(s)</small>
        </label>
        <label>
          Contagem regressiva (s)
          <input
            type="number"
            min={1}
            max={60}
            value={countdown}
            onChange={(e) => setCountdown(Math.max(1, Math.min(60, Number(e.target.value) || 1)))}
          />
        </label>
        <label>
          Enunciado
          <input value={statement} onChange={(e) => setStatement(e.target.value)} />
        </label>
        <div className="options">
          {DIRECTIONS.map((dir) => (
            <label key={dir} className="option">
              <span className="dir">{dir}</span>
              <input
                value={options[dir]}
                placeholder="(não usada)"
                onChange={(e) => setOptions((o) => ({ ...o, [dir]: e.target.value }))}
              />
              <input
                type="radio"
                name="correct"
                checked={correct === dir}
                onChange={() => setCorrect(dir)}
                title="Resposta correta (local — nunca enviada)"
              />
            </label>
          ))}
        </div>
        <button className="primary" onClick={send} disabled={!connected}>
          Enviar para as pulseiras
        </button>
      </section>

      <section className="card">
        <h2>
          Gestos <small>{gestures.length}</small>
        </h2>
        <div className="gestures">
          {gestures.length === 0 && <p className="hint">Nenhum gesto ainda.</p>}
          {gestures.map((g, i) => (
            <div key={i} className={`gesture ${g.correct === true ? 'right' : g.correct === false ? 'wrong' : ''}`}>
              <span className="band">#{g.band ?? '?'}</span>
              <span className="dir">{g.dir}</span>
              <span className="letter">{g.letter ?? '—'}</span>
              <span className="time">{new Date(g.t).toLocaleTimeString()}</span>
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
