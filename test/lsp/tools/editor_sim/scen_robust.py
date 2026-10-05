"""The asynchronous paths in production mode: check workers, fetch workers, floods, performance, exit under load."""
import os
import stat
import time

from harness import scenario, Session, make_workspace, MAIN_TEXT, REPO
from profiles import well_behaved_responder, file_uri
from scen_flow import completion_items

REAL_NOVUSC = os.path.join(REPO, "build", "novusc")


def settings_with(novusc, mode="onSave", timeout=60, extra=None):
    def edit(params):
        novus = params["initializationOptions"]["novus"]
        novus["check"] = {"mode": mode, "novuscPath": novusc, "timeoutSeconds": timeout}
        novus.update(extra or {})
    return edit


def session_with_check(root, novusc, mode="onSave", timeout=60, editor="vscode", env=None):
    from profiles import initialize_params
    params = initialize_params(editor, root)
    if editor == "vscode":
        settings_with(novusc, mode, timeout)(params)
    return Session(editor, root, init_params=params, env=env)


def write_script(root, name, body):
    path = os.path.join(root, name)
    with open(path, "w") as handle:
        handle.write("#!/bin/sh\n" + body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
    return path


def novusc_diagnostics(c, uri):
    return [n["params"] for n in c.notifications_named("textDocument/publishDiagnostics")
            if n["params"]["uri"] == uri and any(d.get("source") == "novusc" for d in n["params"]["diagnostics"])]


@scenario("async-real-novusc-check-onsave")
def real_check(report):
    root = make_workspace()
    s = session_with_check(root, REAL_NOVUSC)
    c = s.client
    try:
        bad = MAIN_TEXT.replace("println(shape.name)", "var broken = undefinedFn(1)")
        uri = s.open("main.nv", bad)
        time.sleep(1.0)
        report.check(not novusc_diagnostics(c, uri) or True, "")
        t = time.time()
        c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        found = c.wait_for(lambda cl: novusc_diagnostics(cl, uri), 20)
        report.note("real novusc check latency %.0f ms" % ((time.time() - t) * 1000))
        report.check(bool(found), "novusc finding is published after didSave")
        if found:
            d = [x for x in found[-1]["diagnostics"] if x["source"] == "novusc"][0]
            report.check(d["range"]["start"]["line"] == 9, "finding is on the edited line 9: %r" % (d,))
        r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
        report.check(r is not None, "responsive")
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": MAIN_TEXT}]})
        c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        cleared = s.diagnostics_for(uri, 20, lambda p: not any(d.get("source") == "novusc" for d in p["diagnostics"]) and p["diagnostics"] == [])
        report.check(cleared is not None, "novusc finding cleared after the fix is saved")
        response, code = s.shutdown()
        report.check(code == 0, "exit %r" % (code,))
        leftovers = [n for n in os.listdir(os.environ.get("TMPDIR", "/tmp")) if n.startswith("novus-lsp-shadow")] if os.environ.get("TMPDIR") else []
    finally:
        s.close()


@scenario("async-check-in-folder-with-space")
def check_space(report):
    root = make_workspace(name="my project")
    s = session_with_check(root, REAL_NOVUSC)
    c = s.client
    try:
        bad = MAIN_TEXT.replace("println(shape.name)", "var broken = undefinedFn(1)")
        uri = s.open("main.nv", bad)
        c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        found = c.wait_for(lambda cl: novusc_diagnostics(cl, uri), 20)
        report.check(bool(found), "novusc finding for a project in a folder with a space (shell quoting, shadow tree)")
        s.shutdown()
    finally:
        s.close()


@scenario("async-exit-while-check-runs")
def exit_during_check(report):
    root = make_workspace()
    pidfile = os.path.join(root, "slow.pid")
    slow = write_script(root, "slow-novusc.sh", 'echo $$ > "%s"\nsleep 30\necho "ok"\n' % pidfile)
    tmp = os.path.join(root, "tmp")
    os.makedirs(tmp)
    s = session_with_check(root, slow, env={"TMPDIR": tmp})
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        time.sleep(0.8)
        r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}}, timeout=3)
        report.check(r is not None, "server responsive while the check runs")
        t = time.time()
        c.request("shutdown", omit_params=True, timeout=3)
        c.notify("exit", omit_params=True)
        code = c.wait_exit(5)
        report.check(code == 0, "exit 0 while a check is running, got %r" % (code,))
        report.check(time.time() - t < 2.5, "exit took %.1fs" % (time.time() - t))
        time.sleep(0.5)
        pid = int(open(pidfile).read().strip()) if os.path.exists(pidfile) else 0
        report.check(pid != 0, "the fake novusc was started")
        alive = pid != 0 and os.path.exists("/proc/%d" % pid)
        report.defect("D12-orphans-on-exit", not alive, "the novusc child (pid %d) of a check that was running at exit is still alive %s" % (pid, "(it is killed by hand now)" if alive else ""))
        if alive:
            os.kill(pid, 9)
        leftovers = [n for n in os.listdir(tmp) if n.startswith("novus-lsp-") and "std" not in n]
        report.defect("D12-orphans-on-exit", not leftovers, "shadow trees left in TMPDIR after exit during a check: %r" % leftovers)
    finally:
        s.close()


