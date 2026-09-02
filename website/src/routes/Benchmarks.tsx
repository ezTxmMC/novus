import { useMemo, useState } from 'react';
import { Layout } from '../components/Layout';
import { Code } from '../components/Code';
import raw from '../generated/benchmarks.json';

type Run = { ms: number; mb: number; output: string; agrees: boolean };
type Workload = { name: string; title: string; description: string };
type Benchmarks = {
  languages: Record<string, { label: string; version: string; previous?: boolean }>;
  workloads: Workload[];
  runs: Record<string, Record<string, Run>>;
  sources: Record<string, Record<string, string>>;
};

// the generated file is data, not a shape the compiler needs to infer
const data = raw as unknown as Benchmarks;

const COLORS: Record<string, string> = {
  novus: '#16a34a',
  rust: '#3b82f6',
  cpp: '#a855f7',
  go: '#06b6d4',
  crystal: '#e2622c',
  java: '#10b981',
  node: '#d99106',
  python: '#e0568f',
};

const SHIKI_LANG: Record<string, string> = {
  novus: 'novus', cpp: 'cpp', rust: 'rust', go: 'go',
  crystal: 'ruby', java: 'java', node: 'javascript', python: 'python',
};

// Preferred order; a language whose toolchain was missing when the suite ran
// is not in the results at all, so only the measured ones are shown. Older
// Novus releases measured alongside ("novus@alpha5") follow the current one.
const PREVIOUS = Object.keys(data.languages).filter((name) => data.languages[name].previous);
const ORDER = ['novus', ...PREVIOUS, 'cpp', 'rust', 'go', 'crystal', 'java', 'node', 'python'].filter(
  (name) => name in data.languages,
);
// the current languages: what the wins are counted over
const CURRENT = ORDER.filter((name) => !data.languages[name].previous);

function colorOf(name: string): string {
  return COLORS[name] ?? (data.languages[name]?.previous ? '#86efac' : '#64748b');
}

function shikiLangOf(name: string): string {
  return SHIKI_LANG[name] ?? (data.languages[name]?.previous ? 'novus' : 'text');
}

function format(value: number, metric: 'ms' | 'mb'): string {
  if (metric === 'ms') return value < 10 ? `${value.toFixed(1)} ms` : `${Math.round(value)} ms`;
  return value < 10 ? `${value.toFixed(1)} MB` : `${Math.round(value)} MB`;
}

