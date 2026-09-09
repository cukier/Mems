import type { FastifyInstance } from 'fastify';
import { randomUUID } from 'node:crypto';
import { db, hydrate, type Record_, type Row } from './db.js';

// The collections the INVOKE Band tool uses. Anything outside this list is
// rejected so the API can't be used as an open key/value store.
export const ENTITIES = ['Question', 'Round'] as const;

const ENTITY_SET = new Set<string>(ENTITIES);

// Fields the client must not be able to set/overwrite directly.
const RESERVED = new Set(['id', 'created_date', 'updated_date']);

function clean(input: unknown): globalThis.Record<string, unknown> {
  const out: globalThis.Record<string, unknown> = {};
  if (input && typeof input === 'object') {
    for (const [k, v] of Object.entries(input)) {
      if (!RESERVED.has(k)) out[k] = v;
    }
  }
  return out;
}

function applySort(rows: Record_[], sort?: string): Record_[] {
  if (!sort) return rows;
  const desc = sort.startsWith('-');
  const key = desc ? sort.slice(1) : sort;
  return [...rows].sort((a, b) => {
    const av = a[key] as unknown;
    const bv = b[key] as unknown;
    if (av === bv) return 0;
    if (av == null) return 1;
    if (bv == null) return -1;
    return (av < bv ? -1 : 1) * (desc ? -1 : 1);
  });
}

function matches(rec: Record_, where: globalThis.Record<string, unknown>): boolean {
  return Object.entries(where).every(([k, v]) => {
    const actual = rec[k];
    // loose match on strings/numbers, like base44's filter()
    return actual === v || String(actual ?? '') === String(v ?? '');
  });
}

const selectAll = db.prepare('SELECT * FROM records WHERE entity = ?');
const selectOne = db.prepare('SELECT * FROM records WHERE entity = ? AND id = ?');
const insertOne = db.prepare(
  'INSERT INTO records (entity, id, data, created_date, updated_date) VALUES (?, ?, ?, ?, ?)',
);
const updateOne = db.prepare(
  'UPDATE records SET data = ?, updated_date = ? WHERE entity = ? AND id = ?',
);
const deleteOne = db.prepare('DELETE FROM records WHERE entity = ? AND id = ?');

function runQuery(
  entity: string,
  where: globalThis.Record<string, unknown> | undefined,
  sort: string | undefined,
  limit: number | undefined,
): Record_[] {
  let recs = (selectAll.all(entity) as Row[]).map(hydrate);
  if (where && Object.keys(where).length) recs = recs.filter((r) => matches(r, where));
  recs = applySort(recs, sort ?? '-created_date');
  if (limit && limit > 0) recs = recs.slice(0, limit);
  return recs;
}

interface EntityParams {
  entity: string;
  id?: string;
}

export function registerEntities(app: FastifyInstance): void {
  app.addHook('preHandler', async (req, reply) => {
    const params = req.params as Partial<EntityParams>;
    if (params.entity && !ENTITY_SET.has(params.entity)) {
      reply.code(404).send({ error: `Unknown entity: ${params.entity}` });
    }
  });

  // list
  app.get<{ Params: EntityParams; Querystring: { sort?: string; limit?: string } }>(
    '/api/entities/:entity',
    async (req) => {
      const { sort, limit } = req.query;
      return runQuery(req.params.entity, undefined, sort, limit ? Number(limit) : undefined);
    },
  );

  // filter
  app.post<{
    Params: EntityParams;
    Body: { where?: globalThis.Record<string, unknown>; sort?: string; limit?: number };
  }>('/api/entities/:entity/query', async (req) => {
    const { where, sort, limit } = req.body ?? {};
    return runQuery(req.params.entity, where, sort, limit ? Number(limit) : undefined);
  });

  // get one
  app.get<{ Params: Required<EntityParams> }>(
    '/api/entities/:entity/:id',
    async (req, reply) => {
      const row = selectOne.get(req.params.entity, req.params.id) as Row | undefined;
      if (!row) return reply.code(404).send({ error: 'Not found' });
      return hydrate(row);
    },
  );

  // create
  app.post<{ Params: EntityParams }>('/api/entities/:entity', async (req, reply) => {
    const now = new Date().toISOString();
    const id = randomUUID();
    const data = clean(req.body);
    insertOne.run(req.params.entity, id, JSON.stringify(data), now, now);
    reply.code(201);
    return { ...data, id, created_date: now, updated_date: now };
  });

  // update (merge)
  app.put<{ Params: Required<EntityParams> }>(
    '/api/entities/:entity/:id',
    async (req, reply) => {
      const row = selectOne.get(req.params.entity, req.params.id) as Row | undefined;
      if (!row) return reply.code(404).send({ error: 'Not found' });
      const merged = { ...(JSON.parse(row.data) as object), ...clean(req.body) };
      const now = new Date().toISOString();
      updateOne.run(JSON.stringify(merged), now, req.params.entity, req.params.id);
      return { ...merged, id: row.id, created_date: row.created_date, updated_date: now };
    },
  );

  // delete
  app.delete<{ Params: Required<EntityParams> }>(
    '/api/entities/:entity/:id',
    async (req) => {
      deleteOne.run(req.params.entity, req.params.id);
      return { ok: true };
    },
  );
}
