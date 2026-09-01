/**
 * Smoke tests for the language server core.
 * Run with `npm test` – exits non-zero on the first failure.
 */
import * as fs from 'fs';
import * as path from 'path';
import { Analysis } from '../server/analyzer';
import { complete } from '../server/completion';
import { codeActions, definition, documentSymbols, hover, semanticTokens, signatureHelp } from '../server/features';
import { applyInsert, createImportEdit } from '../server/imports';
import { DEFAULT_FORMAT_OPTIONS, formatLines, formatNovus, fullFormatEdits, rangeFormatEdits } from '../server/formatter';
import { lex, TokenKind } from '../server/lexer';
import { LineMap } from '../server/parser';
import { Workspace } from '../server/workspace';

let failures = 0;
let checks = 0;

function check(condition: unknown, message: string): void {
  checks++;
  if (!condition) {
    failures++;
    console.error(`  ✗ ${message}`);
  } else {
    console.log(`  ✓ ${message}`);
  }
}

function load(file: string): { uri: string; text: string } {
  return { uri: 'file://' + file, text: fs.readFileSync(file, 'utf8') };
}

function offsetOf(text: string, needle: string, occurrence = 0): number {
  let idx = -1;
  for (let i = 0; i <= occurrence; i++) {
    idx = text.indexOf(needle, idx + 1);
    if (idx < 0) throw new Error(`'${needle}' not found`);
  }
  return idx;
}

function errorsOf(a: Analysis): string[] {
  return a.diagnostics.filter(d => d.severity === 'error').map(d => `${a.lineMap.positionOf(d.span.start).line + 1}: ${d.message}`);
}

const extRoot = path.resolve(__dirname, '..', '..');
const repoTest = path.resolve(extRoot, '..', 'test');
const fixtures = path.join(extRoot, 'test', 'fixtures');

const ws = new Workspace();

