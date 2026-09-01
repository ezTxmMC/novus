// Smoke test: renders the real components without a DOM and checks that the
// pages actually contain their content (catches white screens).
//   bun run smoke
import { renderToStaticMarkup } from 'react-dom/server';
import { StaticRouter } from 'react-router';
import Home from '../src/routes/Home';
import Examples from '../src/routes/Examples';
import Stdlib from '../src/routes/Stdlib';
import Benchmarks from '../src/routes/Benchmarks';

// minimal browser shims used by ThemeToggle / Layout
globalThis.document = { documentElement: { classList: { contains: () => true, toggle: () => {} } } } as never;
globalThis.localStorage = { getItem: () => 'dark', setItem: () => {} } as never;
globalThis.window = { matchMedia: () => ({ matches: true }), addEventListener() {}, removeEventListener() {}, scrollTo() {} } as never;

function render(name: string, element: React.ReactNode, expected: string[]) {
  const html = renderToStaticMarkup(<StaticRouter location="/">{element}</StaticRouter>);
  const text = html.replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ');
  const missing = expected.filter((needle) => !text.includes(needle));
  console.log(`${missing.length ? 'FAIL' : 'ok  '} ${name.padEnd(10)} ${text.length} chars` +
    (missing.length ? `  missing: ${missing.join(', ')}` : ''));
  return missing.length;
}

let failures = 0;
failures += render('home', <Home />, ['compiles itself', 'Get started', 'Why Novus', 'std functions', 'Bootstrapping']);
failures += render('examples', <Examples />, ['Examples', 'hello-world', 'stack-machine', 'Algorithms']);
failures += render('stdlib', <Stdlib />, ['Standard library', 'mkdir(string path)', 'repeat(string s', 'native']);
failures += render('bench', <Benchmarks />, ['Benchmarks', 'Mandelbrot', 'Novus', 'Rust', 'Python', 'ms']);
console.log(failures ? `${failures} check(s) failed` : 'all components render');
process.exit(failures ? 1 : 0);