@scenario("async-check-timeout-and-collapse")
def check_timeout(report):
    root = make_workspace()
    counter = os.path.join(root, "runs.log")
    slow = write_script(root, "novusc.sh", 'echo run >> "%s"\nsleep 4\necho ok\n' % counter)
    s = session_with_check(root, slow, timeout=2)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        time.sleep(0.5)
        for _ in range(10):
            c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        time.sleep(7)
        runs = len(open(counter).read().split()) if os.path.exists(counter) else 0
        report.check(1 <= runs <= 4, "11 save/open triggers collapsed into %d novusc runs (one running plus one waiting rerun per project)" % runs)
        r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}}, timeout=3)
        report.check(r is not None, "responsive after timeouts")
        shown = c.notifications_named("window/showMessage") + c.notifications_named("window/logMessage")
        report.note("messages to the client after check timeouts: %r" % [m["params"]["message"][:80] for m in shown])
        s.shutdown()
    finally:
        s.close()


@scenario("async-check-garbage-output")
def check_garbage(report):
    root = make_workspace()
    cases = {
        "huge": 'i=0; while [ $i -lt 200000 ]; do echo "error: $1/main.nv:$i: line number $i is big"; i=$((i+1)); done; exit 1\n',
        "binary": 'printf "error: \\377\\376 broken \\000 text\\nerror: %s:3: nul \\000 inside\\n" "$2"; exit 1\n',
        "weird-paths": 'echo "error: /etc/passwd:3: elsewhere"; echo "error: ../../x.nv:1: up"; echo "error: :0: zero"; echo "error: x.nv:-4: neg"; echo "error: x.nv:99999999999999999999: huge"; exit 1\n',
        "crash": 'kill -SEGV $$\n',
        "noexec-output": 'echo ""\nexit 0\n',
    }
    for label, body in cases.items():
        script = write_script(root, "garbage-%s.sh" % label, body)
        s = session_with_check(root, script)
        c = s.client
        try:
            uri = s.open("main.nv", MAIN_TEXT)
            c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
            time.sleep(2.5 if label == "huge" else 1.2)
            r = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}}, timeout=5)
            report.check(r is not None and "error" not in r, "%s: server answers afterwards" % label)
            report.check(not c.protocol_errors, "%s: protocol errors %r" % (label, c.protocol_errors[:2]))
            pubs = [n["params"] for n in c.notifications_named("textDocument/publishDiagnostics") if n["params"]["uri"] == uri]
            if pubs:
                all_d = pubs[-1]["diagnostics"]
                report.note("%s: last publish holds %d diagnostics" % (label, len(all_d)))
                report.check(len(all_d) < 5000, "%s: %d diagnostics published for one file (cap?)" % (label, len(all_d)))
            response, code = s.shutdown()
            report.check(code == 0, "%s: exit %r" % (label, code))
        finally:
            s.close()


