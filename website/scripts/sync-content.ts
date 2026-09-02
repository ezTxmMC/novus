/**
 * Copies content out of the repository into the site, so the docs cannot
 * drift from the language itself: the TextMate grammar used for syntax
 * highlighting, every example with its expected output, and the standard
 * library reference.
 *
 *   bun run scripts/sync-content.ts
 */
import { readdirSync, readFileSync, existsSync, mkdirSync, writeFileSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';

const root = resolve(import.meta.dir, '..', '..');
const site = resolve(import.meta.dir, '..');
const generated = join(site, 'src', 'generated');
mkdirSync(generated, { recursive: true });
mkdirSync(join(site, 'src', 'lib'), { recursive: true });

// --- syntax highlighting ----------------------------------------------------
writeFileSync(
  join(site, 'src', 'lib', 'novus.tmLanguage.json'),
  readFileSync(join(root, 'vscode-novus', 'syntaxes', 'novus.tmLanguage.json')),
);

// --- examples ---------------------------------------------------------------
type Example = {
  id: string; chapter: string; chapterId: string; number: string; name: string;
  title: string; summary: string; source: string; output: string; exitCode: number;
};

const CHAPTERS: Record<string, string> = {
  '01-basics': 'Basics',
  '02-control': 'Control flow',
  '03-methods': 'Methods',
  '04-strings': 'Strings',
  '05-arrays': 'Arrays',
  '06-maps': 'Maps',
  '07-classes': 'Classes',
  '08-stdlib': 'Standard library',
  '09-algorithms': 'Algorithms',
  '10-projects': 'Projects',
  '11-concurrency': 'Concurrency',
};

function summaryOf(source: string): string {
  const collected: string[] = [];
  for (const raw of source.split('\n')) {
    const line = raw.trim();
    if (line.startsWith('//')) {
      const text = line.replace(/^\/+/, '').trim();
      if (text) collected.push(text);
      continue;
    }
    if (collected.length) break;
    if (/^(method|define|var|private|final)\b/.test(line)) break;
  }
  return collected.join(' ').replace(/\.$/, '');
}

const examples: Example[] = [];
for (const chapterId of Object.keys(CHAPTERS)) {
  const dir = join(root, 'examples', chapterId);
  if (!existsSync(dir)) continue;
  for (const file of readdirSync(dir).sort()) {
    if (!file.endsWith('.nv')) continue;
    const base = join(dir, file.slice(0, -3));
    const source = readFileSync(join(dir, file), 'utf8');
    const name = file.slice(4, -3);
    examples.push({
      id: `${chapterId}/${file.slice(0, -3)}`,
      chapter: CHAPTERS[chapterId],
      chapterId,
      number: file.slice(0, 3),
      name,
      title: name.replace(/-/g, ' ').replace(/^./, (c) => c.toUpperCase()),
      summary: summaryOf(source),
      source,
      output: existsSync(`${base}.golden`) ? readFileSync(`${base}.golden`, 'utf8') : '',
      exitCode: existsSync(`${base}.rc`) ? Number(readFileSync(`${base}.rc`, 'utf8').trim()) : 0,
    });
  }
}
writeFileSync(join(generated, 'examples.json'), JSON.stringify(examples));

// --- standard library -------------------------------------------------------
type StdFunction = { name: string; signature: string; doc: string; native: boolean };
type StdModule = { name: string; summary: string; functions: StdFunction[] };

const modules: StdModule[] = [];
for (const file of readdirSync(join(root, 'std')).sort()) {
  if (!file.endsWith('.nv')) continue;
  const lines = readFileSync(join(root, 'std', file), 'utf8').split('\n');
  const header: string[] = [];
  for (const raw of lines.slice(1)) {
    const line = raw.trim();
    if (line.startsWith('//')) { header.push(line.replace(/^\/+/, '').trim()); continue; }
    if (header.length) break;
    if (/^(method|var|define)\b/.test(line)) break;
  }
  const functions: StdFunction[] = [];
  let pending: string[] = [];
  for (const raw of lines) {
    const line = raw.trim();
    if (line === '') { pending = []; continue; }            // a blank line ends a doc comment
    if (line.startsWith('//')) { pending.push(line.replace(/^\/+/, '').trim()); continue; }
    if (!line.startsWith('method ')) { pending = []; continue; }

    // `method name(params): type [native "c_fn" [variadic]]  // trailing doc`
    const trailing = /\/\/\s*(.+?)\s*$/.exec(line);
    const code = line.replace(/\s*\/\/.*$/, '').replace(/\s*\{$/, '').trim();
    const native = /\snative\s+"[^"]+"(\s+variadic)?$/.test(code);
    const signature = code.replace(/\s*native\s+"[^"]+"(\s+variadic)?$/, '').replace(/^method\s+/, '');
    const name = /^([A-Za-z_]\w*)/.exec(signature)?.[1] ?? '';
    functions.push({
      name,
      signature,
      doc: (pending.join(' ') || trailing?.[1] || '').replace(/\.$/, ''),
      native,
    });
    pending = [];
  }
  modules.push({ name: file.slice(0, -3), summary: header.join(' ').replace(/\.$/, ''), functions });
}
writeFileSync(join(generated, 'stdlib.json'), JSON.stringify(modules));

// --- benchmarks -------------------------------------------------------------
const benchFile = join(root, 'benchmarks', 'results.json');
if (existsSync(benchFile)) {
  const results = JSON.parse(readFileSync(benchFile, 'utf8'));
  const EXTENSION: Record<string, string> = {
    novus: 'nv', cpp: 'cpp', rust: 'rs', go: 'go',
    crystal: 'cr', java: 'java', node: 'js', python: 'py',
  };
  const JAVA_CLASS: Record<string, string> = {
    fib: 'Fib', loop: 'Loop', primes: 'Primes', mandelbrot: 'Mandelbrot',
    array: 'Arr', sort: 'Sort', strings: 'Str', map: 'MapB',
    objects: 'Obj', wordfreq: 'WordFreq', nbody: 'NBody', spectral: 'SpectralNorm',
  };
  // the sources are shown next to the numbers, so nobody has to trust them;
  // an older Novus release ("novus@alpha5") ran the same novus/ sources
  results.sources = {};
  for (const workload of results.workloads) {
    results.sources[workload.name] = {};
    for (const language of Object.keys(results.languages)) {
      const dir = language.startsWith('novus@') ? 'novus' : language;
      const base = language === 'java' ? JAVA_CLASS[workload.name] : workload.name;
      const file = join(root, 'benchmarks', dir, `${base}.${EXTENSION[dir]}`);
      if (existsSync(file)) results.sources[workload.name][language] = readFileSync(file, 'utf8');
    }
  }
  writeFileSync(join(generated, 'benchmarks.json'), JSON.stringify(results));
}

// --- numbers for the landing page -------------------------------------------
const countLines = (file: string) => readFileSync(file, 'utf8').split('\n').length;
function walk(dir: string, out: string[] = []): string[] {
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) walk(full, out);
    else if (full.endsWith('.nv')) out.push(full);
  }
  return out;
}
const compilerFiles = walk(join(root, 'compiler')).filter(
  (f) => !f.endsWith('runtime.nv') && !f.endsWith('stdlib.nv'),
);
writeFileSync(
  join(generated, 'stats.json'),
  JSON.stringify({
    examples: examples.length,
    stdModules: modules.length,
    stdFunctions: modules.reduce((n, m) => n + m.functions.length, 0),
    compilerLines: compilerFiles.reduce((n, f) => n + countLines(f), 0),
    runtimeLines: readdirSync(join(root, 'runtime'))
      .filter((name) => name.endsWith('.h'))
      .reduce((sum, name) => sum + countLines(join(root, 'runtime', name)), 0),
    snapshotLines: countLines(join(root, 'bootstrap', 'novusc.c')),
    version: /VERSION = "([^"]+)"/.exec(
      readFileSync(join(root, 'compiler', 'driver', 'cli.nv'), 'utf8'),
    )?.[1] ?? '0.0.0',
  }),
);

console.log(`synced ${examples.length} examples, ${modules.length} std modules`);
