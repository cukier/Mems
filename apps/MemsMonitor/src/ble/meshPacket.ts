/**
 * Wire format for the flood-mesh IMU packet broadcast by ESP32-C3 nodes.
 *
 * Source of truth: ../../../../src/Mems/main/mesh_net.c (`mesh_pkt_t`).
 * 24-byte, little-endian, packed struct carried verbatim as the payload of
 * a BLE "Manufacturer Specific Data" AD structure (AD type 0xFF):
 *
 *   offset  size  field
 *   0       2     company_id    (u16)  — always 0xFFFF, used as a packet filter
 *   2       2     node_id       (u16)  — originating node's mesh address
 *   4       1     seq           (u8)   — wraps at 256
 *   5       1     ttl           (u8)   — hops remaining (0-3)
 *   6       6     accel_mg[3]   (i16)  — milli-g
 *   12      6     gyro_dps10[3] (i16)  — deci-degrees/s
 *   18      6     rpy_dd[3]     (i16)  — deci-degrees (roll, pitch, yaw)
 *                                 total: 24 bytes
 */

export const MESH_NET_COMPANY_ID = 0xffff;

/** Size of the on-air struct, company ID included. */
export const MESH_PKT_LEN_WITH_COMPANY_ID = 24;
/** Size of just the payload after a 2-byte company ID has been stripped off
 * (kept for defensiveness — see decodeMeshPacket doc below). */
export const MESH_PKT_LEN_WITHOUT_COMPANY_ID = 22;

export interface MeshPacket {
  nodeId: number;
  seq: number;
  ttl: number;
  accelMg: [number, number, number];
  gyroDps10: [number, number, number];
  rpyDd: [number, number, number];
}

/**
 * Decodes a raw manufacturer-data payload into a MeshPacket.
 *
 * `react-native-ble-plx`'s scanned `Device.manufacturerData` (base64) could
 * in principle either:
 *   (a) include the 2-byte company ID at the front, as the raw BLE AD
 *       "Manufacturer Specific Data" structure content does per spec, or
 *   (b) have the company ID stripped out by the OS/library and exposed via
 *       a separate manufacturer-id key instead.
 *
 * We checked: on Android, this library (v3.5.1) builds `manufacturerData`
 * itself from the raw scan record bytes (see
 * `AdvertisementData.parseScanResponseData` /
 * `RxScanResultToScanResultMapper` in the library's Android sources) rather
 * than delegating to Android's `ScanRecord.getManufacturerSpecificData()`
 * SparseArray API — so it does NOT strip the company ID, and there is no
 * separate manufacturer-id map exposed on Android in this library version.
 * That means the 24-byte, company-id-included path (a) is what this app
 * actually receives. We still keep the 22-byte fallback below in case a
 * future library version, a different platform, or a different Android BLE
 * stack behaves differently — better to degrade gracefully than to silently
 * drop every packet if that assumption ever stops holding.
 *
 * @param bytes raw manufacturer-data payload bytes.
 * @param mapCompanyId if the payload had its company ID stripped by the
 *   platform/library and exposed as a separate map key (not the case on
 *   Android with this library today), pass that key here so the
 *   22-byte path can still validate it.
 */
export function decodeMeshPacket(
  bytes: Uint8Array,
  mapCompanyId?: number,
): MeshPacket | null {
  if (bytes.length === MESH_PKT_LEN_WITH_COMPANY_ID) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const companyId = view.getUint16(0, true);
    if (companyId !== MESH_NET_COMPANY_ID) {
      return null;
    }
    return decodeBody(view, 2);
  }

  if (bytes.length === MESH_PKT_LEN_WITHOUT_COMPANY_ID) {
    if (mapCompanyId !== MESH_NET_COMPANY_ID) {
      return null;
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    return decodeBody(view, 0);
  }

  return null;
}

function decodeBody(view: DataView, offset: number): MeshPacket {
  let o = offset;
  const nodeId = view.getUint16(o, true);
  o += 2;
  const seq = view.getUint8(o);
  o += 1;
  const ttl = view.getUint8(o);
  o += 1;
  const accelMg: [number, number, number] = [
    view.getInt16(o, true),
    view.getInt16(o + 2, true),
    view.getInt16(o + 4, true),
  ];
  o += 6;
  const gyroDps10: [number, number, number] = [
    view.getInt16(o, true),
    view.getInt16(o + 2, true),
    view.getInt16(o + 4, true),
  ];
  o += 6;
  const rpyDd: [number, number, number] = [
    view.getInt16(o, true),
    view.getInt16(o + 2, true),
    view.getInt16(o + 4, true),
  ];

  return { nodeId, seq, ttl, accelMg, gyroDps10, rpyDd };
}

/** node_id formatted the same way the firmware logs it (4 hex digits). */
export function formatNodeId(nodeId: number): string {
  return `0x${nodeId.toString(16).padStart(4, '0')}`;
}
