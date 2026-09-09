import { useState } from 'react';
import { Bluetooth, Check, Loader2 } from 'lucide-react';
import { api } from '@/api/client';
import { Button } from '@/components/ui/button';
import { bleSupported, scanBand } from '@/lib/ble';
import type { Band } from '@/types';

export default function DeviceScanner({ onPaired }: { onPaired?: (band: Band) => void }) {
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState('');
  const [found, setFound] = useState<Band[]>([]);

  const scan = async () => {
    setMsg('');
    setBusy(true);
    try {
      const d = await scanBand();
      const existing = await api.entities.Band.filter({ mac_address: d.mac_address });
      let band = existing[0];
      if (!band) {
        const all = await api.entities.Band.list();
        band = await api.entities.Band.create({
          number: String(all.length + 1),
          device_code: d.name,
          mac_address: d.mac_address,
          status: 'Ativa',
        });
      }
      setFound((p) => (p.some((b) => b.id === band.id) ? p : [...p, band]));
      onPaired?.(band);
    } catch (e) {
      setMsg(e instanceof Error ? e.message : 'Falha ao conectar.');
    }
    setBusy(false);
  };

  return (
    <div className="rounded-2xl border border-border bg-card p-5">
      <div className="flex items-center justify-between gap-3">
        <div>
          <p className="text-sm font-medium">Dispositivos BLE</p>
          <p className="text-xs text-muted-foreground mt-0.5">
            {bleSupported()
              ? 'Procure e adicione as pulseiras da sala.'
              : 'Bluetooth BLE indisponível neste navegador.'}
          </p>
        </div>
        <Button
          onClick={scan}
          disabled={busy}
          variant="outline"
          className="border-amber-400/40 text-amber-300 hover:bg-amber-400/10 shrink-0"
        >
          {busy ? <Loader2 className="w-4 h-4 animate-spin" /> : <Bluetooth className="w-4 h-4 mr-1" />}
          {busy ? '' : 'Procurar'}
        </Button>
      </div>
      {msg && <p className="text-xs text-red-400 mt-3">{msg}</p>}
      {found.length > 0 && (
        <ul className="mt-4 space-y-2">
          {found.map((b) => (
            <li key={b.id} className="flex items-center gap-2 text-xs text-foreground">
              <Check className="w-3.5 h-3.5 text-emerald-400" />
              Pulseira {b.number} · {b.mac_address}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
