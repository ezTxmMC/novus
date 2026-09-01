import { useEffect, useState } from 'react';
import { highlight } from '../lib/highlight';

/** Highlighted code block with a copy button (used outside MDX). */
export function Code({ code, lang = 'novus', className = '' }: { code: string; lang?: string; className?: string }) {
  const [html, setHtml] = useState('');
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    let active = true;
    highlight(code, lang).then((result) => {
      if (active) setHtml(result);
    });
    return () => {
      active = false;
    };
  }, [code, lang]);

  return (
    <div className={`group relative overflow-hidden rounded-xl border border-slate-200 dark:border-white/10 ${className}`}>
      <button
        type="button"
        onClick={() => {
          navigator.clipboard?.writeText(code);
          setCopied(true);
          setTimeout(() => setCopied(false), 1200);
        }}
        className="absolute top-2 right-2 z-10 rounded-md border border-slate-200 bg-white/80 px-2 py-1 font-mono text-[10px] text-slate-500 opacity-0 transition group-hover:opacity-100 dark:border-white/10 dark:bg-white/10 dark:text-slate-300"
      >
        {copied ? 'copied' : 'copy'}
      </button>
      {html ? (
        <div
          className="overflow-x-auto p-4 font-mono text-[13px] leading-6 [&_pre]:bg-transparent!"
          dangerouslySetInnerHTML={{ __html: html }}
        />
      ) : (
        <pre className="overflow-x-auto p-4 font-mono text-[13px] leading-6 text-slate-500">{code}</pre>
      )}
    </div>
  );
}

/** Plain program output, no highlighting. */
export function Output({ text }: { text: string }) {
  if (!text.trim()) return null;
  return (
    <pre className="overflow-x-auto rounded-xl border border-slate-200 bg-slate-50 p-4 font-mono text-[13px] leading-6 text-slate-600 dark:border-white/10 dark:bg-black/30 dark:text-slate-400">
      {text.replace(/\n$/, '')}
    </pre>
  );
}
