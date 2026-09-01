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

/**
 * Grammars beyond the built-in ones, loaded the first time they show up.
 * Listed explicitly: a dynamic import with a variable path makes the bundler
 * ship every grammar shiki has (11 MB instead of a few hundred kilobytes).
 */
const EXTRA_LANGUAGES: Record<string, () => Promise<{ default: unknown }>> = {
  cpp: () => import('shiki/langs/cpp.mjs'),
  rust: () => import('shiki/langs/rust.mjs'),
  go: () => import('shiki/langs/go.mjs'),
  ruby: () => import('shiki/langs/ruby.mjs'),
  java: () => import('shiki/langs/java.mjs'),
  javascript: () => import('shiki/langs/javascript.mjs'),
  python: () => import('shiki/langs/python.mjs'),
};

async function ensureLanguage(shiki: HighlighterCore, lang: string): Promise<boolean> {
  if (shiki.getLoadedLanguages().includes(lang)) return true;
  const load = EXTRA_LANGUAGES[lang];
  if (!load) return false;
  try {
    const grammar = await load();
    await shiki.loadLanguage(grammar.default as never);
    return true;
  } catch {
    return false;
  }
}

export async function highlight(code: string, lang = 'novus'): Promise<string> {
  const shiki = await getHighlighter();
  const known = await ensureLanguage(shiki, lang);
  return shiki.codeToHtml(code, {
    lang: known ? lang : 'text',
    themes: THEMES,
    defaultColor: false,
  });
}
