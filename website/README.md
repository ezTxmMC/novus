# Novus website

The documentation site of Novus, written in Novus: nvh components,
documentation as `.nvmd` (Markdown with Novus, rendered by
[nvh-markdown](https://github.com/ezTxmMC/nvh-markdown)) and Tailwind CSS 4.3 through
[nvh-tailwindcss](https://github.com/ezTxmMC/nvh-tailwindcss). Dark theme with three purples (`primary`, `secondary`,
`accent`, defined in `styles/site.css`).

```sh
cd website
make deps       # fetch nvh-markdown and nvh-tailwindcss (project.nv)
make content    # content/**/*.nvmd -> *.nvh (nvh-markdown's generator; the compiler only reads .nvh)
make dev        # content + dev server on http://localhost:8080, Tailwind watches
make export     # content + the static site in dist/ + the link check
make clean      # removes the generated .nvh files and dist/
```

The `.nvmd` files are the sources; the `.nvh` next to them are generated and git-ignored. Run `make content` (or
`novusc run ~/.novus/deps/github.com/ezTxmMC/nvh-markdown@latest/nvmd.nv watch content` while writing) before `novusc run main.nv`. Without
the generated files the compiler reports `no package 'content/start'`.

`SITE_URL` sets the address written into `sitemap.xml`. The first run downloads the Tailwind standalone
binary (once, into `~/.novus/tools`).

| Folder | Contents |
|---|---|
| `components/` | Layout, Header, Footer, DocLayout, Sidebar, Callout, Code, Chart |
| `pages/` | Home, Examples, ExampleDetail, Benchmarks, NotFound |
| `content/<section>/` | the documentation, one `.nvmd` per page |
| `data/` | navigation, examples, standard library and benchmarks read from `snapshot/` |
| `site/` | the routes and the dev server |
| `publish/` | the static export: pages, search index, sitemap, styles |
| `styles/site.css`, `public/` | the Tailwind theme, `site.js`, the favicon |

The site is standalone: it needs `novusc` and this folder, no Novus source tree. Examples, the benchmarks and the numbers on the landing page are read from `snapshot/`, a copy that
`make sync NOVUS=<path to a Novus checkout>` (`tools/sync.sh`) refreshes; run it with a release so they do not drift
from the language. To add a documentation page: create `content/<section>/<name>.nvmd` (then `make content`), add it to
`data/nav.nv` and `site/routes.nv`.
