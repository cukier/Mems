import { useEffect, useRef, useState } from 'react';
import { toByteArray } from 'base64-js';
import { BleManager, ScanMode, type Device, type BleError } from 'react-native-ble-plx';

import { decodeMeshPacket, type MeshPacket } from '../ble/meshPacket';
import { requestBlePermissions } from '../ble/permissions';

export interface Sample {
  timestamp: number;
  seq: number;
  ttl: number;
  accelMg: [number, number, number];
  gyroDps10: [number, number, number];
  rpyDd: [number, number, number];
}

export interface NodeState {
  nodeId: number;
  lastSeen: number;
  lastSeq: number;
  lastTtl: number;
  samples: Sample[];
}

/** Ring-buffer cap of samples kept per node. */
const SAMPLES_PER_NODE = 200;
/** How many recent (per-node) sequence numbers we remember for dedup. Small
 * on purpose: seq wraps at 256 and duplicate relays of the same reading
 * arrive within a few broadcast cycles of each other, so we don't need to
 * remember seqs for long — this just needs to outlast one flood-mesh cycle
 * (TTL up to 3 hops) worth of relays. */
const SEEN_SEQS_PER_NODE = 32;

/** Which of the two manufacturer-data shapes described in meshPacket.ts we
 * have actually observed in this scan session — surfaced so it can be shown
 * in the UI / logged, since it was something to verify rather than assume. */
export type ManufacturerDataFormat =
  | 'company-id-included (24 bytes)'
  | 'company-id-stripped (22 bytes + key)'
  | null;

interface SeenEntry {
  set: Set<number>;
  order: number[];
}

export function useMeshScanner() {
  const [scanning, setScanning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [nodes, setNodes] = useState<Record<number, NodeState>>({});
  const [manufacturerDataFormat, setManufacturerDataFormat] =
    useState<ManufacturerDataFormat>(null);

  const managerRef = useRef<BleManager | null>(null);
  const seenRef = useRef<Map<number, SeenEntry>>(new Map());
  const loggedFormatRef = useRef(false);

  useEffect(() => {
    let cancelled = false;
    const manager = new BleManager();
    managerRef.current = manager;

    const onPacket = (pkt: MeshPacket) => {
      const seen = seenRef.current;
      let entry = seen.get(pkt.nodeId);
      if (!entry) {
        entry = { set: new Set(), order: [] };
        seen.set(pkt.nodeId, entry);
      }
      if (entry.set.has(pkt.seq)) {
        return; // duplicate relay of a reading we've already graphed
      }
      entry.set.add(pkt.seq);
      entry.order.push(pkt.seq);
      if (entry.order.length > SEEN_SEQS_PER_NODE) {
        const evicted = entry.order.shift()!;
        entry.set.delete(evicted);
      }

      const sample: Sample = {
        timestamp: Date.now(),
        seq: pkt.seq,
        ttl: pkt.ttl,
        accelMg: pkt.accelMg,
        gyroDps10: pkt.gyroDps10,
        rpyDd: pkt.rpyDd,
      };

      setNodes(prev => {
        const existing = prev[pkt.nodeId];
        const samples = existing ? existing.samples.slice() : [];
        samples.push(sample);
        if (samples.length > SAMPLES_PER_NODE) {
          samples.splice(0, samples.length - SAMPLES_PER_NODE);
        }
        const next: NodeState = {
          nodeId: pkt.nodeId,
          lastSeen: sample.timestamp,
          lastSeq: pkt.seq,
          lastTtl: pkt.ttl,
          samples,
        };
        return { ...prev, [pkt.nodeId]: next };
      });
    };

    const handleDevice = (device: Device) => {
      if (!device.manufacturerData) {
        return;
      }
      let bytes: Uint8Array;
      try {
        bytes = toByteArray(device.manufacturerData);
      } catch {
        return;
      }

      const pkt = decodeMeshPacket(bytes);
      if (!pkt) {
        return;
      }

      if (!loggedFormatRef.current) {
        loggedFormatRef.current = true;
        const format: ManufacturerDataFormat =
          bytes.length === 24
            ? 'company-id-included (24 bytes)'
            : 'company-id-stripped (22 bytes + key)';
        console.log(
          `[useMeshScanner] manufacturerData format observed: ${format} ` +
            `(${bytes.length} raw bytes)`,
        );
        setManufacturerDataFormat(format);
      }

      onPacket(pkt);
    };

    const startScanning = () => {
      manager.startDeviceScan(
        null,
        { scanMode: ScanMode.LowLatency },
        (scanError: BleError | null, device: Device | null) => {
          if (cancelled) {
            return;
          }
          if (scanError) {
            console.warn('[useMeshScanner] scan error', scanError);
            setError(scanError.message ?? 'BLE scan error');
            setScanning(false);
            return;
          }
          if (device) {
            handleDevice(device);
          }
        },
      );
      setScanning(true);
      setError(null);
    };

    (async () => {
      const granted = await requestBlePermissions();
      if (cancelled) {
        return;
      }
      if (!granted) {
        setError('Bluetooth/location permissions were not granted');
        return;
      }

      const subscription = manager.onStateChange(state => {
        if (state === 'PoweredOn') {
          subscription.remove();
          if (!cancelled) {
            startScanning();
          }
        }
      }, true);
    })();

    return () => {
      cancelled = true;
      manager.stopDeviceScan();
      manager.destroy();
    };
  }, []);

  return { scanning, error, nodes, manufacturerDataFormat };
}
