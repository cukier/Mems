// Entity shapes for the focused INVOKE Band tool. Every record also carries
// the API bookkeeping fields.

export interface BaseRecord {
  id: string;
  created_date: string;
  updated_date: string;
}

export type AnswerLetter = 'A' | 'B' | 'C' | 'D';

// A reusable question in the bank.
export interface Question extends BaseRecord {
  statement: string;
  a: string;
  b: string;
  c: string;
  d: string;
  correct?: AnswerLetter;
  subject?: string;
}

// One answer reported by one band during a round.
export interface RoundResult {
  band: number;
  answer: AnswerLetter | ''; // '' = no answer
  is_correct: boolean;
}

// A question that was actually sent, plus what came back.
export interface Round extends BaseRecord {
  rid: number;
  question_id?: string;
  statement: string;
  a: string;
  b: string;
  c: string;
  d: string;
  correct?: AnswerLetter;
  cd: number; // countdown seconds
  to: number; // answer window seconds
  results: RoundResult[];
}

export interface EntityMap {
  Question: Question;
  Round: Round;
}

export type EntityName = keyof EntityMap;
