import { Suspense, lazy, useMemo, type ComponentType } from 'react';
import { Link, useLocation } from 'react-router-dom';
import { Layout } from '../components/Layout';
import { TableOfContents } from '../components/TableOfContents';
import { FLAT_NAV, neighbours } from '../lib/nav';

// Every MDX page under src/content is a route: /docs/language/basics maps to
// src/content/language/basics.mdx.
const PAGES = import.meta.glob('../content/**/*.mdx') as Record<
  string,
  () => Promise<{ default: ComponentType }>
>;

function pageFor(slug: string) {
  const key = `../content/${slug}.mdx`;
  return PAGES[key];
}

export default function DocPage() {
  const location = useLocation();
  const slug = location.pathname.replace(/^\/docs\/?/, '').replace(/\/$/, '');
  const loader = pageFor(slug || 'introduction');
  const Content = useMemo(() => (loader ? lazy(loader) : undefined), [loader]);
  const { previous, next } = neighbours(location.pathname);
  const current = FLAT_NAV.find((item) => item.path === location.pathname);

  if (!Content) {
    return (
      <Layout>
        <h1 className="font-mono text-2xl font-semibold text-slate-900 dark:text-white">Page not found</h1>
        <p className="mt-3 text-slate-500">
          No document at <code>{location.pathname}</code>.{' '}
          <Link to="/docs/introduction" className="text-brand-600 dark:text-brand-400">
            Back to the introduction
          </Link>
          .
        </p>
      </Layout>
    );
  }

  return (
    <Layout wide>
      <div className="flex gap-10">
        <article id="doc-content" className="prose-docs min-w-0 max-w-3xl flex-1">
          {current && (
            <div className="mb-2 font-mono text-xs tracking-widest text-brand-600 uppercase dark:text-brand-400">
              {current.summary}
            </div>
          )}
          <Suspense fallback={<div className="py-10 text-sm text-slate-500">Loading...</div>}>
            <Content />
          </Suspense>

          <nav className="mt-16 flex flex-col gap-3 border-t border-slate-200 pt-6 sm:flex-row dark:border-white/10">
            {previous && (
              <Link to={previous.path} className="card-link flex-1">
                <div className="font-mono text-[11px] tracking-widest text-slate-400 uppercase">Previous</div>
                <div className="mt-1 font-medium text-slate-900 dark:text-white">{previous.title}</div>
              </Link>
            )}
            {next && (
              <Link to={next.path} className="card-link flex-1 sm:text-right">
                <div className="font-mono text-[11px] tracking-widest text-slate-400 uppercase">Next</div>
                <div className="mt-1 font-medium text-slate-900 dark:text-white">{next.title}</div>
              </Link>
            )}
          </nav>
        </article>
        <TableOfContents containerId="doc-content" />
      </div>
    </Layout>
  );
}
