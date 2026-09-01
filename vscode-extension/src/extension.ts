/** VS Code client for the Novus language: starts the language server and provides the run command. */
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions, TransportKind } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let outputChannel: vscode.LogOutputChannel | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  outputChannel = vscode.window.createOutputChannel('Novus Language Server', { log: true });
  context.subscriptions.push(outputChannel);

  context.subscriptions.push(
    vscode.commands.registerCommand('novus.runFile', () => executeCurrentFile('run')),
    vscode.commands.registerCommand('novus.buildFile', () => executeCurrentFile('build')),
    vscode.commands.registerCommand('novus.restartServer', async () => {
      if (client) {
        await client.restart();
        vscode.window.setStatusBarMessage('Novus language server restarted', 3000);
      }
    }),
  );

  client = createClient(context);
  try {
    await client.start();
  } catch (err) {
    void vscode.window.showErrorMessage(`Failed to start the Novus language server: ${String(err)}`);
  }
}

export async function deactivate(): Promise<void> {
  if (client) {
    await client.stop();
    client = undefined;
  }
}

function createClient(context: vscode.ExtensionContext): LanguageClient {
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
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.nv'),
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
  if (!editor || editor.document.languageId !== 'novus') {
    void vscode.window.showInformationMessage('Open a Novus (.nv) file to run it.');
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
    const output = document.uri.fsPath.replace(/\.nv$/, '');
    terminal.sendText(`${quote(command)} build ${quote(document.uri.fsPath)} -o ${quote(output)}`);
  } else {
    terminal.sendText(`${quote(command)} run ${quote(document.uri.fsPath)}`);
  }
}

function quote(value: string): string {
  if (/^[\w./\\:-]+$/.test(value)) return value;
  return process.platform === 'win32' ? `"${value.replace(/"/g, '\\"')}"` : `'${value.replace(/'/g, "'\\''")}'`;
}
