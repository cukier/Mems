# INVOKE Web — teacher app

Focused classroom-quiz tool. Two containers:

| Service | Stack | Port | Role |
|---|---|---|---|
| `web` | Vite + React + TS + Tailwind | `5173` (https) | the app |
| `api` | Fastify + better-sqlite3 | `8787` | small JSON-blob REST store |

Three pages: **Rodada ao vivo** (compose a question, connect a band, send,
watch the live answer board), **Banco de perguntas** (reusable question bank),
**Histórico** (past rounds + results).

## Run

```bash
cd apps/invoke-web
docker compose up --build
```

- App: `https://localhost:5173` — accept the self-signed cert once (Web
  Bluetooth needs a secure context). From an Android phone on the same Wi-Fi:
  `https://<dev-machine-LAN-IP>:5173` → **Conectar nó**.
- API health: `http://localhost:8787/api/health`.

The database is a file at `server/data/invoke.db` (bind-mounted, gitignored).
An empty DB is seeded with 2 demo questions; set `SEED=0` in
`docker-compose.yml` to skip, or `docker compose run --rm api npm run seed`.

## Run without Docker

```bash
cd server && npm install && npm run dev   # :8787
cd web && npm install && npm run dev      # :5173, proxies /api to $VITE_API_PROXY
```

## Layout

```
web/src/
  api/client.ts        entities client: list/filter/get/create/update/delete (+ poll subscribe)
  lib/ble.ts           Web Bluetooth NUS client — buildDispatch(), connectMeshNode()
  components/           Layout, ThemeToggle, ui/*, crud/*, live/{WatchPreview,CrossArrows,ResultsPanel}
  pages/               Live, Questions, History
  types.ts             Question, Round, RoundResult
server/src/
  index.ts             Fastify bootstrap
  db.ts                SQLite: one JSON-blob table for every record
  entities.ts          REST: GET/POST /api/entities/:entity[/:id], POST .../query
  seed.ts              2 demo questions (empty DB only)
```

## Entities API

| Method | Path | client method |
|---|---|---|
| `GET` | `/api/entities/:entity?sort=-created_date&limit=200` | `.list(sort, limit)` |
| `POST` | `/api/entities/:entity/query` `{where,sort,limit}` | `.filter(where, sort, limit)` |
| `GET` | `/api/entities/:entity/:id` | `.get(id)` |
| `POST` | `/api/entities/:entity` | `.create(data)` |
| `PUT` | `/api/entities/:entity/:id` | `.update(id, data)` (merge) |
| `DELETE` | `/api/entities/:entity/:id` | `.delete(id)` |

Collections: `Question`, `Round`.

## Protocol / firmware

BLE message shapes and UUIDs: [`../../docs/INVOKE_BLE_ESPECIFICACAO.md`](../../docs/INVOKE_BLE_ESPECIFICACAO.md).
Keep `web/src/lib/ble.ts` UUIDs and JSON shapes in sync with `firmware/`.
