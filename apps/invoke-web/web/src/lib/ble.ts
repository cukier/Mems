// Web Bluetooth client for the INVOKE proxy node (Nordic UART Service).
//
// Ported from the base44 app's src/lib/ble.js, which had already been corrected
// to match apps/invoke-console/src/ble.ts: the happy path is a SINGLE
// gatt.connect() + getPrimaryService(). The old connect -> fail -> disconnect
// -> reconnect churn is itself what triggers the Android `0x3e`
// (BLE_ERR_CONN_ESTABLISHMENT) before service discovery — see
// docs/FIRMWARE_BLE_STATUS.md. Retry only on a real failure, with a full
// disconnect + a >=1s pause, at most twice, and on total failure dump whatever
// services the device *does* expose instead of a silent empty list.

import type { Question } from '@/types';

export const GESTURE_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const GESTURE_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // TX, notify
export const GESTURE_RX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // RX, write

/** Gesture-capture window after the "VÁ" signal (ms). */
export const CAPTURE_MS = 8000;

const DISCOVERY_ATTEMPTS = 2;
const RETRY_PAUSE_MS = 1200;

const DIRECTIONS = ['up', 'down', 'left', 'right'] as const;
export type Direction = (typeof DIRECTIONS)[number];

export interface ScannedBand {
  name: string;
  id: string;
  mac_address: string;
}

export interface GestureEvent {
  band: string | null;
  dir: Direction;
}

export interface MeshNodeHandle {
  /** Write a JSON command to the node's RX characteristic (question dispatch). */
  send: (obj: unknown) => Promise<void>;
  /** Close the GATT connection. */
  stop: () => Promise<void>;
  /** Band number parsed from the node's BLE name ("INVOKE-xx" -> "xx"), or null. */
  bandNumber: string | null;
}

export interface QuestionDispatch {
  t: 'q';
  bands: string[];
  cd: number; // countdown seconds until "VÁ"
  to: number; // answer window (s) after "VÁ"
  s?: string;
  o: Partial<Record<Direction, string>>;
}

export const bleSupported = (): boolean =>
  typeof navigator !== 'undefined' && !!navigator.bluetooth;

const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

function isDirection(v: string): v is Direction {
  return (DIRECTIONS as readonly string[]).includes(v);
}

export async function scanBand(): Promise<ScannedBand> {
  if (!bleSupported()) {
    throw new Error('Bluetooth BLE não é suportado neste navegador. Use o Chrome no Android.');
  }
  // List everything and validate the INVOKE service on connect — some Androids
  // don't apply the service filter in the picker.
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'INVOKE' }],
    optionalServices: [GESTURE_SERVICE_UUID],
  });
  return {
    name: device.name || 'INVOKE Band',
    id: device.id,
    mac_address: device.id,
  };
}

/**
 * Connect to an ESP32 mesh node (proxy/gateway) over GATT. The browser holds a
 * single GATT connection; the bands relay each other's messages over the mesh.
 *
 * Accepted TX notifications:
 *   JSON:   {"b":"3","d":"up"}   (mesh form)
 *   legacy: "3:up" | "up"
 */
