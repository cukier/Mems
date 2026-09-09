export interface Option {
  value: string;
  label: string;
}

export interface CrudField {
  name: string;
  label: string;
  type?: 'text' | 'number' | 'date' | 'textarea' | 'select';
  required?: boolean;
  options?: Option[];
}
