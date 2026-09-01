import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import mdx from '@mdx-js/rollup';
import remarkGfm from 'remark-gfm';
import remarkFrontmatter from 'remark-frontmatter';
import remarkMdxFrontmatter from 'remark-mdx-frontmatter';
import rehypeSlug from 'rehype-slug';
import rehypeShiki from '@shikijs/rehype';
import novusGrammar from './src/lib/novus.tmLanguage.json';

// The Novus grammar is the same TextMate file the VS Code extension uses
// (copied by scripts/sync-content.ts), so code blocks look exactly like the
// editor does.
const novus = { ...novusGrammar, name: 'novus', aliases: ['nv'] } as never;

export default defineConfig({
  base: process.env.DOCS_BASE ?? '/',
  plugins: [
    {
      enforce: 'pre',
      ...mdx({
        providerImportSource: '@mdx-js/react',
        remarkPlugins: [remarkGfm, remarkFrontmatter, [remarkMdxFrontmatter, { name: 'meta' }]],
        rehypePlugins: [
          rehypeSlug,
          [
            rehypeShiki,
            {
              themes: { light: 'github-light', dark: 'github-dark-default' },
              langs: ['bash', 'json', 'c', 'typescript', 'ini', novus],
              defaultColor: false,
            },
          ],
        ],
      }),
    },
    react(),
    tailwindcss(),
  ],
  build: { chunkSizeWarningLimit: 1500 },
});
