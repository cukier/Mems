# Mems — project soul

Living doc for what this project is, what's been decided, and what's next.
Update it as the project evolves — this is not a one-time snapshot.

## What this is

Firmware for an ESP32-C3 "Super Mini" board (integrated 1.44" ST7735 TFT)
reading an LSM6DS3 accelerometer/gyroscope over I2C. Originally a standalone
IMU dashboard (histogram + roll/pitch/yaw on the TFT); now growing into a
multi-node BLE network so several boards' IMU data can be seen together.

Firmware lives in `src/Mems/` (ESP-IDF project, target `esp32c3`, built
against ESP-IDF `master`). See `README.md` for wiring and flashing.

## Hardware in play

- Board: ESP32-C3 Super Mini, integrated 1.44" ST7735 TFT.
- IMU: LSM6DS3 breakout over I2C (GPIO20/21).
- Only breaks out GPIO0-10 + GPIO20/21 — a real constraint if more
  peripherals get added later.

## Architecture decisions

### BLE flood-mesh for IMU telemetry (2026-08-18)

**Decision:** built a lightweight custom flood-mesh over BLE advertising,
*not* the official Bluetooth SIG BLE Mesh stack (`esp_ble_mesh`).

**Why:** confirmed `esp_ble_mesh` is present and ESP32-C3-supported in the
installed ESP-IDF, but it requires a provisioning step per node (phone app
or PC provisioner) and a custom vendor model, since there's no standard IMU
sensor model. That's a lot of ceremony for a hobby sensor mesh. The
alternative — each node broadcasts its reading as BLE manufacturer-specific
data, all nodes scan and relay what they haven't seen — needs no pairing, no
provisioning, and any BLE scanner (nRF Connect, etc.) can already read it.
User picked this explicitly over the SIG stack when asked.

**Gateway/sink:** none built yet — "any BLE scanner" was picked as the sink,
so a phone with nRF Connect (or similar) filtering on company ID `0xFFFF`
is the intended way to see mesh data right now. No Wi-Fi/serial bridge node
exists. If a live combined stream (vs. manually scanning) is wanted later,
that's a follow-up, not yet designed.

**How it works** (implementation: `src/Mems/main/mesh_net.c` / `.h`):
- NimBLE (not Bluedroid) — lighter footprint, this app never connects.
- Each node samples IMU + orientation every 200ms (existing sensor loop in
  `main.c`) and packs it into a 24-byte packet: company ID (`0xFFFF`,
  Bluetooth SIG's reserved test/prototype value — fine for a private
  non-commercial mesh, must never ship in a real product), node ID (from the
  low 16 bits of the BT MAC), a wrapping sequence number, a TTL (starts at
  `MESH_NET_TTL_MAX` = 3), then accel (milli-g), gyro (deci-deg/s), and
  roll/pitch/yaw (deci-deg), all int16. Fits inside the 31-byte legacy ADV
  payload alongside the flags AD structure.
- A broadcast task round-robins every 150ms: pending relayed packets first
  (keeps the mesh moving), the node's own latest reading otherwise.
- Continuous passive scanning runs in parallel (BLE controller
  time-multiplexes scan/adv automatically — this is the normal way
  flood-mesh networks work over BLE, nothing special needed for concurrency).
- A small "seen" table `{node_id, last_seq}` per known node suppresses
  reprocessing/re-relaying — including a node's own reading echoing back
  from a neighbor, since a node marks its own packet "seen" the moment it
  generates it.
- On receipt: TTL is decremented and, if still > 0, the packet is queued for
  relay; every reception is logged via `ESP_LOGI` regardless of relay.

**Verified:** builds cleanly against ESP-IDF master for `esp32c3` (48% flash
free), no warnings from the new code. **Not yet tested on real hardware** —
only one board's firmware has been flashed/run historically; multi-node
behavior (relay, TTL decay, seen-table dedup under real RF conditions) is
unverified. Next real step is flashing 2+ boards and confirming with a BLE
scanner that both readings appear and relay works past direct radio range.

### MemsMonitor phone app as the mesh sink (2026-08-18)

**Decision:** built the mesh sink as a bare React Native + TypeScript
Android app (`apps/MemsMonitor/`) that scans for the flood-mesh BLE
advertisements directly, instead of building a firmware-side gateway node
(e.g. a Wi-Fi- or serial-bridging ESP32).

**Why:** the earlier BLE flood-mesh entry above left the gateway/sink
undecided, noting "any BLE scanner" as a stopgap. A phone app that scans,
decodes, and graphs the packets live is closer to what's actually wanted
(a usable dashboard) than either continuing to eyeball raw bytes in
nRF Connect or building and flashing a dedicated bridge node. Bare React
Native (not Expo) was used specifically because `react-native-ble-plx`
needs native linking, which is friction-free in bare RN but requires a
custom dev client under Expo.

**How it works** (implementation: `apps/MemsMonitor/`):
- `react-native-ble-plx` for scanning; `PermissionsAndroid` requests
  `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` on API 31+ or `ACCESS_FINE_LOCATION`
  below that, matching the running API level.
- `src/ble/meshPacket.ts` decodes the 24-byte `mesh_pkt_t` layout
  (little-endian, offsets matching `mesh_net.c` exactly) from the scanned
  device's `manufacturerData`, filtering on company ID `0xFFFF`.
- Resolved without a live node to test against: read
  `react-native-ble-plx`'s Android source (`AdvertisementData.java`,
  `RxScanResultToScanResultMapper.java`, v3.5.1) rather than guess. On
  Android, this library parses the raw scan record itself and does not
  strip the company ID, so `manufacturerData` is the full 24-byte payload;
  the decoder still accepts a 22-byte, company-ID-stripped shape
  defensively and logs which shape it actually sees.
- Packets are deduped per `(node_id, seq)` (small per-node seen-set,
  capped so it doesn't grow unbounded) so flood-relay duplicates don't
  produce duplicate graph points; each node keeps a capped 200-sample ring
  buffer.
- The chart is a hand-rolled `react-native-svg` line chart (three
  polylines for accel X/Y/Z in g, legend, axis labels) rather than a
  charting library dependency — the app only ever needs this one graph.

**Verified:** `npx tsc --noEmit` passes with no type errors; `cd
apps/MemsMonitor/android && ./gradlew assembleDebug` succeeds and produces
a debug APK. **Not tested on a real device or against a real mesh node** —
no Android device/emulator was available in the build environment, so the
permission flow, live scanning, and the manufacturer-data shape assumption
above are all unverified against actual hardware/radio behavior. Next real
step is running the app on a phone in range of a flashed node and
confirming packets decode and dedupe as expected.

## Open items / next steps

- Flash multiple boards, verify mesh relay works over actual distance (not
  just single-node loopback).
- No gateway/bridge exists yet — decide later if a Wi-Fi- or serial-bridging
  node is wanted instead of manual phone scanning.
- TTL=3, 150ms broadcast cycle, and 16-entry seen-table are first-guess
  constants, not tuned against real node counts/distances yet.
- If interoperability with standard BLE Mesh tooling ever becomes a
  requirement, the current flood-mesh would need to be replaced (not
  extended) with `esp_ble_mesh` + a vendor model — noted as the road not
  taken above.
