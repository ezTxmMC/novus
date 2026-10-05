/** VS Code client for the Novus language: starts `novus-lsp` (or the TypeScript server) and provides the run command. */
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions, TransportKind } from 'vscode-languageclient/node';
import { locateServer } from './launcher';

let client: LanguageClient | undefined;
// Watchers belong to the client that was created with them: a restart must not leave the old ones running.
let watchers: vscode.Disposable[] = [];
let outputChannel: vscode.LogOutputChannel | undefined;

type Implementation = 'lsp' | 'typescript';

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  outputChannel = vscode.window.createOutputChannel('Novus Language Server', { log: true });
  context.subscriptions.push(outputChannel);

  context.subscriptions.push(
    vscode.commands.registerCommand('novus.runFile', () => executeCurrentFile('run')),
    vscode.commands.registerCommand('novus.buildFile', () => executeCurrentFile('build')),
    vscode.commands.registerCommand('novus.restartServer', async () => {
      await restartClient(context);
      vscode.window.setStatusBarMessage('Novus language server restarted', 3000);
    }),
    vscode.workspace.onDidChangeConfiguration(async event => {
      if (event.affectsConfiguration('novus.server')) await restartClient(context);
    }),
  );

  await startClient(context);
}

export async function deactivate(): Promise<void> {
  await stopClient();
}

async function stopClient(): Promise<void> {
  const running = client;
  client = undefined;
  watchers.forEach(watcher => watcher.dispose());
  watchers = [];
  if (running) await running.stop();
}

function watchFiles(glob: string): vscode.FileSystemWatcher {
  const watcher = vscode.workspace.createFileSystemWatcher(glob);
  watchers.push(watcher);
  return watcher;
}

async function restartClient(context: vscode.ExtensionContext): Promise<void> {
  await stopClient();
  await startClient(context);
}

async function startClient(context: vscode.ExtensionContext): Promise<void> {
  const created = createClient(context);
  if (!created) return;
  client = created;
  try {
    await created.start();
  } catch (err) {
    client = undefined;
    void vscode.window.showErrorMessage(`Failed to start the Novus language server: ${String(err)}`);
  }
}

function implementation(): Implementation {
  const chosen = vscode.workspace.getConfiguration('novus').get<string>('server.implementation', 'lsp');
  return chosen === 'typescript' ? 'typescript' : 'lsp';
}

function createClient(context: vscode.ExtensionContext): LanguageClient | undefined {
  if (implementation() === 'typescript') return createTypeScriptClient(context);
  return createNativeClient();
}

/** `novus-lsp`: the language server written in Novus, spoken to over stdio. */
function createNativeClient(): LanguageClient | undefined {
  const server = locateServer();
  if (!server.found) {
    void offerFallback(server.command);
    return undefined;
  }
  outputChannel?.info(`Starting ${server.command} (from ${server.source})`);
  const serverOptions: ServerOptions = {
    command: server.command,
    args: ['--stdio'],
    transport: TransportKind.stdio,
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      { scheme: 'file', language: 'novus' },
      { scheme: 'file', language: 'novus-html' },
      { scheme: 'untitled', language: 'novus' },
    ],
    synchronize: {
      fileEvents: watchFiles('**/*.{nv,nvh}'),
      configurationSection: 'novus',
    },
    initializationOptions: { novus: JSON.parse(JSON.stringify(vscode.workspace.getConfiguration('novus'))) },
    outputChannel,
  };
  return new LanguageClient('novus', 'Novus Language Server', serverOptions, clientOptions);
}

async function offerFallback(command: string): Promise<void> {
  const choice = await vscode.window.showErrorMessage(
    `The Novus language server '${command}' was not found. Build it with 'make lsp', put it on PATH or set 'novus.server.path'.`,
    'Open settings',
    'Use the TypeScript server',
  );
  if (choice === 'Open settings') {
    void vscode.commands.executeCommand('workbench.action.openSettings', 'novus.server.path');
  }
  if (choice === 'Use the TypeScript server') {
    await vscode.workspace.getConfiguration('novus').update('server.implementation', 'typescript', vscode.ConfigurationTarget.Global);
  }
}

