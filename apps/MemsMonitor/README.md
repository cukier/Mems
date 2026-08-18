# MemsMonitor

A bare React Native (TypeScript, Android-only) app that scans for BLE
advertising packets from the flood-mesh IMU network built in this repo's
ESP32-C3 firmware, and plots the accelerometer data for a chosen node as a
live line chart.

## How it relates to the firmware

The firmware (`../../src/Mems/`) turns each ESP32-C3 "Super Mini" board into
a mesh node: it samples an LSM6DS3 IMU, and both broadcasts its own reading
and relays other nodes' readings as non-connectable BLE advertisements,
using company ID `0xFFFF` as a private/prototype filter (see
`../../soul.md` for why a custom flood-mesh was built instead of the
official BLE Mesh stack). This app is the "any BLE scanner" sink that
architecture decision assumed would exist — it's a live-graphing
replacement for manually scanning with nRF Connect.

The exact 24-byte wire format this app decodes is defined in
[`../../src/Mems/main/mesh_net.c`](../../src/Mems/main/mesh_net.c) (the
`mesh_pkt_t` struct) and re-documented, with byte offsets, at the top of
[`src/ble/meshPacket.ts`](src/ble/meshPacket.ts). If the firmware's struct
ever changes, that file is the one place on the app side to update.

## What the app does

- Requests the Android permissions BLE scanning needs, requesting either
  `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` (Android 12+, API 31+) or
  `ACCESS_FINE_LOCATION` (older Android), matching the running API level.
- Continuously scans for BLE advertisements, decodes any manufacturer-data
  payload matching the mesh packet format and company ID `0xFFFF`, and
  drops everything else (other BLE traffic, unrelated beacons, etc.).
- Dedupes packets by `(node_id, seq)` so the same reading relayed by
  multiple hops only produces one graph point.
- Keeps a capped ring buffer (200 samples) per discovered node of
  `{ timestamp, accelMg, gyroDps10, rpyDd, seq, ttl }`.
- Shows a node picker (one chip per discovered `node_id`, with last-seen
  time and current seq/ttl) and a live SVG line chart of the selected
  node's accelerometer X/Y/Z (converted to g) over its recent sample
  window.
- Shows a scanning status indicator, any BLE error, and a discovered-node
  count.

The chart is hand-rolled with `react-native-svg` (three colored polylines,
a legend, and axis labels) rather than a full charting library — this app
only ever needs the one graph, so a small dependency-free component was
simpler to reason about than wiring up a charting library for it.

## A wire-format ambiguity that had to be resolved by reading source, not guessing

`react-native-ble-plx`'s scanned `Device.manufacturerData` is a base64
blob whose exact shape (company-ID-prefixed or not) depends on how the
library builds it internally. We read the library's Android sources
(`android/src/main/java/com/bleplx/adapter/AdvertisementData.java` and
`RxScanResultToScanResultMapper.java`, v3.5.1) rather than assume: on
Android, this library parses the raw scan record itself and copies the
full BLE AD structure content (company ID included) into
`manufacturerData`, rather than delegating to Android's
`ScanRecord.getManufacturerSpecificData()` keyed-map API. So on Android,
with this library, `manufacturerData` is the full 24-byte payload,
company ID included — there's no separate manufacturer-id map on this
platform in this library version.

`src/ble/meshPacket.ts`'s `decodeMeshPacket()` still accepts a 22-byte,
company-ID-stripped shape (validated against a caller-supplied key) as a
defensive fallback, and `src/hooks/useMeshScanner.ts` logs which shape it
actually observed the first time it decodes a packet — in case a future
library version, or a different Android BLE stack, behaves differently.
This has not been exercised against a real device in this environment (see
Verification below); it's what source-reading predicts, not what was
observed on-air.

## Running it on a real device

You need a phone with Android 8+ (min SDK 24), a USB cable, and (ideally)
at least one flashed mesh node within BLE range.

```sh
cd apps/MemsMonitor
npm install
npx react-native run-android
```

- Enable USB debugging on the phone and accept the RSA key prompt when it
  connects.
- On first launch, grant the Bluetooth (Android 12+) or Location
  (Android < 12) permission prompt — without it, scanning silently never
  starts (the status line will show the app's own error message if the
  permission was denied rather than just dismissed).
- Bluetooth needs to be turned on. On Android < 12, system Location
  services also need to be on for BLE scan results to be delivered, even
  though this app does not use GPS/location itself — that's an OS
  restriction on pre-Android-12 BLE scanning, not something this app
  chose.
- Flash at least one node from `../../src/Mems/` and keep it within BLE
  range. Nodes should start showing up as chips within a few seconds
  (nodes broadcast roughly every 150ms per the firmware's `ADV_CYCLE_MS`).

## Verification status

- `npx tsc --noEmit` passes with no type errors.
- `cd android && ./gradlew assembleDebug` succeeds and produces a debug
  APK (see repo-level notes for the exact path/output).
- **Not tested against a real device or a real mesh node** — no Android
  device/emulator was available in the environment this was built in. The
  BLE scanning, permission flow, and the manufacturer-data shape
  assumption above are all unverified against actual hardware/radio
  behavior.
