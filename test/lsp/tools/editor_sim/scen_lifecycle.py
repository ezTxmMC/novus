"""Lifecycle, error codes and transport shapes of real clients."""
import json
import os
import subprocess
import time

from client import Frame, EditorClient
from harness import scenario, Session, make_workspace, MAIN_TEXT, BINARY
from profiles import initialize_params, well_behaved_responder


def raw_client(root, **kw):
    return EditorClient(BINARY, root, responder=well_behaved_responder, **kw)


@scenario("lifecycle-before-initialize")
def before_initialize(report):
    root = make_workspace()
    s = Session("vscode", root, handshake=False)
    c = s.client
    try:
        r = c.request("textDocument/hover", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 0, "character": 0}}, timeout=5)
        report.check(r and r.get("error", {}).get("code") == -32002, "hover before initialize -> -32002, got %r" % (r,))
        c.notify("textDocument/didOpen", {"textDocument": {"uri": s.uri("main.nv"), "languageId": "novus", "version": 1, "text": MAIN_TEXT}})
        r = c.request("shutdown", omit_params=True, timeout=5)
        report.check(r and r.get("error", {}).get("code") == -32002, "shutdown before initialize -> -32002, got %r" % (r,))
        r = c.request("initialize", initialize_params("vscode", root), timeout=10)
        report.check(r and "result" in r, "initialize still works after rejected requests")
        c.notify("initialized", {})
        r = c.request("initialize", initialize_params("vscode", root), timeout=5)
        report.check(r and r.get("error", {}).get("code") == -32600, "second initialize -> -32600, got %r" % (r,))
        pubs = c.notifications_named("textDocument/publishDiagnostics")
        report.check(not pubs, "didOpen before initialize must be dropped, got %d publishes" % len(pubs))
    finally:
        s.close()


@scenario("lifecycle-exit-codes")
def exit_codes(report):
    cases = {}
    root = make_workspace()
    # exit before initialize
    s = Session("vscode", root, handshake=False)
    s.client.notify("exit", omit_params=True)
    report.check(s.client.wait_exit(4) == 1, "exit without initialize/shutdown -> 1")
    s.close()
    # exit without shutdown
    s = Session("vscode", root)
    s.client.notify("exit", omit_params=True)
    report.check(s.client.wait_exit(4) == 1, "exit without shutdown -> 1")
    s.close()
    # EOF without exit
    s = Session("vscode", root)
    s.client.proc.stdin.close()
    t = time.time()
    code = s.client.wait_exit(4)
    report.check(code == 1, "EOF without shutdown -> 1, got %r" % (code,))
    s.close()
    # shutdown then EOF
    s = Session("vscode", root)
    s.client.request("shutdown", omit_params=True)
    s.client.proc.stdin.close()
    code = s.client.wait_exit(4)
    report.check(code == 0, "shutdown then EOF -> 0, got %r" % (code,))
    s.close()
    # shutdown with params:null (Helix), exit with params:null
    s = Session("helix", root)
    r = s.client.request("shutdown", None)
    report.check(r and r.get("result", 1) is None and "error" not in r, "shutdown with params:null: %r" % (r,))
    s.client.notify("exit", None)
    report.check(s.client.wait_exit(4) == 0, "exit with params:null -> 0")
    s.close()
    # stdin open, process must end on its own after exit
    s = Session("nvim", root)
    s.open("main.nv", MAIN_TEXT)
    s.client.request("shutdown", omit_params=True)
    t = time.time()
    s.client.notify("exit")
    code = s.client.wait_exit(3)
    report.check(code == 0 and time.time() - t < 2.0, "exit takes %.2fs, code %r" % (time.time() - t, code))
    s.close()


@scenario("lifecycle-after-shutdown")
def after_shutdown(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        s.open("main.nv", MAIN_TEXT)
        c.request("shutdown", omit_params=True)
        for method, params in (("textDocument/hover", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 4, "character": 8}}),
                               ("textDocument/completion", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 4, "character": 8}}),
                               ("shutdown", None), ("workspace/symbol", {"query": "a"})):
            r = c.request(method, params, timeout=5)
            report.check(r and r.get("error", {}).get("code") == -32600, "%s after shutdown -> -32600, got %r" % (method, r))
        c.notify("exit", omit_params=True)
        report.check(c.wait_exit(4) == 0, "exit code 0")
    finally:
        s.close()


