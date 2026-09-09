// Drop-in replacement for the base44 SDK's `entities` surface, talking to the
// local Fastify + SQLite API (see ../../server). The method names mirror what
// the ported pages call: list(sort, limit), filter(where, sort, limit),
// get(id), create(data), update(id, data), delete(id), subscribe(cb).

import type { EntityMap, EntityName } from '@/types';

const BASE = '/api/entities';

async function http<T>(url: string, init?: RequestInit): Promise<T> {
  const res = await fetch(url, {
    ...init,
    headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) },
  });
  if (!res.ok) {
    let detail = res.statusText;
    try {
      const body = (await res.json()) as { error?: string };
      if (body.error) detail = body.error;
    } catch {
      /* keep statusText */
    }
    throw new Error(`API ${res.status}: ${detail}`);
  }
  if (res.status === 204) return undefined as T;
  return (await res.json()) as T;
}

export interface EntityClient<T> {
  list(sort?: string, limit?: number): Promise<T[]>;
  filter(where: Record<string, unknown>, sort?: string, limit?: number): Promise<T[]>;
  get(id: string): Promise<T>;
  create(data: Partial<T>): Promise<T>;
  update(id: string, data: Partial<T>): Promise<T>;
  delete(id: string): Promise<void>;
  /** Poll-based stand-in for base44's realtime subscribe(). Returns an unsubscribe fn. */
  subscribe(cb: () => void, intervalMs?: number): () => void;
}

function entityClient<T>(name: string): EntityClient<T> {
  const root = `${BASE}/${name}`;
  return {
    list(sort, limit) {
      const q = new URLSearchParams();
      if (sort) q.set('sort', sort);
      if (limit) q.set('limit', String(limit));
      const qs = q.toString();
      return http<T[]>(qs ? `${root}?${qs}` : root);
    },
    filter(where, sort, limit) {
      return http<T[]>(`${root}/query`, {
        method: 'POST',
        body: JSON.stringify({ where, sort, limit }),
      });
    },
    get(id) {
      return http<T>(`${root}/${encodeURIComponent(id)}`);
    },
    create(data) {
      return http<T>(root, { method: 'POST', body: JSON.stringify(data) });
    },
    update(id, data) {
      return http<T>(`${root}/${encodeURIComponent(id)}`, {
        method: 'PUT',
        body: JSON.stringify(data),
      });
    },
    async delete(id) {
      await http<unknown>(`${root}/${encodeURIComponent(id)}`, { method: 'DELETE' });
    },
    subscribe(cb, intervalMs = 3000) {
      const t = setInterval(cb, intervalMs);
      return () => clearInterval(t);
    },
  };
}

type Entities = { [K in EntityName]: EntityClient<EntityMap[K]> };

const entities = Object.fromEntries(
  (['Question', 'Round'] as EntityName[]).map((name) => [name, entityClient(name)]),
) as Entities;

export const api = { entities };
