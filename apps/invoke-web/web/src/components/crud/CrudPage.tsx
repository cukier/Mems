import { useEffect, useState } from 'react';
import { Loader2, Pencil, Plus, Trash2 } from 'lucide-react';
import { api } from '@/api/client';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import type { BaseRecord, EntityName } from '@/types';
import RecordForm from './RecordForm';
import type { CrudField } from './types';

interface CrudPageProps<T extends BaseRecord> {
  entity: EntityName;
  title: string;
  subtitle: string;
  fields: CrudField[];
  primary: (it: T) => React.ReactNode;
  secondary?: (it: T) => React.ReactNode;
}

export default function CrudPage<T extends BaseRecord>({
  entity,
  title,
  subtitle,
  fields,
  primary,
  secondary,
}: CrudPageProps<T>) {
  const [items, setItems] = useState<T[] | null>(null);
  const [open, setOpen] = useState(false);
  const [editing, setEditing] = useState<T | null>(null);

  const load = async () => {
    setItems((await api.entities[entity].list('-created_date')) as unknown as T[]);
  };
  useEffect(() => {
    load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [entity]);

  const save = async (values: Record<string, unknown>) => {
    if (editing) await api.entities[entity].update(editing.id, values as never);
    else await api.entities[entity].create(values as never);
    setOpen(false);
    setEditing(null);
    load();
  };

  const remove = async (id: string) => {
    await api.entities[entity].delete(id);
    load();
  };

  return (
    <div>
      <div className="flex items-end justify-between gap-4 mb-8">
        <div>
          <h1 className="text-3xl font-light tracking-tight">{title}</h1>
          <p className="text-sm text-muted-foreground mt-1">{subtitle}</p>
        </div>
        <Button
          onClick={() => {
            setEditing(null);
            setOpen(true);
          }}
          className="bg-amber-400 text-black hover:bg-amber-300 rounded-full"
        >
          <Plus className="w-4 h-4 mr-1" /> Novo
        </Button>
      </div>

      {items === null ? (
        <div className="flex justify-center py-20">
          <Loader2 className="w-5 h-5 animate-spin text-muted-foreground" />
        </div>
      ) : items.length === 0 ? (
        <div className="text-center py-20 border border-dashed border-border rounded-2xl">
          <p className="text-muted-foreground text-sm">Nenhum registro ainda.</p>
        </div>
      ) : (
        <div className="grid gap-3 sm:grid-cols-2">
          {items.map((it) => (
            <div
              key={it.id}
              className="group rounded-2xl border border-border bg-card p-5 hover:border-amber-400/30 transition-all duration-300"
            >
              <div className="flex items-start justify-between gap-3">
                <div className="min-w-0">
                  <p className="font-medium truncate">{primary(it)}</p>
                  <p className="text-xs text-muted-foreground mt-1 truncate">
                    {secondary ? secondary(it) : ''}
                  </p>
                </div>
                <div className="flex gap-1 opacity-60 group-hover:opacity-100 transition-opacity">
                  <button
                    onClick={() => {
                      setEditing(it);
                      setOpen(true);
                    }}
                    className="p-2 rounded-lg hover:bg-muted"
                  >
                    <Pencil className="w-3.5 h-3.5" />
                  </button>
                  <button
                    onClick={() => remove(it.id)}
                    className="p-2 rounded-lg hover:bg-red-500/15 text-red-400"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>
            </div>
          ))}
        </div>
      )}

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent className="max-h-[90vh] overflow-y-auto sm:max-w-2xl">
          <DialogHeader>
            <DialogTitle className="font-light text-xl">
              {editing ? 'Editar' : 'Novo'} — {title}
            </DialogTitle>
          </DialogHeader>
          <RecordForm
            fields={fields}
            initial={editing as Record<string, unknown> | null}
            onSubmit={save}
            onCancel={() => setOpen(false)}
          />
        </DialogContent>
      </Dialog>
    </div>
  );
}
