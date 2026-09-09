// Entity shapes — ported from base44/entities/*.jsonc. Every record also
// carries the API bookkeeping fields.

export interface BaseRecord {
  id: string;
  created_date: string;
  updated_date: string;
}

export type Direction = 'up' | 'down' | 'left' | 'right';
export type AnswerLetter = 'A' | 'B' | 'C' | 'D';
export type Period = 'Manhã' | 'Tarde' | 'Noite' | 'Integral';

export interface School extends BaseRecord {
  name: string;
  cnpj?: string;
  director?: string;
  phone?: string;
  email?: string;
  city?: string;
  state?: string;
  address?: string;
}

export interface SchoolClass extends BaseRecord {
  name: string;
  number?: string;
  period?: Period;
  grade?: string;
  year?: number;
  school_id?: string;
}

export interface Teacher extends BaseRecord {
  name: string;
  cpf?: string;
  subject?: string;
  email?: string;
  phone?: string;
  school_id?: string;
  class_id?: string;
}

export interface Student extends BaseRecord {
  name: string;
  registration?: string;
  birth_date?: string;
  guardian?: string;
  phone?: string;
  class_id?: string;
  band_number?: string;
}

export interface Question extends BaseRecord {
  subject?: string;
  period?: Period;
  difficulty?: 'Fácil' | 'Média' | 'Difícil';
  statement: string;
  answer_a?: string;
  answer_a_dir?: Direction;
  answer_b?: string;
  answer_b_dir?: Direction;
  answer_c?: string;
  answer_c_dir?: Direction;
  answer_d?: string;
  answer_d_dir?: Direction;
  correct?: AnswerLetter;
}

export interface Band extends BaseRecord {
  number: string;
  device_code?: string;
  mac_address?: string;
  firmware?: string;
  status?: 'Ativa' | 'Inativa' | 'Manutenção';
  notes?: string;
}

export interface QuizResult {
  student_id: string;
  student_name: string;
  band_number?: string;
  answer: string | null;
  direction?: string;
  is_correct: boolean;
}

export interface QuizSession extends BaseRecord {
  class_id?: string;
  question_id?: string;
  statement?: string;
  correct?: string;
  results?: QuizResult[];
}

export interface EntityMap {
  School: School;
  SchoolClass: SchoolClass;
  Teacher: Teacher;
  Student: Student;
  Question: Question;
  Band: Band;
  QuizSession: QuizSession;
}

export type EntityName = keyof EntityMap;
