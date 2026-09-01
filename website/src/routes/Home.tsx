import { Link } from 'react-router-dom';
import { Layout } from '../components/Layout';
import { Code } from '../components/Code';
import stats from '../generated/stats.json';

const HELLO = `package main

import strings

// Novus reads like a scripting language and compiles like C.
method main {
    var languages = ["novus", "c", "rust"]
    for (language in languages) {
        println "Hello from \${strings.capitalize(language)}!"
    }
}`;

const FEATURES = [
  {
    title: 'Self-hosting',
    body: 'The compiler is written in Novus and compiles itself byte-identically. A checked-in C snapshot bootstraps everything.',
    to: '/docs/bootstrapping',
  },
  {
    title: 'Compiles to portable C',
    body: 'Every program becomes one self-contained C file with an embedded runtime - hand it to any C compiler on any platform.',
    to: '/docs/architecture',
  },
  {
    title: 'No dependencies',
    body: 'A C compiler is all you need. No LLVM, no runtime to install, no package manager to set up first.',
    to: '/docs/installation',
  },
  {
    title: 'Familiar syntax',
    body: 'Classes, interfaces, enums, overloading and string interpolation - with type inference where it helps.',
    to: '/docs/language/basics',
  },
  {
    title: 'Batteries included',
    body: `${stats.stdModules} standard modules with ${stats.stdFunctions} functions: files, processes, JSON, HTTP, math, time and more.`,
    to: '/stdlib',
  },
  {
    title: 'Go style modules',
    body: 'A project.nv manifest, git URLs as dependencies and a module cache - novusc deps fetches everything.',
    to: '/docs/projects/manifest',
  },
];

function Stat({ value, label }: { value: string; label: string }) {
  return (
    <div>
      <div className="font-mono text-2xl font-semibold text-slate-900 dark:text-white">{value}</div>
      <div className="mt-1 text-xs tracking-wide text-slate-500 uppercase">{label}</div>
    </div>
  );
}

