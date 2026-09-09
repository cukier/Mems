// Web Bluetooth client for the INVOKE Band proxy (Nordic UART Service).
//
// The happy path is a SINGLE gatt.connect() + getPrimaryService(). The old
// connect -> fail -> disconnect -> reconnect churn is itself what triggers the
// Android 0x3e (BLE_ERR_CONN_ESTABLISHMENT) before service discovery. Retry
// only on a real failure, with a full disconnect + a >=1s pause, at most
// twice, and on total failure dump whatever services the device *does*
// expose. See docs/INVOKE_BLE_ESPECIFICACAO.md.

export const NUS_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // node -> app, notify
export const NUS_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // app -> node, write

const DISCOVERY_ATTEMPTS = 3;
const RETRY_PAUSE_MS = 1200;

export type AnswerLetter = 'A' | 'B' | 'C' | 'D';

export interface AnswerEvent {
  band: number;
  ans: AnswerLetter | ''; // '' = the band locked in no answer
}

// app -> proxy (NUS RX). The proxy re-broadcasts this to every band.
export interface QuestionDispatch {
  t: 'q';
  rid: number; // round id
  cd: number; // countdown seconds before answering opens
  to: number; // answer window seconds
  s: string; // statement
  a: string;
  b: string;
  c: string;
  d: string;
  bands?: number[]; // optional address filter; omitted/empty = all
}

export interface DispatchInput {
  rid: number;
  statement: string;
  a: string;
  b: string;
  c: string;
  d: string;
  cd: number;
  to: number;
  bands?: number[];
}

export interface MeshNodeHandle {
  send: (obj: unknown) => Promise<void>;
  stop: () => Promise<void>;
  bandNumber: number | null; // from the node's "INVOKE-xx" BLE name
}

export const bleSupported = (): boolean =>
  typeof navigator !== 'undefined' && !!navigator.bluetooth;

const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

export function buildDispatch(input: DispatchInput): QuestionDispatch {
  return {
    t: 'q',
    rid: input.rid,
    cd: Math.max(0, Math.round(input.cd)),
    to: Math.max(1, Math.round(input.to)),
    s: input.statement,
    a: input.a,
    b: input.b,
    c: input.c,
    d: input.d,
    ...(input.bands && input.bands.length ? { bands: input.bands } : {}),
  };
}

function parseAnswer(raw: string): AnswerEvent | null {
  const text = raw.trim();
  if (!text.startsWith('{')) return null;
  try {
    const j = JSON.parse(text) as { t?: unknown; n?: unknown; ans?: unknown };
    if (j.t !== 'a') return null;
    const band = Number(j.n);
    if (!Number.isFinite(band)) return null;
    const ans = String(j.ans ?? '').toUpperCase();
    return { band, ans: (['A', 'B', 'C', 'D'].includes(ans) ? ans : '') as AnswerLetter | '' };
  } catch {
    return null;
  }
}

/**
 * Connect to a band over GATT (it becomes the "proxy" for this round). Returns
 * a handle to send the question and receive every band's answer.
 */
export async function connectMeshNode(
  onAnswer: (a: AnswerEvent) => void,
  onError?: (err: Error) => void,
): Promise<MeshNodeHandle> {
  if (!bleSupported()) {
    throw new Error('Web Bluetooth não é suportado neste navegador. Use o Chrome no Android.');
  }

  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'INVOKE' }],
    optionalServices: [NUS_SERVICE_UUID],
  });

  let service: BluetoothRemoteGATTService | undefined;
  let lastErr: unknown;
  for (let attempt = 1; attempt <= DISCOVERY_ATTEMPTS; attempt++) {
    try {
      const server = await device.gatt!.connect();
      service = await server.getPrimaryService(NUS_SERVICE_UUID);
      break;
    } catch (e) {
      lastErr = e;
      try {
        if (device.gatt?.connected) device.gatt.disconnect();
      } catch {
        /* ignore */
      }
      if (attempt < DISCOVERY_ATTEMPTS) await sleep(RETRY_PAUSE_MS);
    }
  }

  if (!service) {
    let uuids = 'nenhum';
    try {
      const sv = device.gatt?.connected ? device.gatt : await device.gatt!.connect();
      const found = await sv.getPrimaryServices();
      uuids = found.map((s) => s.uuid).join(', ') || 'nenhum';
    } catch {
      /* ignore */
    }
    try {
      if (device.gatt?.connected) device.gatt.disconnect();
    } catch {
      /* ignore */
    }
    throw new Error(
      `Conexão instável ou cache GATT do Chrome — serviço NUS (${NUS_SERVICE_UUID}) ` +
        `não apareceu após ${DISCOVERY_ATTEMPTS} tentativas (serviços vistos: ${uuids}). ` +
        'Reset a permissão Bluetooth do site (config do site → Bluetooth) ou desligue/religue ' +
        `o Bluetooth do aparelho e tente de novo.${lastErr ? ` [${String(lastErr)}]` : ''}`,
    );
  }

  const tx = await service.getCharacteristic(NUS_TX_UUID);
  let rx: BluetoothRemoteGATTCharacteristic | null = null;
  try {
    rx = await service.getCharacteristic(NUS_RX_UUID);
  } catch {
    /* no RX — send() will throw */
  }

  const handler = (evt: Event): void => {
    const value = (evt.target as BluetoothRemoteGATTCharacteristic).value;
    if (!value) return;
    const a = parseAnswer(new TextDecoder().decode(value));
    if (a) onAnswer(a);
  };

  tx.addEventListener('characteristicvaluechanged', handler);
  if (tx.properties.notify) await tx.startNotifications();

  device.addEventListener('gattserverdisconnected', () => {
    try {
      onError?.(new Error('Dispositivo desconectado.'));
    } catch {
      /* ignore */
    }
  });

  const send = async (obj: unknown): Promise<void> => {
    if (!rx) throw new Error('O nó não aceita comandos (sem característica RX no firmware).');
    await rx.writeValue(new TextEncoder().encode(JSON.stringify(obj)));
  };

  const stop = async (): Promise<void> => {
    try {
      await tx.stopNotifications();
    } catch {
      /* ignore */
    }
    try {
      if (device.gatt?.connected) device.gatt.disconnect();
    } catch {
      /* ignore */
    }
  };

  const m = /INVOKE-(\d+)/i.exec(device.name ?? '');
  const bandNumber = m ? Number(m[1]) : null;

  return { send, stop, bandNumber };
}
