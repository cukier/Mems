import type { Option } from '@/hooks/useOptions';

export interface CrudField {
  name: string;
  label: string;
  type?: 'text' | 'number' | 'date' | 'textarea' | 'select';
  required?: boolean;
  options?: Option[];
  /** show a "scan Bluetooth" button next to the input (Bands) */
  ble?: boolean;
}
