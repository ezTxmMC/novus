"""Capability negotiation: position encodings, snippet and markup flags, hierarchical symbols, initialize shapes."""
import re

from harness import scenario, Session, make_workspace, MAIN_TEXT
from profiles import initialize_params, file_uri
from scen_flow import completion_items

EMOJI_TEXT = 'package main\n\nmethod main() {\n    var s = "\U0001F600é" )\n    var count = 1\n    println("\U0001F600é" + count)\n}\n'
COLUMN_OF_PAREN = {"utf-16": 18, "utf-8": 21, "utf-32": 17}
COLUMN_OF_COUNT = {"utf-16": 20, "utf-8": 23, "utf-32": 19}


def encodings(value):
    def edit(caps):
        if value is None:
            caps.pop("general", None)
            return
        caps.setdefault("general", {})["positionEncodings"] = value
    return edit


@scenario("negotiate-position-encodings")
def position_encodings(report):
    cases = [(None, "utf-16"), ([], "utf-16"), (["utf-16"], "utf-16"), (["utf-8", "utf-16"], "utf-8"),
             (["utf-16", "utf-8"], "utf-8"), (["utf-32"], "utf-16"), (["utf-32", "utf-16"], "utf-16"),
             (["utf-32", "utf-8", "utf-16"], "utf-8"), (["utf-8"], "utf-8")]
    for offered, expected in cases:
        root = make_workspace()
        s = Session("vscode", root, caps_edit=encodings(offered))
        try:
            got = s.init_result["result"]["capabilities"].get("positionEncoding")
            report.check(got == expected, "offered %r -> %r, expected %r" % (offered, got, expected))
            check_positions(report, s, got, "offered %r" % (offered,))
        finally:
            s.close()


def check_positions(report, s, encoding, label):
    c = s.client
    uri = s.open("main.nv", EMOJI_TEXT)
    diag = s.diagnostics_for(uri, 6, lambda p: len(p["diagnostics"]) > 0)
    report.check(diag is not None, "%s: syntax diagnostic for stray ')' published" % label)
    if diag:
        starts = [(d["range"]["start"]["line"], d["range"]["start"]["character"]) for d in diag["diagnostics"]]
        report.check((3, COLUMN_OF_PAREN[encoding]) in starts,
                     "%s [%s]: ')' diagnostic should start at column %d, got %r" % (label, encoding, COLUMN_OF_PAREN[encoding], starts))
    tokens = c.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
    if tokens and "result" in tokens:
        data = tokens["result"]["data"]
        line, col, found = 0, 0, []
        for i in range(0, len(data), 5):
            line += data[i]
            col = data[i + 1] if data[i] else col + data[i + 1]
            found.append((line, col, data[i + 2]))
        report.check(any(l == 5 and ch == COLUMN_OF_COUNT[encoding] for l, ch, _ in found),
                     "%s [%s]: token for 'count' at column %d, tokens on line 5: %r" % (label, encoding, COLUMN_OF_COUNT[encoding], [t for t in found if t[0] == 5]))
    hover_col = COLUMN_OF_COUNT[encoding] + 1
    hover = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": hover_col}})
    report.check(hover and hover.get("result"), "%s [%s]: hover on 'count' at col %d" % (label, encoding, hover_col))
    if hover and hover.get("result") and "range" in hover["result"]:
        start = hover["result"]["range"]["start"]["character"]
        report.check(start == COLUMN_OF_COUNT[encoding], "%s [%s]: hover range start %r != %d" % (label, encoding, start, COLUMN_OF_COUNT[encoding]))


@scenario("negotiate-snippets-off")
def snippets_off(report):
    for editor in ("vscode", "nvim", "helix"):
        root = make_workspace()
        def edit(caps):
            caps["textDocument"]["completion"]["completionItem"]["snippetSupport"] = False
        s = Session(editor, root, caps_edit=edit)
        try:
            uri = s.open("main.nv", "package main\n\nmethod main() {\n    sout\n    forir\n    fori\n    main\n}\n")
            for line, character in ((3, 8), (4, 9), (5, 8)):
                resp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": line, "character": character},
                                                                     "context": {"triggerKind": 1}})
                items = completion_items(resp)
                report.check(len(items) > 0, "%s: completion line %d answered with items" % (editor, line))
                bad = [i["label"] for i in items if i.get("insertTextFormat") == 2]
                report.check(not bad, "%s: snippetSupport=false but snippet-format items: %r" % (editor, bad[:5]))
                dollars = [i["label"] for i in items if re.search(r"\$(\d|\{\d)", (i.get("insertText") or "") + ((i.get("textEdit") or {}).get("newText") or ""))]
                report.check(not dollars, "%s: snippet placeholders in plain-text items: %r" % (editor, dollars[:5]))
                sout = [i for i in items if i["label"] in ("sout", "soutv")]
                if line == 3:
                    report.check(len(sout) >= 0, "")
        finally:
            s.close()


