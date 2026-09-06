# INVOKE Console

Minimal Web Bluetooth console for the INVOKE Band proxy node — connect to a
node, send a question dispatch, watch gestures come back. No backend, no auth.
It's a bench tool and a testbed for the BLE transport; the full teacher app is
a separate, later effort.

Protocol: [`../../docs/INVOKE_BLE_ESPECIFICACAO.md`](../../docs/INVOKE_BLE_ESPECIFICACAO.md).
Firmware: [`../../src/Mems/main/invoke_ble.c`](../../src/Mems/main/invoke_ble.c).
UUIDs and message shapes live in [`src/protocol.ts`](src/protocol.ts) — keep them
in sync with the firmware.

## Run locally (desktop Chrome/Edge)

```bash
npm install
npm run dev
```

Open the printed `https://localhost:5173` and accept the self-signed cert once.
Web Bluetooth needs Chromium + a secure context (`https://` or `localhost`).

## Run from an Android phone

The phone needs `https://` to a reachable host.

```bash
npm run dev            # or: docker compose up --build
```

On the phone (same Wi-Fi), open `https://<dev-machine-LAN-IP>:5173`, accept the
cert warning, then **Conectar nó**. If the LAN route is awkward, tunnel it:

```bash
cloudflared tunnel --url https://localhost:5173
```

## Docker

```bash
docker compose up --build
```

Serves the same Vite dev server on `:5173` with live reload.

## Simulator

Toggle **Simulador** (on by default when Web Bluetooth is unavailable) to drive
the whole UI with fake gestures — no hardware needed.

## What the BLE transport does differently

`src/ble.ts` retries service discovery with a hard `gatt.disconnect()` between
tries and, on failure, dumps whatever services the device *does* expose, so a
bad connection is visible in the log instead of a silent empty list. It does
**not** work around a wrong firmware UUID or a poisoned Android GATT cache —
those are fixed on the firmware / in phone Bluetooth settings.