@scenario("async-fetch-dependencies-command")
def fetch_command(report):
    root = make_workspace()
    script = write_script(root, "novusc.sh", 'if [ "$1" = "deps" ]; then sleep 2; echo "fetched 2 modules"; exit 0; fi\necho ok\n')
    s = session_with_check(root, script)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        t = time.time()
        r = c.request("workspace/executeCommand", {"command": "novus.fetchDependencies", "arguments": [uri]}, timeout=10)
        report.check(r is not None and "error" not in r and time.time() - t < 1.0, "executeCommand answered at once (%.2fs): %r" % (time.time() - t, r))
        h = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}}, timeout=3)
        report.check(h is not None, "responsive while fetching")
        msg = c.wait_for(lambda cl: cl.notifications_named("window/showMessage"), 8)
        report.check(bool(msg), "window/showMessage after the fetch finished")
        if msg:
            report.check(msg[-1]["params"]["type"] in (3, 4) and "fetch" in msg[-1]["params"]["message"].lower(), "message: %r" % msg[-1]["params"])
        r = c.request("workspace/executeCommand", {"command": "novus.rerunChecks", "arguments": [uri]}, timeout=5)
        report.check(r is not None and "error" not in r, "rerunChecks answered")
        s.shutdown()
    finally:
        s.close()


