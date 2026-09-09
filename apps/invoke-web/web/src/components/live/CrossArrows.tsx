import type { Question } from '@/types';

// Block arrow (viewBox 48x48), amber outline with a neon glow.
// `active` fills the body and intensifies the glow.
const PATH_UP = 'M24 2 L44 20 L36 20 L36 46 L12 46 L12 20 Z';

const ANGLE: Record<string, number> = { up: 0, right: 90, down: 180, left: 270 };
const LETTER_POS: Record<string, { x: number; y: number }> = {
  up: { x: 24, y: 34 },
  right: { x: 15, y: 24 },
  down: { x: 24, y: 14 },
  left: { x: 34, y: 24 },
};

export function CrossArrow({
  dir,
  letter,
  active,
}: {
  dir: string;
  letter: string;
  active?: boolean;
}) {
  const angle = ANGLE[dir] ?? 0;
  const pos = LETTER_POS[dir] ?? LETTER_POS.up;
  return (
    <svg viewBox="0 0 48 48" className="w-full h-full">
      <g transform={`rotate(${angle} 24 24)`}>
        <path
          d={PATH_UP}
          fill={active ? 'rgba(255,179,71,0.28)' : 'transparent'}
          stroke="#FFB347"
          strokeWidth="2"
          strokeLinejoin="round"
          style={{
            filter: active
              ? 'drop-shadow(0 0 7px rgba(255,179,71,0.95))'
              : 'drop-shadow(0 0 3px rgba(255,179,71,0.4))',
          }}
        />
      </g>
      <text
        x={pos.x}
        y={pos.y}
        textAnchor="middle"
        dominantBaseline="central"
        fontSize="15"
        fontWeight="800"
        fill="#FFFFFF"
      >
        {letter}
      </text>
    </svg>
  );
}

// Cross layout: up on top, left/right on the sides, down at the bottom.
// Each position's letter comes from the question's direction mapping.
export default function CrossArrows({
  question,
  activeDir,
}: {
  question?: Question;
  activeDir?: string;
}) {
  const q = question as unknown as Record<string, unknown> | undefined;
  const byDir: Record<string, string> = {};
  (['A', 'B', 'C', 'D'] as const).forEach((l) => {
    const d = String(q?.[`answer_${l.toLowerCase()}_dir`] ?? '').toLowerCase();
    if (d) byDir[d] = l;
  });
  return (
    <div className="grid grid-cols-3 grid-rows-3 gap-1 w-full max-w-[180px]">
      <div className="col-start-2 row-start-1 h-14">
        <CrossArrow dir="up" letter={byDir.up || 'A'} active={activeDir === 'up'} />
      </div>
      <div className="col-start-1 row-start-2 h-14">
        <CrossArrow dir="left" letter={byDir.left || 'C'} active={activeDir === 'left'} />
      </div>
      <div className="col-start-3 row-start-2 h-14">
        <CrossArrow dir="right" letter={byDir.right || 'D'} active={activeDir === 'right'} />
      </div>
      <div className="col-start-2 row-start-3 h-14">
        <CrossArrow dir="down" letter={byDir.down || 'B'} active={activeDir === 'down'} />
      </div>
    </div>
  );
}
