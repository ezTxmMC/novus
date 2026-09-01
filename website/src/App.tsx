import { lazy, Suspense } from 'react';
import { Navigate, Route, Routes } from 'react-router-dom';
import { MDXProvider } from '@mdx-js/react';
import { Layout } from './components/Layout';
import { mdxComponents } from './components/mdx';
import Home from './routes/Home';
import DocPage from './routes/DocPage';

const Examples = lazy(() => import('./routes/Examples'));
const ExampleDetail = lazy(() => import('./routes/ExampleDetail'));
const Stdlib = lazy(() => import('./routes/Stdlib'));

function Loading() {
  return <div className="py-20 text-center text-sm text-slate-500">Loading...</div>;
}

function NotFound() {
  return (
    <Layout>
      <h1 className="font-mono text-3xl font-semibold text-slate-900 dark:text-white">404</h1>
      <p className="mt-3 text-slate-500">This page does not exist (yet).</p>
    </Layout>
  );
}

export default function App() {
  return (
    <MDXProvider components={mdxComponents}>
      <Suspense fallback={<Loading />}>
        <Routes>
          <Route path="/" element={<Home />} />
          <Route path="/docs" element={<Navigate to="/docs/introduction" replace />} />
          <Route path="/docs/*" element={<DocPage />} />
          <Route path="/examples" element={<Examples />} />
          <Route path="/examples/:chapter/:name" element={<ExampleDetail />} />
          <Route path="/stdlib" element={<Stdlib />} />
          <Route path="*" element={<NotFound />} />
        </Routes>
      </Suspense>
    </MDXProvider>
  );
}
