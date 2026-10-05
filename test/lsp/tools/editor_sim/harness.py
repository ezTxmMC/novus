"""Test registry, workspace fixture and session helpers shared by the scenario modules."""
import json
import os
import shutil
import tempfile
import time
import traceback

from client import EditorClient
from profiles import initialize_params, well_behaved_responder, file_uri

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
FIXTURE = os.path.join(REPO, "test", "lsp", "completion-basic", "files")
BINARY = os.environ.get("NOVUS_LSP_BIN", os.path.join(REPO, "build", "novus-lsp"))

MAIN_TEXT = ('package main\n\nimport geo\n\nmethod main() {\n    var count = 3\n    var label = "\U0001F600é"\n'
             '    println("\U0001F600é" + count)\n    var shape = Shape("round", 2.5)\n    println(shape.name)\n}\n')

TESTS = []
VIOLATIONS = []


def scenario(name):
    def register(function):
        TESTS.append((name, function))
        return function
    return register


class Report:
    def __init__(self, name):
        self.name = name
        self.failures = []
        self.checks = 0
        self.notes = []
        self.known = []

    def check(self, condition, message):
        self.checks += 1
        if not condition:
            self.failures.append(message)
        return bool(condition)

    def defect(self, defect_id, condition, message):
        """A check that is known to fail (see known_defects.txt): a failure is expected, a pass means the defect is fixed."""
        self.checks += 1
        if condition:
            self.failures.append("known defect %s no longer reproduces (%s): remove it from known_defects.txt and make it a plain check" % (defect_id, message))
        else:
            self.known.append("%s: %s" % (defect_id, message))

    def note(self, message):
        self.notes.append(message)


def make_workspace(parent=None, name="ws", files=None):
    base = tempfile.mkdtemp(prefix="edsim-", dir=parent or os.environ.get("EDSIM_TMP"))
    root = os.path.join(base, name)
    shutil.copytree(FIXTURE, root)
    for relative, text in (files or {}).items():
        target = os.path.join(root, relative)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as handle:
            handle.write(text)
    return root


class Session:
    """An initialized session of one simulated editor."""

    def __init__(self, editor, root, caps_edit=None, responder=well_behaved_responder, env=None, extra=None,
                 init_params=None, args=None, handshake=True):
        self.editor = editor
        self.root = root
        params = init_params if init_params is not None else initialize_params(editor, root, extra)
        if caps_edit:
            caps_edit(params["capabilities"])
        self.params = params
        self.client = EditorClient(BINARY, root, env=env, responder=responder, args=args, name=editor)
        self.init_result = None
        options = params.get("initializationOptions")
        self.client.config = options if isinstance(options, dict) else None
        if handshake:
            self.init_result = self.client.request("initialize", params)
            self.client.notify("initialized", {})

    def uri(self, relative):
        return file_uri(os.path.join(self.root, relative))

    def open(self, relative, text=None, language="novus", version=1, uri=None):
        path = os.path.join(self.root, relative)
        if text is None:
            with open(path, encoding="utf-8") as handle:
                text = handle.read()
        uri = uri or self.uri(relative)
        self.client.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": language,
                                                                    "version": version, "text": text}})
        return uri

    def result(self, method, params, timeout=20.0):
        response = self.client.request(method, params, timeout=timeout)
        return response

    def diagnostics_for(self, uri, timeout=5.0, predicate=None):
        def find(client):
            for note in reversed(client.notifications):
                if note["method"] == "textDocument/publishDiagnostics" and note["params"]["uri"] == uri:
                    if predicate is None or predicate(note["params"]):
                        return note["params"]
            return None
        return self.client.wait_for(find, timeout)

    def shutdown(self, expect_rc=0):
        response = self.client.request("shutdown", omit_params=True, timeout=10)
        self.client.notify("exit", omit_params=True)
        code = self.client.wait_exit(5)
        if code is None:
            self.client.kill()
        return response, code

    def close(self):
        self.client.kill()
        VIOLATIONS.extend("%s: %s" % (self.editor, text) for text in self.client.protocol_errors)


def run_all(selected=None):
    outcomes = []
    for name, function in TESTS:
        if selected and not any(part in name for part in selected):
            continue
        report = Report(name)
        started = time.time()
        try:
            function(report)
        except Exception:
            report.failures.append("EXCEPTION " + traceback.format_exc(limit=4))
        report.seconds = time.time() - started
        for text in VIOLATIONS:
            report.failures.append("protocol violation by the server: " + text)
        del VIOLATIONS[:]
        outcomes.append(report)
        status = "ok  " if not report.failures else "FAIL"
        print("%s %-52s %3d checks %5.1fs" % (status, name, report.checks, report.seconds))
        for failure in report.failures:
            print("       - " + failure.replace("\n", "\n         "))
        for note in report.notes:
            print("       . " + note)
        for known in report.known:
            print("       x known defect " + known)
    return outcomes
