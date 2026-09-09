import { History, HelpCircle, Radio } from 'lucide-react';
import { Link, Outlet, useLocation } from 'react-router-dom';
import ThemeToggle from '@/components/ThemeToggle';

const NAV = [
  { to: '/', label: 'Rodada ao vivo', icon: Radio },
  { to: '/questions', label: 'Banco de perguntas', icon: HelpCircle },
  { to: '/history', label: 'Histórico', icon: History },
];

export default function Layout() {
  const { pathname } = useLocation();
  return (
    <div className="min-h-screen bg-background text-foreground">
      <div className="pointer-events-none fixed inset-x-0 top-0 h-64 bg-gradient-to-b from-amber-500/10 to-transparent" />
      <header className="sticky top-0 z-30 backdrop-blur-xl bg-background/80 border-b border-border">
        <div className="max-w-5xl mx-auto px-5 h-16 flex items-center justify-between">
          <Link to="/" className="flex items-center gap-2.5">
            <span className="w-8 h-8 rounded-lg bg-gradient-to-br from-amber-400 to-orange-600 flex items-center justify-center">
              <Radio className="w-4 h-4 text-black" />
            </span>
            <span className="text-[15px] tracking-[0.2em] font-semibold">
              INVOKE<span className="text-amber-400"> BAND</span>
            </span>
          </Link>
          <ThemeToggle />
        </div>
        <nav className="max-w-5xl mx-auto px-3 pb-2 flex gap-1 overflow-x-auto scrollbar-none">
          {NAV.map(({ to, label, icon: Icon }) => {
            const active = pathname === to;
            return (
              <Link
                key={to}
                to={to}
                className={`shrink-0 flex items-center gap-1.5 px-3 py-1.5 rounded-full text-[13px] transition-all duration-300 ${
                  active
                    ? 'bg-primary text-primary-foreground'
                    : 'text-muted-foreground hover:text-foreground hover:bg-muted'
                }`}
              >
                <Icon className="w-3.5 h-3.5" />
                {label}
              </Link>
            );
          })}
        </nav>
      </header>
      <main className="max-w-5xl mx-auto px-5 py-8 relative">
        <Outlet />
      </main>
    </div>
  );
}
