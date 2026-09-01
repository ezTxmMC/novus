import type { MDXComponents } from 'mdx/types';
import { Link } from 'react-router-dom';
import type { ReactNode } from 'react';

/** Internal links use the router, external ones open in a new tab. */
function Anchor({ href = '', children, ...rest }: { href?: string; children?: ReactNode }) {
  if (href.startsWith('/')) {
    return (
      <Link to={href} {...rest}>
        {children}
      </Link>
    );
  }
  const external = href.startsWith('http');
  return (
    <a href={href} target={external ? '_blank' : undefined} rel={external ? 'noreferrer' : undefined} {...rest}>
      {children}
    </a>
  );
}

export function Callout({ type = 'note', children }: { type?: 'note' | 'warning' | 'tip'; children: ReactNode }) {
  const styles = {
    note: 'border-slate-300 bg-slate-50 dark:border-white/15 dark:bg-white/5',
    tip: 'border-brand-500/50 bg-brand-50 dark:bg-brand-950/40',
    warning: 'border-amber-400/60 bg-amber-50 dark:border-amber-400/30 dark:bg-amber-950/30',
  }[type];
  const label = { note: 'Note', tip: 'Tip', warning: 'Careful' }[type];
  return (
    <div className={`my-6 rounded-xl border p-4 text-sm ${styles}`}>
      <div className="mb-1 font-mono text-xs font-semibold tracking-wide uppercase">{label}</div>
      <div className="[&>p:first-child]:mt-0 [&>p:last-child]:mb-0">{children}</div>
    </div>
  );
}

export const mdxComponents: MDXComponents = {
  a: Anchor as MDXComponents['a'],
  Callout,
};
