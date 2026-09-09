import Database from 'better-sqlite3';
import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

// One table for every entity: base44 entities are effectively schemaless
// documents, so we keep the record body as a JSON blob and only promote the
// bookkeeping columns the app sorts/filters on frequently.
const file = process.env.DB_PATH ?? './data/invoke.db';
mkdirSync(dirname(file), { recursive: true });

export const db = new Database(file);
db.pragma('journal_mode = WAL');
db.exec(`
  CREATE TABLE IF NOT EXISTS records (
    entity       TEXT NOT NULL,
    id           TEXT NOT NULL,
    data         TEXT NOT NULL,
    created_date TEXT NOT NULL,
    updated_date TEXT NOT NULL,
    PRIMARY KEY (entity, id)
  );
  CREATE INDEX IF NOT EXISTS idx_records_entity_created
    ON records (entity, created_date);
`);

export interface Row {
  entity: string;
  id: string;
  data: string;
  created_date: string;
  updated_date: string;
}

export type Record_ = globalThis.Record<string, unknown> & {
  id: string;
  created_date: string;
  updated_date: string;
};

export function hydrate(row: Row): Record_ {
  return {
    ...(JSON.parse(row.data) as globalThis.Record<string, unknown>),
    id: row.id,
    created_date: row.created_date,
    updated_date: row.updated_date,
  };
}
