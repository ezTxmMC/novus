"""Minimal LSP test client for the robustness tools (test/lsp/robust/). Standard library only.

It speaks the same framing as the server: Content-Length header, CRLF CRLF, UTF-8 JSON body. Every frame the
server writes is checked: a header that is not Content-Length, a body that is not valid JSON or a message that
is neither a response nor a notification nor a request is recorded in `violations`.
"""
import json
import os
import queue
import subprocess
import threading
import time

REQUEST_TIMEOUT_SECONDS = 5.0
INVALID_MARKERS = {"\ue000": b"\xff", "\ue001": b"\xc0\xaf", "\ue002": b"\xed\xa0\x80", "\ue003": b"\xf0\x9f\x98", "\ue004": b"\\ud800", "\ue005": b"\\udc00\\ud83d"}


class Server:
    def __init__(self, binary, root, env=None, cwd=None, stderr_path=None):
        self.binary = binary
        self.root = root
        full_env = dict(os.environ)
        full_env.update(env or {})
        self.stderr_file = open(stderr_path or os.devnull, "wb")
        self.proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.stderr_file, env=full_env, cwd=cwd or root, bufsize=0)
        self.responses = {}
        self.answered_ids = set()
        self.notifications = []
        self.violations = []
        self.condition = threading.Condition()
        self.next_id = 1
        self.write_lock = threading.Lock()
        self.eof = False
        self.legend = None
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()

    # ---- reading -------------------------------------------------------------------------------------------
    def _read_exact(self, count):
        data = b""
        while len(data) < count:
            chunk = self.proc.stdout.read(count - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _read_frame(self):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            byte = self.proc.stdout.read(1)
            if not byte:
                return None
            header += byte
            if len(header) > 4096:
                self.violations.append("header longer than 4096 bytes")
                return None
        length = None
        for line in header.decode("ascii", "replace").split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
            elif line:
                self.violations.append("unexpected header line: " + line)
        if length is None:
            self.violations.append("frame without Content-Length")
            return None
        return self._read_exact(length)

    def _read_loop(self):
        while True:
            body = self._read_frame()
            if body is None:
                break
            self._dispatch(body)
        with self.condition:
            self.eof = True
            self.condition.notify_all()

    def _dispatch(self, body):
        try:
            message = json.loads(body.decode("utf-8"))
        except Exception as error:  # noqa: BLE001
            self.violations.append("invalid JSON from server: %r (%s)" % (body[:200], error))
            return
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
            self.violations.append("not a JSON-RPC 2.0 object: %r" % (body[:200],))
            return
        if "method" in message and "id" in message:
            self.send({"jsonrpc": "2.0", "id": message["id"], "result": None})
            self.notifications.append(message)
            return
        if "method" in message:
            with self.condition:
                self.notifications.append(message)
                self.condition.notify_all()
            return
        if "id" not in message or ("result" not in message) == ("error" not in message):
            self.violations.append("response must have id and exactly one of result/error: %r" % (body[:200],))
            return
        with self.condition:
            self.responses[message["id"]] = message
            self.answered_ids.add(message["id"])
            self.condition.notify_all()

    # ---- writing -------------------------------------------------------------------------------------------
    def send_raw(self, data):
        with self.write_lock:
            try:
                self.proc.stdin.write(data)
                return True
            except (BrokenPipeError, OSError):
                return False

    def send(self, message):
        body = json.dumps(message, ensure_ascii=False).encode("utf-8")
        return self.send_raw(b"Content-Length: %d\r\n\r\n" % len(body) + body)

    def send_with_markers(self, message):
        """Like send, but the private-use characters U+E000..U+E005 in the text become what the table says: bytes that are
        not UTF-8 (a lone continuation byte, an overlong form, an encoded surrogate, a cut sequence) or a JSON escape of a lone
        surrogate. The server must survive them and answer later requests."""
        body = json.dumps(message, ensure_ascii=False).encode("utf-8")
        for marker, raw in INVALID_MARKERS.items():
            body = body.replace(marker.encode("utf-8"), raw)
        return self.send_raw(b"Content-Length: %d\r\n\r\n" % len(body) + body)

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        return self.send(message)

    def call(self, method, params=None):
        request_id = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        self.send(message)
        return request_id

    def wait(self, request_id, timeout=REQUEST_TIMEOUT_SECONDS):
        """The response of request_id, or None on timeout / server end."""
        deadline = time.time() + timeout
        with self.condition:
            while request_id not in self.responses:
                remaining = deadline - time.time()
                if remaining <= 0 or self.eof:
                    return self.responses.pop(request_id, None)
                self.condition.wait(min(remaining, 0.2))
            return self.responses.pop(request_id)

    def request(self, method, params=None, timeout=REQUEST_TIMEOUT_SECONDS):
        return self.wait(self.call(method, params), timeout)

    # ---- lifecycle -----------------------------------------------------------------------------------------
    def alive(self):
        return self.proc.poll() is None

    def initialize(self, options=None, capabilities=None, root_uri="auto", timeout=15.0):
        root_uri = ("file://" + self.root) if root_uri == "auto" else root_uri
        params = {"processId": None, "rootUri": root_uri, "capabilities": capabilities or {}}
        if options is not None:
            params["initializationOptions"] = options
        response = self.request("initialize", params, timeout)
        self.legend = None
        try:
            legend = response["result"]["capabilities"]["semanticTokensProvider"]["legend"]
            self.legend = (len(legend["tokenTypes"]), len(legend["tokenModifiers"]))
        except (KeyError, TypeError):
            pass
        self.notify("initialized", {})
        return response

    def shutdown(self, timeout=10.0):
        """Clean end: shutdown + exit, then the exit code or None when the process did not end in time."""
        response = self.request("shutdown", None, timeout)
        self.notify("exit")
        try:
            code = self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            self.kill()
            return None, response
        return code, response

    def kill(self):
        if self.alive():
            self.proc.kill()
        try:
            self.proc.wait(5)
        except Exception:  # noqa: BLE001
            pass

    def close(self):
        self.kill()
        try:
            self.proc.stdin.close()
        except Exception:  # noqa: BLE001
            pass
        self.stderr_file.close()

    def drain_diagnostics(self):
        return [n for n in self.notifications if n.get("method") == "textDocument/publishDiagnostics"]


def uri_of(path):
    return "file://" + path


def open_document(server, uri, text, version=1, language="novus"):
    server.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": language, "version": version, "text": text}})