export async function connectMeshNode(
  onGesture: (g: GestureEvent) => void,
  onError?: (err: Error) => void,
): Promise<MeshNodeHandle> {
  if (!bleSupported()) {
    throw new Error('Web Bluetooth não é suportado neste navegador.');
  }

  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'INVOKE' }],
    optionalServices: [GESTURE_SERVICE_UUID],
  });

  let service: BluetoothRemoteGATTService | undefined;
  let lastErr: unknown;
  for (let attempt = 1; attempt <= DISCOVERY_ATTEMPTS; attempt++) {
    try {
      const server = await device.gatt!.connect();
      service = await server.getPrimaryService(GESTURE_SERVICE_UUID);
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
      `Conexão instável ou cache GATT do Chrome — serviço NUS (${GESTURE_SERVICE_UUID}) ` +
        `não apareceu após ${DISCOVERY_ATTEMPTS} tentativas (serviços vistos: ${uuids}). ` +
        'Reset a permissão Bluetooth do site (config do site → Bluetooth) ou desligue/religue ' +
        `o Bluetooth do aparelho e tente de novo.${lastErr ? ` [${String(lastErr)}]` : ''}`,
    );
  }

  const characteristic = await service.getCharacteristic(GESTURE_CHAR_UUID);
  let rxCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  try {
    rxCharacteristic = await service.getCharacteristic(GESTURE_RX_CHAR_UUID);
  } catch {
    /* node has no RX — send() will throw */
  }

  const handler = (evt: Event): void => {
    const value = (evt.target as BluetoothRemoteGATTCharacteristic).value;
    if (!value) return;
    const text = new TextDecoder().decode(value).trim().toLowerCase();
    if (!text) return;

    let band: string | null = null;
    let dir = text;
    if (text.startsWith('{')) {
      try {
        const j = JSON.parse(text) as { b?: unknown; d?: unknown };
        band = j.b != null ? String(j.b) : null;
        dir = String(j.d ?? '');
      } catch {
        return;
      }
    } else if (text.includes(':')) {
      const [b, d] = text.split(':');
      band = b || null;
      dir = d ?? '';
    }
    if (!isDirection(dir)) return;
    onGesture({ band, dir });
  };

  characteristic.addEventListener('characteristicvaluechanged', handler);
  if (characteristic.properties.notify) {
    await characteristic.startNotifications();
  } else if (characteristic.properties.read) {
    const value = await characteristic.readValue();
    handler({ target: { value } } as unknown as Event);
  }

  device.addEventListener('gattserverdisconnected', () => {
    try {
      onError?.(new Error('Dispositivo desconectado.'));
    } catch {
      /* ignore */
    }
  });

  const send = async (obj: unknown): Promise<void> => {
    if (!rxCharacteristic) {
      throw new Error('O nó não aceita comandos (sem característica RX no firmware).');
    }
    await rxCharacteristic.writeValue(new TextEncoder().encode(JSON.stringify(obj)));
  };

  const stop = async (): Promise<void> => {
    try {
      await characteristic.stopNotifications();
    } catch {
      /* ignore */
    }
    try {
      if (device.gatt?.connected) device.gatt.disconnect();
    } catch {
      /* ignore */
    }
  };

  // Band number comes from the BLE name ("INVOKE-xx"), which the firmware
  // builds from the number stored in the node's NVS.
  const m = /INVOKE-(\d+)/i.exec(device.name ?? '');
  const bandNumber = m ? m[1] : null;

  return { send, stop, bandNumber };
}

/** Back-compat (competitive mode): returns only stop(). */
export async function connectGestureHub(
  onGesture: (g: GestureEvent) => void,
  onError?: (err: Error) => void,
): Promise<() => Promise<void>> {
  const { stop } = await connectMeshNode(onGesture, onError);
  return stop;
}

/**
 * Build the mesh question-dispatch command. The question is addressed only to
 * the selected class's band numbers; the answer key is never sent to the bands.
 */
export function buildDispatch(
  question: Question,
  bandNumbers: string[],
  countdown = 5,
  timeoutSec = 8,
): QuestionDispatch {
  const q = question as unknown as Record<string, unknown>;
  const o: Partial<Record<Direction, string>> = {};
  for (const l of ['a', 'b', 'c', 'd'] as const) {
    const dir = String(q[`answer_${l}_dir`] ?? '').toLowerCase();
    if (isDirection(dir)) o[dir] = String(q[`answer_${l}`] ?? '');
  }
  return {
    t: 'q',
    bands: bandNumbers.map(String),
    cd: countdown,
    to: timeoutSec,
    s: question.statement,
    o,
  };
}

/** Map a gesture direction to the answer letter A/B/C/D via answer_x_dir. */
export function directionToLetter(
  question: Question | undefined,
  dir: string | undefined,
): string | null {
  if (!question || !dir) return null;
  const q = question as unknown as Record<string, unknown>;
  const d = String(dir).toLowerCase();
  for (const l of ['a', 'b', 'c', 'd'] as const) {
    if (String(q[`answer_${l}_dir`] ?? '').toLowerCase() === d) return l.toUpperCase();
  }
  return null;
}
