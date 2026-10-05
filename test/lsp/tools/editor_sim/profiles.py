"""Initialize parameters and server-request responders of the simulated editors.

The capability sets follow what vscode-languageclient 9/10, Neovim 0.10 and Helix 24/25 send; fields
that the server cannot depend on are kept anyway because a real client sends them."""
import os

SYMBOL_KINDS = list(range(1, 27))
COMPLETION_KINDS = list(range(1, 26))
TOKEN_TYPES = ["namespace", "type", "class", "enum", "interface", "struct", "typeParameter", "parameter", "variable",
               "property", "enumMember", "event", "function", "method", "macro", "keyword", "modifier", "comment",
               "string", "number", "regexp", "operator", "decorator"]
TOKEN_MODIFIERS = ["declaration", "definition", "readonly", "static", "deprecated", "abstract", "async", "modification",
                   "documentation", "defaultLibrary"]


def file_uri(path):
    from urllib.parse import quote
    return "file://" + quote(path)


def vscode_capabilities(configuration=True, watchers=True, snippets=True, hierarchical=True, markdown=True):
    docs = ["markdown", "plaintext"] if markdown else ["plaintext"]
    return {
        "workspace": {
            "applyEdit": True,
            "workspaceEdit": {"documentChanges": True, "resourceOperations": ["create", "rename", "delete"],
                              "failureHandling": "textOnlyTransactional", "normalizesLineEndings": True,
                              "changeAnnotationSupport": {"groupsOnLabel": True}},
            "configuration": configuration,
            "didChangeWatchedFiles": {"dynamicRegistration": watchers, "relativePatternSupport": True},
            "symbol": {"dynamicRegistration": True, "symbolKind": {"valueSet": SYMBOL_KINDS}, "tagSupport": {"valueSet": [1]},
                       "resolveSupport": {"properties": ["location.range"]}},
            "codeLens": {"refreshSupport": True},
            "executeCommand": {"dynamicRegistration": True},
            "didChangeConfiguration": {"dynamicRegistration": True},
            "workspaceFolders": True,
            "semanticTokens": {"refreshSupport": True},
            "diagnostics": {"refreshSupport": True},
            "fileOperations": {"dynamicRegistration": True, "didCreate": True, "didRename": True, "didDelete": True},
        },
        "textDocument": {
            "publishDiagnostics": {"relatedInformation": True, "versionSupport": False, "tagSupport": {"valueSet": [1, 2]},
                                   "codeDescriptionSupport": True, "dataSupport": True},
            "synchronization": {"dynamicRegistration": True, "willSave": True, "willSaveWaitUntil": True, "didSave": True},
            "completion": {"dynamicRegistration": True, "contextSupport": True,
                           "completionItem": {"snippetSupport": snippets, "commitCharactersSupport": True,
                                              "documentationFormat": docs, "deprecatedSupport": True,
                                              "preselectSupport": True, "tagSupport": {"valueSet": [1]},
                                              "insertReplaceSupport": True,
                                              "resolveSupport": {"properties": ["documentation", "detail", "additionalTextEdits"]},
                                              "insertTextModeSupport": {"valueSet": [1, 2]}, "labelDetailsSupport": True},
                           "insertTextMode": 2, "completionItemKind": {"valueSet": COMPLETION_KINDS},
                           "completionList": {"itemDefaults": ["commitCharacters", "editRange", "insertTextFormat",
                                                               "insertTextMode", "data"]}},
            "hover": {"dynamicRegistration": True, "contentFormat": docs},
            "signatureHelp": {"dynamicRegistration": True,
                              "signatureInformation": {"documentationFormat": docs,
                                                       "parameterInformation": {"labelOffsetSupport": True},
                                                       "activeParameterSupport": True},
                              "contextSupport": True},
            "definition": {"dynamicRegistration": True, "linkSupport": True},
            "references": {"dynamicRegistration": True},
            "documentHighlight": {"dynamicRegistration": True},
            "documentSymbol": {"dynamicRegistration": True, "symbolKind": {"valueSet": SYMBOL_KINDS},
                               "hierarchicalDocumentSymbolSupport": hierarchical, "tagSupport": {"valueSet": [1]},
                               "labelSupport": True},
            "codeAction": {"dynamicRegistration": True, "isPreferredSupport": True, "disabledSupport": True,
                           "dataSupport": True, "resolveSupport": {"properties": ["edit"]},
                           "codeActionLiteralSupport": {"codeActionKind": {"valueSet": ["", "quickfix", "refactor", "source",
                                                                                         "source.organizeImports"]}},
                           "honorsChangeAnnotations": False},
            "formatting": {"dynamicRegistration": True},
            "rangeFormatting": {"dynamicRegistration": True},
            "rename": {"dynamicRegistration": True, "prepareSupport": True, "prepareSupportDefaultBehavior": 1,
                       "honorsChangeAnnotations": True},
            "documentLink": {"dynamicRegistration": True, "tooltipSupport": True},
            "foldingRange": {"dynamicRegistration": True, "rangeLimit": 5000, "lineFoldingOnly": True,
                             "foldingRangeKind": {"valueSet": ["comment", "imports", "region"]}},
            "semanticTokens": {"dynamicRegistration": True, "tokenTypes": TOKEN_TYPES, "tokenModifiers": TOKEN_MODIFIERS,
                               "formats": ["relative"], "requests": {"range": True, "full": {"delta": True}},
                               "multilineTokenSupport": False, "overlappingTokenSupport": False,
                               "serverCancelSupport": True, "augmentsSyntaxTokens": True},
            "diagnostic": {"dynamicRegistration": True, "relatedDocumentSupport": False},
        },
        "window": {"showMessage": {"messageActionItem": {"additionalPropertiesSupport": True}},
                   "showDocument": {"support": True}, "workDoneProgress": True},
        "general": {"staleRequestSupport": {"cancel": True, "retryOnContentModified": ["textDocument/semanticTokens/full"]},
                    "regularExpressions": {"engine": "ECMAScript", "version": "ES2020"},
                    "markdown": {"parser": "marked", "version": "1.1.0"}, "positionEncodings": ["utf-16"]},
    }


