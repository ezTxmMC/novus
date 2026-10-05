"""Document synchronisation, diagnostics timing, uri spellings, watched files and workspace folders."""
import os
import random
import re
import shutil
import time

from harness import scenario, Session, make_workspace, MAIN_TEXT
from profiles import file_uri
from scen_flow import completion_items
from textmodel import Model, width

PADS = ["", "a", "é", "中文", "\U0001F600", "x\U0001F600éy", "\U0001F468‍\U0001F469‍\U0001F467", "é"]


def symbol_text(count, newline):
    return newline.join("/* pad */ method f%d() {}" % i for i in range(count)) + newline


def symbol_positions(model, count):
    found = {}
    for index in range(count):
        needle = "method f%d(" % index
        offset = model.text.find(needle)
        line, column = model.position(offset + len("method "))
        found["f%d" % index] = (line, column)
    return found


def flatten(symbols):
    for entry in symbols:
        yield entry
        yield from flatten(entry.get("children") or [])


def sync_property(report, encoding, newline, label, seed):
    root = make_workspace()
    offered = [encoding] if encoding != "utf-16" else ["utf-16"]
    def edit(caps):
        caps["general"]["positionEncodings"] = offered
    s = Session("vscode", root, caps_edit=edit)
    try:
        got = s.init_result["result"]["capabilities"]["positionEncoding"]
        report.check(got == encoding, "%s: encoding %r" % (label, got))
        count = 12
        model = Model(symbol_text(count, newline), encoding)
        uri = s.open("main.nv", model.text)
        rng = random.Random(seed)
        version = 1
        mismatches = 0
        for step in range(60):
            version += 1
            changes = []
            for _ in range(rng.choice([1, 1, 2, 3])):
                index = rng.randrange(count)
                needle = "method f%d(" % index
                off = model.text.find(needle)
                comment_open = model.text.rfind("/*", 0, off)
                inner_start = comment_open + 3
                inner_end = model.text.find(" */", inner_start)
                a, b = model.position(inner_start), model.position(inner_end)
                new = rng.choice(PADS) + rng.choice(PADS)
                if rng.random() < 0.15:
                    new = new + "\n" + rng.choice(PADS)
                if newline == "\r\n":
                    new = new.replace("\n", "\r\n")
                change = {"range": {"start": {"line": a[0], "character": a[1]}, "end": {"line": b[0], "character": b[1]}}, "text": new}
                if rng.random() < 0.5:
                    change["rangeLength"] = len(model.text[model.offset(*a):model.offset(*b)])
                changes.append(change)
                model.apply(a, b, new)
            s.client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version}, "contentChanges": changes})
            if step % 6 == 5:
                resp = s.client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
                symbols = list(flatten(resp.get("result") or []))
                expected = symbol_positions(model, count)
                for symbol in symbols:
                    if symbol["name"] in expected or symbol["name"].rstrip("()") in expected:
                        name = symbol["name"].rstrip("()")
                        sel = symbol["selectionRange"]["start"]
                        if (sel["line"], sel["character"]) != expected[name]:
                            mismatches += 1
                            if mismatches <= 2:
                                report.check(False, "%s step %d: symbol %s at %r, model says %r" % (label, step, name, (sel["line"], sel["character"]), expected[name]))
                report.check(len(symbols) >= count, "%s step %d: %d symbols (expected %d)" % (label, step, len(symbols), count)) if len(symbols) < count else None
        report.check(mismatches == 0, "%s: %d symbol position mismatches after random incremental edits" % (label, mismatches))
        diag = s.diagnostics_for(uri, 3)
        report.check(diag is None or diag["diagnostics"] == [], "%s: still no diagnostics for a valid file: %r" % (label, diag and diag["diagnostics"][:2]))
        s.shutdown()
    finally:
        s.close()


@scenario("documents-incremental-sync-utf16")
def sync_utf16(report):
    sync_property(report, "utf-16", "\n", "utf-16 LF", 11)
    sync_property(report, "utf-16", "\r\n", "utf-16 CRLF", 12)


@scenario("documents-incremental-sync-utf8")
def sync_utf8(report):
    sync_property(report, "utf-8", "\n", "utf-8 LF", 21)
    sync_property(report, "utf-8", "\r\n", "utf-8 CRLF", 22)


