"""Full editor sessions: VS Code, Neovim 0.10 and Helix from initialize to exit."""
import json
import time

from harness import scenario, Session, make_workspace, MAIN_TEXT
from profiles import TOKEN_TYPES

EXPECTED_ENCODING = {"vscode": "utf-16", "nvim": "utf-8", "helix": "utf-8"}


def full_session(report, editor):
    root = make_workspace()
    s = Session(editor, root)
    c = s.client
    try:
        init = s.init_result
        report.check(init and "result" in init, "initialize answered")
        caps = init["result"]["capabilities"]
        report.check(caps.get("positionEncoding") == EXPECTED_ENCODING[editor],
                     "positionEncoding %r expected %s" % (caps.get("positionEncoding"), EXPECTED_ENCODING[editor]))
        report.check("diagnosticProvider" not in caps, "no pull diagnostics offered")
        sync = caps["textDocumentSync"]
        report.check(isinstance(sync, dict) and sync.get("openClose") is True, "openClose")

        cfg = c.wait_for(lambda cl: cl.requests_named("workspace/configuration"), 5)
        report.check(bool(cfg), "server pulled workspace/configuration after initialized")
        reg = c.wait_for(lambda cl: cl.requests_named("client/registerCapability"), 5)
        report.check(bool(reg), "server registered file watchers")
        ids = [m["id"] for m in c.server_requests]
        report.check(len(ids) == len(set(ids)), "server request ids are unique: %r" % ids)
        report.check(all(isinstance(i, (int, str)) for i in ids), "ids are int or string")

        uri = s.open("main.nv", MAIN_TEXT)
        diag = s.diagnostics_for(uri, 8)
        report.check(diag is not None, "publishDiagnostics after didOpen (production mode, no test sync)")
        report.check(diag is not None and diag["diagnostics"] == [], "clean file has no diagnostics: %r" % (diag,))

        # type "shape." inside main (line 9 col 4 is "println(shape.name)")
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                 "contentChanges": [{"range": {"start": {"line": 9, "character": 18}, "end": {"line": 9, "character": 22}},
                                     "text": "", "rangeLength": 4}]})
        comp = c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 18},
                         "context": {"triggerKind": 2, "triggerCharacter": "."}, "workDoneToken": "tok-1"})
        items = completion_items(comp)
        labels = [i["label"] for i in items]
        report.check("area" in labels and "name" in labels, "member completion after '.' lists area/name: %r" % labels[:10])

        hover = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 10},
                          "workDoneToken": 7})
        report.check(hover and "result" in hover, "hover answered: %r" % (hover,))
        definition = c.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 17}})
        report.check(definition and definition.get("result"), "definition of Shape: %r" % (definition,))

        sym = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        report.check(sym and isinstance(sym.get("result"), list) and len(sym["result"]) >= 1, "documentSymbol: %r" % (sym,))

        tokens = c.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        data = tokens["result"]["data"]
        report.check(len(data) % 5 == 0 and len(data) > 0, "semantic tokens data multiple of 5")
        legend = caps["semanticTokensProvider"]["legend"]
        report.check(all(data[i + 3] < len(legend["tokenTypes"]) for i in range(0, len(data), 5)), "token type index in legend")
        report.check(all(data[i + 4] < (1 << len(legend["tokenModifiers"])) for i in range(0, len(data), 5)), "modifier bits in legend")
        rng = c.request("textDocument/semanticTokens/range", {"textDocument": {"uri": uri},
                        "range": {"start": {"line": 4, "character": 0}, "end": {"line": 10, "character": 0}}})
        report.check(rng and "result" in rng, "semanticTokens/range answered")

        c.notify("textDocument/didSave", {"textDocument": {"uri": uri}, "text": MAIN_TEXT})
        c.notify("$/setTrace", {"value": "verbose"})
        c.notify("$/progress", {"token": "tok-1", "value": {"kind": "end"}})
        c.notify("window/workDoneProgress/cancel", {"token": "tok-1"})
        c.notify("$/cancelRequest", {"id": 9999})
        c.notify("workspace/didChangeConfiguration", {"settings": {"novus": {"pureline": {"enabled": True}}}})
        after = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 6}})
        report.check(after is not None and "result" in after, "server alive after $/ notifications and config change")
        unknown = c.request("$/nonexistent", {})
        report.check(unknown and unknown.get("error", {}).get("code") == -32601, "unknown $/ request -> -32601: %r" % (unknown,))

        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        cleared = s.diagnostics_for(uri, 5, lambda p: p["diagnostics"] == [])
        report.check(cleared is not None, "didClose publishes empty diagnostics")
        response, code = s.shutdown()
        report.check(response and response.get("result", "x") is None, "shutdown result null: %r" % (response,))
        report.check(code == 0, "exit code 0 after shutdown+exit, got %r" % (code,))
        report.check(not c.protocol_errors, "no protocol errors: %r" % c.protocol_errors)
        stray = [m for m in c.server_requests if m["method"] not in ("workspace/configuration", "client/registerCapability")]
        report.check(not stray, "unexpected server requests: %r" % stray)
    finally:
        s.close()


def completion_items(response):
    result = response.get("result") if response else None
    if isinstance(result, dict):
        return result.get("items", [])
    return result or []


@scenario("flow-vscode")
def flow_vscode(report):
    full_session(report, "vscode")


@scenario("flow-nvim")
def flow_nvim(report):
    full_session(report, "nvim")


@scenario("flow-helix")
def flow_helix(report):
    full_session(report, "helix")
