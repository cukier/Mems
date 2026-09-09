# Mems / INVOKE Band — project soul

Living doc: what this is, what's decided, what's next. Update as it evolves.

## What this is

A classroom quiz system:

- **Pulseira** (wearable) — ESP32-C3 "Super Mini" with an integrated 1.44"
  ST7735 TFT and an LSM6DS3 IMU. Firmware in `firmware/` (ESP-IDF, target
  `esp32c3`, built against ESP-IDF `master`). Three screens:
  1. **WAIT** — the band's `INVOKE-xx` name + the NUS service UUID.
  2. **QUESTION / ANSWER** — the statement + 4 options; the option the wrist is
     tilted toward is highlighted live (up=A, down=B, left=C, right=D), with a
     countdown. The option held when the timer expires is the answer.
  3. **RESULT** — the answer, then it broadcasts it back.
- **App do professor** — `apps/invoke-web/` (Vite + React + TS + a small
  Fastify/SQLite backend, docker-compose). Compose a question (statement + 4
  answers + times), connect one band over Web Bluetooth, send. A live board
  shows every band and its answer; rounds land in a history.
- **`apps/invoke-console/`** — minimal Web Bluetooth bench tool for the BLE
  transport (with a simulator).

## Architecture decisions

### BLE transport: proxy + BLE 5 extended advertising (2026-09-09)

**Decision:** the teacher's browser holds **one** GATT connection to a single
band (its "proxy"). The proxy re-broadcasts the question over **extended
advertising**, chunked; the other bands scan for it, run the round locally, and
broadcast their answer the same way; the proxy reassembles answers and forwards
them to the app over GATT notify. Star topology, one room, no multi-hop relay.

**Why:** the question now carries full text (statement + 4 answer strings +
times) — it does not fit the 31-byte legacy advertising the old v1 flood-mesh
used. Extended advertising (BLE 5, supported on the C3) carries it; keeping the
single Web Bluetooth connection means the picker/connect path that was finally
made reliable (see the `0x3e` fixes below) is unchanged.

**Protocol:** `docs/INVOKE_BLE_ESPECIFICACAO.md` (v2). NUS UUIDs unchanged.

### Connection reliability (carried over from v1)

The Android `0x3e` (BLE_ERR_CONN_ESTABLISHMENT) churn was fixed by: correct
128-bit UUID byte order, a fresh random address per boot, no half-configured
NimBLE privacy, and not running a heavy continuous scan on an unconnected
connectable band. v2 keeps all of that; the one concession is a **light ~10%
duty-cycled idle scan** so a band can hear a round start without a GATT
connection — tune on hardware. `docs/FIRMWARE_BLE_STATUS.md` is the historical
record.

### Answer selection: fixed tilt cross (2026-09-09)

Continuous orientation, not a flick gesture. Fixed map A=up / B=down / C=left /
D=right, centre deadzone = no answer, latched at timeout. Thresholds are
`#define`s in `firmware/main/invoke_round.c` — untuned against a real wrist.

### Web app scope (2026-09-09)

Stripped from the earlier base44-style CRUD SaaS to a focused tool:
**Rodada ao vivo · Banco de perguntas · Histórico**. Backend collections:
`Question` (bank) + `Round` (a sent question + its results).

## Open items / next steps

- **Firmware not yet built against ESP-IDF in this pass** — the extended-adv /
  reassembly code in `firmware/main/invoke_ble.c` needs a compile pass (CI runs
  it on PR) and hardware testing.
- Tilt thresholds, the idle-scan duty cycle, and the per-band timeline skew all
  need tuning on real bands.
- ST7735 font is full printable ASCII; Portuguese accents are transliterated
  (á→a). Add accented glyphs if the panel text needs them.
- Multi-hop mesh (bigger than one room) is deliberately out of scope.
