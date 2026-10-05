"""Minimal JSON-RPC/LSP client over stdio for the parity driver (test/lsp/parity/parity.py)."""
import json
import os
import subprocess
import threading
import time
import queue

READ_TIMEOUT = 20.0


class Client:
    def __init__(self, binary, root, env=None, options=None, caps=None):
        self.root = root
        self.uri = "file://" + root
        full_env = dict(os.environ)
        full_env.update(env or {})
        self.proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, env=full_env)
        self.inbox = queue.Queue()
        self.notifications = []
        self.next_id = 1
        self.versions = {}
        threading.Thread(target=self._reader, daemon=True).start()
        params = {"processId": None, "rootUri": self.uri, "capabilities": caps or default_caps(),
                  "workspaceFolders": [{"uri": self.uri, "name": os.path.basename(root)}]}
        if options is not None:
            params["initializationOptions"] = options
        self.init = self.request("initialize", params)
        self.notify("initialized", {})

    def _reader(self):
        out = self.proc.stdout
        while True:
            length = None
            while True:
                line = out.readline()
                if not line:
                    self.inbox.put(None)
                    return
                line = line.strip()
                if not line:
                    break
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":")[1])
            if length is None:
                continue
            body = out.read(length)
            self.inbox.put(json.loads(body.decode("utf-8")))

    def send(self, obj):
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
        self.proc.stdin.flush()

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=READ_TIMEOUT):
        rid = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        deadline = time.time() + timeout
        while True:
            left = deadline - time.time()
            if left <= 0:
                raise TimeoutError(method)
            try:
                msg = self.inbox.get(timeout=left)
            except queue.Empty:
                raise TimeoutError(method)
            if msg is None:
                raise EOFError("server closed during " + method)
            if "method" in msg and "id" in msg:
                self.send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
                continue
            if "method" in msg:
                self.notifications.append(msg)
                continue
            if msg.get("id") == rid:
                return msg

    def drain(self, quiet=0.3):
        while True:
            try:
                msg = self.inbox.get(timeout=quiet)
            except queue.Empty:
                return
            if msg is None:
                return
            if "method" in msg and "id" in msg:
                self.send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
            elif "method" in msg:
                self.notifications.append(msg)

    def uri_of(self, rel):
        return self.uri + "/" + rel

    def open(self, rel, text):
        uri = self.uri_of(rel)
        self.versions[uri] = 1
        self.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 1, "text": text}})
        return uri

    def change(self, rel, text):
        uri = self.uri_of(rel)
        self.versions[uri] += 1
        self.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": self.versions[uri]},
                                               "contentChanges": [{"text": text}]})

    def call(self, method, rel, extra=None):
        params = {"textDocument": {"uri": self.uri_of(rel)}}
        params.update(extra or {})
        return self.request(method, params)

    def at(self, method, rel, line, char, extra=None):
        params = {"position": {"line": line, "character": char}}
        params.update(extra or {})
        return self.call(method, rel, params)

    def diagnostics(self, rel, quiet=0.8):
        self.drain(quiet)
        uri = self.uri_of(rel)
        last = None
        for n in self.notifications:
            if n["method"] == "textDocument/publishDiagnostics" and n["params"]["uri"] == uri:
                last = n["params"]["diagnostics"]
        return last

    def close(self):
        try:
            self.request("shutdown", None, timeout=5)
            self.notify("exit", None)
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def default_caps():
    return {
        "general": {"positionEncodings": ["utf-16"]},
        "textDocument": {
            "completion": {"completionItem": {"snippetSupport": True, "labelDetailsSupport": True,
                                              "documentationFormat": ["markdown", "plaintext"]}},
            "documentSymbol": {"hierarchicalDocumentSymbolSupport": True},
            "hover": {"contentFormat": ["markdown", "plaintext"]},
        },
        "workspace": {"workspaceFolders": True, "configuration": True},
    }


def offset_to_pos(text, offset):
    """UTF-16 (line, character) of a Python str offset."""
    before = text[:offset]
    line = before.count("\n")
    col_text = before[before.rfind("\n") + 1:]
    return line, len(col_text.encode("utf-16-le")) // 2


def pos_of(text, needle, occurrence=0, shift=0):
    idx = -1
    for _ in range(occurrence + 1):
        idx = text.index(needle, idx + 1)
    return offset_to_pos(text, idx + shift)


def apply_edits(text, edits):
    """Apply LSP TextEdits (utf-16 positions) to text."""
    def off(p):
        lines = text.split("\n")
        o = sum(len(x) + 1 for x in lines[:p["line"]])
        return o + len(lines[p["line"]].encode("utf-16-le")[:p["character"] * 2].decode("utf-16-le"))
    out = text
    for e in sorted(edits, key=lambda e: (e["range"]["start"]["line"], e["range"]["start"]["character"]), reverse=True):
        s, t = off(e["range"]["start"]), off(e["range"]["end"])
        out = out[:s] + e["newText"] + out[t:]
    return out