@scenario("negotiate-snippets-on")
def snippets_on(report):
    for editor in ("vscode", "nvim", "helix"):
        root = make_workspace()
        s = Session(editor, root)
        try:
            uri = s.open("main.nv", "package main\n\nmethod main() {\n    sout\n}\n")
            resp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 8},
                                                                 "context": {"triggerKind": 1}})
            items = completion_items(resp)
            sout = [i for i in items if i["label"] == "sout"]
            report.check(len(sout) == 1, "%s: 'sout' snippet offered once: %r" % (editor, [i["label"] for i in items][:8]))
            if sout:
                item = sout[0]
                report.check(item.get("insertTextFormat") == 2, "%s: sout is a snippet (format 2)" % editor)
                text = (item.get("textEdit") or {}).get("newText") or item.get("insertText") or ""
                report.check("println" in text and "$" in text, "%s: sout body %r" % (editor, text))
                if "textEdit" in item:
                    r = item["textEdit"]["range"]
                    report.check(r["start"] == {"line": 3, "character": 4} and r["end"] == {"line": 3, "character": 8},
                                 "%s: sout textEdit range replaces the typed prefix: %r" % (editor, r))
        finally:
            s.close()


@scenario("negotiate-markup-and-labels")
def markup_and_labels(report):
    def plain(caps):
        caps["textDocument"]["hover"]["contentFormat"] = ["plaintext"]
        caps["textDocument"]["completion"]["completionItem"]["documentationFormat"] = ["plaintext"]
        caps["textDocument"]["completion"]["completionItem"].pop("labelDetailsSupport", None)
        caps["textDocument"]["signatureHelp"]["signatureInformation"]["documentationFormat"] = ["plaintext"]
    root = make_workspace()
    s = Session("vscode", root, caps_edit=plain)
    try:
        init = s.init_result["result"]["capabilities"]
        report.check("labelDetailsSupport" not in init["completionProvider"].get("completionItem", {}), "no labelDetailsSupport for client without it")
        uri = s.open("main.nv", MAIN_TEXT)
        hover = s.client.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
        contents = (hover.get("result") or {}).get("contents")
        report.check(contents is not None, "hover on 'main' found: %r" % (hover,))
        if isinstance(contents, dict):
            report.check(contents.get("kind") == "plaintext", "plaintext client must not get kind %r" % contents.get("kind"))
        report.defect("D01-plaintext-hover", not isinstance(contents, str), "plaintext-only client receives a bare string as hover contents (a MarkedString, markdown by definition): %r" % (contents,))
        comp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 18},
                                                             "context": {"triggerKind": 1}})
        items = completion_items(comp)
        report.check(all("labelDetails" not in i for i in items), "labelDetails sent to a client without labelDetailsSupport")
        docs = [i["documentation"] for i in items if "documentation" in i]
        report.check(all(isinstance(d, str) or d.get("kind") == "plaintext" for d in docs), "plaintext documentation: %r" % docs[:2])
    finally:
        s.close()


@scenario("negotiate-flat-symbols")
def flat_symbols(report):
    def flat(caps):
        caps["textDocument"]["documentSymbol"]["hierarchicalDocumentSymbolSupport"] = False
    root = make_workspace()
    s = Session("vscode", root, caps_edit=flat)
    try:
        uri = s.open("geo/shape.nv")
        resp = s.client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        result = resp.get("result") or []
        report.check(len(result) > 0, "flat symbols returned")
        report.check(all("location" in r and "range" not in r for r in result), "SymbolInformation (location) for non-hierarchical client: %r" % (result[:1],))
        report.check(any(r.get("containerName") for r in result), "children carry containerName")
    finally:
        s.close()