/** The previous server (TypeScript), kept until the native one has the same features. */
function createTypeScriptClient(context: vscode.ExtensionContext): LanguageClient {
  const serverModule = context.asAbsolutePath(path.join('out', 'server', 'server.js'));
  const serverOptions: ServerOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ['--nolazy', '--inspect=6009'] } },
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      { scheme: 'file', language: 'novus' },
      { scheme: 'untitled', language: 'novus' },
    ],
    synchronize: {
      fileEvents: watchFiles('**/*.nv'),
      configurationSection: 'novus',
    },
    outputChannel,
  };
  return new LanguageClient('novus', 'Novus Language Server', serverOptions, clientOptions);
}

// ------------------------------------------------------------------- run

const CANDIDATES = ['build/novusc', 'novusc', 'dist/novusc'];

function exists(file: string): boolean {
  try {
    return fs.statSync(file).isFile();
  } catch {
    return false;
  }
}

function resolveExecutable(document: vscode.TextDocument): { command: string; found: boolean } {
  const exe = process.platform === 'win32' ? '.exe' : '';
  const configured = vscode.workspace.getConfiguration('novus', document).get<string>('executablePath', '').trim();
  const folders = vscode.workspace.workspaceFolders ?? [];
  const docFolder = vscode.workspace.getWorkspaceFolder(document.uri);
  const roots = [...(docFolder ? [docFolder] : []), ...folders.filter(f => f !== docFolder)].map(f => f.uri.fsPath);

  if (configured) {
    if (path.isAbsolute(configured)) return { command: configured, found: exists(configured) };
    for (const root of roots) {
      const candidate = path.join(root, configured);
      if (exists(candidate)) return { command: candidate, found: true };
    }
    return { command: configured, found: false };
  }

  for (const root of roots) {
    for (const rel of CANDIDATES) {
      const candidate = path.join(root, rel + exe);
      if (exists(candidate)) return { command: candidate, found: true };
    }
  }
  return { command: 'novusc' + exe, found: false };
}

async function executeCurrentFile(mode: 'run' | 'build'): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor || (editor.document.languageId !== 'novus' && editor.document.languageId !== 'novus-html')) {
    void vscode.window.showInformationMessage('Open a Novus (.nv) file or a component (.nvh) to run it.');
    return;
  }
  const document = editor.document;
  if (document.isUntitled) {
    void vscode.window.showInformationMessage('Save the file before running it.');
    return;
  }
  if (document.isDirty) await document.save();

  const { command, found } = resolveExecutable(document);
  if (!found && path.isAbsolute(command)) {
    void vscode.window.showErrorMessage(`novusc not found at '${command}'. Check the 'novus.executablePath' setting.`);
    return;
  }
  if (!found) {
    const choice = await vscode.window.showWarningMessage(
      "No 'build/novusc' binary found in the workspace – running 'novusc' from PATH. Build the compiler (scripts/bootstrap.sh) or set 'novus.executablePath'.",
      'Run anyway',
      'Open settings',
    );
    if (choice === 'Open settings') {
      void vscode.commands.executeCommand('workbench.action.openSettings', 'novus.executablePath');
      return;
    }
    if (choice !== 'Run anyway') return;
  }

  const cwd = vscode.workspace.getWorkspaceFolder(document.uri)?.uri.fsPath ?? path.dirname(document.uri.fsPath);
  const terminal = vscode.window.terminals.find(t => t.name === 'Novus') ?? vscode.window.createTerminal({ name: 'Novus', cwd });
  terminal.show(true);
  if (mode === 'build') {
    const output = document.uri.fsPath.replace(/\.nvh?$/, '');
    terminal.sendText(`${quote(command)} build ${quote(document.uri.fsPath)} -o ${quote(output)}`);
  } else {
    terminal.sendText(`${quote(command)} run ${quote(document.uri.fsPath)}`);
  }
}

function quote(value: string): string {
  if (/^[\w./\\:-]+$/.test(value)) return value;
  return process.platform === 'win32' ? `"${value.replace(/"/g, '\\"')}"` : `'${value.replace(/'/g, "'\\''")}'`;
}
