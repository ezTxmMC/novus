/** Keeps analyses of open documents and an index of every .nv file in the workspace. */
import * as fs from 'fs';
import * as path from 'path';
import { URI } from 'vscode-uri';
import { Analysis } from './analyzer';
import { builtinModules } from './builtins';
import { NSymbol } from './symbols';

export interface Settings {
  diagnostics: {
    enabled: boolean;
    undefinedSymbols: boolean;
    unusedVariables: boolean;
  };
  format: {
    namedArgumentSpacing: 'none' | 'spaces';
    maxBlankLines: number;
  };
}

export const DEFAULT_SETTINGS: Settings = {
  diagnostics: { enabled: true, undefinedSymbols: true, unusedVariables: true },
  format: { namedArgumentSpacing: 'none', maxBlankLines: 1 },
};

const IGNORED_DIRS = new Set(['node_modules', '.git', 'build', 'out', 'dist', 'bin', '.vscode', 'vcpkg_installed']);
const MAX_FILE_SIZE = 2 * 1024 * 1024;

export class Workspace {
  private readonly analyses = new Map<string, Analysis>();
  private readonly openUris = new Set<string>();
  settings: Settings = DEFAULT_SETTINGS;
  readonly builtins: NSymbol[] = builtinModules();

  analyze(uri: string, text: string): Analysis {
    const analysis = new Analysis(uri, text, {
      globalLookup: (name, excludeUri) => this.globalLookup(name, excludeUri),
      packageOf: fileUri => this.packageOf(fileUri),
      builtinModule: name => this.builtinModule(name),
      undefinedSymbols: this.settings.diagnostics.undefinedSymbols,
      unusedVariables: this.settings.diagnostics.unusedVariables,
    });
    this.analyses.set(uri, analysis);
    return analysis;
  }

  get(uri: string): Analysis | undefined {
    return this.analyses.get(uri);
  }

  all(): Analysis[] {
    return [...this.analyses.values()];
  }

  markOpen(uri: string, open: boolean): void {
    if (open) this.openUris.add(uri);
    else this.openUris.delete(uri);
  }

  isOpen(uri: string): boolean {
    return this.openUris.has(uri);
  }

  remove(uri: string): void {
    this.analyses.delete(uri);
  }

  /** Top-level symbols of other files (imported or not). */
  globalLookup(name: string, excludeUri: string): NSymbol[] {
    const out: NSymbol[] = [];
    for (const [uri, analysis] of this.analyses) {
      if (uri === excludeUri) continue;
      const found = analysis.moduleScope.symbols.get(name);
      if (found) for (const s of found) if (s.kind !== 'module') out.push(s);
    }
    return out;
  }

  builtinModule(name: string): NSymbol | undefined {
    return this.builtins.find(b => b.kind === 'module' && b.name === name);
  }

  /** Package declared by the file at `uri` ('' when unknown or absent). */
  packageOf(uri: string): string {
    return this.analyses.get(uri)?.packageName ?? '';
  }

  /** All package names declared in the workspace. */
  packages(excludeUri?: string): string[] {
    const out = new Set<string>();
    for (const [uri, analysis] of this.analyses) {
      if (uri === excludeUri) continue;
      if (analysis.packageName) out.add(analysis.packageName);
    }
    return [...out].sort();
  }

  globalSymbols(excludeUri: string): NSymbol[] {
    const out: NSymbol[] = [];
    for (const [uri, analysis] of this.analyses) {
      if (uri === excludeUri) continue;
      for (const list of analysis.moduleScope.symbols.values()) {
        for (const s of list) if (s.kind !== 'module') out.push(s);
      }
    }
    out.push(...this.builtins);
    return out;
  }

  findSymbol(id: string): NSymbol | undefined {
    for (const analysis of this.analyses.values()) {
      const s = analysis.symbolById(id);
      if (s) return s;
    }
    return this.builtins.find(b => b.id === id);
  }

  /** Indexes every .nv file below the given folders (skipping open documents). */
  async scanFolders(folderUris: string[]): Promise<number> {
    let count = 0;
    for (const folderUri of folderUris) {
      let root: string;
      try {
        root = URI.parse(folderUri).fsPath;
      } catch {
        continue;
      }
      for (const file of walkDir(root)) {
        const uri = URI.file(file).toString();
        if (this.openUris.has(uri)) continue;
        if (this.indexFile(uri, file)) count++;
      }
    }
    return count;
  }

  indexFile(uri: string, fsPath?: string): boolean {
    try {
      const file = fsPath ?? URI.parse(uri).fsPath;
      const stat = fs.statSync(file);
      if (!stat.isFile() || stat.size > MAX_FILE_SIZE) return false;
      const text = fs.readFileSync(file, 'utf8');
      this.analyze(uri, text);
      return true;
    } catch {
      return false;
    }
  }

  /** Re-analyzes every non-open file so cross-file references pick up changes. */
  refreshIndexed(): void {
    for (const [uri, analysis] of [...this.analyses]) {
      if (this.openUris.has(uri)) continue;
      this.analyze(uri, analysis.text);
    }
  }
}

function* walkDir(dir: string, depth = 0): Generator<string> {
  if (depth > 12) return;
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const entry of entries) {
    if (entry.isDirectory()) {
      if (IGNORED_DIRS.has(entry.name) || entry.name.startsWith('.')) continue;
      yield* walkDir(path.join(dir, entry.name), depth + 1);
    } else if (entry.isFile() && entry.name.endsWith('.nv')) {
      yield path.join(dir, entry.name);
    }
  }
}