@scenario("documents-lone-cr")
def lone_cr(report):
    # LSP 3.17: '\r' alone is a line terminator, too. The server treats only \n and \r\n as breaks (DESIGN 5.1).
    root = make_workspace()
    s = Session("vscode", root)
    try:
        text = "package main\rmethod main() {\r}\r"
        uri = s.open("main.nv", text)
        resp = s.client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        symbols = list(flatten(resp.get("result") or []))
        main = [x for x in symbols if x["name"].startswith("main")]
        got = main[0]["selectionRange"]["start"] if main else None
        report.defect("D02-lone-cr", got == {"line": 1, "character": 7}, "lone-CR document: 'main' should be at 1:7 for the client, server says %r" % (got,))
        s.shutdown()
    finally:
        s.close()


@scenario("documents-sync-misuse")
def sync_misuse(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.uri("main.nv")
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3}, "contentChanges": [{"text": "x"}]})
        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        s.open("main.nv", MAIN_TEXT, version=1)
        s.open("main.nv", MAIN_TEXT, version=1)        # duplicate open (restart of the VS Code client re-sends)
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 0}, "contentChanges": [{"text": MAIN_TEXT}]})  # older version
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 5}, "contentChanges": []})
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 6}, "contentChanges": [{"range": {"start": {"line": 500, "character": 3}, "end": {"line": 600, "character": 1}}, "text": "zz"}]})
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 7}, "contentChanges": [{"range": {"start": {"line": 3, "character": 9}, "end": {"line": 1, "character": 0}}, "text": "reversed"}]})
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 8}, "contentChanges": [{"range": {"start": {"line": 0, "character": 3}, "end": {"line": 0, "character": 2000000000}}, "text": ""}]})
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 9}, "contentChanges": [{"range": None, "text": MAIN_TEXT}]})
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri}, "contentChanges": [{"text": MAIN_TEXT}]})  # no version
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 10}, "contentChanges": "nope"})
        c.notify("textDocument/didOpen", {"textDocument": {"uri": uri}})
        c.notify("textDocument/didOpen", None)
        c.notify("textDocument/didClose", None)
        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
        report.check(r is not None and "error" not in r, "alive after sync misuse: %r" % (r,))
        s.shutdown()
    finally:
        s.close()


@scenario("documents-diagnostics-timeline")
def diagnostics_timeline(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        first = s.diagnostics_for(uri, 3)
        report.check(first is not None and first["diagnostics"] == [], "initial publish is empty")
        before = len(c.notifications_named("textDocument/publishDiagnostics"))
        bad = "package main\n\nmethod main() {\n    var x = (\n}\n"
        t = time.time()
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": bad}]})
        got = s.diagnostics_for(uri, 4, lambda p: len(p["diagnostics"]) > 0)
        latency = time.time() - t
        report.check(got is not None, "syntax error published after the debounce")
        report.check(latency < 1.0, "syntax error latency %.2fs (debounce is 150 ms)" % latency)
        report.note("syntax diagnostic latency %.0f ms" % (latency * 1000))
        if got:
            report.check(got.get("version") in (None, 2), "publish carries version 2 or none, got %r" % got.get("version"))
        # typing storm: 100 changes without pauses, ending on valid text
        for i in range(100):
            text = bad if i % 2 == 0 else MAIN_TEXT
            c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 10 + i}, "contentChanges": [{"text": text}]})
        final = s.diagnostics_for(uri, 4, lambda p: p["diagnostics"] == [] and p.get("version") in (None, 109))
        report.check(final is not None, "final publish after a typing storm is empty for the last text")
        time.sleep(0.5)
        last = c.notifications_named("textDocument/publishDiagnostics")[-1]["params"]
        report.check(last["diagnostics"] == [], "last published state is the clean one: %r" % (last["diagnostics"][:1],))
        n = len(c.notifications_named("textDocument/publishDiagnostics")) - before
        report.check(n <= 20, "100 rapid changes produced %d publishes (debounce should coalesce)" % n)
        report.note("publishes for a 100-change storm: %d" % n)
        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        cleared = s.diagnostics_for(uri, 3, lambda p: p["diagnostics"] == [])
        report.check(cleared is not None, "didClose clears")
        s.shutdown()
    finally:
        s.close()


