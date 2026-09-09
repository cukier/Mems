import { useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Textarea } from '@/components/ui/textarea';
import type { CrudField } from './types';

type Values = Record<string, unknown>;

interface RecordFormProps {
  fields: CrudField[];
  initial?: Values | null;
  onSubmit: (values: Values) => Promise<void> | void;
  onCancel: () => void;
}

export default function RecordForm({ fields, initial, onSubmit, onCancel }: RecordFormProps) {
  const [values, setValues] = useState<Values>(initial ?? {});
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  const set = (k: string, v: unknown) => setValues((p) => ({ ...p, [k]: v }));

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    setSaving(true);
    try {
      await onSubmit(values);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Falha ao salvar.');
    }
    setSaving(false);
  };

  return (
    <form onSubmit={submit} className="space-y-4">
      <div className="grid sm:grid-cols-2 gap-4">
        {fields.map((f) => (
          <div key={f.name} className={f.type === 'textarea' ? 'sm:col-span-2' : ''}>
            <Label className="text-[11px] uppercase tracking-wider text-muted-foreground">
              {f.label}
            </Label>
            {f.type === 'select' ? (
              <select
                value={String(values[f.name] ?? '')}
                onChange={(e) => set(f.name, e.target.value)}
                required={f.required}
                className="mt-1.5 w-full h-10 rounded-md bg-background border border-border px-3 text-sm text-foreground focus:outline-none focus:border-amber-400/60"
              >
                <option value="">—</option>
                {(f.options ?? []).map((o) => (
                  <option key={o.value} value={o.value}>
                    {o.label}
                  </option>
                ))}
              </select>
            ) : f.type === 'textarea' ? (
              <Textarea
                value={String(values[f.name] ?? '')}
                onChange={(e) => set(f.name, e.target.value)}
                required={f.required}
                rows={3}
                className="mt-1.5"
              />
            ) : (
              <Input
                className="mt-1.5"
                type={f.type === 'number' ? 'number' : f.type === 'date' ? 'date' : 'text'}
                value={String(values[f.name] ?? '')}
                onChange={(e) =>
                  set(f.name, f.type === 'number' ? Number(e.target.value) : e.target.value)
                }
                required={f.required}
              />
            )}
          </div>
        ))}
      </div>
      {error && <p className="text-xs text-red-400">{error}</p>}
      <div className="flex justify-end gap-2 pt-2">
        <Button type="button" variant="ghost" onClick={onCancel} className="text-muted-foreground">
          Cancelar
        </Button>
        <Button
          type="submit"
          disabled={saving}
          className="bg-amber-400 text-black hover:bg-amber-300"
        >
          {saving ? 'Salvando...' : 'Salvar'}
        </Button>
      </div>
    </form>
  );
}
