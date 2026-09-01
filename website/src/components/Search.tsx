import { useEffect, useMemo, useRef, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { FLAT_NAV } from '../lib/nav';

type Hit = { title: string; detail: string; path: string; kind: string };

const DOC_HITS: Hit[] = FLAT_NAV.map((item) => ({
  title: item.title,
  detail: item.summary,
  path: item.path,
  kind: 'docs',
}));

/** Examples and std functions are only pulled in when search is opened. */
async function loadIndex(): Promise<Hit[]> {
  const [examples, stdlib] = await Promise.all([
    import('../generated/examples.json'),
    import('../generated/stdlib.json'),
  ]);
  const stdHits: Hit[] = stdlib.default.flatMap((module) =>
    module.functions.map((fn) => ({
      title: `${module.name}.${fn.name}`,
      detail: fn.doc,
      path: `/stdlib#${module.name}-${fn.name}`,
      kind: 'std',
    })),
  );
  const exampleHits: Hit[] = examples.default.map((example) => ({
    title: `${example.number} ${example.title}`,
    detail: example.summary,
    path: `/examples/${example.id}`,
    kind: example.chapter,
  }));
  return [...DOC_HITS, ...stdHits, ...exampleHits];
}

export function Search() {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [selected, setSelected] = useState(0);
  const input = useRef<HTMLInputElement>(null);
  const navigate = useNavigate();
  const [index, setIndex] = useState<Hit[]>(DOC_HITS);

  const hits = useMemo(() => {
    const needle = query.trim().toLowerCase();
    if (!needle) return index.slice(0, 8);
    return index
      .map((hit) => {
        const title = hit.title.toLowerCase();
        const score = title.startsWith(needle) ? 0 : title.includes(needle) ? 1 : hit.detail.toLowerCase().includes(needle) ? 2 : -1;
        return { hit, score };
      })
      .filter((entry) => entry.score >= 0)
      .sort((a, b) => a.score - b.score)
      .slice(0, 12)
      .map((entry) => entry.hit);
  }, [index, query]);

  useEffect(() => {
    function onKey(event: KeyboardEvent) {
      if ((event.metaKey || event.ctrlKey) && event.key === 'k') {
        event.preventDefault();
        setOpen((value) => !value);
      }
      if (event.key === 'Escape') setOpen(false);
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  useEffect(() => {
    if (open) {
      setQuery('');
      setSelected(0);
      requestAnimationFrame(() => input.current?.focus());
      if (index.length === DOC_HITS.length) loadIndex().then(setIndex);
    }
  }, [open, index.length]);

  function go(hit: Hit) {
    setOpen(false);
    navigate(hit.path);
  }

  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        className="flex h-9 items-center gap-2 rounded-lg border border-slate-200 px-3 text-sm text-slate-500 transition hover:border-brand-500/60 dark:border-white/10 dark:text-slate-400"
      >
        <svg viewBox="0 0 24 24" className="size-4" fill="none" stroke="currentColor" strokeWidth="2">
          <circle cx="11" cy="11" r="7" />
          <path d="m20 20-3.5-3.5" strokeLinecap="round" />
        </svg>
        <span className="hidden sm:inline">Search</span>
        <kbd className="hidden rounded border border-slate-200 px-1.5 font-mono text-[10px] sm:inline dark:border-white/10">
          ⌘K
        </kbd>
      </button>

      {open && (
        <div
          className="fixed inset-0 z-50 flex items-start justify-center bg-slate-900/40 p-4 pt-[12vh] backdrop-blur-sm"
          onClick={() => setOpen(false)}
        >
          <div
            className="w-full max-w-xl overflow-hidden rounded-2xl border border-slate-200 bg-white shadow-2xl dark:border-white/10 dark:bg-[#0f141b]"
            onClick={(event) => event.stopPropagation()}
          >
            <input
              ref={input}
              value={query}
              onChange={(event) => {
                setQuery(event.target.value);
                setSelected(0);
              }}
              onKeyDown={(event) => {
                if (event.key === 'ArrowDown') {
                  event.preventDefault();
                  setSelected((value) => Math.min(value + 1, hits.length - 1));
                }
                if (event.key === 'ArrowUp') {
                  event.preventDefault();
                  setSelected((value) => Math.max(value - 1, 0));
                }
                if (event.key === 'Enter' && hits[selected]) go(hits[selected]);
              }}
              placeholder="Search docs, std functions and examples..."
              className="w-full border-b border-slate-200 bg-transparent px-5 py-4 text-sm outline-none placeholder:text-slate-400 dark:border-white/10"
            />
            <ul className="max-h-[50vh] overflow-y-auto p-2">
              {hits.map((hit, i) => (
                <li key={hit.path + hit.title}>
                  <button
                    type="button"
                    onMouseEnter={() => setSelected(i)}
                    onClick={() => go(hit)}
                    className={`flex w-full items-baseline gap-3 rounded-lg px-3 py-2 text-left ${
                      i === selected ? 'bg-brand-500/10 text-brand-700 dark:text-brand-300' : ''
                    }`}
                  >
                    <span className="font-mono text-sm">{hit.title}</span>
                    <span className="truncate text-xs text-slate-500">{hit.detail}</span>
                    <span className="ml-auto shrink-0 font-mono text-[10px] tracking-wide text-slate-400 uppercase">
                      {hit.kind}
                    </span>
                  </button>
                </li>
              ))}
              {hits.length === 0 && <li className="px-3 py-6 text-center text-sm text-slate-500">No results</li>}
            </ul>
          </div>
        </div>
      )}
    </>
  );
}
