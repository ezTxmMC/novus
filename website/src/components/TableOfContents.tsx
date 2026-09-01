import { useEffect, useState } from 'react';

type Heading = { id: string; text: string; level: number };

/** Reads the headings of the rendered MDX page and tracks the active one. */
export function TableOfContents({ containerId }: { containerId: string }) {
  const [headings, setHeadings] = useState<Heading[]>([]);
  const [active, setActive] = useState('');

  useEffect(() => {
    const container = document.getElementById(containerId);
    if (!container) return;
    const found = [...container.querySelectorAll<HTMLElement>('h2[id], h3[id]')].map((element) => ({
      id: element.id,
      text: element.textContent ?? '',
      level: Number(element.tagName[1]),
    }));
    setHeadings(found);
    if (found.length === 0) return;

    const observer = new IntersectionObserver(
      (entries) => {
        const visible = entries.filter((entry) => entry.isIntersecting);
        if (visible.length) setActive(visible[0].target.id);
      },
      { rootMargin: '-80px 0px -70% 0px' },
    );
    for (const heading of found) {
      const element = document.getElementById(heading.id);
      if (element) observer.observe(element);
    }
    return () => observer.disconnect();
  }, [containerId]);

  if (headings.length < 2) return null;

  return (
    <div className="sticky top-16 hidden max-h-[calc(100dvh-4rem)] w-56 shrink-0 overflow-y-auto py-10 xl:block">
      <div className="mb-2 font-mono text-[11px] font-semibold tracking-widest text-slate-400 uppercase">
        On this page
      </div>
      <ul className="space-y-1 border-l border-slate-200 text-sm dark:border-white/10">
        {headings.map((heading) => (
          <li key={heading.id}>
            <a
              href={`#${heading.id}`}
              className={`-ml-px block border-l py-1 transition ${heading.level === 3 ? 'pl-7' : 'pl-4'} ${
                active === heading.id
                  ? 'border-brand-500 text-brand-600 dark:text-brand-400'
                  : 'border-transparent text-slate-500 hover:text-slate-900 dark:hover:text-white'
              }`}
            >
              {heading.text}
            </a>
          </li>
        ))}
      </ul>
    </div>
  );
}
