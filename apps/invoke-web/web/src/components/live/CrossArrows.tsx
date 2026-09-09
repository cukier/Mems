// Fixed answer cross for the band's screen: A = up, B = down, C = left,
// D = right. `active` is the letter currently selected (or none).

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
  dir: 'up' | 'down' | 'left' | 'right';
  letter: string;
  active?: boolean;
}) {
  const pos = LETTER_POS[dir];
  return (
    <svg viewBox="0 0 48 48" className="w-full h-full">
      <g transform={`rotate(${ANGLE[dir]} 24 24)`}>
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

export default function CrossArrows({ active }: { active?: string }) {
  const a = String(active ?? '').toUpperCase();
  return (
    <div className="grid grid-cols-3 grid-rows-3 gap-1 w-full max-w-[180px]">
      <div className="col-start-2 row-start-1 h-14">
        <CrossArrow dir="up" letter="A" active={a === 'A'} />
      </div>
      <div className="col-start-1 row-start-2 h-14">
        <CrossArrow dir="left" letter="C" active={a === 'C'} />
      </div>
      <div className="col-start-3 row-start-2 h-14">
        <CrossArrow dir="right" letter="D" active={a === 'D'} />
      </div>
      <div className="col-start-2 row-start-3 h-14">
        <CrossArrow dir="down" letter="B" active={a === 'B'} />
      </div>
    </div>
  );
}
