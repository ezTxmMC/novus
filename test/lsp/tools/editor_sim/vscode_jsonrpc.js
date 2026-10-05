// Drives novus-lsp through the real vscode-jsonrpc / vscode-languageserver-protocol of the extension (the transport
// that VS Code uses): node vscode_jsonrpc.js <novus-lsp> <workspace> ; exits 1 on any failed check.
const path = require('path');
const cp = require('child_process');
const modules = path.resolve(__dirname, '..', '..', '..', '..', 'vscode-novus', 'node_modules');
const protocol = require(path.join(modules, 'vscode-languageserver-protocol', 'lib', 'node', 'main.js'));
const { pathToFileURL } = require('url');

const [binary, root] = process.argv.slice(2);
const failures = [];
const check = (ok, message) => { if (!ok) failures.push(message); };
const server = cp.spawn(binary, ['--stdio'], { cwd: root, stdio: ['pipe', 'pipe', 'inherit'] });
const connection = protocol.createProtocolConnection(
  new protocol.StreamMessageReader(server.stdout), new protocol.StreamMessageWriter(server.stdin));
const serverRequests = [];
const diagnostics = [];
connection.onRequest('workspace/configuration', (params) => { serverRequests.push(['configuration', params]); return params.items.map(() => ({ check: { mode: 'off' } })); });
connection.onRequest('client/registerCapability', (params) => { serverRequests.push(['register', params]); return null; });
connection.onNotification(protocol.PublishDiagnosticsNotification.type, (p) => diagnostics.push(p));
connection.onNotification('window/logMessage', () => {});
connection.onNotification('window/showMessage', () => {});
connection.onError((e) => failures.push('connection error ' + e));
connection.listen();

const rootUri = pathToFileURL(root).toString();
const mainUri = rootUri + '/main.nv';
const text = require('fs').readFileSync(path.join(root, 'main.nv'), 'utf8');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function main() {
  const init = await connection.sendRequest(protocol.InitializeRequest.type, {
    processId: process.pid, rootUri, workspaceFolders: [{ uri: rootUri, name: 'ws' }],
    clientInfo: { name: 'Visual Studio Code', version: '1.139.1' },
    capabilities: { general: { positionEncodings: ['utf-16'] }, workspace: { configuration: true, didChangeWatchedFiles: { dynamicRegistration: true } },
      textDocument: { completion: { completionItem: { snippetSupport: true } }, hover: { contentFormat: ['markdown', 'plaintext'] } } },
  });
  check(init.capabilities.positionEncoding === 'utf-16', 'positionEncoding');
  check(init.serverInfo.name === 'novus-lsp', 'serverInfo');
  await connection.sendNotification(protocol.InitializedNotification.type, {});
  await connection.sendNotification(protocol.DidOpenTextDocumentNotification.type, { textDocument: { uri: mainUri, languageId: 'novus', version: 1, text } });
  const hover = await connection.sendRequest(protocol.HoverRequest.type, { textDocument: { uri: mainUri }, position: { line: 5, character: 10 } });
  check(hover !== undefined, 'hover through the real jsonrpc');
  const completion = await connection.sendRequest(protocol.CompletionRequest.type, { textDocument: { uri: mainUri }, position: { line: 5, character: 17 }, context: { triggerKind: 1 } });
  const items = Array.isArray(completion) ? completion : completion.items;
  check(items.some((i) => i.label === 'Shape'), 'completion lists Shape');
  const tokens = await connection.sendRequest(protocol.SemanticTokensRequest.type, { textDocument: { uri: mainUri } });
  check(tokens.data.length > 0 && tokens.data.length % 5 === 0, 'semantic tokens');
  await sleep(600);
  check(serverRequests.some((r) => r[0] === 'configuration'), 'workspace/configuration reached the client library');
  check(serverRequests.some((r) => r[0] === 'register'), 'registerCapability reached the client library');
  check(diagnostics.some((d) => d.uri === mainUri), 'publishDiagnostics reached the client library');
  const rejected = await connection.sendRequest('textDocument/nothing', {}).then(() => false, (e) => e.code === -32601);
  check(rejected, 'unknown request rejected with -32601');
  await connection.sendRequest(protocol.ShutdownRequest.type);
  await connection.sendNotification(protocol.ExitNotification.type);
  const code = await new Promise((resolve) => server.on('exit', (c) => resolve(c)));
  check(code === 0, 'exit code ' + code);
}
const timer = setTimeout(() => { console.log('FAIL timeout'); server.kill('SIGKILL'); process.exit(1); }, 30000);
main().then(() => { clearTimeout(timer); console.log(failures.length ? 'FAIL ' + failures.join('; ') : 'ok vscode-jsonrpc session'); process.exit(failures.length ? 1 : 0); },
  (e) => { console.log('FAIL exception ' + (e && e.stack || e)); process.exit(1); });
