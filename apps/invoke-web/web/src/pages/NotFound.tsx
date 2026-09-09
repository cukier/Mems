import { Link } from 'react-router-dom';

export default function NotFound() {
  return (
    <div className="text-center py-24">
      <p className="text-5xl font-light">404</p>
      <p className="text-sm text-muted-foreground mt-3">Página não encontrada.</p>
      <Link to="/" className="inline-block mt-6 text-sm text-amber-400 hover:underline">
        Voltar ao painel
      </Link>
    </div>
  );
}
