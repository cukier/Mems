import cors from '@fastify/cors';
import Fastify from 'fastify';
import { ENTITIES, registerEntities } from './entities.js';
import { seedIfEmpty } from './seed.js';

const app = Fastify({ logger: true });

async function start(): Promise<void> {
  await app.register(cors, { origin: true });

  app.get('/api/health', async () => ({ ok: true, entities: ENTITIES }));
  registerEntities(app);

  if (seedIfEmpty()) app.log.info('Seeded demo data (empty database).');

  const port = Number(process.env.PORT ?? 8787);
  const host = process.env.HOST ?? '0.0.0.0';
  await app.listen({ port, host });
}

start().catch((err) => {
  app.log.error(err);
  process.exit(1);
});
