import CrudPage from '@/components/crud/CrudPage';
import type { CrudField } from '@/components/crud/types';
import type { Band } from '@/types';

const fields: CrudField[] = [
  { name: 'number', label: 'Número da pulseira', required: true },
  { name: 'device_code', label: 'Código único do dispositivo' },
  { name: 'mac_address', label: 'MAC Address (BLE ou serial)', ble: true },
  { name: 'firmware', label: 'Firmware ESP32' },
  {
    name: 'status',
    label: 'Situação',
    type: 'select',
    options: ['Ativa', 'Inativa', 'Manutenção'].map((s) => ({ value: s, label: s })),
  },
  { name: 'notes', label: 'Observações', type: 'textarea' },
];

export default function Bands() {
  return (
    <CrudPage<Band>
      entity="Band"
      title="Pulseiras"
      subtitle="Dispositivos ESP32 — capture o MAC via Bluetooth ou copie do monitor serial"
      fields={fields}
      primary={(b) => `Pulseira ${b.number}`}
      secondary={(b) => [b.mac_address, b.firmware, b.status].filter(Boolean).join(' · ')}
    />
  );
}
