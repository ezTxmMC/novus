"""Capabilities of the TS server (server.ts onInitialize), workspace handling and request robustness."""
import os
import random
import time

from plib import check, pkg_files, read, REPO

PRO = "protocol"

TS_PROVIDERS = ["hoverProvider", "definitionProvider", "referencesProvider", "documentHighlightProvider", "renameProvider", "documentSymbolProvider",
                "workspaceSymbolProvider", "signatureHelpProvider", "foldingRangeProvider", "codeActionProvider", "documentFormattingProvider",
                "documentRangeFormattingProvider", "semanticTokensProvider", "completionProvider"]
TS_COMPLETION_TRIGGERS = [".", "@", ":", "<", "(", ","]


def caps(env):
    ses = env.shared("proto", {"main.nv": "package a\n"})
    return ses.c.init["result"]["capabilities"]


@check(PRO, "providers", "every provider of the TS server is advertised")
def providers(env):
    c = caps(env)
    missing = [p for p in TS_PROVIDERS if not c.get(p)]
    return (not missing, str(missing))


@check(PRO, "sync-incremental", "incremental text synchronisation")
def sync(env):
    sync_opt = caps(env).get("textDocumentSync")
    kind = sync_opt.get("change") if isinstance(sync_opt, dict) else sync_opt
    return (kind == 2, str(sync_opt))


@check(PRO, "prepare-rename", "prepareRename is advertised")
def prepare(env):
    rename = caps(env).get("renameProvider")
    return (isinstance(rename, dict) and rename.get("prepareProvider") is True, str(rename))


@check(PRO, "completion-trigger-characters", "the completion trigger characters include those of the TS server . @ : < ( ,")
def triggers(env):
    got = caps(env).get("completionProvider", {}).get("triggerCharacters", [])
    missing = [t for t in TS_COMPLETION_TRIGGERS if t not in got]
    return (not missing, "missing %s in %s" % (missing, got))


@check(PRO, "signature-trigger-characters", "signature help triggers on ( and ,")
def sig_triggers(env):
    got = caps(env).get("signatureHelpProvider", {}).get("triggerCharacters", [])
    return ("(" in got and "," in got, str(got))


@check(PRO, "semantic-tokens-legend-full", "semantic tokens full are advertised with a legend")
def tokens_caps(env):
    st = caps(env).get("semanticTokensProvider", {})
    return (bool(st.get("full")) and bool(st.get("legend", {}).get("tokenTypes")), str(st)[:100])


@check(PRO, "workspace-folder-added", "a workspace folder added at run time is indexed (server.ts onDidChangeWorkspaceFolders)")
def folder_added(env):
    ses = env.session({"main.nv": "package a\n\nmethod main {\n}\n"})
    other = env.session({"project.nv": "project \"x\"\nmain \"main.nv\"\n", "main.nv": "package b\n\ndefine class Late {\n}\n\nmethod main {\n}\n"}, open_all=False)
    try:
        uri = ses.c.uri_of("")[:-1].replace(ses.dir, other.dir)
        ses.c.notify("workspace/didChangeWorkspaceFolders", {"event": {"added": [{"uri": uri, "name": "other"}], "removed": []}})
        deadline = time.time() + 3
        found = []
        while time.time() < deadline:
            res = ses.c.request("workspace/symbol", {"query": "Late"}).get("result") or []
            found = [s["name"] for s in res]
            if found:
                break
            time.sleep(0.1)
        return ("Late" in found, str(found))
    finally:
        other.close()
        ses.close()


def watched_session(env):
    files = {"main.nv": "package a\n\nmethod main {\n}\n", "util.nv": "package a\n\nmethod helperOne() {\n}\n"}
    return env.session(files, open_all=False)


def symbol_names(ses, query):
    deadline = time.time() + 2
    found = []
    while time.time() < deadline:
        found = [s["name"] for s in ses.c.request("workspace/symbol", {"query": query}).get("result") or []]
        if found:
            break
        time.sleep(0.1)
    return found


@check(PRO, "watched-file-created", "a file created on disk (didChangeWatchedFiles) shows up in workspace symbols")
def watched_created(env):
    ses = watched_session(env)
    try:
        ses.open("main.nv")
        with open(os.path.join(ses.dir, "fresh.nv"), "w") as handle:
            handle.write("package a\n\nmethod freshOne() {\n}\n")
        ses.c.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": ses.c.uri_of("fresh.nv"), "type": 1}]})
        time.sleep(0.4)
        found = symbol_names(ses, "freshOne")
        return ("freshOne" in found, str(found))
    finally:
        ses.close()