@scenario("documents-uri-spellings")
def uri_spellings(report):
    base = make_workspace(name="my project äö")
    s = Session("vscode", base)
    try:
        report.check(os.path.isfile(os.path.join(base, "project.nv")), "fixture")
        uri = s.uri("main.nv")
        report.note("uri with space/umlaut: " + uri)
        s.open("main.nv", MAIN_TEXT)
        pub = s.diagnostics_for(uri, 3)
        report.check(pub is not None, "publishDiagnostics uses the uri exactly as opened (%%20 / %%C3%%A4 spelling)")
        comp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 17}})
        labels = [i["label"] for i in completion_items(comp)]
        report.check("Shape" in labels, "project symbols found in a folder with space and umlauts: %r" % labels[:5])
        definition = s.client.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 17}})
        result = definition.get("result") or []
        result = result if isinstance(result, list) else [result]
        uris = [(r.get("uri") or r.get("targetUri")) for r in result]
        report.check(any(u and u.endswith("/geo/shape.nv") for u in uris), "definition target %r" % uris)
        report.check(all(u == s.uri("geo/shape.nv") for u in uris if u), "definition uri spelled like the client's: %r vs %r" % (uris, s.uri("geo/shape.nv")))
        # same document through other spellings
        lower = uri.replace("%C3", "%c3").replace("%20", "%20")
        r = s.client.request("textDocument/hover", {"textDocument": {"uri": lower}, "position": {"line": 4, "character": 8}})
        report.note("hover with lower-case percent escapes: %s" % ("answered with content" if r and r.get("result") else "null"))
        s.shutdown()
    finally:
        s.close()


@scenario("documents-untitled-and-foreign-uris")
def untitled(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        for uri in ("untitled:Untitled-1", "vscode-userdata:/x/y.nv", "file:///c%3A/Users/a/x.nv", "git:/x.nv?ref", "file://localhost" + os.path.join(root, "main.nv")):
            c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 1, "text": "package main\n\nmethod main() {\n    sout\n    var q = (\n}\n"}})
            pub = s.diagnostics_for(uri, 3, lambda p: len(p["diagnostics"]) > 0)
            report.check(pub is not None, "%s: syntax diagnostics for the document are published" % uri)
            comp = c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 8}})
            labels = [i["label"] for i in completion_items(comp)]
            report.check("sout" in labels, "%s: snippet completion works (%r)" % (uri, labels[:3]))
            c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        s.shutdown()
    finally:
        s.close()


@scenario("documents-nvh-and-manifest")
def nvh_manifest(report):
    root = make_workspace(files={"page.nvh": "<h1>${title}</h1>\n<p>hello</p>\n"})
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("page.nvh", language="novus-html")
        r = c.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        report.check(r and "result" in r, "semantic tokens on .nvh: %r" % (r,))
        r = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        report.check(r and "result" in r, "documentSymbol on .nvh")
        r = c.request("textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 2, "insertSpaces": True}})
        report.check(r and (r.get("result") in (None, [])), "formatting .nvh is a no-op: %r" % (r,))
        muri = s.open("project.nv")
        r = c.request("textDocument/completion", {"textDocument": {"uri": muri}, "position": {"line": 4, "character": 0}})
        report.check(r and "result" in r, "completion in project.nv")
        c.notify("textDocument/didChange", {"textDocument": {"uri": muri, "version": 2}, "contentChanges": [{"text": 'project "demo"\nversion "x"\nmain "nope.nv"\nrequire\n'}]})
        d = s.diagnostics_for(muri, 3, lambda p: len(p["diagnostics"]) > 0)
        report.check(d is not None, "manifest problems are published")
        s.shutdown()
    finally:
        s.close()


@scenario("documents-watched-files")
def watched_files(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", "package main\n\nimport geo\n\nmethod main() {\n    var z = 1\n    Ex\n}\n")
        def labels_at():
            r = c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 6, "character": 6}})
            return [i["label"] for i in completion_items(r)]
        report.check("Extra" not in labels_at(), "Extra not known yet")
        extra = os.path.join(root, "geo", "extra.nv")
        with open(extra, "w") as handle:
            handle.write("package geo\n\ndefine class Extra {\n    string tag\n}\n")
        c.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": file_uri(extra), "type": 1}]})
        time.sleep(0.4)
        report.check("Extra" in labels_at(), "created file is picked up after didChangeWatchedFiles")
        with open(extra, "w") as handle:
            handle.write("package geo\n\ndefine class Extra2 {\n    string tag\n}\n")
        c.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": file_uri(extra), "type": 2}]})
        time.sleep(0.4)
        names = labels_at()
        report.check("Extra2" in names and "Extra" not in names, "changed file re-read: %r" % [n for n in names if n.startswith("Extra")])
        os.remove(extra)
        c.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": file_uri(extra), "type": 3}]})
        time.sleep(0.4)
        report.check("Extra2" not in labels_at(), "deleted file disappears")
        # without any notification the file system change is picked up by the periodic revalidation (1 s)
        with open(extra, "w") as handle:
            handle.write("package geo\n\ndefine class ExSilent {\n}\n")
        deadline = time.time() + 2.5
        seen = False
        while time.time() < deadline and not seen:
            time.sleep(0.5)
            seen = "ExSilent" in labels_at()
        report.defect("D03-new-file-without-watcher", seen, "a new file is found by revalidation even without a watcher event (clients without dynamic registration)")
        # events for files outside the workspace, junk uris
        c.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": "file:///etc/passwd", "type": 2}, {"uri": "junk", "type": 9}, {"type": 1}, {"uri": 5, "type": "x"}]})
        c.notify("workspace/didChangeWatchedFiles", {"changes": []})
        c.notify("workspace/didChangeWatchedFiles", {})
        r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
        report.check(r is not None and "error" not in r, "alive after junk watcher events")
        s.shutdown()
    finally:
        s.close()