// --- repository sample files -------------------------------------------------
console.log('repository samples');
for (const name of ['syntax.nv', 'test.nv']) {
  const file = path.join(repoTest, name);
  if (!fs.existsSync(file)) {
    console.log(`  (skipping ${name}: not found)`);
    continue;
  }
  const { uri, text } = load(file);
  const a = ws.analyze(uri, text);
  const errors = errorsOf(a);
  check(errors.length === 0, `${name} parses without errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  // The repository samples are living documents – report unresolved names, but do not fail on them.
  const undefinedWarnings = a.diagnostics.filter(d => d.message.startsWith('Cannot find'));
  if (undefinedWarnings.length) console.log(`  (info) ${name}: ${undefinedWarnings.length} unresolved name(s) – ${undefinedWarnings.map(d => d.message).join(' | ')}`);
}

{
  const { uri, text } = load(path.join(repoTest, 'syntax.nv'));
  const a = ws.analyze(uri, text);
  const mains = a.symbols.filter(s => s.name === 'main');
  check(mains.length === 2, 'syntax.nv declares two main overloads');
  const person = a.symbols.find(s => s.name === 'Person');
  check(!!person && person.kind === 'class', 'Person is a class');
  check(!!person && person.members.some(m => m.name === 'friends' && m.kind === 'field'), 'Person has field friends');
  const personDefs = (text.match(/define class Person\b/g) ?? []).length;
  check(personDefs < 2 || a.diagnostics.some(d => d.message.startsWith('Duplicate definition of \'Person\'')), 'duplicate Person definition is reported (when present)');
  const symbols = documentSymbols(a);
  check(symbols.some(s => s.name === 'Gender' && (s.children ?? []).some(c => c.name === 'MALE')), 'outline lists enum constants');

  // hover on `tom.friends()` resolves the getter
  const off = offsetOf(text, 'tom.friends()') + 'tom.'.length;
  const h = hover(ws, a, off);
  check(!!h && JSON.stringify(h.contents).includes('friends()'), 'hover on friends() shows the accessor signature');

  // go to definition on `json.parse` targets nothing (builtin) but on `Person{` targets the class
  const personInit = offsetOf(text, 'Person{');
  const defs = definition(ws, a, personInit);
  check(defs.length === 1 && defs[0].range.start.line === a.lineMap.positionOf(person!.selectionSpan.start).line, 'definition of Person{ jumps to the class');

  // completion after `tom.`
  const items = complete(ws, a, off);
  check(items.some(i => i.label === 'friends') && items.some(i => i.label === 'name'), 'member completion lists Person fields');

  // signature help inside json.save(
  const sig = signatureHelp(ws, a, offsetOf(text, 'json.save(') + 'json.save('.length);
  check(!!sig && sig.signatures[0].label.includes('save('), 'signature help for json.save');

  const tokens = semanticTokens(a);
  check(tokens.data.length > 50, 'semantic tokens are produced');
}

// --- feature fixture --------------------------------------------------------
console.log('features.nv');
{
  const { uri, text } = load(path.join(fixtures, 'features.nv'));
  const a = ws.analyze(uri, text);
  const errors = errorsOf(a);
  check(errors.length === 0, `features.nv parses without errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  const warnings = a.diagnostics.filter(d => d.severity === 'warning');
  check(warnings.length === 0, `features.nv has no warnings${warnings.length ? ': ' + warnings.map(d => d.message).join(' | ') : ''}`);
  const greeting = a.symbols.find(s => s.name === 'GREETING');
  check(greeting?.doc === 'Greeting used by main.', 'doc comment attached to constant');
  const main = a.symbols.find(s => s.name === 'main');
  check(main?.doc === 'Entry point.', 'block doc comment attached to method');
  const h = hover(ws, a, offsetOf(text, 'ratio', 1));
  check(!!h && JSON.stringify(h.contents).includes('float'), 'inferred type of ratio is float');
  const items = complete(ws, a, offsetOf(text, 'println counter.value()') + 'println counter.'.length);
  check(items.some(i => i.label === 'increment') && items.some(i => i.label === 'value'), 'completion after counter. lists members incl. getter');
  const colorItems = complete(ws, a, offsetOf(text, 'Color.RED') + 'Color.'.length);
  check(colorItems.some(i => i.label === 'RED'), 'completion after Color. lists enum constants');
  const stmtItems = complete(ws, a, offsetOf(text, '  var total') + 2);
  check(stmtItems.some(i => i.label === 'var') && stmtItems.some(i => i.label === 'add'), 'statement completion lists keywords and methods');
}

// --- error fixture ----------------------------------------------------------
console.log('errors.nv');
{
  const { uri, text } = load(path.join(fixtures, 'errors.nv'));
  const a = ws.analyze(uri, text);
  const messages = a.diagnostics.map(d => d.message);
  const has = (part: string) => messages.some(m => m.includes(part));
  check(has('Unterminated string'), 'reports unterminated string');
  check(has("Cannot find name 'undefinedName'"), 'reports undefined name');
  check(has("'x' is already declared"), 'reports duplicate variable');
  check(has("Method 'noBody' has no body"), 'reports method without body');
  check(has("Cannot find type 'Missing'"), 'reports unknown base type');
  check(a.symbols.some(s => s.name === 'A' && s.kind === 'class'), 'recovers and still declares class A');
}

// --- packages & auto import -------------------------------------------------
console.log('pkg fixtures (auto import)');
{
  const pkgWs = new Workspace();
  const shapes = load(path.join(fixtures, 'pkg', 'shapes', 'shapes.nv'));
  const helper = load(path.join(fixtures, 'pkg', 'helper.nv'));
  const main = load(path.join(fixtures, 'pkg', 'main.nv'));
  pkgWs.analyze(shapes.uri, shapes.text);
  pkgWs.analyze(helper.uri, helper.text);
  let a = pkgWs.analyze(main.uri, main.text);
  const missing = a.diagnostics.filter(d => d.code === 'missing-import');
  const names = missing.map(d => (d.data as { name: string }).name).sort();
  check(errorsOf(a).length === 0, `main.nv has no syntax errors (typed local 'Shape s = …')${errorsOf(a).length ? ': ' + errorsOf(a).join(' | ') : ''}`);
  check(names.join(',') === 'Circle,Color,Shape,json', `missing-import diagnostics for Circle, Color, Shape, json (got ${names.join(',')})`);
  check(a.missingImports().join(',') === 'json,shapes', `missing packages are json + shapes (got ${a.missingImports().join(',')})`);
  check(!a.diagnostics.some(d => d.message.startsWith('Cannot find')), 'no plain "cannot find" warnings for importable names');
  const defs = definition(pkgWs, a, offsetOf(main.text, 'Circle{'));
  check(defs.length === 1 && defs[0].uri === shapes.uri, 'go to definition works before the import is added');
  const h = hover(pkgWs, a, offsetOf(main.text, 's.area()') + 2);
  check(!!h && JSON.stringify(h.contents).includes('area()'), 'hover resolves members of an unimported class');

  // quick fixes
  const actions = codeActions(a, offsetOf(main.text, 'Circle{'), offsetOf(main.text, 'Circle{') + 1);
  check(actions.some(x => x.title === "Import 'shapes'") && actions.some(x => x.title.startsWith('Add all missing imports')), `code actions offered (${actions.map(x => x.title).join(' / ')})`);
  const fixAll = actions.find(x => x.title.startsWith('Add all missing imports'))!;
  const edit = fixAll.edit!.changes![main.uri][0];
  check(edit.range.start.line === 0 && edit.newText === '\n\nimport json\nimport shapes', `fix-all inserts after the package line (${JSON.stringify(edit.newText)})`);
  const fixed = applyInsert(a, edit);
  a = pkgWs.analyze(main.uri, fixed);
  check(a.diagnostics.filter(d => d.severity === 'warning' || d.severity === 'error').length === 0, `after applying the fix there are no warnings${a.diagnostics.length ? ' (' + a.diagnostics.map(d => d.message).join(' | ') + ')' : ''}`);
  check(a.importNames.join(',') === 'json,shapes', 'imports are recorded');
  // a second fix appends after the last import
  const again = createImportEdit(a, ['http']);
  check(again.newText === '\nimport http' && again.range.start.line === a.lineMap.positionOf(offsetOf(fixed, 'import shapes') + 'import shapes'.length).line, 'further imports go after the last import line');

  // same package needs no import
  check(!a.diagnostics.some(d => d.message.includes('helper')), 'same-package symbols need no import');

  // completion offers auto-import items
  const original = pkgWs.analyze(main.uri, main.text);
  const items = complete(pkgWs, original, offsetOf(main.text, '  Shape s') + 2);
  const circle = items.find(i => i.label === 'Circle');
  check(!!circle && !!circle.additionalTextEdits && circle.additionalTextEdits[0].newText.includes('import shapes'), 'completion item Circle carries an auto-import edit');
  check(!!circle && circle.labelDetails?.description === 'import shapes', 'auto-import item is labelled with its package');
  const helperItem = items.find(i => i.label === 'helper');
  check(!!helperItem && !helperItem.additionalTextEdits, 'same-package helper() needs no import edit');
  const typeItems = complete(pkgWs, original, offsetOf(main.text, 'var c = Color') - 0);
  check(typeItems.some(i => i.label === 'Color' && i.additionalTextEdits), 'expression completion offers Color with import');
  const importItems = complete(pkgWs, pkgWs.analyze(main.uri, 'package app\nimport '), 'package app\nimport '.length);
  check(importItems.some(i => i.label === 'shapes') && importItems.some(i => i.label === 'json'), 'import completion lists workspace packages and builtin modules');
}

// --- object initializer suggestions ----------------------------------------
console.log('object initializers');
{
  const initWs = new Workspace();
  const shapes = load(path.join(fixtures, 'pkg', 'shapes', 'shapes.nv'));
  initWs.analyze(shapes.uri, shapes.text);
  const src = 'package shapes\n\nmethod main {\n  var c = Cir\n  var d = Circle{ }\n  var e = Circle{ radius=1.0, }\n}\n';
  const uri = 'file:///init.nv';
  let a = initWs.analyze(uri, src);
  const nameItems = complete(initWs, a, offsetOf(src, 'Cir') + 3);
  const initItem = nameItems.find(i => i.label === 'Circle{…}');
  check(!!initItem && initItem.filterText === 'Circle' && initItem.insertText === 'Circle{radius=${1}}', `class completion offers Circle{…} snippet (${initItem?.insertText})`);
  check(nameItems.some(i => i.label === 'Circle' && i.kind !== initItem?.kind), 'plain Circle item is still offered');
  const fieldItems = complete(initWs, a, offsetOf(src, 'Circle{ }') + 'Circle{ '.length);
  check(!!fieldItems.length && fieldItems.every(i => i.insertText?.endsWith('=')) && fieldItems.some(i => i.label === 'radius'), `inside Circle{ } the fields are suggested (${fieldItems.map(i => i.label).join(',')})`);
  const afterComma = complete(initWs, a, offsetOf(src, 'radius=1.0, ') + 'radius=1.0, '.length);
  check(!afterComma.some(i => i.label === 'radius'), 'already assigned fields are not suggested again');

  const multi = 'package shapes\n\nmethod main {\n  var p = Circle{\n    \n  }\n}\n';
  a = initWs.analyze(uri, multi);
  const multiItems = complete(initWs, a, offsetOf(multi, '    \n  }') + 4);
  check(multiItems.some(i => i.label === 'radius'), 'multi-line initializer suggests fields');

  const bodyStart = 'package shapes\n\nmethod main {\n  \n}\n';
  a = initWs.analyze(uri, bodyStart);
  const stmtItems = complete(initWs, a, offsetOf(bodyStart, '  \n}') + 2);
  check(stmtItems.some(i => i.label === 'var'), 'method body start is not mistaken for an initializer');

  const ann = 'package shapes\n\n@Deprecated{ }\nmethod old {\n}\n@Marker{ }\nmethod tagged {\n}\n';
  a = initWs.analyze(uri, ann);
  const annItems = complete(initWs, a, offsetOf(ann, '@Deprecated{ ') + '@Deprecated{ '.length);
  check(annItems.some(i => i.label === 'text') && annItems.some(i => i.label === 'since'), 'builtin annotation arguments are suggested');
  const markerItems = complete(initWs, a, offsetOf(ann, '@Marker{ ') + '@Marker{ '.length);
  check(markerItems.some(i => i.label === 'note'), 'user annotation arguments are suggested');

  const foreign = 'package app\n\nmethod main {\n  var c = Cir\n}\n';
  a = initWs.analyze(uri, foreign);
  const foreignItems = complete(initWs, a, offsetOf(foreign, 'Cir') + 3);
  const fi = foreignItems.find(i => i.label === 'Circle{…}');
  check(!!fi && !!fi.additionalTextEdits && fi.additionalTextEdits[0].newText.includes('import shapes'), 'Circle{…} from another package auto-imports');
}

// --- formatter --------------------------------------------------------------
console.log('formatter');
{
  const fmt = (t: string) => formatNovus(t, DEFAULT_FORMAT_OPTIONS);
  const tokensOf = (t: string) => {
    const l = lex(t);
    return [...l.tokens.filter(x => x.kind !== TokenKind.EOF).map(x => t.slice(x.start, x.end)), ...l.comments.map(c => c.text.trim())].join('');
  };
  for (const name of ['syntax.nv', 'test.nv']) {
    const file = path.join(repoTest, name);
    if (!fs.existsSync(file)) continue;
    const text = fs.readFileSync(file, 'utf8');
    const unit = /^( +)\S/m.exec(text)?.[1].length ?? 2; // follow the file's own indentation like the editor does
    const fmtFile = (t: string) => formatNovus(t, { ...DEFAULT_FORMAT_OPTIONS, tabSize: unit });
    const once = fmtFile(text);
    check(fmtFile(once) === once, `${name}: formatting is idempotent`);
    check(tokensOf(once) === tokensOf(text), `${name}: formatting only changes whitespace`);
    // compare non-blank lines (the formatter may drop blank lines directly inside braces)
    const orig = text.split('\n').filter(l => l.trim());
    const now = once.split('\n').filter(l => l.trim());
    const changed = now.filter((l, i) => l !== orig[i]).length + Math.abs(now.length - orig.length);
    check(changed <= 3, `${name}: already in canonical style (${changed} non-blank line(s) differ)`);
    if (changed > 3) {
      now.forEach((l, i) => { if (l !== orig[i]) console.log(`    ${JSON.stringify(orig[i])} -> ${JSON.stringify(l)}`); });
    }
  }
  const messy = 'package  x\nimport  json\n\n\n\nmethod   main{\n var x=Person{ name = "a" ,age=1 }\n if(x>1){println x}else{\nprintln  -1\n}\n\n}\nmethod b( integer a,integer b ):integer{\nreturn a+b\n}\n';
  const expected = 'package x\nimport json\n\nmethod main {\n  var x = Person{name="a", age=1}\n  if (x > 1) { println x } else {\n    println -1\n  }\n}\nmethod b(integer a, integer b): integer {\n  return a + b\n}\n';
  const got = fmt(messy);
  check(got === expected, `messy input is normalized${got === expected ? '' : '\n--- got ---\n' + got + '--- expected ---\n' + expected}`);

  const cases: [string, string][] = [
    ['define class A based B,C{\nprivate final array<Item>items:get,set\nabstract method buy():bool\n}\n', 'define class A based B, C {\n  private final array<Item> items: get, set\n  abstract method buy(): bool\n}\n'],
    ['method m {\nhttp.post("u",{\n"items":x,\n"sum":s\n})\n}\n', 'method m {\n  http.post("u", {\n    "items": x,\n    "sum": s\n  })\n}\n'],
    ['@Deprecated{\ntext="a",\nsince="1"\n}\nmethod m{\n}\n', '@Deprecated{\n  text="a",\n  since="1"\n}\nmethod m {\n}\n'],
    ['method m {\nvar p=Person{\nname="T",\nfriends=[]\n}\nfor(item in items){\nsum+item.price()\n}\nwhile(n>0){n=n-1}\n}\n', 'method m {\n  var p = Person{\n    name="T",\n    friends=[]\n  }\n  for (item in items) {\n    sum + item.price()\n  }\n  while (n > 0) { n = n - 1 }\n}\n'],
    ['method m {\nvar ok=a<b&&!done\nvar y=-x*(1+2)\nthis.text=text;\ntom.friends()\n.append(a,b)\n}\n', 'method m {\n  var ok = a < b && !done\n  var y = -x * (1 + 2)\n  this.text = text;\n  tom.friends()\n    .append(a, b)\n}\n'],
    ['method m { // trailing\n  /* block */ println "x"   // note\n  /**\n   * doc\n   */\n  var z = 1\n}\n', 'method m { // trailing\n  /* block */ println "x" // note\n  /**\n   * doc\n   */\n  var z = 1\n}\n'],
    ['define enum G{\nA("a"),\nB("b");\nprivate final str text:get\n}\n', 'define enum G {\n  A("a"),\n  B("b");\n  private final str text: get\n}\n'],
    ['method main {\n  println "Starting ${NAME} with args: ${args}"\n  Key key = Key{field="value"}\n}\n', 'method main {\n  println "Starting ${NAME} with args: ${args}"\n  Key key = Key{field="value"}\n}\n'],
    ['method m {\n  var s = "unterminated\n  println s\n}\n', 'method m {\n  var s = "unterminated\n  println s\n}\n'],
    ['method m {\n  if (a) {\n    x = 1\n  }\n  else {\n    x = 2\n  }\n}\n', 'method m {\n  if (a) {\n    x = 1\n  }\n  else {\n    x = 2\n  }\n}\n'],
  ];
  for (const [input, want] of cases) {
    const out = fmt(input);
    check(out === want, `format: ${JSON.stringify(input.slice(0, 40))}...${out === want ? '' : '\n--- got ---\n' + out + '--- expected ---\n' + want}`);
    check(fmt(out) === out, '  ...and idempotent');
  }

  const tabs = formatNovus('method m {\nprintln 1\n}\n', { ...DEFAULT_FORMAT_OPTIONS, insertSpaces: false });
  check(tabs === 'method m {\n\tprintln 1\n}\n', 'tab indentation honoured');
  const spaced = formatNovus('var x = K{a=1}\n', { ...DEFAULT_FORMAT_OPTIONS, namedArgumentSpacing: 'spaces' });
  check(spaced === 'var x = K{a = 1}\n', 'namedArgumentSpacing=spaces');

  // edits
  const src = 'method main {\nprintln 1\n}\n\n\n\nmethod b {\n  println 2\n}\n';
  const edits = fullFormatEdits(src, fmt(src), new LineMap(src));
  const lm = new LineMap(src);
  const applied = edits.length === 1 ? src.slice(0, lm.offsetOf(edits[0].range.start.line, edits[0].range.start.character)) + edits[0].newText + src.slice(lm.offsetOf(edits[0].range.end.line, edits[0].range.end.character)) : '';
  check(edits.length === 1 && applied === fmt(src) && edits[0].range.start.line === 1, `full format produces one minimal edit that reproduces the formatted text (lines ${edits[0]?.range.start.line}-${edits[0]?.range.end.line})`);
  const rangeEdits = rangeFormatEdits(src, formatLines(src, DEFAULT_FORMAT_OPTIONS), new LineMap(src), { start: { line: 1, character: 0 }, end: { line: 1, character: 0 } });
  check(rangeEdits.length === 1 && rangeEdits[0].newText === '  println 1' && rangeEdits[0].range.start.line === 1, 'range format only touches the selected line');
}

// --- user project layout (test/project) --------------------------------------
{
  const projectDir = path.join(repoTest, 'project');
  const mainFile = path.join(projectDir, 'main.nv');
  if (fs.existsSync(mainFile)) {
    console.log('test/project');
    const projWs = new Workspace();
    const walk = (dir: string): string[] => fs.readdirSync(dir, { withFileTypes: true }).flatMap(e => (e.isDirectory() ? walk(path.join(dir, e.name)) : e.name.endsWith('.nv') ? [path.join(dir, e.name)] : []));
    for (const f of walk(projectDir)) {
      const { uri, text } = load(f);
      projWs.analyze(uri, text);
    }
    const { uri, text } = load(mainFile);
    // The file may or may not already import `test` – test the variant without the import.
    const stripped = text.replace(/^\s*import\s+test\s*\n+/m, '');
    const a = projWs.analyze(uri, stripped);
    const miss = a.diagnostics.filter(d => d.code === 'missing-import');
    check(miss.length > 0 && (miss[0].data as { packages: string[] }).packages.includes('test'), `main.nv reports Key as importable from package 'test' (${miss.map(d => d.message).join(' | ')})`);
    const fixed = applyInsert(a, createImportEdit(a, a.missingImports()));
    const b = projWs.analyze(uri, fixed);
    check(!b.diagnostics.some(d => d.severity === 'warning'), `no warnings after import (${b.diagnostics.map(d => d.message).join(' | ')})`);
    check(fixed.startsWith('import test\n\nmethod main'), 'import inserted at the top of a package-less file');
    if (fixed.includes('Key{')) {
      const keyInit = complete(projWs, b, fixed.indexOf('Key{') + 'Key{'.length);
      check(keyInit.some(i => i.label === 'field' && i.insertText === 'field='), `Key{ suggests its field (${keyInit.map(i => i.label).join(',')})`);
      const keyName = complete(projWs, b, fixed.indexOf('Key{') + 'Key'.length);
      check(keyName.some(i => i.label === 'Key{…}' && i.insertText === 'Key{field=${1}}'), 'Key completion offers the Key{…} initializer snippet');
    }
  }
}

console.log(`\n${checks - failures}/${checks} checks passed`);
process.exit(failures ? 1 : 0);
