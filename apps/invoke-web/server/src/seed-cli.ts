import { seedIfEmpty } from './seed.js';

const seeded = seedIfEmpty();
console.log(seeded ? 'Seeded demo data.' : 'Database not empty (or SEED=0) — nothing to do.');
