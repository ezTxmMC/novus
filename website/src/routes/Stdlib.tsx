import { useState } from 'react';
import { Layout } from '../components/Layout';
import stdlib from '../generated/stdlib.json';

export default function Stdlib() {
  const [query, setQuery] = useState('');
  const needle = query.trim().toLowerCase();

  const modules = stdlib
    .map((module) => ({
      ...module,
      functions: module.functions.filter(
        (fn) =>
          !needle ||
          module.name.includes(needle) ||
          fn.name.toLowerCase().includes(needle) ||
          fn.doc.toLowerCase().includes(needle),
      ),
    }))
    .filter((module) => module.functions.length > 0);

  const total = stdlib.reduce((n, module) => n + module.functions.length, 0);

  return (
    <Layout wide>
      <h1 className="font-mono text-3xl font-semibold tracking-tight text-slate-900 dark:text-white">
        Standard library
      </h1>
      <p className="mt-3 max-w-2xl text-slate-500">
        {stdlib.length} modules with {total} functions, generated from{' '}
        <a
          href="https://github.com/ezTxmMC/novus/tree/master/std"
          target="_blank"
          rel="noreferrer"
          className="text-brand-600 dark:text-brand-400"
        >
          std/
        </a>
        . Import a module by name (<code className="font-mono">import strings</code>) and call its functions namespaced
        (<code className="font-mono">strings.repeat(...)</code>). Functions marked{' '}
        <span className="font-mono text-[11px] text-brand-600 dark:text-brand-400">native</span> are implemented by the
        C runtime, the rest is Novus you can read.
      </p>

      <div className="mt-6 flex flex-wrap gap-1.5">
        {stdlib.map((module) => (
          <a
            key={module.name}
            href={`#${module.name}`}
            className="rounded-full border border-slate-200 px-3 py-1 font-mono text-xs text-slate-600 transition hover:border-brand-500/60 hover:text-brand-600 dark:border-white/10 dark:text-slate-400"
          >
            {module.name}
          </a>
        ))}
      </div>

      <input
        value={query}
        onChange={(event) => setQuery(event.target.value)}
        placeholder="Filter functions..."
        className="mt-6 w-full max-w-md rounded-lg border border-slate-200 bg-transparent px-3 py-2 text-sm outline-none placeholder:text-slate-400 focus:border-brand-500 dark:border-white/10"
      />

      <div className="mt-10 space-y-12">
        {modules.map((module) => (
          <section key={module.name} id={module.name} className="scroll-mt-24">
            <h2 className="flex items-baseline gap-3 font-mono text-xl font-semibold text-slate-900 dark:text-white">
              {module.name}
              <span className="font-sans text-xs font-normal text-slate-400">{module.functions.length} functions</span>
            </h2>
            <p className="mt-1 max-w-3xl text-sm text-slate-500">{module.summary}</p>
            <div className="mt-4 overflow-hidden rounded-xl border border-slate-200 dark:border-white/10">
              <table className="w-full text-left text-sm">
                <tbody>
                  {module.functions.map((fn) => (
                    <tr
                      key={fn.name + fn.signature}
                      id={`${module.name}-${fn.name}`}
                      className="scroll-mt-24 border-b border-slate-100 last:border-0 dark:border-white/5"
                    >
                      <td className="w-1/2 px-4 py-3 align-top">
                        <code className="font-mono text-[13px] text-slate-900 dark:text-slate-100">
                          <span className="text-brand-600 dark:text-brand-400">{module.name}.</span>
                          {fn.signature}
                        </code>
                      </td>
                      <td className="px-4 py-3 align-top text-slate-500">
                        {fn.doc}
                        {fn.native && (
                          <span className="ml-2 rounded border border-brand-500/30 px-1.5 py-0.5 font-mono text-[10px] text-brand-600 dark:text-brand-400">
                            native
                          </span>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </section>
        ))}
      </div>
    </Layout>
  );
}
