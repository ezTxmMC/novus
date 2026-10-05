/** Finds the `novus-lsp` binary: the `novus.server.path` setting, then `build/novus-lsp` of the workspace, then PATH. */
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';

export const SERVER_NAME = 'novus-lsp';

export interface ServerLocation {
  command: string;
  found: boolean;
  /** Where the command came from, for the log: `setting`, `workspace` or `PATH`. */
  source: 'setting' | 'workspace' | 'PATH';
}

function exeSuffix(): string {
  return process.platform === 'win32' ? '.exe' : '';
}

function isFile(file: string): boolean {
  try {
    return fs.statSync(file).isFile();
  } catch {
    return false;
  }
}

function workspaceRoots(): string[] {
  return (vscode.workspace.workspaceFolders ?? []).map(folder => folder.uri.fsPath);
}

function findOnPath(command: string): string | undefined {
  const directories = (process.env.PATH ?? '').split(path.delimiter).filter(entry => entry !== '');
  for (const directory of directories) {
    const candidate = path.join(directory, command);
    if (isFile(candidate)) return candidate;
  }
  return undefined;
}

/** A configured path is absolute, relative to a workspace folder, or a bare command name that PATH resolves. */
function fromSetting(configured: string): ServerLocation {
  if (path.isAbsolute(configured)) {
    return { command: configured, found: isFile(configured), source: 'setting' };
  }
  for (const root of workspaceRoots()) {
    const candidate = path.join(root, configured);
    if (isFile(candidate)) return { command: candidate, found: true, source: 'setting' };
  }
  const onPath = path.basename(configured) === configured ? findOnPath(configured) : undefined;
  return { command: onPath ?? configured, found: onPath !== undefined, source: 'setting' };
}

export function locateServer(): ServerLocation {
  const configured = vscode.workspace.getConfiguration('novus').get<string>('server.path', '').trim();
  if (configured !== '') return fromSetting(configured);

  for (const root of workspaceRoots()) {
    const candidate = path.join(root, 'build', SERVER_NAME + exeSuffix());
    if (isFile(candidate)) return { command: candidate, found: true, source: 'workspace' };
  }
  const onPath = findOnPath(SERVER_NAME + exeSuffix());
  return { command: onPath ?? SERVER_NAME + exeSuffix(), found: onPath !== undefined, source: 'PATH' };
}
