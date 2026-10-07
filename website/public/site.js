// Behaviour of the static site: copy buttons, list filters, tabs, search and the table of contents.
// Every feature starts only when its markup is on the page.
"use strict";

const SEARCH_INDEX_URL = "/search-index.json";
const MAX_SEARCH_HITS = 30;
const COPIED_LABEL_MS = 1500;
const SEARCH_KEYS = ["/", "k"];

function ready(callback) {
    if (document.readyState !== "loading") {
        callback();
        return;
    }
    document.addEventListener("DOMContentLoaded", callback);
}

function isTyping(target) {
    return target instanceof HTMLElement && (target.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(target.tagName));
}

// --- copy buttons ---------------------------------------------------------

function addCopyButton(block) {
    const frame = block.parentElement.classList.contains("code-frame") ? block.parentElement : wrapBlock(block);
    if (frame.querySelector(".copy-button")) {
        return;
    }
    const button = document.createElement("button");
    button.type = "button";
    button.className = "copy-button";
    button.textContent = "copy";
    button.setAttribute("aria-label", "Copy the code");
    button.addEventListener("click", async () => {
        await navigator.clipboard.writeText(block.innerText);
        button.textContent = "copied";
        setTimeout(() => { button.textContent = "copy"; }, COPIED_LABEL_MS);
    });
    frame.appendChild(button);
}

function wrapBlock(block) {
    const frame = document.createElement("div");
    frame.className = "code-frame";
    block.replaceWith(frame);
    frame.appendChild(block);
    return frame;
}

function initCopyButtons() {
    if (!navigator.clipboard) {
        return;
    }
    document.querySelectorAll("pre.code-block").forEach(addCopyButton);
}

// --- filters: <input data-filter-input> and <button data-filter-group="x"> narrow <li data-filter-item> ---

function initFilter(root) {
    const input = root.querySelector("[data-filter-input]");
    const chips = [...root.querySelectorAll("[data-filter-group]")];
    const items = [...root.querySelectorAll("[data-filter-item]")];
    const counter = root.querySelector("[data-filter-count]");
    let group = "all";

    const apply = () => {
        const needle = input ? input.value.trim().toLowerCase() : "";
        let shown = 0;
        for (const item of items) {
            const visible = (group === "all" || item.dataset.group === group) && item.dataset.text.includes(needle);
            item.hidden = !visible;
            shown += visible ? 1 : 0;
        }
        for (const section of root.querySelectorAll("[data-filter-section]")) {
            section.hidden = !section.querySelector("[data-filter-item]:not([hidden])");
        }
        if (counter) {
            counter.textContent = `${shown} shown`;
        }
    };

    input?.addEventListener("input", apply);
    for (const chip of chips) {
        chip.addEventListener("click", () => {
            group = chip.dataset.filterGroup;
            chips.forEach((other) => other.setAttribute("aria-pressed", String(other === chip)));
            apply();
        });
    }
}

// --- tabs: <button data-tab="id"> shows <div data-tab-panel="id"> -------------------

function initTabs(root) {
    const tabs = [...root.querySelectorAll("[data-tab]")];
    const panels = [...root.querySelectorAll("[data-tab-panel]")];
    for (const tab of tabs) {
        tab.addEventListener("click", () => {
            tabs.forEach((other) => other.setAttribute("aria-selected", String(other === tab)));
            panels.forEach((panel) => { panel.hidden = panel.dataset.tabPanel !== tab.dataset.tab; });
        });
    }
}

// --- search ---------------------------------------------------------------

let searchEntries = null;

async function loadSearchIndex() {
    if (searchEntries) {
        return searchEntries;
    }
    const response = await fetch(SEARCH_INDEX_URL);
    searchEntries = response.ok ? await response.json() : [];
    return searchEntries;
}

function scoreEntry(entry, words) {
    const title = entry.t.toLowerCase();
    const text = entry.s.toLowerCase();
    let score = 0;
    for (const word of words) {
        if (title.includes(word)) {
            score += title.startsWith(word) ? 12 : 8;
        } else if (text.includes(word)) {
            score += 2;
        } else {
            return 0;
        }
    }
    return score;
}

function searchFor(entries, query) {
    const words = query.toLowerCase().split(/\s+/).filter(Boolean);
    if (words.length === 0) {
        return [];
    }
    return entries
        .map((entry) => ({ entry, score: scoreEntry(entry, words) }))
        .filter((hit) => hit.score > 0)
        .sort((a, b) => b.score - a.score)
        .slice(0, MAX_SEARCH_HITS)
        .map((hit) => hit.entry);
}

function hitElement(entry) {
    const item = document.createElement("li");
    item.setAttribute("role", "option");
    item.className = "search-hit rounded-lg";
    const link = document.createElement("a");
    link.href = entry.u;
    link.className = "block rounded-lg px-3 py-2 no-underline";
    const kind = document.createElement("span");
    kind.className = "eyebrow mr-2";
    kind.textContent = entry.k;
    const title = document.createElement("span");
    title.className = "text-ink-50";
    title.textContent = entry.t;
    const summary = document.createElement("span");
    summary.className = "mt-0.5 block truncate text-xs text-ink-400";
    summary.textContent = entry.s.slice(0, 120);
    link.append(kind, title, summary);
    item.appendChild(link);
    return item;
}

