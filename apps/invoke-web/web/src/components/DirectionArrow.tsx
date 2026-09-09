import { ArrowDown, ArrowLeft, ArrowRight, ArrowUp } from 'lucide-react';
import type { Direction } from '@/types';

export const DIRECTIONS: { value: Direction; label: string }[] = [
  { value: 'up', label: 'Cima ↑' },
  { value: 'down', label: 'Baixo ↓' },
  { value: 'left', label: 'Esquerda ←' },
  { value: 'right', label: 'Direita →' },
];

const ICONS = { up: ArrowUp, down: ArrowDown, left: ArrowLeft, right: ArrowRight };

export default function DirectionArrow({
  dir,
  className = 'w-4 h-4',
}: {
  dir?: string;
  className?: string;
}) {
  const Icon = ICONS[dir as Direction] ?? ArrowUp;
  return <Icon className={className} />;
}
