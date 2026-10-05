"""Minimal LSP client used by the e2e checks of the user-facing features (snippets, project and dependency completion)."""
import atexit
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
from urllib.parse import quote

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SERVER = os.environ.get("NOVUS_LSP_BIN", os.path.join(ROOT, "build", "novus-lsp"))
NOVUSC = os.environ.get("NOVUSC", os.path.join(ROOT, "build", "novusc"))


def uri_of(path):
    return "file://" + quote(path)


class Client:
    def __init__(self, root, env=None, snippets=True, options=None, extra_caps=None, timeout=30):
        self.root = root
        self.timeout = timeout
        self.next_id = 1
        self.pending = {}
        self.notes = []
        self.messages = []
        self.lock = threading.Condition()
        self.versions = {}
        environment = dict(os.environ)
        environment["NOVUS_LSP_TEST"] = "0"
        environment.pop("NOVUS_LSP_TEST")
        environment.update(env or {})
        self.proc = subprocess.Popen([SERVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, env=environment)
        self.stderr = []
        threading.Thread(target=self._read_err, daemon=True).start()
        threading.Thread(target=self._read, daemon=True).start()
        caps = {"textDocument": {"completion": {"completionItem": {
            "snippetSupport": snippets, "labelDetailsSupport": True,
            "documentationFormat": ["markdown", "plaintext"],
            "resolveSupport": {"properties": ["documentation", "detail", "additionalTextEdits"]},
            "insertTextModeSupport": {"valueSet": [1, 2]}}},
            "hover": {"contentFormat": ["markdown", "plaintext"]}},
            "workspace": {"didChangeWatchedFiles": {"dynamicRegistration": True}}}
        if extra_caps:
            caps.update(extra_caps)
        params = {"processId": None, "rootUri": uri_of(root), "capabilities": caps}
        if options is not None:
            params["initializationOptions"] = options
        self.init = self.request("initialize", params)
        self.notify("initialized", {})

    def _read_err(self):
        for line in self.proc.stderr:
            self.stderr.append(line.decode("utf-8", "replace"))

    def _read(self):
        out = self.proc.stdout
        while True:
            length = None
            while True:
                line = out.readline()
                if not line:
                    return
                line = line.strip()
                if not line:
                    break
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":")[1])
            body = out.read(length)
            message = json.loads(body.decode("utf-8"))
            self._dispatch(message)

    def _dispatch(self, message):
        with self.lock:
            if "method" in message and "id" in message:
                self._answer_server_request(message)
            elif "method" in message:
                self.notes.append(message)
            else:
                self.pending[message["id"]] = message
            self.messages.append(message)
            self.lock.notify_all()

    def _answer_server_request(self, message):
        result = None
        if message["method"] == "workspace/configuration":
            result = [None for _ in message["params"]["items"]]
        self._send({"jsonrpc": "2.0", "id": message["id"], "result": result})

    def _send(self, obj):
        data = json.dumps(obj).encode("utf-8")
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
        self.proc.stdin.flush()

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=None):
        request_id = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        deadline = time.time() + (timeout or self.timeout)
        with self.lock:
            while request_id not in self.pending:
                left = deadline - time.time()
                if left <= 0:
                    raise TimeoutError("no answer to %s" % method)
                self.lock.wait(left)
            return self.pending.pop(request_id)

    def open(self, path, text, language="novus"):
        self.versions[path] = 1
        self.notify("textDocument/didOpen", {"textDocument": {"uri": uri_of(path), "languageId": language,
                                                              "version": 1, "text": text}})

    def change(self, path, text):
        self.versions[path] = self.versions.get(path, 1) + 1
        self.notify("textDocument/didChange", {"textDocument": {"uri": uri_of(path), "version": self.versions[path]},
                                               "contentChanges": [{"text": text}]})

    def save(self, path):
        self.notify("textDocument/didSave", {"textDocument": {"uri": uri_of(path)}})

    def watched(self, path, kind):
        self.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": uri_of(path), "type": kind}]})

    def complete(self, path, line, character, trigger=None):
        context = {"triggerKind": 1}
        if trigger:
            context = {"triggerKind": 2, "triggerCharacter": trigger}
        answer = self.request("textDocument/completion", {"textDocument": {"uri": uri_of(path)},
                              "position": {"line": line, "character": character}, "context": context})
        result = answer.get("result")
        if result is None:
            return []
        return result["items"] if isinstance(result, dict) else result

    def resolve(self, item):
        return self.request("completionItem/resolve", item).get("result")

    def hover(self, path, line, character):
        return self.request("textDocument/hover", {"textDocument": {"uri": uri_of(path)},
                            "position": {"line": line, "character": character}}).get("result")

    def definition(self, path, line, character):
        return self.request("textDocument/definition", {"textDocument": {"uri": uri_of(path)},
                            "position": {"line": line, "character": character}}).get("result")

    def diagnostics(self, path, wait=1.0):
        time.sleep(wait)
        found = None
        with self.lock:
            for message in self.notes:
                if message["method"] == "textDocument/publishDiagnostics" and message["params"]["uri"] == uri_of(path):
                    found = message["params"]["diagnostics"]
        return found

    def close(self):
        try:
            self.request("shutdown", None, timeout=10)
            self.notify("exit", None)
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


def at(text, marker="|"):
    """Remove the first marker from text and return (text, line, character in UTF-16 units)."""
    index = text.index(marker)
    cleaned = text[:index] + text[index + len(marker):]
    before = text[:index]
    line = before.count("\n")
    last = before.rsplit("\n", 1)[-1].replace("\r", "")
    return cleaned, line, len(last.encode("utf-16-le")) // 2


def apply_edits(text, edits):
    """Apply LSP text edits (UTF-16 positions) to text, last first."""
    lines = text.split("\n")
    def offset(position):
        total = 0
        for i in range(position["line"]):
            total += len(lines[i]) + 1
        raw = lines[position["line"]] if position["line"] < len(lines) else ""
        units = 0
        for index, char in enumerate(raw):
            if units >= position["character"]:
                return total + index
            units += 2 if ord(char) > 0xFFFF else 1
        return total + len(raw)
    ordered = sorted(edits, key=lambda e: (e["range"]["start"]["line"], e["range"]["start"]["character"]), reverse=True)
    for edit in ordered:
        start = offset(edit["range"]["start"])
        end = offset(edit["range"]["end"])
        text = text[:start] + edit["newText"] + text[end:]
        lines = text.split("\n")
    return text


SCRATCH = []
atexit.register(lambda: [shutil.rmtree(path, ignore_errors=True) for path in SCRATCH if not os.environ.get("E2E_KEEP")])


def make_project(files, base=None):
    root = tempfile.mkdtemp(prefix="nvlspe2e", dir=base or os.environ.get("E2E_TMP"))
    SCRATCH.append(root)
    for name, content in files.items():
        full = os.path.join(root, name)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", newline="") as handle:
            handle.write(content)
    return root