export default function Home() {
  return (
    <Layout>
      <section className="relative overflow-hidden border-b border-slate-200 dark:border-white/10">
        <div
          aria-hidden
          className="pointer-events-none absolute inset-0 -z-10 bg-[radial-gradient(60%_60%_at_50%_0%,var(--color-brand-500)/12,transparent)]"
        />
        <div className="mx-auto max-w-[1400px] px-4 py-20 sm:px-6 lg:py-28">
          <div className="grid items-center gap-12 lg:grid-cols-2">
            <div>
              <div className="inline-flex items-center gap-2 rounded-full border border-brand-500/30 bg-brand-500/5 px-3 py-1 font-mono text-xs text-brand-700 dark:text-brand-300">
                <span className="size-1.5 rounded-full bg-brand-500" />
                {stats.version}
              </div>
              <h1 className="mt-6 text-4xl leading-[1.1] font-semibold tracking-tight text-slate-900 sm:text-5xl lg:text-6xl dark:text-white">
                A language that
                <br />
                <span className="text-brand-600 dark:text-brand-400">compiles itself</span>
              </h1>
              <p className="mt-6 max-w-xl text-lg leading-relaxed text-slate-600 dark:text-slate-400">
                Novus is a small, self-hosting programming language. Its compiler is written in Novus, emits portable C
                and reproduces itself byte for byte. All you need to build it is a C compiler.
              </p>
              <div className="mt-8 flex flex-wrap gap-3">
                <Link
                  to="/docs/introduction"
                  className="rounded-lg bg-brand-600 px-5 py-2.5 text-sm font-medium text-white transition hover:bg-brand-700"
                >
                  Get started
                </Link>
                <Link
                  to="/examples"
                  className="rounded-lg border border-slate-200 px-5 py-2.5 text-sm font-medium text-slate-700 transition hover:border-brand-500/60 dark:border-white/10 dark:text-slate-300"
                >
                  Browse {stats.examples} examples
                </Link>
              </div>
              <div className="mt-10 flex flex-wrap gap-10">
                <Stat value={String(stats.examples)} label="examples" />
                <Stat value={String(stats.stdFunctions)} label="std functions" />
                <Stat value={`${(stats.compilerLines / 1000).toFixed(1)}k`} label="lines of compiler" />
                <Stat value="1" label="dependency: cc" />
              </div>
            </div>

            <div>
              <Code code={HELLO} />
              <div className="mt-4 rounded-xl border border-slate-200 bg-slate-50 p-4 font-mono text-[13px] text-slate-600 dark:border-white/10 dark:bg-black/30 dark:text-slate-400">
                <div className="text-slate-400">$ novusc run hello.nv</div>
                <div>Hello from Novus!</div>
                <div>Hello from C!</div>
                <div>Hello from Rust!</div>
              </div>
            </div>
          </div>
        </div>
      </section>

      <section className="mx-auto max-w-[1400px] px-4 py-20 sm:px-6">
        <h2 className="font-mono text-2xl font-semibold tracking-tight text-slate-900 dark:text-white">
          Why Novus
        </h2>
        <div className="mt-8 grid gap-4 md:grid-cols-2 lg:grid-cols-3">
          {FEATURES.map((feature) => (
            <Link key={feature.title} to={feature.to} className="card-link">
              <div className="font-mono text-sm font-semibold text-slate-900 dark:text-white">{feature.title}</div>
              <p className="mt-2 text-sm leading-relaxed text-slate-500">{feature.body}</p>
            </Link>
          ))}
        </div>
      </section>

      <section className="border-y border-slate-200 bg-slate-50/60 dark:border-white/10 dark:bg-white/[0.02]">
        <div className="mx-auto grid max-w-[1400px] gap-10 px-4 py-20 sm:px-6 lg:grid-cols-2">
          <div>
            <h2 className="font-mono text-2xl font-semibold tracking-tight text-slate-900 dark:text-white">
              Install in one command
            </h2>
            <p className="mt-4 text-slate-600 dark:text-slate-400">
              The repository carries the C that the compiler generates for itself. Compile that snapshot, let it build
              the current sources, and let the result rebuild itself - three stages, a few seconds.
            </p>
            <Link to="/docs/installation" className="mt-6 inline-block text-sm text-brand-600 dark:text-brand-400">
              Installation guide →
            </Link>
          </div>
          <Code
            lang="bash"
            code={`git clone https://github.com/ezTxmMC/novus
cd novus
scripts/bootstrap.sh          # or: make

build/novusc run examples/01-basics/001-hello-world.nv
build/novusc build app.nv -o app --target x86_64-windows-gnu`}
          />
        </div>
      </section>

      <section className="mx-auto max-w-[1400px] px-4 py-20 sm:px-6">
        <div className="grid gap-10 lg:grid-cols-2">
          <div>
            <h2 className="font-mono text-2xl font-semibold tracking-tight text-slate-900 dark:text-white">
              The bootstrap fixpoint
            </h2>
            <p className="mt-4 text-slate-600 dark:text-slate-400">
              Stage 2 and stage 3 of the build produce the same {stats.snapshotLines.toLocaleString('en-US')} lines of C,
              byte for byte - the proof that the compiler is a fixpoint of itself. Every commit checks it.
            </p>
            <Link to="/docs/bootstrapping" className="mt-6 inline-block text-sm text-brand-600 dark:text-brand-400">
              How bootstrapping works →
            </Link>
          </div>
          <Code
            lang="bash"
            code={`bootstrap/novusc.c ──cc──▶ novusc0 ──build──▶ novusc1 ──▶ novusc2

stage 0 ok: snapshot builds
stage 1 ok: snapshot compiles the current sources
stage 2 ok: the compiler compiles itself
stage 3 ok: FIXPOINT - novusc compiles itself byte-identically`}
          />
        </div>
      </section>
    </Layout>
  );
}