/** One horizontal bar chart: the languages of a single workload and metric. */
function Chart({ title, runs, metric }: { title: string; runs: Record<string, Run>; metric: 'ms' | 'mb' }) {
  const languages = ORDER.filter((name) => runs[name]);
  const max = Math.max(...languages.map((name) => runs[name][metric]));
  return (
    <div className="rounded-xl border border-slate-200 p-4 dark:border-white/10">
      <div className="mb-4 font-mono text-[11px] font-semibold tracking-widest text-slate-400 uppercase">{title}</div>
      <div className="space-y-1.5">
        {languages.map((name) => {
          const run = runs[name];
          const width = Math.max((run[metric] / max) * 100, 0.6);
          const inside = width > 55;
          return (
            <div key={name} className="flex items-center gap-3">
              <div className="w-20 shrink-0 font-mono text-xs text-slate-500">{data.languages[name].label}</div>
              <div className="relative h-6 flex-1 rounded bg-slate-100 dark:bg-white/5">
                <div
                  className="absolute inset-y-0 left-0 rounded"
                  style={{ width: `${width}%`, backgroundColor: colorOf(name) }}
                />
                <span
                  className={`absolute inset-y-0 flex items-center font-mono text-[11px] ${
                    inside ? 'text-white/95' : 'text-slate-600 dark:text-slate-300'
                  }`}
                  style={inside ? { right: '0.5rem' } : { left: `calc(${width}% + 0.5rem)` }}
                >
                  {format(run[metric], metric)}
                </span>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function WorkloadSection({ workload }: { workload: Workload }) {
  const [language, setLanguage] = useState('novus');
  const [showCode, setShowCode] = useState(false);
  const runs = data.runs[workload.name];
  const source = data.sources[workload.name]?.[language];
  const output = runs.novus?.output ?? '';

  return (
    <section id={workload.name} className="scroll-mt-24 border-t border-slate-200 py-10 dark:border-white/10">
      <h2 className="font-mono text-xl font-semibold text-slate-900 dark:text-white">{workload.title}</h2>
      <p className="mt-1 text-sm text-slate-500">
        {workload.description} - every language prints{' '}
        <code className="font-mono text-xs">{output.split('\n')[0]}</code>
      </p>

      <div className="mt-5 grid gap-4 lg:grid-cols-2">
        <Chart title="Wall clock time" runs={runs} metric="ms" />
        <Chart title="Peak memory (RSS)" runs={runs} metric="mb" />
      </div>

      <div className="mt-4">
        <button
          type="button"
          onClick={() => setShowCode(!showCode)}
          className="font-mono text-xs text-brand-600 hover:underline dark:text-brand-400"
        >
          {showCode ? 'hide the code' : 'show the code'}
        </button>
        {showCode && (
          <div className="mt-3">
            <div className="mb-2 flex flex-wrap gap-1.5">
              {ORDER.filter((name) => runs[name]).map((name) => (
                <button
                  key={name}
                  type="button"
                  onClick={() => setLanguage(name)}
                  className={`rounded-full border px-3 py-1 font-mono text-xs transition ${
                    language === name
                      ? 'border-brand-500 bg-brand-500/10 text-brand-700 dark:text-brand-300'
                      : 'border-slate-200 text-slate-500 hover:border-brand-500/50 dark:border-white/10'
                  }`}
                >
                  {data.languages[name].label}
                </button>
              ))}
            </div>
            {source && <Code code={source.trimEnd()} lang={shikiLangOf(language)} />}
          </div>
        )}
      </div>
    </section>
  );
}

export default function Benchmarks() {
  const [metric, setMetric] = useState<'ms' | 'mb'>('ms');

  // how often each language is fastest / leanest across all workloads -
  // older Novus releases are shown for comparison, not counted
  const wins = useMemo(() => {
    const counts: Record<string, number> = {};
    for (const workload of data.workloads) {
      const runs = data.runs[workload.name];
      const best = Object.entries(runs)
        .filter(([name]) => CURRENT.includes(name))
        .sort((a, b) => a[1][metric] - b[1][metric])[0];
      if (best) counts[best[0]] = (counts[best[0]] ?? 0) + 1;
    }
    return counts;
  }, [metric]);

  return (
    <Layout wide>
      <h1 className="font-mono text-3xl font-semibold tracking-tight text-slate-900 dark:text-white">Benchmarks</h1>
      <p className="mt-3 max-w-3xl text-slate-500">
        {data.workloads.length} workloads, implemented once in each language, run on the same machine. Every
        implementation prints the same output - the runner refuses to compare results that disagree. Times are the
        best of five runs and include process start, memory is the peak RSS of the child process.
        {PREVIOUS.length > 0 && (
          <>
            {' '}
            The previous Novus release ({PREVIOUS.map((name) => data.languages[name].version).join(', ')}) was
            measured in the same run, so the two columns are directly comparable.
          </>
        )}
      </p>

      <div className="mt-6 flex flex-wrap gap-x-6 gap-y-1 text-xs text-slate-500">
        {ORDER.map((name) => (
          <span key={name} className="flex items-center gap-2">
            <span className="size-2.5 rounded-full" style={{ backgroundColor: colorOf(name) }} />
            <span className="font-mono">{data.languages[name].version}</span>
          </span>
        ))}
      </div>

      <div className="mt-8 rounded-xl border border-slate-200 p-5 dark:border-white/10">
        <div className="flex flex-wrap items-center gap-3">
          <span className="font-mono text-[11px] font-semibold tracking-widest text-slate-400 uppercase">
            Wins across all {data.workloads.length} workloads
          </span>
          <div className="ml-auto flex gap-1">
            {(['ms', 'mb'] as const).map((option) => (
              <button
                key={option}
                type="button"
                onClick={() => setMetric(option)}
                className={`rounded-full border px-3 py-1 font-mono text-xs transition ${
                  metric === option
                    ? 'border-brand-500 bg-brand-500/10 text-brand-700 dark:text-brand-300'
                    : 'border-slate-200 text-slate-500 dark:border-white/10'
                }`}
              >
                {option === 'ms' ? 'fastest' : 'leanest'}
              </button>
            ))}
          </div>
        </div>
        <div className="mt-4 flex flex-wrap gap-6">
          {CURRENT.filter((name) => wins[name]).map((name) => (
            <div key={name}>
              <div className="font-mono text-2xl font-semibold" style={{ color: colorOf(name) }}>
                {wins[name]}
              </div>
              <div className="text-xs text-slate-500">{data.languages[name].label}</div>
            </div>
          ))}
        </div>
      </div>

      <div className="mt-8 overflow-x-auto rounded-xl border border-slate-200 dark:border-white/10">
        <table className="w-full text-left text-sm">
          <thead>
            <tr className="border-b border-slate-200 dark:border-white/10">
              <th className="px-4 py-3 font-mono text-[11px] tracking-widest text-slate-400 uppercase">Workload</th>
              {ORDER.map((name) => (
                <th key={name} className="px-3 py-3 font-mono text-[11px] tracking-widest text-slate-400 uppercase">
                  {data.languages[name].label}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {data.workloads.map((workload) => {
              const runs = data.runs[workload.name];
              const best = Math.min(...CURRENT.filter((n) => runs[n]).map((n) => runs[n][metric]));
              return (
                <tr key={workload.name} className="border-b border-slate-100 last:border-0 dark:border-white/5">
                  <td className="px-4 py-2">
                    <a href={`#${workload.name}`} className="text-brand-600 hover:underline dark:text-brand-400">
                      {workload.title}
                    </a>
                  </td>
                  {ORDER.map((name) => {
                    const run = runs[name];
                    if (!run) return <td key={name} className="px-3 py-2 text-slate-400">-</td>;
                    const isBest = run[metric] === best;
                    return (
                      <td
                        key={name}
                        className={`px-3 py-2 font-mono text-xs ${
                          isBest ? 'font-semibold text-slate-900 dark:text-white' : 'text-slate-500'
                        }`}
                      >
                        {format(run[metric], metric)}
                      </td>
                    );
                  })}
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      {data.workloads.map((workload) => (
        <WorkloadSection key={workload.name} workload={workload} />
      ))}

      <section className="border-t border-slate-200 py-10 dark:border-white/10">
        <h2 className="font-mono text-xl font-semibold text-slate-900 dark:text-white">How to read this</h2>
        <div className="mt-4 max-w-3xl space-y-4 text-sm text-slate-600 dark:text-slate-400">
          <p>
            These are microbenchmarks. They say something about the code a compiler generates for tight loops,
            allocation and standard library calls - and nothing about how a language feels in a large program, how
            good its tooling is, or how it behaves under a real workload.
          </p>
          <p>
            Times include process start, which is what a user waits for. That is the honest number for a command line
            tool and an unfair one for the JVM and Node, whose JITs need a long running process to pay off.
          </p>
          <p>
            <strong className="text-slate-900 dark:text-white">Where Novus does well:</strong> integer and float
            arithmetic, because the compiler proves which locals are numbers and emits plain C for them, and memory,
            because values are tagged pointers and objects are a single flat block.
          </p>
          <p>
            <strong className="text-slate-900 dark:text-white">Where it does not:</strong> sorting and allocation
            heavy code, where every element is still a boxed value behind a pointer, and hash maps, which store more
            per entry than a C++ <code className="font-mono text-xs">unordered_map</code>. Those are the next things
            to work on.
          </p>
          <p>
            Everything here lives in{' '}
            <a
              href="https://github.com/ezTxmMC/novus/tree/master/benchmarks"
              target="_blank"
              rel="noreferrer"
              className="text-brand-600 dark:text-brand-400"
            >
              benchmarks/
            </a>{' '}
            - sources, the runner and the raw results.
          </p>
        </div>
      </section>
    </Layout>
  );
}
