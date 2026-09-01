# novus docs

The documentation site for [Novus](../README.md): React 19, React Router 7,
Vite, MDX and Tailwind CSS 4, built and run with [bun](https://bun.sh).

```bash
bun install
bun run dev        # http://localhost:5173
bun run build      # static site in dist/
bun run preview
```

## How it works

`scripts/sync-content.ts` runs before every dev and build and copies content
out of the repository, so the site cannot drift from the language:

| Source | Becomes |
|---|---|
| `vscode-novus/syntaxes/novus.tmLanguage.json` | syntax highlighting for `nv` code blocks (same grammar as the editor) |
| `examples/**/*.nv` + `.golden` | `/examples` browser with source and expected output |
| `std/*.nv` | `/stdlib` reference: every module, signature and doc comment |
| `compiler/driver/cli.nv`, `bootstrap/`, `runtime/` | version and the numbers on the landing page |

Prose lives in `src/content/**/*.mdx`; the sidebar, prev/next links and the
search index come from `src/lib/nav.ts`. Code blocks in MDX are highlighted at
build time (rehype + shiki), everything else lazily in the browser.

## Deploying

The output in `dist/` is a static site. `404.html` is a copy of `index.html`,
so client side routing works on GitHub Pages and similar hosts. For a project
page under a subpath, build with a base:

```bash
DOCS_BASE=/novus/ bun run build
```
