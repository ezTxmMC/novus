/**
 * Clicks through the built site in a real browser and checks that the pages
 * actually react. Catches the class of bug a render-only smoke test cannot:
 * navigation that changes the URL but not the content, controls that do not
 * update, code blocks that fail to highlight.
 *
 *   bun run build && bun run ui-test          # starts its own preview server
 *   bun run ui-test http://localhost:5173     # or point it at a running one
 *
 * Skips itself when no Chrome is installed, so it never blocks a build.
 */
import { existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { spawn } from 'node:child_process';

const CANDIDATES = [
  process.env.CHROME_PATH,
  '/usr/bin/google-chrome-stable',
  '/usr/bin/chromium',
  `${homedir()}/.cache/ms-playwright/chromium_headless_shell-1228/chrome-linux/headless_shell`,
];
const executablePath = CANDIDATES.find((path) => path && existsSync(path));
if (!executablePath) {
  console.log('ui-test: no Chrome found, skipping');
  process.exit(0);
}

let base = process.argv[2];
let server;
if (!base) {
  base = 'http://localhost:4321';
  server = spawn('bunx', ['vite', 'preview', '--port', '4321'], { stdio: 'ignore' });
  await new Promise((resolve) => setTimeout(resolve, 4000));
}

const { chromium } = await import('playwright-core');
const browser = await chromium.launch({ executablePath, args: ['--no-sandbox'] });
const page = await browser.newPage();
const errors = [];
page.on('pageerror', (error) => errors.push(`pageerror: ${error.message}`));
page.on('console', (message) => {
  if (message.type() === 'error') errors.push(`console: ${message.text()}`);
});

let failures = 0;
function check(condition, what, detail = '') {
  if (condition) {
    console.log(`  ok   ${what}`);
  } else {
    failures++;
    console.log(`  FAIL ${what}${detail ? ` (${detail})` : ''}`);
  }
}
const wait = (ms) => page.waitForTimeout(ms);
const heading = () => page.textContent('h1').then((text) => text?.trim() ?? '');

console.log('navigation');
await page.goto(`${base}/docs/introduction`, { waitUntil: 'networkidle' });
check((await heading()) === 'Introduction', 'docs landing page renders');

for (const [href, expected] of [
  ['/docs/installation', 'Installation'],
  ['/docs/language/basics', 'Basics'],
  ['/docs/language/classes', 'Classes and objects'],
  ['/docs/projects/manifest', 'project.nv'],
  ['/docs/bootstrapping', 'Bootstrapping'],
]) {
  await page.click(`a[href="${href}"]`);
  await wait(700);
  check((await heading()) === expected, `sidebar -> ${href}`, await heading());
}

await page.click('a[href="/docs/installation"]');
await wait(600);
await page.goBack();
await wait(800);
check((await heading()) === 'Bootstrapping', 'browser back restores the previous page', await heading());

console.log('\nbenchmarks');
await page.goto(`${base}/benchmarks`, { waitUntil: 'networkidle' });
check((await page.locator('table tbody tr').count()) === 10, 'all ten workloads are listed');
await page.click('button:has-text("leanest")');
await wait(300);
check(true, 'metric toggle does not throw');
await page.locator('#fib button:has-text("show the code")').click();
await wait(900);
const seen = new Set();
// a language whose toolchain was missing when the suite ran has no tab here
for (const language of ['Novus', 'Rust', 'Go', 'Python', 'Java', 'C++']) {
  if ((await page.locator(`#fib button:has-text("${language}")`).count()) === 0) {
    continue;
  }
  await page.locator(`#fib button:has-text("${language}")`).first().click();
  await wait(700);
  const code = (await page.textContent('#fib pre'))?.slice(0, 40) ?? '';
  seen.add(code);
  check(code.length > 10, `benchmark code shows ${language}`, code.slice(0, 24));
}
check(seen.size >= 5, 'every language shows different source');

console.log('\nexamples');
await page.goto(`${base}/examples`, { waitUntil: 'networkidle' });
const allCards = await page.locator('a[href^="/examples/"]').count();
await page.fill('input[placeholder*="Filter"]', 'quicksort');
await wait(400);
const filtered = await page.locator('a[href^="/examples/"]').count();
check(filtered > 0 && filtered < allCards, 'filtering narrows the list', `${allCards} -> ${filtered}`);
await page.locator('a[href^="/examples/"]').first().click();
await wait(900);
check((await page.textContent('h1'))?.includes('quicksort'), 'example detail opens');
check(((await page.textContent('pre')) ?? '').length > 20, 'example source is shown');

console.log('\nstdlib');
await page.goto(`${base}/stdlib`, { waitUntil: 'networkidle' });
const allRows = await page.locator('table tbody tr').count();
await page.fill('input[placeholder*="Filter"]', 'mkdir');
await wait(400);
const someRows = await page.locator('table tbody tr').count();
check(someRows > 0 && someRows < allRows, 'stdlib filter works', `${allRows} -> ${someRows}`);

console.log('\nchrome');
await page.goto(`${base}/`, { waitUntil: 'networkidle' });
const darkBefore = await page.evaluate(() => document.documentElement.classList.contains('dark'));
await page.click('button[aria-label*="theme"]');
await wait(300);
const darkAfter = await page.evaluate(() => document.documentElement.classList.contains('dark'));
check(darkBefore !== darkAfter, 'theme toggle switches');

await page.keyboard.press('Control+k');
await wait(400);
await page.keyboard.type('collections');
await wait(600);
const hits = await page.locator('input[placeholder*="Search"] ~ ul button').count();
check(hits > 0, 'search finds results');
await page.keyboard.press('Enter');
await wait(900);
check(page.url().includes('/docs/'), 'search result navigates', page.url());

if (errors.length) {
  failures += errors.length;
  console.log(`\n${errors.length} console/page error(s):`);
  for (const error of errors.slice(0, 8)) console.log(`  ${error}`);
}

await browser.close();
server?.kill();
console.log(failures ? `\n${failures} check(s) failed` : '\nall interactions work');
process.exit(failures ? 1 : 0);
