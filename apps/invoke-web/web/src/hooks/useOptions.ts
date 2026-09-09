import { useEffect, useState } from 'react';
import { api } from '@/api/client';
import type { EntityMap, EntityName } from '@/types';

export interface Option {
  value: string;
  label: string;
}

export default function useOptions<K extends EntityName>(
  entity: K,
  labelFn: (r: EntityMap[K]) => string,
): Option[] {
  const [options, setOptions] = useState<Option[]>([]);
  useEffect(() => {
    let alive = true;
    api.entities[entity].list().then((rows) => {
      if (alive) setOptions((rows as EntityMap[K][]).map((r) => ({ value: r.id, label: labelFn(r) })));
    });
    return () => {
      alive = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [entity]);
  return options;
}
