/** The documentation tree - also drives prev/next links and the search index. */
export type NavItem = { title: string; path: string; summary: string };
export type NavSection = { title: string; items: NavItem[] };

export const NAV: NavSection[] = [
  {
    title: 'Getting started',
    items: [
      { title: 'Introduction', path: '/docs/introduction', summary: 'What Novus is and why it exists' },
      { title: 'Installation', path: '/docs/installation', summary: 'Bootstrap from source or grab a binary' },
      { title: 'Your first program', path: '/docs/first-program', summary: 'From hello world to a native binary' },
      { title: 'The novusc CLI', path: '/docs/cli', summary: 'run, build, emit, check, init, deps' },
    ],
  },
  {
    title: 'Language',
    items: [
      { title: 'Basics', path: '/docs/language/basics', summary: 'Values, variables, operators, strings' },
      { title: 'Control flow', path: '/docs/language/control-flow', summary: 'if, while, for..in, break, continue' },
      { title: 'Methods', path: '/docs/language/methods', summary: 'Parameters, returns, overloading, recursion' },
      { title: 'Collections', path: '/docs/language/collections', summary: 'Arrays and maps' },
      { title: 'Classes and objects', path: '/docs/language/classes', summary: 'Fields, inheritance, interfaces, enums' },
      { title: 'Modules and imports', path: '/docs/language/modules', summary: 'Packages, file imports, std modules' },
      { title: 'Concurrency', path: '/docs/language/concurrency', summary: 'thread, virtual, async, await, sync, channels' },
    ],
  },
  {
    title: 'Projects',
    items: [
      { title: 'project.nv', path: '/docs/projects/manifest', summary: 'The project manifest' },
      { title: 'Dependencies', path: '/docs/projects/dependencies', summary: 'require, replace and the module cache' },
      { title: 'Editor support', path: '/docs/projects/editor', summary: 'The VS Code extension' },
    ],
  },
  {
    title: 'Reference',
    items: [
      { title: 'Standard library', path: '/stdlib', summary: 'Every module and function of std/' },
      { title: 'Examples', path: '/examples', summary: '258 programs from hello world to a VM' },
      { title: 'Benchmarks', path: '/benchmarks', summary: 'Ten workloads across eight languages' },
      { title: 'Architecture', path: '/docs/architecture', summary: 'How the compiler is put together' },
      { title: 'Bootstrapping', path: '/docs/bootstrapping', summary: 'The self-hosting ladder and the fixpoint' },
    ],
  },
];

export const FLAT_NAV = NAV.flatMap((section) => section.items);

export function neighbours(path: string): { previous?: NavItem; next?: NavItem } {
  const index = FLAT_NAV.findIndex((item) => item.path === path);
  if (index < 0) return {};
  return { previous: FLAT_NAV[index - 1], next: FLAT_NAV[index + 1] };
}
