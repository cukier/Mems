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