def neovim_capabilities():
    caps = vscode_capabilities(watchers=True)
    caps["general"] = {"positionEncodings": ["utf-8", "utf-16", "utf-32"]}
    del caps["window"]["showDocument"]
    caps["window"]["workDoneProgress"] = True
    caps["textDocument"]["completion"]["completionItem"].pop("labelDetailsSupport")
    caps["textDocument"]["completion"].pop("completionList")
    caps["textDocument"]["semanticTokens"].update({"overlappingTokenSupport": True, "serverCancelSupport": False})
    caps["workspace"].pop("fileOperations")
    caps["textDocument"].pop("diagnostic")
    return caps


def helix_capabilities():
    return {
        "workspace": {"configuration": True, "didChangeConfiguration": {"dynamicRegistration": False},
                      "workspaceFolders": True, "applyEdit": True, "symbol": {"dynamicRegistration": False},
                      "executeCommand": {"dynamicRegistration": False},
                      "didChangeWatchedFiles": {"dynamicRegistration": True, "relativePatternSupport": False},
                      "workspaceEdit": {"documentChanges": True, "resourceOperations": ["create", "rename", "delete"],
                                        "failureHandling": "abort"}},
        "textDocument": {
            "synchronization": {"dynamicRegistration": False, "didSave": True},
            "completion": {"completionItem": {"snippetSupport": True, "resolveSupport": {"properties": ["documentation", "detail", "additionalTextEdits"]},
                                              "insertReplaceSupport": True, "deprecatedSupport": True,
                                              "tagSupport": {"valueSet": [1]}}, "completionItemKind": {}},
            "hover": {"contentFormat": ["markdown"]},
            "signatureHelp": {"signatureInformation": {"documentationFormat": ["markdown"],
                                                       "parameterInformation": {"labelOffsetSupport": True},
                                                       "activeParameterSupport": True}},
            "documentSymbol": {"hierarchicalDocumentSymbolSupport": True},
            "publishDiagnostics": {"relatedInformation": True, "tagSupport": {"valueSet": [1, 2]},
                                   "versionSupport": True, "codeDescriptionSupport": True, "dataSupport": True},
            "codeAction": {"codeActionLiteralSupport": {"codeActionKind": {"valueSet": ["", "quickfix", "refactor", "source"]}},
                           "isPreferredSupport": True, "dataSupport": True, "resolveSupport": {"properties": ["edit", "command"]}},
            "rename": {"dynamicRegistration": False, "prepareSupport": True},
            "formatting": {"dynamicRegistration": False},
            "definition": {"linkSupport": True},
            "inlayHint": {"dynamicRegistration": False},
        },
        "window": {"workDoneProgress": True, "showMessage": {}},
        "general": {"positionEncodings": ["utf-8", "utf-32", "utf-16"]},
    }


def initialize_params(editor, root, extra=None):
    caps = {"vscode": vscode_capabilities, "nvim": neovim_capabilities, "helix": helix_capabilities}[editor]()
    folder = {"uri": file_uri(root), "name": os.path.basename(root)}
    params = {"processId": os.getpid(), "capabilities": caps, "trace": "off"}
    if editor == "vscode":
        params.update({"clientInfo": {"name": "Visual Studio Code", "version": "1.95.0"}, "locale": "en",
                       "rootPath": root, "rootUri": file_uri(root), "workspaceFolders": [folder],
                       "initializationOptions": {"novus": {"diagnostics": {"own": True}, "check": {"mode": "off"},
                                                           "pureline": {"enabled": False}}}})
    if editor == "nvim":
        params.update({"clientInfo": {"name": "Neovim", "version": "0.10.0"}, "rootPath": root,
                       "rootUri": file_uri(root), "workspaceFolders": [folder]})
    if editor == "helix":
        params.update({"clientInfo": {"name": "helix", "version": "24.7"}, "rootPath": None,
                       "rootUri": file_uri(root), "workspaceFolders": [folder]})
    params.update(extra or {})
    return params


def settings_value(section, client=None):
    """The configuration an editor answers a pull with: its initializationOptions, else the defaults."""
    options = getattr(client, "config", None) or {"novus": {"diagnostics": {"own": True}, "check": {"mode": "off"}, "pureline": {"enabled": False}}}
    return options.get(section)


def well_behaved_responder(client, message):
    """What VS Code / Neovim / Helix answer to the two server requests of the design."""
    method = message["method"]
    if method == "workspace/configuration":
        return ("result", [settings_value(item.get("section"), client) for item in message["params"]["items"]])
    if method == "client/registerCapability":
        return ("result", None)
    if method == "window/workDoneProgress/create":
        return ("result", None)
    if method == "window/showMessageRequest":
        return ("result", None)
    return ("error", -32601, "Unhandled method " + method)
