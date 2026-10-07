# Novus website

The documentation site of Novus, written in Novus: [nvh](../README.md#web-components-nvh) components,
documentation as `.nvmd` (Markdown with Novus, rendered by
[nvh-markdown](../../nvh-markdown)) and Tailwind CSS 4.3 through
[nvh-tailwindcss](../../nvh-tailwindcss). Dark theme with three purples (`primary`, `secondary`,
`accent`, defined in `styles/site.css`).

```sh
cd website
make content    # content/**/*.nvmd -> *.nvh (nvh-markdown's generator; the compiler only reads .nvh)
make dev        # content + dev server on http://localhost:8080, Tailwind watches
make export     # content + the static site in dist/ + the link check
make clean      # removes the generated .nvh files and dist/
```

The `.nvmd` files are the sources; the `.nvh` next to them are generated and git-ignored. Run `make content` (or
`../build/novusc run ../../nvh-markdown/nvmd.nv watch content` while writing) before `novusc run main.nv`. Without
the generated files the compiler reports `no package 'content/start'`.

`SITE_URL` sets the address written into `sitemap.xml`. The first run downloads the Tailwind standalone
binary (once, into `~/.novus/tools`).

| Folder | Contents |
|---|---|
| `components/` | Layout, Header, Footer, DocLayout, Sidebar, Callout, Code, Chart |
| `pages/` | Home, Examples, ExampleDetail, Stdlib, Benchmarks, NotFound |
| `content/<section>/` | the documentation, one `.nvmd` per page |
| `data/` | navigation, examples, standard library and benchmarks read from the repository |
| `site/` | the routes and the dev server |
| `publish/` | the static export: pages, search index, sitemap, styles |
| `styles/site.css`, `public/` | the Tailwind theme, `site.js`, the favicon |

Examples, the standard library reference, the benchmarks and the numbers on the landing page are read
from `../examples`, `../std`, `../benchmarks` and `../compiler` when the site is built, so they cannot
drift from the language. To add a documentation page: create `content/<section>/<name>.nvmd` (then `make content`), add it to
`data/nav.nv` and `site/routes.nv`.
