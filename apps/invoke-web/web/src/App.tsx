import { BrowserRouter, Route, Routes } from 'react-router-dom';
import Layout from '@/components/Layout';
import ScrollToTop from '@/components/ScrollToTop';
import Bands from '@/pages/Bands';
import Classes from '@/pages/Classes';
import Dashboard from '@/pages/Dashboard';
import Game from '@/pages/Game';
import Live from '@/pages/Live';
import NotFound from '@/pages/NotFound';
import Performance from '@/pages/Performance';
import Questions from '@/pages/Questions';
import Ranking from '@/pages/Ranking';
import Schools from '@/pages/Schools';
import Showcase from '@/pages/Showcase';
import Students from '@/pages/Students';
import Teachers from '@/pages/Teachers';

export default function App() {
  return (
    <BrowserRouter>
      <ScrollToTop />
      <Routes>
        <Route element={<Layout />}>
          <Route path="/" element={<Dashboard />} />
          <Route path="/live" element={<Live />} />
          <Route path="/performance" element={<Performance />} />
          <Route path="/ranking" element={<Ranking />} />
          <Route path="/game" element={<Game />} />
          <Route path="/showcase" element={<Showcase />} />
          <Route path="/questions" element={<Questions />} />
          <Route path="/students" element={<Students />} />
          <Route path="/classes" element={<Classes />} />
          <Route path="/teachers" element={<Teachers />} />
          <Route path="/schools" element={<Schools />} />
          <Route path="/bands" element={<Bands />} />
          <Route path="*" element={<NotFound />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