@check(PRO, "watched-file-deleted", "a file deleted on disk disappears from workspace symbols")
def watched_deleted(env):
    ses = watched_session(env)
    try:
        ses.open("main.nv")
        found_before = symbol_names(ses, "helperOne")
        os.remove(os.path.join(ses.dir, "util.nv"))
        ses.c.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": ses.c.uri_of("util.nv"), "type": 3}]})
        time.sleep(0.5)
        res = ses.c.request("workspace/symbol", {"query": "helperOne"}).get("result") or []
        return (bool(found_before) and not res, str((found_before, res)))
    finally:
        ses.close()


@check(PRO, "other-file-edit-refreshes-diagnostics", "editing one open file re-validates another that depends on it (server.ts revalidateOthers)")
def revalidate_others(env):
    files = {"main.nv": "package a\n\nimport @lib\n\nmethod main {\n  println(helper())\n}\n", "lib.nv": "package a\n\nmethod helper(): integer {\n  return 1\n}\n"}
    ses = env.session(files)
    try:
        before = ses.diagnostics("main.nv")
        ses.c.notifications.clear()
        ses.set("lib.nv", "package a\n\nmethod other(): integer {\n  return 1\n}\n")
        after = ses.diagnostics("main.nv", 1.0)
        return (before == [] and any(d[1] == "unknown-name" for d in after), str((before, after)))
    finally:
        ses.close()


@check(PRO, "request-on-unopened-document", "a request for a document that was never opened answers without crashing")
def unopened_request(env):
    ses = env.shared("proto", {"main.nv": "package a\n"})
    res = ses.c.request("textDocument/hover", {"textDocument": {"uri": ses.c.uri_of("nope.nv")}, "position": {"line": 0, "character": 0}})
    alive = ses.c.proc.poll() is None
    return (alive and ("result" in res or "error" in res), str(res)[:100])


@check(PRO, "robust-every-position", "hover/definition/completion/signature/prepareRename/highlight/references/rename at ~1500 positions of syntax.nv: no crash, no error, none slower than 0.5 s")
def every_position(env):
    text = read(os.path.join(REPO, "test", "syntax.nv"))
    ses = env.session({"main.nv": text})
    try:
        rng = random.Random(7)
        methods = ["hover", "definition", "completion", "signatureHelp", "prepareRename", "documentHighlight", "references", "rename"]
        bad = []
        for line_no, line in enumerate(text.split("\n")):
            for col in sorted({0, len(line), len(line) // 2, rng.randint(0, max(len(line), 1))}):
                for name in methods:
                    extra = {"context": {"includeDeclaration": True}} if name == "references" else ({"newName": "zzz"} if name == "rename" else {})
                    started = time.time()
                    res = ses.c.at("textDocument/" + name, "main.nv", line_no, col, extra)
                    took = time.time() - started
                    if "error" in res and res["error"]["code"] not in (-32803, -32602) or took > 0.5:
                        bad.append((name, line_no, col, res.get("error"), round(took, 2)))
        alive = ses.c.proc.poll() is None
        return (alive and not bad, str(bad[:4]))
    finally:
        ses.close()


@check(PRO, "positions-out-of-range", "positions beyond the end of the file or line do not crash the server")
def out_of_range(env):
    ses = env.session({"main.nv": "package a\n\nmethod main {\n}\n"})
    try:
        for pos in [(10000, 0), (0, 10000), (-1, 0), (3, 50)]:
            for name in ["hover", "definition", "completion", "signatureHelp", "documentHighlight"]:
                ses.c.at("textDocument/" + name, "main.nv", pos[0], pos[1])
        return (ses.c.proc.poll() is None, "")
    finally:
        ses.close()


@check(PRO, "unicode-positions", "positions count UTF-16 units (a name after a non-BMP character resolves)")
def unicode_positions(env):
    src = "package a\n\nmethod main {\n  var gruss = \"\U0001F600ä\"\n  println(gruss)\n}\n"
    ses = env.session({"main.nv": src})
    try:
        got = ses.definition("main.nv", "println(gruss)", 0, 8)
        return (got == [("main.nv", 3)], str(got))
    finally:
        ses.close()
