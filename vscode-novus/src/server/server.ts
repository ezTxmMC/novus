/** Novus language server entry point. */
import {
  CodeActionKind,
  CompletionItem,
  DidChangeConfigurationNotification,
  FileChangeType,
  InitializeParams,
  InitializeResult,
  ProposedFeatures,
  TextDocumentSyncKind,
  TextDocuments,
  createConnection,
} from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { Analysis } from './analyzer';
import { complete } from './completion';
import {
  TOKEN_MODIFIERS,
  TOKEN_TYPES,
  codeActions,
  definition,
  documentHighlights,
  documentSymbols,
  foldingRanges,
  hover,
  prepareRename,
  references,
  rename,
  semanticTokens,
  signatureHelp,
  toDiagnostic,
  workspaceSymbols,
} from './features';
import { formatLines, formatNovus, fullFormatEdits, rangeFormatEdits } from './formatter';
import { DEFAULT_SETTINGS, Settings, Workspace } from './workspace';

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const ws = new Workspace();

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;

connection.onInitialize((params: InitializeParams): InitializeResult => {
  const caps = params.capabilities;
  hasConfigurationCapability = !!caps.workspace?.configuration;
  hasWorkspaceFolderCapability = !!caps.workspace?.workspaceFolders;

  const result: InitializeResult = {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: { resolveProvider: false, triggerCharacters: ['.', '@', ':', '<', '(', ','] },
      hoverProvider: true,
      definitionProvider: true,
      referencesProvider: true,
      documentHighlightProvider: true,
      renameProvider: { prepareProvider: true },
      documentSymbolProvider: { label: 'Novus' },
      workspaceSymbolProvider: true,
      signatureHelpProvider: { triggerCharacters: ['(', ','], retriggerCharacters: [','] },
      foldingRangeProvider: true,
      codeActionProvider: { codeActionKinds: [CodeActionKind.QuickFix, CodeActionKind.SourceFixAll] },
      documentFormattingProvider: true,
      documentRangeFormattingProvider: true,
      semanticTokensProvider: {
        legend: { tokenTypes: TOKEN_TYPES, tokenModifiers: TOKEN_MODIFIERS },
        full: true,
        range: false,
      },
    },
    serverInfo: { name: 'Novus Language Server', version: '0.1.0' },
  };
  if (hasWorkspaceFolderCapability) {
    result.capabilities.workspace = { workspaceFolders: { supported: true, changeNotifications: true } };
  }
  return result;
});

connection.onInitialized(async () => {
  if (hasConfigurationCapability) {
    await connection.client.register(DidChangeConfigurationNotification.type, undefined);
  }
  await refreshSettings();
  if (hasWorkspaceFolderCapability) {
    try {
      const folders = (await connection.workspace.getWorkspaceFolders()) ?? [];
      await indexFolders(folders.map(f => f.uri));
      connection.workspace.onDidChangeWorkspaceFolders(async event => {
        await indexFolders(event.added.map(f => f.uri));
        revalidateAll();
      });
    } catch (err) {
      connection.console.warn(`Novus: workspace folders unavailable: ${String(err)}`);
    }
  }
  revalidateAll();
});

async function indexFolders(uris: string[]): Promise<void> {
  try {
    const count = await ws.scanFolders(uris);
    connection.console.log(`Novus: indexed ${count} file(s) in ${uris.length} workspace folder(s)`);
  } catch (err) {
    connection.console.error(`Novus: failed to index workspace: ${String(err)}`);
  }
}

async function refreshSettings(): Promise<void> {
  if (!hasConfigurationCapability) return;
  try {
    const cfg = (await connection.workspace.getConfiguration('novus')) as Partial<Settings> | null;
    ws.settings = {
      diagnostics: {
        enabled: cfg?.diagnostics?.enabled ?? DEFAULT_SETTINGS.diagnostics.enabled,
        undefinedSymbols: cfg?.diagnostics?.undefinedSymbols ?? DEFAULT_SETTINGS.diagnostics.undefinedSymbols,
        unusedVariables: cfg?.diagnostics?.unusedVariables ?? DEFAULT_SETTINGS.diagnostics.unusedVariables,
      },
      format: {
        namedArgumentSpacing: cfg?.format?.namedArgumentSpacing === 'spaces' ? 'spaces' : 'none',
        maxBlankLines: typeof cfg?.format?.maxBlankLines === 'number' ? Math.max(0, cfg.format.maxBlankLines) : DEFAULT_SETTINGS.format.maxBlankLines,
      },
    };
  } catch {
    ws.settings = DEFAULT_SETTINGS;
  }
}

connection.onDidChangeConfiguration(async () => {
  await refreshSettings();
  ws.refreshIndexed();
  revalidateAll();
});

// ------------------------------------------------------------- documents

/** Returns an analysis that matches the current document text. */
function analysisFor(doc: TextDocument): Analysis {
  const existing = ws.get(doc.uri);
  if (existing && existing.text === doc.getText()) return existing;
  return ws.analyze(doc.uri, doc.getText());
}

function analysisForUri(uri: string): Analysis | undefined {
  const doc = documents.get(uri);
  if (doc) return analysisFor(doc);
  return ws.get(uri);
}