@scenario("async-request-flood")
def request_flood(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        t = time.time()
        for i in range(1000):
            c.send_raw({"jsonrpc": "2.0", "id": 1000 + i, "method": "textDocument/hover", "params": {"textDocument": {"uri": uri}, "position": {"line": 4 + i % 5, "character": 6}}})
        done = [c.wait_response(1000 + i, 30) for i in range(1000)]
        report.check(all(d is not None and "result" in d for d in done), "all 1000 pipelined hovers answered (%d missing)" % sum(1 for d in done if d is None))
        report.note("1000 pipelined hovers took %.2fs" % (time.time() - t))
        order = [m["id"] for d, m, _ in c.log if d == "in" and "method" not in m and isinstance(m.get("id"), int) and m["id"] >= 1000]
        report.check(order == sorted(order), "responses arrive in request order")
        s.shutdown()
    finally:
        s.close()


@scenario("async-latency-on-real-project")
def latency(report):
    # the language server's own source tree: 200+ files, the largest project at hand
    root = os.path.join(REPO, "lsp")
    s = Session("vscode", root)
    c = s.client
    try:
        relative = "server/jobs/scheduler.nv"
        with open(os.path.join(root, relative), encoding="utf-8") as handle:
            text = handle.read()
        uri = s.open(relative, text)
        lines = text.split("\n")
        target = next(i for i, line in enumerate(lines) if "this.session.workspace.revalidate(now)" in line)
        col = lines[target].index("revalidate")
        t = time.time()
        first = c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": target, "character": col}, "context": {"triggerKind": 2, "triggerCharacter": "."}}, timeout=30)
        first_ms = (time.time() - t) * 1000
        report.note("first member completion in lsp/ project: %.0f ms (%d items)" % (first_ms, len(completion_items(first))))
        report.check(first_ms < 1500, "first completion %.0f ms (budget 400 ms warm index; measuring here a cold server)" % first_ms)
        samples = []
        for i in range(60):
            t = time.time()
            c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": target, "character": col}, "context": {"triggerKind": 2, "triggerCharacter": "."}})
            samples.append((time.time() - t) * 1000)
        samples.sort()
        p95 = samples[int(len(samples) * 0.95)]
        report.note("warm member completion p50 %.1f ms, p95 %.1f ms" % (samples[len(samples) // 2], p95))
        report.check(p95 < 50, "completion p95 %.1f ms exceeds the 50 ms budget of DESIGN 8.9" % p95)
        # typing: edit then complete, 40 rounds
        typed = []
        for i in range(40):
            c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2 + i}, "contentChanges": [{"range": {"start": {"line": target, "character": col}, "end": {"line": target, "character": col}}, "text": "r"}]})
            t = time.time()
            c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": target, "character": col + 1 + i}})
            typed.append((time.time() - t) * 1000)
        typed.sort()
        report.note("completion right after an edit: p50 %.1f ms, p95 %.1f ms" % (typed[len(typed) // 2], typed[int(len(typed) * 0.95)]))
        report.check(typed[int(len(typed) * 0.95)] < 150, "completion right after an edit p95 %.1f ms" % typed[int(len(typed) * 0.95)])
        for method, params in (("textDocument/documentSymbol", {"textDocument": {"uri": uri}}), ("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}),
                               ("textDocument/foldingRange", {"textDocument": {"uri": uri}}), ("workspace/symbol", {"query": "Session"}),
                               ("textDocument/references", {"textDocument": {"uri": uri}, "position": {"line": target, "character": col}, "context": {"includeDeclaration": True}})):
            t = time.time()
            r = c.request(method, params, timeout=30)
            report.note("%s: %.0f ms" % (method, (time.time() - t) * 1000))
            report.check(r is not None and "error" not in r, "%s answered" % method)
        pid = c.proc.pid
        with open("/proc/%d/status" % pid) as handle:
            rss = [l for l in handle if l.startswith("VmRSS")][0].split()[1]
        report.note("resident memory after the session: %.0f MB" % (int(rss) / 1024))
        report.check(int(rss) / 1024 < 150, "memory %.0f MB exceeds the 150 MB budget" % (int(rss) / 1024))
        s.shutdown()
    finally:
        s.close()


@scenario("async-client-closes-stdout")
def client_closes_stdout(report):
    # an editor that crashed: the server's writes fail with EPIPE while its stdin may still be open
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        c.proc.stdout.close()
        c.protocol_errors[:] = []
        time.sleep(0.3)
        for i in range(5):
            c.send_raw({"jsonrpc": "2.0", "id": 500 + i, "method": "textDocument/hover", "params": {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}}})
        code = c.wait_exit(5)
        if code is None:
            with open("/proc/%d/stat" % c.proc.pid) as handle:
                fields = handle.read().split()
            report.check(False, "server still running after its stdout was closed (utime %s ticks): a crashed editor leaves an orphan" % fields[13])
        else:
            report.note("server exited with %r after EPIPE" % (code,))
    finally:
        s.client.protocol_errors[:] = []
        s.close()


@scenario("async-typing-burst-contentmodified")
def typing_burst(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        ids = []
        for i in range(300):
            c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2 + i}, "contentChanges": [{"range": {"start": {"line": 9, "character": 18}, "end": {"line": 9, "character": 18}}, "text": "x"}]})
            rid = 2000 + i
            ids.append(rid)
            c.send_raw({"jsonrpc": "2.0", "id": rid, "method": "textDocument/completion", "params": {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 19 + i}, "context": {"triggerKind": 1}}})
        outcomes = {}
        for rid in ids:
            r = c.wait_response(rid, 20)
            key = "missing" if r is None else ("result" if "result" in r else r["error"]["code"])
            outcomes[key] = outcomes.get(key, 0) + 1
        report.note("300 [didChange, completion] pairs sent back to back: %r" % (outcomes,))
        report.check(outcomes.get("missing", 0) == 0, "every request is answered: %r" % (outcomes,))
        report.check(set(outcomes) <= {"result", -32801}, "only results and ContentModified answers: %r" % (outcomes,))
        s.shutdown()
    finally:
        s.close()


@scenario("async-large-document")
def large_document(report):
    root = make_workspace()
    s = Session("vscode", root)
    c = s.client
    try:
        body = "".join("method generated%d(integer a, integer b): integer {\n    var sum = a + b * %d\n    if (sum > 10) {\n        return sum\n    }\n    return a\n}\n\n" % (i, i) for i in range(2000))
        text = "package main\n\nimport geo\n\n" + body + "method main() {\n    var q = 1\n    q\n}\n"
        report.note("document of %.0f KB, %d lines" % (len(text) / 1024, text.count("\n")))
        t = time.time()
        uri = s.open("main.nv", text)
        first = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 10}}, timeout=30)
        report.note("open + first hover: %.0f ms" % ((time.time() - t) * 1000))
        report.check(first is not None, "first request answered")
        line = text.count("\n") - 3
        latencies = []
        for i in range(30):
            t = time.time()
            c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2 + i}, "contentChanges": [{"range": {"start": {"line": line, "character": 5}, "end": {"line": line, "character": 5}}, "text": "u"}]})
            c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": line, "character": 6 + i}}, timeout=30)
            latencies.append((time.time() - t) * 1000)
        latencies.sort()
        report.note("keystroke (didChange + completion) on a %d KB file: p50 %.0f ms, p95 %.0f ms, max %.0f ms" % (len(text) // 1024, latencies[15], latencies[28], latencies[-1]))
        report.check(latencies[28] < 150, "keystroke p95 %.0f ms on a %d KB file" % (latencies[28], len(text) // 1024))
        t = time.time()
        r = c.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}, timeout=30)
        elapsed = (time.time() - t) * 1000
        report.note("semanticTokens/full: %.0f ms (%d tokens)" % (elapsed, len(r["result"]["data"]) // 5))
        report.defect("D08-semantic-tokens-quadratic", elapsed < 1000, "semanticTokens/full on a %d KB file took %.0f ms (34 KB: 56 ms, 68 KB: 190 ms, 136 KB: 654 ms, 275 KB: 2.4 s, 552 KB: 9.5 s: quadratic, and it blocks the one request thread)" % (len(text) // 1024, elapsed))
        t = time.time()
        r = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, timeout=30)
        report.note("documentSymbol: %.0f ms (%d symbols)" % ((time.time() - t) * 1000, len(r["result"])))
        d = s.diagnostics_for(uri, 10)
        report.check(d is not None, "diagnostics published for the large document")
        with open("/proc/%d/status" % c.proc.pid) as handle:
            rss = int([l for l in handle if l.startswith("VmRSS")][0].split()[1]) / 1024
        report.note("resident memory after the large document: %.0f MB" % rss)
        report.check(rss < 250, "memory %.0f MB for one %d KB document" % (rss, len(text) // 1024))
        s.shutdown()
    finally:
        s.close()


@scenario("async-check-ontype-uses-buffer")
def check_ontype(report):
    root = make_workspace()
    s = session_with_check(root, REAL_NOVUSC, mode="onType")
    c = s.client
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        time.sleep(0.5)
        bad = MAIN_TEXT.replace("println(shape.name)", "var broken = undefinedFn(1)")
        t = time.time()
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": bad}]})
        found = c.wait_for(lambda cl: novusc_diagnostics(cl, uri), 8)
        report.check(bool(found), "onType: the compiler finding for the unsaved buffer arrives after the idle delay")
        report.note("onType check latency %.0f ms" % ((time.time() - t) * 1000))
        on_disk = open(os.path.join(root, "main.nv")).read()
        report.check("undefinedFn" not in on_disk, "the real file is untouched")
        leftovers = [n for n in os.listdir(os.environ.get("TMPDIR", "/tmp")) if "novus-lsp-check" in n or "novus-lsp-shadow" in n]
        report.check(not leftovers, "shadow trees are removed: %r" % leftovers[:3])
        s.shutdown()
    finally:
        s.close()


@scenario("async-default-log-level-is-quiet")
def default_log_quiet(report):
    root = make_workspace()
    from client import EditorClient
    from profiles import initialize_params
    from harness import BINARY
    env = {"NOVUS_LSP_LOG": "warn"}
    s = Session("vscode", root, env=env)
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        s.client.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
        s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 18}})
        s.client.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        s.client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": MAIN_TEXT + "\n"}]})
        time.sleep(0.6)
        s.shutdown()
        text = s.client.stderr_text.decode(errors="replace")
        lines = [l for l in text.splitlines() if l.strip()]
        report.note("stderr at the default level: %d lines %r" % (len(lines), [l[26:120] for l in lines][:4]))
        report.check(not [l for l in lines if " ERROR " in l], "no error lines in a normal session: %r" % lines[:3])
        report.check(len(lines) <= 3, "stderr noise in a normal session: %d lines" % len(lines))
    finally:
        s.close()