function initSearch() {
    const dialog = document.getElementById("search-dialog");
    const input = document.getElementById("search-input");
    const list = document.getElementById("search-results");
    const empty = document.getElementById("search-empty");
    if (!dialog || !input || !list || typeof dialog.showModal !== "function") {
        return;
    }
    let selected = -1;

    const select = (index) => {
        const hits = [...list.children];
        selected = Math.max(-1, Math.min(index, hits.length - 1));
        hits.forEach((hit, position) => hit.setAttribute("aria-selected", String(position === selected)));
        hits[selected]?.scrollIntoView({ block: "nearest" });
    };

    const render = async () => {
        const hits = searchFor(await loadSearchIndex(), input.value);
        list.replaceChildren(...hits.map(hitElement));
        empty.hidden = hits.length > 0 || input.value.trim() === "";
        select(hits.length > 0 ? 0 : -1);
    };

    const open = () => {
        if (!dialog.open) {
            dialog.showModal();
        }
        input.select();
        loadSearchIndex();
    };

    document.querySelectorAll("[data-search-open]").forEach((button) => button.addEventListener("click", open));
    input.addEventListener("input", render);
    input.addEventListener("keydown", (event) => {
        if (event.key === "ArrowDown" || event.key === "ArrowUp") {
            event.preventDefault();
            select(selected + (event.key === "ArrowDown" ? 1 : -1));
        }
        if (event.key === "Enter") {
            list.children[selected]?.querySelector("a")?.click();
        }
        // a search field clears itself on Escape and swallows the key: the dialog closes at once instead
        if (event.key === "Escape") {
            event.preventDefault();
            dialog.close();
        }
    });
    dialog.addEventListener("click", (event) => {
        if (event.target === dialog) {
            dialog.close();
        }
    });
    document.addEventListener("keydown", (event) => {
        const wantsSearch = (event.key === "/" && !isTyping(event.target)) || (event.key === "k" && (event.ctrlKey || event.metaKey));
        if (wantsSearch && SEARCH_KEYS.includes(event.key)) {
            event.preventDefault();
            open();
        }
    });
}

// --- table of contents: the heading in view is marked --------------------------

function initTocSpy() {
    const links = [...document.querySelectorAll("#toc a")];
    if (links.length === 0 || !("IntersectionObserver" in window)) {
        return;
    }
    const byId = new Map(links.map((link) => [decodeURIComponent(link.hash.slice(1)), link]));
    const observer = new IntersectionObserver((records) => {
        for (const record of records.filter((entry) => entry.isIntersecting)) {
            links.forEach((link) => link.removeAttribute("aria-current"));
            byId.get(record.target.id)?.setAttribute("aria-current", "true");
        }
    }, { rootMargin: "-10% 0px -80% 0px" });
    for (const id of byId.keys()) {
        const heading = document.getElementById(id);
        if (heading) {
            observer.observe(heading);
        }
    }
}

// --- motion: reading progress and elements that rise into view -----------------

const REVEAL_SELECTOR = ".card, main section h2, main section > p, .prose-nv > h2, .prose-nv > .code-frame, .prose-nv > .callout, .prose-nv > .table-wrapper, table, [data-reveal]";
const REVEAL_STAGGER_MS = 60;
const REVEAL_MAX_DELAY_MS = 360;

function initScrollProgress() {
    const bar = document.createElement("div");
    bar.className = "scroll-progress";
    bar.setAttribute("aria-hidden", "true");
    document.body.appendChild(bar);
    let frame = 0;
    const update = () => {
        frame = 0;
        const range = document.documentElement.scrollHeight - window.innerHeight;
        bar.style.transform = `scaleX(${range > 0 ? Math.min(window.scrollY / range, 1) : 0})`;
    };
    window.addEventListener("scroll", () => {
        frame = frame || requestAnimationFrame(update);
    }, { passive: true });
    update();
}

function initReveal() {
    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (reduced || !("IntersectionObserver" in window)) {
        return;
    }
    const observer = new IntersectionObserver((records, self) => {
        for (const record of records.filter((entry) => entry.isIntersecting)) {
            record.target.classList.add("is-visible");
            self.unobserve(record.target);
        }
    }, { rootMargin: "0px 0px -8% 0px" });
    const seen = new Map();
    document.querySelectorAll(REVEAL_SELECTOR).forEach((element) => {
        if (element.getBoundingClientRect().top < window.innerHeight || element.closest("dialog")) {
            return;
        }
        const siblings = seen.get(element.parentElement) ?? 0;
        seen.set(element.parentElement, siblings + 1);
        element.style.setProperty("--reveal-delay", `${Math.min(siblings * REVEAL_STAGGER_MS, REVEAL_MAX_DELAY_MS)}ms`);
        element.classList.add("reveal");
        observer.observe(element);
    });
}

ready(() => {
    initScrollProgress();
    initReveal();
    initCopyButtons();
    document.querySelectorAll("[data-filter]").forEach(initFilter);
    document.querySelectorAll("[data-tabs]").forEach(initTabs);
    initSearch();
    initTocSpy();
});
