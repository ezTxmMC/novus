import { useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { Layout } from '../components/Layout';
import examples from '../generated/examples.json';

const CHAPTERS = [...new Set(examples.map((example) => example.chapter))];

export default function Examples() {
  const [query, setQuery] = useState('');
  const [chapter, setChapter] = useState('all');

  const shown = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return examples.filter((example) => {
      if (chapter !== 'all' && example.chapter !== chapter) return false;
      if (!needle) return true;
      return (
        example.title.toLowerCase().includes(needle) ||
        example.summary.toLowerCase().includes(needle) ||
        example.source.toLowerCase().includes(needle)
      );
    });
  }, [query, chapter]);

  return (
    <Layout wide>
      <h1 className="font-mono text-3xl font-semibold tracking-tight text-slate-900 dark:text-white">Examples</h1>
      <p className="mt-3 max-w-2xl text-slate-500">
        {examples.length} programs, ordered from the first line of Novus to complete projects. Every one of them runs
        as it stands and is checked against its expected output on every commit.
      </p>

      <div className="sticky top-16 z-30 -mx-4 mt-8 border-y border-slate-200 bg-white/90 px-4 py-3 backdrop-blur sm:mx-0 sm:rounded-xl sm:border dark:border-white/10 dark:bg-[#0b0f14]/90">
        <div className="flex flex-col gap-3 sm:flex-row sm:items-center">
          <input
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            placeholder="Filter by name, description or source code..."
            className="w-full rounded-lg border border-slate-200 bg-transparent px-3 py-2 text-sm outline-none placeholder:text-slate-400 focus:border-brand-500 dark:border-white/10"
          />
          <div className="flex flex-wrap gap-1.5">
            {['all', ...CHAPTERS].map((name) => (
              <button
                key={name}
                type="button"
                onClick={() => setChapter(name)}
                className={`rounded-full border px-3 py-1 text-xs transition ${
                  chapter === name
                    ? 'border-brand-500 bg-brand-500/10 text-brand-700 dark:text-brand-300'
                    : 'border-slate-200 text-slate-500 hover:border-brand-500/50 dark:border-white/10'
                }`}
              >
                {name}
              </button>
            ))}
          </div>
        </div>
      </div>

      <div className="mt-4 text-xs text-slate-500">{shown.length} shown</div>

      <div className="mt-4 grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
        {shown.map((example) => (
          <Link key={example.id} to={`/examples/${example.id}`} className="card-link group">
            <div className="flex items-baseline gap-2">
              <span className="font-mono text-xs text-brand-600 dark:text-brand-400">{example.number}</span>
              <span className="font-mono text-sm font-medium text-slate-900 group-hover:text-brand-600 dark:text-white dark:group-hover:text-brand-400">
                {example.name}
              </span>
            </div>
            <p className="mt-2 line-clamp-2 text-sm text-slate-500">{example.summary}</p>
            <div className="mt-3 font-mono text-[10px] tracking-widest text-slate-400 uppercase">{example.chapter}</div>
          </Link>
        ))}
      </div>
    </Layout>
  );
}
