/** Computes the text edit that adds `import` statements to a document. */
import { TextEdit } from 'vscode-languageserver/node';
import { Analysis } from './analyzer';

/** Returns a single edit inserting `import <pkg>` lines for every package not imported yet. */
export function createImportEdit(analysis: Analysis, packages: string[]): TextEdit {
  const needed = [...new Set(packages)].filter(p => p && !analysis.importNames.includes(p)).sort();
  const lines = needed.map(p => `import ${p}`);
  const items = analysis.program.items;
  const imports = items.filter(i => i.kind === 'Import');
  const pkg = items.find(i => i.kind === 'Package');

  if (!lines.length) {
    return TextEdit.insert({ line: 0, character: 0 }, '');
  }
  if (imports.length) {
    const last = imports[imports.length - 1];
    return TextEdit.insert(analysis.lineMap.positionOf(last.end), '\n' + lines.join('\n'));
  }
  if (pkg) {
    return TextEdit.insert(analysis.lineMap.positionOf(pkg.end), '\n\n' + lines.join('\n'));
  }
  const leading = analysis.text.length ? '\n\n' : '\n';
  return TextEdit.insert({ line: 0, character: 0 }, lines.join('\n') + leading);
}

/** Applies `edit` (an insertion) to `text` – used by tests. */
export function applyInsert(analysis: Analysis, edit: TextEdit): string {
  const offset = analysis.lineMap.offsetOf(edit.range.start.line, edit.range.start.character);
  return analysis.text.slice(0, offset) + edit.newText + analysis.text.slice(offset);
}