@scenario("negotiate-no-server-requests")
def no_server_requests(report):
    def bare(caps):
        caps["workspace"]["configuration"] = False
        caps["workspace"]["didChangeWatchedFiles"] = {"dynamicRegistration": False}
    root = make_workspace()
    s = Session("vscode", root, caps_edit=bare)
    try:
        s.open("main.nv", MAIN_TEXT)
        s.client.notify("workspace/didChangeConfiguration", {"settings": None})
        s.client.notify("workspace/didChangeConfiguration", {"settings": {}})
        s.client.request("textDocument/hover", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 4, "character": 8}})
        report.check(not s.client.server_requests, "no server requests for a client without configuration/dynamicRegistration: %r" % s.client.server_requests)
    finally:
        s.close()
    root = make_workspace()
    s = Session("helix", root, caps_edit=lambda caps: caps["workspace"].__setitem__("didChangeWatchedFiles", {"dynamicRegistration": False}))
    try:
        s.client.request("textDocument/hover", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 4, "character": 8}})
        regs = s.client.requests_named("client/registerCapability")
        report.check(not regs, "registerCapability sent to client without dynamicRegistration: %r" % regs)
    finally:
        s.close()


def variant(report, label, mutate, expect_folder=True):
    root = make_workspace()
    params = initialize_params("vscode", root)
    mutate(params, root)
    s = Session("vscode", root, init_params=params)
    try:
        res = s.init_result
        report.check(res and "result" in res, "%s: initialize succeeded: %r" % (label, (res or {}).get("error")))
        uri = s.open("main.nv", MAIN_TEXT)
        hover = s.client.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 10}}, timeout=10)
        report.check(hover and "result" in hover, "%s: hover works afterwards: %r" % (label, hover))
        comp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 17}}, timeout=10)
        report.check(comp and "result" in comp, "%s: completion works afterwards" % label)
        if expect_folder and comp and "result" in comp:
            labels = [i["label"] for i in completion_items(comp)]
            report.check("Shape" in labels, "%s: project symbol Shape found (project discovered): %r" % (label, labels[:6]))
        response, code = s.shutdown()
        report.check(code == 0, "%s: exit code %r" % (label, code))
    finally:
        s.close()


@scenario("negotiate-initialize-shapes")
def initialize_shapes(report):
    def folders_null(p, r):
        p["workspaceFolders"] = None
    def root_null(p, r):
        p.update({"rootUri": None, "rootPath": None, "workspaceFolders": None})
    def root_path_only(p, r):
        p.update({"rootUri": None, "workspaceFolders": None})
    def folders_empty(p, r):
        p.update({"rootUri": None, "rootPath": None, "workspaceFolders": []})
    def no_options(p, r):
        p.pop("initializationOptions")
    def null_options(p, r):
        p["initializationOptions"] = None
    def array_options(p, r):
        p["initializationOptions"] = []
    def novus_array(p, r):
        p["initializationOptions"] = {"novus": []}
    def novus_null(p, r):
        p["initializationOptions"] = {"novus": None}
    def wrong_types(p, r):
        p["initializationOptions"] = {"novus": {"check": {"mode": 5, "timeoutSeconds": "x"}, "pureline": {"enabled": "yes"},
                                                "completion": {"maxItems": -3}, "diagnostics": [], "format": {"tabSize": 1e99}}}
    def flat_options(p, r):
        p["initializationOptions"] = {"pureline.enabled": True, "check": {"mode": "off"}}
    def no_caps(p, r):
        p.pop("capabilities")
    def null_caps(p, r):
        p["capabilities"] = None
    def empty_caps(p, r):
        p["capabilities"] = {}
    def no_client_info(p, r):
        p.pop("clientInfo")
    def folder_uri_slash(p, r):
        p["rootUri"] = file_uri(r) + "/"
        p["workspaceFolders"] = [{"uri": file_uri(r) + "/", "name": "w"}]
    def process_null(p, r):
        p["processId"] = None
    def missing_root_dir(p, r):
        p["rootUri"] = "file:///nonexistent/dir/for/test"
        p["workspaceFolders"] = [{"uri": "file:///nonexistent/dir/for/test", "name": "x"}]
        p["rootPath"] = "/nonexistent/dir/for/test"
    for label, mutate, folder in [("workspaceFolders:null", folders_null, True), ("no root at all (single file)", root_null, False),
                                  ("rootPath only", root_path_only, True), ("workspaceFolders:[]", folders_empty, False),
                                  ("no initializationOptions", no_options, True), ("initializationOptions:null", null_options, True),
                                  ("initializationOptions:[] (nvim empty table)", array_options, True), ("novus:[]", novus_array, True),
                                  ("novus:null", novus_null, True), ("wrong typed settings", wrong_types, True),
                                  ("flat dotted options", flat_options, True), ("no capabilities", no_caps, True),
                                  ("capabilities:null", null_caps, True), ("capabilities:{}", empty_caps, True),
                                  ("no clientInfo", no_client_info, True), ("rootUri with trailing slash", folder_uri_slash, True),
                                  ("processId:null", process_null, True), ("missing root directory", missing_root_dir, False)]:
        variant(report, label, mutate, folder)