@scenario("documents-workspace-folders")
def workspace_folders(report):
    root = make_workspace()
    other = make_workspace(name="other", files={"main.nv": "package main\n\nmethod main() {\n}\n", "project.nv": 'project "other"\nversion "0.1.0"\nmain "main.nv"\n', "lib/tool.nv": "package lib\n\ndefine class OtherTool {\n}\n"})
    s = Session("vscode", root)
    c = s.client
    try:
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {"added": [{"uri": file_uri(other), "name": "other"}], "removed": []}})
        r = c.request("workspace/symbol", {"query": "OtherTool"}, timeout=10)
        names = [x.get("name") for x in (r.get("result") or [])]
        report.check("OtherTool" in names, "workspace/symbol finds a class of an added folder: %r" % names)
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {"added": [], "removed": [{"uri": file_uri(other), "name": "other"}]}})
        r = c.request("workspace/symbol", {"query": "OtherTool"}, timeout=10)
        names = [x.get("name") for x in (r.get("result") or [])]
        report.defect("D04-removed-folder-stays", "OtherTool" not in names, "removed folder's symbols are gone: %r" % names)
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {}})
        c.notify("workspace/didChangeWorkspaceFolders", {})
        r = c.request("workspace/symbol", {"query": "Shape"}, timeout=10)
        report.check(any(x.get("name") == "Shape" for x in (r.get("result") or [])), "original folder still indexed")
        s.shutdown()
    finally:
        s.close()


@scenario("documents-close-races")
def close_races(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    bad = "package main\n\nmethod main() {\n    var x = (\n}\n"
    try:
        for label in ("change then close", "close then reopen", "change, close, reopen, change"):
            uri = s.uri("geo/shape.nv" if label[0] == "c" and label.startswith("change then") else "main.nv")
            c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 1, "text": bad}})
            if label == "change then close":
                c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": bad + "\n"}]})
                c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
                final_open = False
            elif label == "close then reopen":
                c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
                c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 3, "text": MAIN_TEXT}})
                final_open = True
            else:
                c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": bad + "\n"}]})
                c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
                c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 3, "text": MAIN_TEXT}})
                c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 4}, "contentChanges": [{"text": bad}]})
                final_open = True
            time.sleep(1.0)
            last = [n["params"] for n in c.notifications_named("textDocument/publishDiagnostics") if n["params"]["uri"] == uri][-1]
            if label.startswith("change, close"):
                report.check(len(last["diagnostics"]) > 0, "%s: the reopened and edited document ends with its syntax error: %r" % (label, last["diagnostics"][:1]))
            else:
                report.check(last["diagnostics"] == [], "%s: last publish for %s is empty, got %r" % (label, "an open clean doc" if final_open else "a closed doc", last["diagnostics"][:1]))
            c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
            time.sleep(0.3)
        s.shutdown()
    finally:
        s.close()


@scenario("lifecycle-no-initialized-notification")
def no_initialized(report):
    root = make_workspace()
    s = Session("vscode", root, handshake=False)
    c = s.client
    try:
        from profiles import initialize_params
        r = c.request("initialize", initialize_params("vscode", root), request_id="init-1")
        report.check(r and r.get("id") == "init-1" and "result" in r, "initialize with a string id")
        uri = s.open("main.nv", MAIN_TEXT)
        h = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}}, timeout=5)
        report.check(h is not None and "error" not in h, "a client that never sends 'initialized' still gets answers: %r" % (h,))
        d = s.diagnostics_for(uri, 3)
        report.check(d is not None, "diagnostics without 'initialized'")
        s.shutdown()
    finally:
        s.close()
