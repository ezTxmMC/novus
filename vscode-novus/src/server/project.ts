/**
 * project.nv manifests: parsing, lookup of the nearest manifest and
 * resolution of module imports (`import "github.com/user/lib"`) to files
 * in the dependency cache - mirrors compiler/project/ of novusc.
 */
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';

export interface ManifestDiagnostic {
  line: number;
  message: string;
}

export interface Manifest {
  file: string;
  dir: string;
  name: string;
  main: string;
  lib: string;
  requires: { module: string; version: string }[];
  replaces: Map<string, string>;
  diagnostics: ManifestDiagnostic[];
}

export const MANIFEST_KEYS = ['project', 'version', 'main', 'lib', 'output', 'novus', 'require', 'replace'];

export function isManifestUri(uri: string): boolean {
  return /(^|[\\/])project\.nv$/.test(uri);
}

/** Parses the line based manifest syntax: `key "value" ["value"]`. */
export function parseManifestText(file: string, text: string): Manifest {
  const m: Manifest = { file, dir: path.dirname(file), name: '', main: 'main.nv', lib: '', requires: [], replaces: new Map(), diagnostics: [] };
  const lines = text.split(/\r?\n/);
  lines.forEach((raw, index) => {
    const line = raw.replace(/\/\/.*$/, '').trim();
    if (!line) return;
    const match = /^([A-Za-z_]\w*)\s*(.*)$/.exec(line);
    if (!match) {
      m.diagnostics.push({ line: index, message: 'Expected a setting such as project, version, main, require or replace' });
      return;
    }
    const key = match[1];
    const values: string[] = [];
    const rest = match[2];
    const valueRe = /"((?:\\.|[^"\\])*)"/g;
    let v: RegExpExecArray | null;
    let consumed = 0;
    while ((v = valueRe.exec(rest))) {
      values.push(v[1]);
      consumed = valueRe.lastIndex;
    }
    if (rest.slice(consumed).trim() || (rest.trim() && values.length === 0)) {
      m.diagnostics.push({ line: index, message: 'Values in project.nv must be quoted strings' });
      return;
    }
    if (!MANIFEST_KEYS.includes(key)) {
      m.diagnostics.push({ line: index, message: `Unknown setting '${key}' (known: ${MANIFEST_KEYS.join(', ')})` });
      return;
    }
    if (key === 'require') {
      if (values.length < 1 || values.length > 2) {
        m.diagnostics.push({ line: index, message: 'require needs a module path and an optional version' });
        return;
      }
      m.requires.push({ module: values[0], version: values[1] ?? 'latest' });
      return;
    }
    if (key === 'replace') {
      if (values.length !== 2) {
        m.diagnostics.push({ line: index, message: 'replace needs a module path and a local directory' });
        return;
      }
      m.replaces.set(values[0], values[1]);
      return;
    }
    if (values.length !== 1) {
      m.diagnostics.push({ line: index, message: `'${key}' takes exactly one value` });
      return;
    }
    if (key === 'project') m.name = values[0];
    if (key === 'main') m.main = values[0];
    if (key === 'lib') m.lib = values[0];
  });
  return m;
}

export function readManifest(file: string): Manifest | undefined {
  try {
    return parseManifestText(file, fs.readFileSync(file, 'utf8'));
  } catch {
    return undefined;
  }
}

/** The nearest project.nv in `dir` or one of its parents. */
export function findManifest(dir: string): string | undefined {
  let current = path.resolve(dir);
  for (let i = 0; i < 64; i++) {
    const candidate = path.join(current, 'project.nv');
    if (fs.existsSync(candidate)) return candidate;
    const parent = path.dirname(current);
    if (parent === current) return undefined;
    current = parent;
  }
  return undefined;
}

export function depsCacheDir(): string {
  return process.env.NOVUS_DEPS || path.join(os.homedir(), '.novus', 'deps');
}

function moduleKey(module: string): string {
  const at = module.indexOf('://');
  let key = at >= 0 ? module.slice(at + 3) : module;
  while (key.startsWith('/')) key = key.slice(1);
  return key;
}

/** Directory of a dependency: a `replace` target or the cache entry (if fetched). */
export function dependencyDir(manifest: Manifest, module: string, version: string): string | undefined {
  const replaced = manifest.replaces.get(module);
  if (replaced) return path.resolve(manifest.dir, replaced);
  const cached = path.join(depsCacheDir(), `${moduleKey(module)}@${version}`);
  return fs.existsSync(cached) ? cached : undefined;
}

/** Entry file of a dependency directory (its project.nv `lib`/`main`, else lib.nv/main.nv). */
export function moduleEntry(dir: string): string {
  const manifest = readManifest(path.join(dir, 'project.nv'));
  if (manifest) return path.join(dir, manifest.lib || manifest.main);
  if (fs.existsSync(path.join(dir, 'lib.nv'))) return path.join(dir, 'lib.nv');
  return path.join(dir, 'main.nv');
}

/**
 * Resolves a module import (`github.com/user/lib` or `github.com/user/lib/sub/file.nv`)
 * for a file at `fromFile`; returns the target file and the dependency root, or undefined
 * when the import is not a module of the enclosing project (or it is not fetched yet).
 */
export function resolveModuleImport(fromFile: string, rel: string): { file: string; root: string } | undefined {
  const manifestFile = findManifest(path.dirname(fromFile));
  if (!manifestFile) return undefined;
  const seen = new Set<string>();
  const queue = [manifestFile];
  while (queue.length) {
    const current = readManifest(queue.shift()!);
    if (!current || seen.has(current.file)) continue;
    seen.add(current.file);
    for (const dep of current.requires) {
      if (rel !== dep.module && !rel.startsWith(dep.module + '/')) {
        const dir = dependencyDir(current, dep.module, dep.version);
        if (dir) queue.push(path.join(dir, 'project.nv'));
        continue;
      }
      const dir = dependencyDir(current, dep.module, dep.version);
      if (!dir) return undefined;
      if (rel === dep.module) return { file: moduleEntry(dir), root: dir };
      return { file: path.join(dir, rel.slice(dep.module.length + 1)), root: dir };
    }
  }
  return undefined;
}