const pendingValidation = new Map<string, NodeJS.Timeout>();

function scheduleValidation(doc: TextDocument, delay = 150): void {
  const existing = pendingValidation.get(doc.uri);
  if (existing) clearTimeout(existing);
  pendingValidation.set(
    doc.uri,
    setTimeout(() => {
      pendingValidation.delete(doc.uri);
      validate(doc);
    }, delay),
  );
}

function validate(doc: TextDocument): void {
  const analysis = analysisFor(doc);
  if (!ws.settings.diagnostics.enabled) {
    connection.sendDiagnostics({ uri: doc.uri, diagnostics: [] });
    return;
  }
  connection.sendDiagnostics({ uri: doc.uri, diagnostics: analysis.diagnostics.map(d => toDiagnostic(analysis, d)) });
}

let revalidateTimer: NodeJS.Timeout | undefined;
/** Re-validates every open document (cross-file symbols may have changed). */
function revalidateAll(delay = 300): void {
  if (revalidateTimer) clearTimeout(revalidateTimer);
  revalidateTimer = setTimeout(() => {
    revalidateTimer = undefined;
    for (const doc of documents.all()) {
      ws.analyze(doc.uri, doc.getText());
      validate(doc);
    }
  }, delay);
}

documents.onDidOpen(e => {
  ws.markOpen(e.document.uri, true);
  ws.analyze(e.document.uri, e.document.getText());
  validate(e.document);
});

documents.onDidChangeContent(e => {
  ws.markOpen(e.document.uri, true);
  scheduleValidation(e.document);
  revalidateOthers(e.document.uri);
});

let othersTimer: NodeJS.Timeout | undefined;
function revalidateOthers(changedUri: string): void {
  if (othersTimer) clearTimeout(othersTimer);
  othersTimer = setTimeout(() => {
    othersTimer = undefined;
    for (const doc of documents.all()) {
      if (doc.uri === changedUri) continue;
      ws.analyze(doc.uri, doc.getText());
      validate(doc);
    }
  }, 600);
}

documents.onDidClose(e => {
  ws.markOpen(e.document.uri, false);
  const pending = pendingValidation.get(e.document.uri);
  if (pending) clearTimeout(pending);
  connection.sendDiagnostics({ uri: e.document.uri, diagnostics: [] });
  // keep the file in the index from disk
  if (!ws.indexFile(e.document.uri)) ws.remove(e.document.uri);
});

connection.onDidChangeWatchedFiles(params => {
  for (const change of params.changes) {
    if (ws.isOpen(change.uri)) continue;
    if (change.type === FileChangeType.Deleted) ws.remove(change.uri);
    else ws.indexFile(change.uri);
  }
  revalidateAll();
});

// -------------------------------------------------------------- features

connection.onCompletion((params): CompletionItem[] => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const analysis = analysisFor(doc);
  return complete(ws, analysis, doc.offsetAt(params.position));
});

connection.onHover(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return undefined;
  return hover(ws, analysisFor(doc), doc.offsetAt(params.position));
});

connection.onDefinition(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  return definition(ws, analysisFor(doc), doc.offsetAt(params.position));
});

connection.onReferences(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  return references(ws, analysisFor(doc), doc.offsetAt(params.position), params.context.includeDeclaration);
});

connection.onDocumentHighlight(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  return documentHighlights(analysisFor(doc), doc.offsetAt(params.position));
});

connection.onPrepareRename(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return undefined;
  return prepareRename(analysisFor(doc), doc.offsetAt(params.position)) ?? null;
});

connection.onRenameRequest(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return undefined;
  return rename(ws, analysisFor(doc), doc.offsetAt(params.position), params.newName) ?? null;
});

connection.onDocumentSymbol(params => {
  const analysis = analysisForUri(params.textDocument.uri);
  return analysis ? documentSymbols(analysis) : [];
});

connection.onWorkspaceSymbol(params => workspaceSymbols(ws, params.query));

connection.onSignatureHelp(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return undefined;
  return signatureHelp(ws, analysisFor(doc), doc.offsetAt(params.position)) ?? null;
});

connection.languages.semanticTokens.on(params => {
  const analysis = analysisForUri(params.textDocument.uri);
  return analysis ? semanticTokens(analysis) : { data: [] };
});

connection.onCodeAction(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  return codeActions(analysisFor(doc), doc.offsetAt(params.range.start), doc.offsetAt(params.range.end), params.context.only);
});

connection.onDocumentFormatting(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const analysis = analysisFor(doc);
  const opts = { tabSize: params.options.tabSize, insertSpaces: params.options.insertSpaces, ...ws.settings.format };
  return fullFormatEdits(analysis.text, formatNovus(analysis.text, opts), analysis.lineMap);
});

connection.onDocumentRangeFormatting(params => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const analysis = analysisFor(doc);
  const opts = { tabSize: params.options.tabSize, insertSpaces: params.options.insertSpaces, ...ws.settings.format };
  return rangeFormatEdits(analysis.text, formatLines(analysis.text, opts), analysis.lineMap, params.range);
});

connection.onFoldingRanges(params => {
  const analysis = analysisForUri(params.textDocument.uri);
  return analysis ? foldingRanges(analysis) : [];
});

documents.listen(connection);
connection.listen();
