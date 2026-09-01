import { useEffect, useState, type ReactNode } from 'react';
import { Link, NavLink, useLocation } from 'react-router-dom';
import { NAV } from '../lib/nav';
import { ThemeToggle } from './ThemeToggle';
import { Search } from './Search';
import stats from '../generated/stats.json';

const GITHUB = 'https://github.com/ezTxmMC/novus';

function Logo() {
  return (
    <Link to="/" className="flex items-center gap-2.5">
      <span className="grid size-8 place-items-center rounded-lg bg-brand-600 font-mono text-sm font-bold text-white">N</span>
      <span className="font-mono text-[15px] font-semibold tracking-tight text-slate-900 dark:text-white">novus</span>
      <span className="hidden rounded-md border border-slate-200 px-1.5 py-0.5 font-mono text-[10px] text-slate-500 sm:inline dark:border-white/10 dark:text-slate-400">
        {stats.version}
      </span>
    </Link>
  );
}

function SidebarLinks({ onNavigate }: { onNavigate?: () => void }) {
  return (
    <nav className="space-y-7 text-sm">
      {NAV.map((section) => (
        <div key={section.title}>
          <div className="mb-2 font-mono text-[11px] font-semibold tracking-widest text-slate-400 uppercase dark:text-slate-500">
            {section.title}
          </div>
          <ul className="space-y-0.5 border-l border-slate-200 dark:border-white/10">
            {section.items.map((item) => (
              <li key={item.path}>
                <NavLink
                  to={item.path}
                  onClick={onNavigate}
                  className={({ isActive }) =>
                    `-ml-px block border-l py-1.5 pl-4 transition ${
                      isActive
                        ? 'border-brand-500 font-medium text-brand-600 dark:text-brand-400'
                        : 'border-transparent text-slate-600 hover:border-slate-400 hover:text-slate-900 dark:text-slate-400 dark:hover:text-white'
                    }`
                  }
                >
                  {item.title}
                </NavLink>
              </li>
            ))}
          </ul>
        </div>
      ))}
    </nav>
  );
}

export function Layout({ children, wide = false }: { children: ReactNode; wide?: boolean }) {
  const [menuOpen, setMenuOpen] = useState(false);
  const location = useLocation();

  useEffect(() => {
    setMenuOpen(false);
    window.scrollTo({ top: 0 });
  }, [location.pathname]);

  const home = location.pathname === '/';

  return (
    <div className="min-h-dvh">
      <header className="sticky top-0 z-40 border-b border-slate-200 bg-white/80 backdrop-blur-md dark:border-white/10 dark:bg-[#0b0f14]/80">
        <div className="mx-auto flex h-16 max-w-[1400px] items-center gap-4 px-4 sm:px-6">
          {!home && (
            <button
              type="button"
              onClick={() => setMenuOpen(true)}
              aria-label="Open navigation"
              className="grid size-9 place-items-center rounded-lg border border-slate-200 lg:hidden dark:border-white/10"
            >
              <svg viewBox="0 0 24 24" className="size-4" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M4 6h16M4 12h16M4 18h16" strokeLinecap="round" />
              </svg>
            </button>
          )}
          <Logo />
          <div className="ml-auto flex items-center gap-2">
            <nav className="mr-2 hidden items-center gap-5 text-sm text-slate-600 md:flex dark:text-slate-400">
              <NavLink to="/docs/introduction" className="hover:text-brand-600 dark:hover:text-brand-400">
                Docs
              </NavLink>
              <NavLink to="/stdlib" className="hover:text-brand-600 dark:hover:text-brand-400">
                Stdlib
              </NavLink>
              <NavLink to="/examples" className="hover:text-brand-600 dark:hover:text-brand-400">
                Examples
              </NavLink>
            </nav>
            <Search />
            <ThemeToggle />
            <a
              href={GITHUB}
              target="_blank"
              rel="noreferrer"
              aria-label="GitHub repository"
              className="grid size-9 place-items-center rounded-lg border border-slate-200 text-slate-500 transition hover:border-brand-500/60 hover:text-brand-600 dark:border-white/10 dark:text-slate-400"
            >
              <svg viewBox="0 0 16 16" className="size-4" fill="currentColor">
                <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8Z" />
              </svg>
            </a>
          </div>
        </div>
      </header>

      {home ? (
        children
      ) : (
        <div className={`mx-auto flex max-w-[1400px] gap-10 px-4 sm:px-6`}>
          <aside className="hidden w-56 shrink-0 lg:block">
            <div className="sticky top-16 max-h-[calc(100dvh-4rem)] overflow-y-auto py-10 pr-2">
              <SidebarLinks />
            </div>
          </aside>

          {menuOpen && (
            <div className="fixed inset-0 z-50 lg:hidden" onClick={() => setMenuOpen(false)}>
              <div className="absolute inset-0 bg-slate-900/40 backdrop-blur-sm" />
              <div
                className="absolute inset-y-0 left-0 w-72 overflow-y-auto border-r border-slate-200 bg-white p-6 dark:border-white/10 dark:bg-[#0b0f14]"
                onClick={(event) => event.stopPropagation()}
              >
                <SidebarLinks onNavigate={() => setMenuOpen(false)} />
              </div>
            </div>
          )}

          <main className={`min-w-0 flex-1 py-10 ${wide ? '' : 'max-w-3xl'}`}>{children}</main>
        </div>
      )}

      <footer className="mt-16 border-t border-slate-200 py-10 dark:border-white/10">
        <div className="mx-auto flex max-w-[1400px] flex-col gap-3 px-4 text-sm text-slate-500 sm:flex-row sm:items-center sm:px-6">
          <span>
            Novus {stats.version} - a self-hosting language that compiles to C.
          </span>
          <span className="sm:ml-auto">
            <a href={GITHUB} target="_blank" rel="noreferrer" className="hover:text-brand-600 dark:hover:text-brand-400">
              GitHub
            </a>
            <span className="px-2 text-slate-300 dark:text-slate-700">/</span>
            <Link to="/docs/bootstrapping" className="hover:text-brand-600 dark:hover:text-brand-400">
              Bootstrapping
            </Link>
          </span>
        </div>
      </footer>
    </div>
  );
}
