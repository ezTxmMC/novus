import { Link, useParams } from 'react-router-dom';
import { Layout } from '../components/Layout';
import { Code, Output } from '../components/Code';
import examples from '../generated/examples.json';

const GITHUB = 'https://github.com/ezTxmMC/novus/blob/master/examples';

export default function ExampleDetail() {
  const { chapter, name } = useParams();
  const id = `${chapter}/${name}`;
  const index = examples.findIndex((example) => example.id === id);
  const example = examples[index];

  if (!example) {
    return (
      <Layout>
        <h1 className="font-mono text-2xl font-semibold">Example not found</h1>
        <Link to="/examples" className="mt-4 inline-block text-brand-600 dark:text-brand-400">
          Back to all examples
        </Link>
      </Layout>
    );
  }

  const previous = examples[index - 1];
  const next = examples[index + 1];

  return (
    <Layout wide>
      <Link to="/examples" className="font-mono text-xs text-slate-500 hover:text-brand-600 dark:hover:text-brand-400">
        ← all examples
      </Link>
      <div className="mt-3 flex flex-wrap items-baseline gap-3">
        <h1 className="font-mono text-2xl font-semibold tracking-tight text-slate-900 dark:text-white">
          <span className="text-brand-600 dark:text-brand-400">{example.number}</span> {example.name}
        </h1>
        <span className="rounded-full border border-slate-200 px-2.5 py-0.5 font-mono text-[10px] tracking-widest text-slate-500 uppercase dark:border-white/10">
          {example.chapter}
        </span>
      </div>
      <p className="mt-2 max-w-2xl text-slate-500">{example.summary}</p>

      <div className="mt-6 flex flex-wrap items-center gap-3 text-xs">
        <code className="rounded-lg border border-slate-200 px-2.5 py-1.5 font-mono text-slate-600 dark:border-white/10 dark:text-slate-400">
          novusc run examples/{example.id}.nv
        </code>
        <a
          href={`${GITHUB}/${example.id}.nv`}
          target="_blank"
          rel="noreferrer"
          className="text-slate-500 hover:text-brand-600 dark:hover:text-brand-400"
        >
          view on GitHub
        </a>
      </div>

      <div className="mt-8 grid gap-6 lg:grid-cols-2">
        <div>
          <div className="mb-2 font-mono text-[11px] font-semibold tracking-widest text-slate-400 uppercase">Source</div>
          <Code code={example.source} />
        </div>
        <div>
          <div className="mb-2 font-mono text-[11px] font-semibold tracking-widest text-slate-400 uppercase">
            Output{example.exitCode !== 0 && ` (exit code ${example.exitCode})`}
          </div>
          <Output text={example.output} />
        </div>
      </div>

      <nav className="mt-12 flex flex-col gap-3 border-t border-slate-200 pt-6 sm:flex-row dark:border-white/10">
        {previous && (
          <Link to={`/examples/${previous.id}`} className="card-link flex-1">
            <div className="font-mono text-[11px] tracking-widest text-slate-400 uppercase">Previous</div>
            <div className="mt-1 font-mono text-sm text-slate-900 dark:text-white">
              {previous.number} {previous.name}
            </div>
          </Link>
        )}
        {next && (
          <Link to={`/examples/${next.id}`} className="card-link flex-1 sm:text-right">
            <div className="font-mono text-[11px] tracking-widest text-slate-400 uppercase">Next</div>
            <div className="mt-1 font-mono text-sm text-slate-900 dark:text-white">
              {next.number} {next.name}
            </div>
          </Link>
        )}
      </nav>
    </Layout>
  );
}
