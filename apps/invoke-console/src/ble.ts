// Web Bluetooth client for the INVOKE proxy node.
//
// The failure this is built to survive: on Android, Chrome caches the GATT
// attribute table per device and, after a connect where discovery came up
// empty (e.g. the link dropped mid-handshake), keeps handing back that empty
// cache on later connects — nRF Connect sees the full table, Chrome sees
// nothing. A plain disconnect+reconnect does not flush it. What helps:
//   - a full gatt.disconnect() and a real pause before retrying, and
//   - `navigator.bluetooth`'s per-origin permission reset (site settings) or a
//     Bluetooth adapter toggle when even that fails.
// So connect() retries discovery a few times with a hard disconnect between
// tries, logs every step, and on total failure dumps whatever services the
// device *does* expose so the cause is visible instead of guessed.

import {
  NUS_RX,
  NUS_SERVICE,
  NUS_TX,
  parseAnswer,
  type QuestionDispatch,
} from './protocol';
import type { Transport, TransportEvents } from './transport';

const DISCOVERY_ATTEMPTS = 3;
const RETRY_PAUSE_MS = 1200;

export function bleSupported(): boolean {
  return typeof navigator !== 'undefined' && !!navigator.bluetooth;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export class BleTransport implements Transport {
  readonly kind = 'ble' as const;

  private device: BluetoothDevice | null = null;
  private rx: BluetoothRemoteGATTCharacteristic | null = null;
  private tx: BluetoothRemoteGATTCharacteristic | null = null;
  private onDisconnected = () => this.handleDrop();

  constructor(private readonly ev: TransportEvents) {}

  async connect(): Promise<void> {
    if (!bleSupported()) {
      throw new Error(
        'Web Bluetooth indisponível. Use Chrome/Edge no Android ou desktop, em https:// ou localhost.',
      );
    }
    this.ev.onStateChange('connecting');

    // The Android device picker often ignores the service filter, so filter by
    // name and validate the NUS service after connecting.
    this.log('info', 'Abrindo seletor de dispositivos…');
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: 'INVOKE' }],
      optionalServices: [NUS_SERVICE],
    });
    this.log('ok', `Selecionado: ${this.device.name ?? '(sem nome)'} [${this.device.id}]`);
    this.device.addEventListener('gattserverdisconnected', this.onDisconnected);

    const service = await this.discoverNus(this.device);

    this.rx = await service.getCharacteristic(NUS_RX);
    this.tx = await service.getCharacteristic(NUS_TX);
    this.log('ok', 'Características RX/TX resolvidas.');

    this.tx.addEventListener('characteristicvaluechanged', this.onTxValue);
    await this.tx.startNotifications();
    this.log('ok', 'Notificações TX ativas. Pronto.');
    this.ev.onStateChange('connected');
  }

  async disconnect(): Promise<void> {
    const d = this.device;
    this.teardown();
    try {
      if (d?.gatt?.connected) d.gatt.disconnect();
    } catch {
      /* ignore */
    }
    this.log('info', 'Desconectado.');
    this.ev.onStateChange('idle');
  }

  async sendQuestion(dispatch: QuestionDispatch): Promise<void> {
    if (!this.rx) throw new Error('Não conectado.');
    const json = JSON.stringify(dispatch);
    await this.rx.writeValue(new TextEncoder().encode(json));
    this.log('tx', json);
  }

  // --- internals -------------------------------------------------------------

  private async discoverNus(device: BluetoothDevice): Promise<BluetoothRemoteGATTService> {
    let lastErr: unknown;

    for (let attempt = 1; attempt <= DISCOVERY_ATTEMPTS; attempt++) {
      try {
        this.log('info', `GATT connect (tentativa ${attempt}/${DISCOVERY_ATTEMPTS})…`);
        const server = await device.gatt!.connect();
        const service = await server.getPrimaryService(NUS_SERVICE);
        this.log('ok', 'Serviço NUS encontrado.');
        return service;
      } catch (e) {
        lastErr = e;
        this.log('warn', `Falha na descoberta: ${errText(e)}`);
        try {
          device.gatt?.disconnect();
        } catch {
          /* ignore */
        }
        if (attempt < DISCOVERY_ATTEMPTS) {
          this.log('info', `Aguardando ${RETRY_PAUSE_MS}ms antes de reconectar…`);
          await sleep(RETRY_PAUSE_MS);
        }
      }
    }

    await this.dumpServices(device);
    this.ev.onStateChange('error');
    throw new Error(
      `Conectou mas o serviço NUS (${NUS_SERVICE}) não apareceu após ${DISCOVERY_ATTEMPTS} tentativas. ` +
        'Se o nRF Connect vê o serviço e aqui não, é cache do Chrome: reset a permissão Bluetooth do site ' +
        '(config do site → Bluetooth) ou desligue/religue o Bluetooth do aparelho. Detalhe: ' +
        errText(lastErr),
    );
  }

  private async dumpServices(device: BluetoothDevice): Promise<void> {
    try {
      const server = device.gatt!.connected ? device.gatt! : await device.gatt!.connect();
      const services = await server.getPrimaryServices();
      if (!services.length) {
        this.log('error', 'getPrimaryServices() → [] (nenhum serviço — cache vazio do Chrome).');
      } else {
        this.log('error', `Serviços expostos: ${services.map((s) => s.uuid).join(', ')}`);
      }
    } catch (e) {
      this.log('error', `Não consegui listar serviços: ${errText(e)}`);
    }
  }

  private onTxValue = (evt: Event) => {
    const value = (evt.target as BluetoothRemoteGATTCharacteristic).value;
    if (!value) return;
    const raw = new TextDecoder().decode(value);
    this.log('rx', raw.trim());
    const a = parseAnswer(raw);
    if (a) this.ev.onAnswer(a, raw.trim());
    else this.log('warn', `Notificação TX ignorada (não é uma resposta): ${raw.trim()}`);
  };

  private handleDrop() {
    this.log('warn', 'Dispositivo desconectou.');
    this.teardown();
    this.ev.onStateChange('idle');
  }

  private teardown() {
    try {
      this.tx?.removeEventListener('characteristicvaluechanged', this.onTxValue);
    } catch {
      /* ignore */
    }
    this.device?.removeEventListener('gattserverdisconnected', this.onDisconnected);
    this.rx = null;
    this.tx = null;
    this.device = null;
  }

  private log(level: Parameters<TransportEvents['onLog']>[0], msg: string) {
    this.ev.onLog(level, msg);
  }
}

function errText(e: unknown): string {
  if (e instanceof Error) return `${e.name}: ${e.message}`;
  return String(e);
}