@scenario("lifecycle-error-codes")
def error_codes(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        def code_of(method, params, **kw):
            r = c.request(method, params, timeout=8, **kw)
            return (r or {}).get("error", {}).get("code"), r
        got, r = code_of("textDocument/nonexistent", {})
        report.check(got == -32601, "unknown method -> -32601, got %r" % (r,))
        got, r = code_of("textDocument/hover", None, omit_params=True)
        report.defect("D06-lenient-params", got == -32602, "hover without params -> -32602 (Invalid params), got %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": uri}})
        report.defect("D06-lenient-params", got == -32602, "hover without position -> -32602, got %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": "a", "character": 0}})
        report.defect("D06-lenient-params", got == -32602, "hover with non-integer line -> -32602, got %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": -1, "character": -5}})
        report.check(got is None, "hover with negative position must not error, got %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": s.uri("geo/shape.nv")}, "position": {"line": 1, "character": 1}})
        report.check(got is None, "hover on a document that is not open must answer (null) not error, got %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": "not a uri"}, "position": {"line": 1, "character": 1}})
        report.check(got is None, "hover with a malformed uri: %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": 5}, "position": {"line": 1, "character": 1}})
        report.check(got in (None, -32602), "hover with numeric uri: %r" % (r,))
        got, r = code_of("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 99999, "character": 99999}})
        report.check(got is None, "hover far beyond the document: %r" % (r,))
        got, r = code_of("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 99999, "character": 99999}})
        report.check(got is None, "completion far beyond the document: %r" % (r,))
        got, r = code_of("textDocument/rename", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}, "newName": ""})
        report.check(got in (-32602, -32803, -32600, None), "rename to empty name: %r" % (r,))
        got, r = code_of("workspace/executeCommand", {"command": "does.not.exist", "arguments": []})
        report.check(got == -32602, "unknown command -> -32602, got %r" % (r,))
        got, r = code_of("workspace/executeCommand", {"command": "novus.rerunChecks"})
        report.check(got is None, "executeCommand without arguments key: %r" % (r,))
        got, r = code_of("completionItem/resolve", {"label": "x"})
        report.check(got is None, "resolve of an item without data: %r" % (r,))
        got, r = code_of("completionItem/resolve", {"label": "x", "data": {"junk": [1, 2]}})
        report.check(got is None, "resolve of an item with foreign data: %r" % (r,))
        for method in ("textDocument/inlayHint", "textDocument/codeLens", "textDocument/selectionRange", "textDocument/declaration",
                       "textDocument/typeDefinition", "textDocument/implementation", "textDocument/diagnostic", "workspace/diagnostic",
                       "textDocument/prepareCallHierarchy", "textDocument/documentColor", "textDocument/onTypeFormatting",
                       "textDocument/linkedEditingRange", "textDocument/moniker", "workspace/willCreateFiles", "textDocument/willSaveWaitUntil",
                       "textDocument/semanticTokens/full/delta", "workspace/symbol/resolve"):
            got, r = code_of(method, {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}, "range": {"start": {"line": 0, "character": 0}, "end": {"line": 1, "character": 0}}, "previousResultId": "x", "query": "a", "files": []})
            report.check(got == -32601, "%s (not offered) -> -32601, got %r" % (method, (r or {}).get("error")))
        for method in ("textDocument/willSave", "workspace/didCreateFiles", "workspace/didRenameFiles", "workspace/didDeleteFiles",
                       "notebookDocument/didOpen", "textDocument/unknown", "$/anything", "telemetry/event"):
            c.notify(method, {"textDocument": {"uri": uri}})
        r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
        report.check(r and "result" in r, "alive after unknown notifications")
        errs = [m for m in c.log if m[0] == "in" and "error" in m[1] and m[1].get("id") is None]
        report.check(not errs, "server answered a notification with an error response: %r" % [e[1] for e in errs])
    finally:
        s.close()


@scenario("lifecycle-transport-shapes")
def transport_shapes(report):
    root = make_workspace()
    s = Session("vscode", root, handshake=False)
    c = s.client
    try:
        def send_bytes(data):
            c.proc.stdin.write(data)
            c.proc.stdin.flush()
        body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": initialize_params("vscode", root)}).encode()
        # VS Code sends Content-Type as well in some versions; header order and case vary between clients
        send_bytes(b"Content-Type: application/vscode-jsonrpc; charset=utf-8\r\ncontent-length: %d\r\n\r\n" % len(body) + body)
        report.check(c.wait_response(1, 10) is not None, "Content-Type header before Content-Length")
        c.notify("initialized", {})
        # one write with three messages
        batch = b""
        for i in (2, 3, 4):
            m = json.dumps({"jsonrpc": "2.0", "id": i, "method": "textDocument/hover", "params": {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 0, "character": 0}}}).encode()
            batch += b"Content-Length: %d\r\n\r\n" % len(m) + m
        send_bytes(batch)
        report.check(all(c.wait_response(i, 5) is not None for i in (2, 3, 4)), "three messages in one write")
        # byte-at-a-time write of a request with multibyte text
        m = json.dumps({"jsonrpc": "2.0", "id": 5, "method": "textDocument/hover", "params": {"textDocument": {"uri": s.uri("mäin\U0001F600.nv")}, "position": {"line": 0, "character": 0}}}, ensure_ascii=False).encode()
        frame = b"Content-Length: %d\r\n\r\n" % len(m) + m
        for i in range(len(frame)):
            send_bytes(frame[i:i + 1])
        report.check(c.wait_response(5, 5) is not None, "byte-at-a-time frame with multibyte uri")
        # a body with a trailing newline counted in the length (some clients) and CRLF inside
        m = b'{"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///x.nv"},"position":{"line":0,"character":0}}}\r\n'
        send_bytes(b"Content-Length: %d\r\n\r\n" % len(m) + m)
        report.check(c.wait_response(6, 5) is not None, "trailing CRLF inside the declared body")
        # headers separated by bare LF (broken client): must not hang the server for good
        r = c.request("shutdown", omit_params=True, timeout=5)
        report.check(r is not None, "shutdown still answered")
        c.notify("exit", omit_params=True)
        report.check(c.wait_exit(4) == 0, "exit 0")
    finally:
        s.close()


@scenario("lifecycle-cancel")
def cancel(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        for i in range(50):
            rid = 100 + i
            c.send_raw({"jsonrpc": "2.0", "id": rid, "method": "textDocument/completion", "params": {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 18}}})
            c.notify("$/cancelRequest", {"id": rid})
        for i in range(50):
            r = c.wait_response(100 + i, 10)
            report.check(r is not None and ("result" in r or r["error"]["code"] in (-32800, -32801)), "request %d answered exactly once with a result or a cancel code: %r" % (100 + i, r and r.get("error")))
        counts = {}
        for d, m, _ in c.log:
            if d == "in" and "method" not in m and isinstance(m.get("id"), int) and m["id"] >= 100:
                counts[m["id"]] = counts.get(m["id"], 0) + 1
        report.check(all(v == 1 for v in counts.values()), "duplicate responses: %r" % {k: v for k, v in counts.items() if v != 1})
        s.shutdown()
    finally:
        s.close()
