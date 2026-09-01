/**
 * Runtime syntax highlighting for code that is not part of an MDX page
 * (examples, the landing page, standard library sources). Only the themes
 * and grammars we actually need are bundled, and shiki is loaded lazily so
 * it stays out of the initial chunk.
 */
import type { HighlighterCore } from 'shiki/core';

let highlighter: Promise<HighlighterCore> | undefined;

export const THEMES = { light: 'github-light', dark: 'github-dark-default' } as const;

async function load(): Promise<HighlighterCore> {
  const [{ createHighlighterCore }, { createJavaScriptRegexEngine }, light, dark, bash, json, grammar] =
    await Promise.all([
      import('shiki/core'),
      import('shiki/engine/javascript'),
      import('shiki/themes/github-light.mjs'),
      import('shiki/themes/github-dark-default.mjs'),
      import('shiki/langs/bash.mjs'),
      import('shiki/langs/json.mjs'),
      import('./novus.tmLanguage.json'),
    ]);
  return createHighlighterCore({
    themes: [light.default, dark.default],
    langs: [bash.default, json.default, [{ ...(grammar.default as object), name: 'novus' }] as never],
    engine: createJavaScriptRegexEngine({ forgiving: true }),
  });
}

export async function getHighlighter(): Promise<HighlighterCore> {
  if (!highlighter) highlighter = load();
  return highlighter;
}

export async function highlight(code: string, lang = 'novus'): Promise<string> {
  const shiki = await getHighlighter();
  return shiki.codeToHtml(code, { lang, themes: THEMES, defaultColor: false });
}
