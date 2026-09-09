# INVOKE Web

TypeScript port of the base44 teacher app (`docs/invoke-band-class.zip`),
self-hosted — no base44 SDK, no hosted backend, no auth. Two containers:

| Service | Stack | Port | Role |
|---|---|---|---|
| `web` | Vite + React + TS + Tailwind | `5173` (https) | the app — same UI/pages as the base44 export |
| `api` | Fastify + better-sqlite3 | `8787` | generic entities REST API, replaces `base44.entities.*` |

Why this exists: the base44 platform round-trips and the recurring BLE
`0x03` / `0x3e` connect churn (see `docs/FIRMWARE_BLE_STATUS.md`) were costing
time. The BLE client here is the corrected single-`gatt.connect()` path from
`apps/invoke-console/src/ble.ts`, ported to `web/src/lib/ble.ts`.

## Run

```bash
cd apps/invoke-web
docker compose up --build
```

- App: `https://localhost:5173` (accept the self-signed cert once — Web
  Bluetooth needs a secure context).
- From an Android phone on the same Wi-Fi: `https://<dev-machine-LAN-IP>:5173`,
  accept the cert, then **Conectar nó** on the Sessão page.
- API health: `http://localhost:8787/api/health`.

The database is a file at `server/data/invoke.db` (bind-mounted, gitignored). On
first boot with an empty DB the API seeds one school / class / 4 band-linked
students / 2 questions so the Live session is testable immediately. Set
`SEED=0` in `docker-compose.yml` to skip, or `docker compose run --rm api npm run seed`
to seed manually.

## Run without Docker

```bash
# terminal 1
cd server && npm install && npm run dev
# terminal 2
cd web && npm install && npm run dev
```

`web` proxies `/api` to `http://localhost:8787` by default
(`VITE_API_PROXY` overrides the target).

## Layout

```
web/src/
  api/client.ts        drop-in for base44's entities surface (list/filter/create/update/delete/subscribe)
  lib/ble.ts           Web Bluetooth NUS client — corrected connect path
  lib/utils.ts         cn()
  components/           Layout, ThemeToggle, DirectionArrow, ui/*, crud/*, live/*
  pages/               Dashboard, Live, Game, Performance, Ranking + CRUD (Schools/Classes/Teachers/Students/Questions/Bands)
  types.ts             entity types (from base44/entities/*.jsonc)
server/src/
  index.ts             Fastify bootstrap
  db.ts                SQLite schema (one JSON-blob table for every entity)
  entities.ts          REST: GET/POST /api/entities/:entity[/:id], POST .../query
  seed.ts              demo data (empty DB only)
```

## Entities API

| Method | Path | base44 equivalent |
|---|---|---|
| `GET` | `/api/entities/:entity?sort=-created_date&limit=200` | `.list(sort, limit)` |
| `POST` | `/api/entities/:entity/query` `{where,sort,limit}` | `.filter(where, sort, limit)` |
| `GET` | `/api/entities/:entity/:id` | `.get(id)` |
| `POST` | `/api/entities/:entity` | `.create(data)` |
| `PUT` | `/api/entities/:entity/:id` | `.update(id, data)` (merge) |
| `DELETE` | `/api/entities/:entity/:id` | `.delete(id)` |

`subscribe()` is polling (3 s) — enough for the Game live scoreboard.

## Protocol / firmware

BLE message shapes and UUIDs: `docs/INVOKE_BLE_ESPECIFICACAO.md`.
Keep `web/src/lib/ble.ts` UUIDs in sync with the firmware.
