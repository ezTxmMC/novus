"""A scriptable LSP client for real-editor message sequences against novus-lsp.

The server runs in production mode (no NOVUS_LSP_TEST): real reader/ticker threads, debouncing and
no synchronous check. The client answers server requests like the simulated editor would and records
every message in both directions so that a scenario can assert on ordering and ids."""
import json
import os
import subprocess
import sys
import threading
import time
import queue

DEFAULT_TIMEOUT = 20.0


class Frame:
    @staticmethod
    def encode(message):
        body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        return b"Content-Length: %d\r\n\r\n" % len(body) + body

    @staticmethod
    def read(stream):
        length = None
        while True:
            line = stream.readline()
            if line == b"":
                return None
            line = line.strip()
            if line == b"":
                break
            name, _, value = line.partition(b":")
            if name.lower() == b"content-length":
                length = int(value.strip())
        if length is None:
            raise ValueError("frame without Content-Length")
        body = stream.read(length)
        if len(body) != length:
            return None
        return json.loads(body.decode("utf-8"))


def validate_outgoing(message):
    """Shape rules of the server-to-client messages the server is known to send."""
    method, params = message["method"], message.get("params")
    problems = []
    if "id" in message and not isinstance(message["id"], (int, str)):
        problems.append("request id must be int or string")
    if method == "textDocument/publishDiagnostics":
        if not isinstance(params, dict) or not isinstance(params.get("uri"), str) or not isinstance(params.get("diagnostics"), list):
            problems.append("publishDiagnostics params")
        else:
            for diagnostic in params["diagnostics"]:
                rng = diagnostic.get("range", {})
                if not all(isinstance(rng.get(k, {}).get(f), int) and rng[k][f] >= 0 for k in ("start", "end") for f in ("line", "character")):
                    problems.append("diagnostic range")
                elif (rng["end"]["line"], rng["end"]["character"]) < (rng["start"]["line"], rng["start"]["character"]):
                    problems.append("diagnostic range end before start")
                if not isinstance(diagnostic.get("message"), str):
                    problems.append("diagnostic message")
                if diagnostic.get("severity") not in (None, 1, 2, 3, 4):
                    problems.append("diagnostic severity")
    elif method in ("window/showMessage", "window/logMessage"):
        if not isinstance(params, dict) or params.get("type") not in (1, 2, 3, 4) or not isinstance(params.get("message"), str):
            problems.append(method + " params")
    elif method == "workspace/configuration":
        if not isinstance(params, dict) or not isinstance(params.get("items"), list) or not params["items"]:
            problems.append("configuration items")
    elif method == "client/registerCapability":
        regs = (params or {}).get("registrations")
        if not isinstance(regs, list) or not regs or not all(isinstance(r.get("id"), str) and isinstance(r.get("method"), str) for r in regs):
            problems.append("registrations")
    elif method.startswith("$/") is False and "id" not in message and method not in ("window/showMessage",):
        problems.append("unknown server notification " + method)
    return problems


class EditorClient:
    """responder(client, message) -> ("result", value) | ("error", code, text) | None (never answer)."""

    def __init__(self, binary, root, env=None, responder=None, args=None, name="client"):
        self.name = name
        self.root = root
        self.log = []                 # (direction, message, time)
        self.inbox = queue.Queue()
        self.next_id = 1
        self.responses = {}
        self.server_requests = []
        self.notifications = []
        self.responder = responder
        self.lock = threading.RLock()
        self.cond = threading.Condition(self.lock)
        merged = dict(os.environ)
        merged.pop("NOVUS_LSP_TEST", None)
        merged.setdefault("NOVUS_LSP_LOG", "debug")
        merged.update(env or {})
        self.proc = subprocess.Popen([binary] + (args or []), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, env=merged, cwd=root)
        self.stderr_text = b""
        threading.Thread(target=self._pump_stderr, daemon=True).start()
        self.reader = threading.Thread(target=self._pump, daemon=True)
        self.reader.start()
        self.eof = False
        self.protocol_errors = []

    def _pump_stderr(self):
        for line in self.proc.stderr:
            self.stderr_text += line

    def _pump(self):
        while True:
            try:
                message = Frame.read(self.proc.stdout)
            except Exception as error:
                self.protocol_errors.append("bad frame from server: %r" % (error,))
                break
            if message is None:
                break
            self._dispatch(message)
        with self.cond:
            self.eof = True
            self.cond.notify_all()

    def _validate(self, message):
        problems = []
        if message.get("jsonrpc") != "2.0":
            problems.append("missing jsonrpc 2.0")
        if "method" in message:
            problems += validate_outgoing(message)
        else:
            if ("result" in message) == ("error" in message):
                problems.append("response needs exactly one of result/error")
            if "error" in message:
                error = message["error"]
                if not isinstance(error, dict) or not isinstance(error.get("code"), int) or not isinstance(error.get("message"), str):
                    problems.append("malformed error object")
            if "id" not in message:
                problems.append("response without id")
        for problem in problems:
            self.protocol_errors.append("%s: %s" % (problem, json.dumps(message)[:200]))

    def _dispatch(self, message):
        self._validate(message)
        with self.cond:
            self.log.append(("in", message, time.time()))
            if "method" in message and "id" in message:
                self.server_requests.append(message)
            elif "method" in message:
                self.notifications.append(message)
            else:
                self.responses[json.dumps(message.get("id"))] = message
            self.cond.notify_all()
        if "method" in message and "id" in message:
            self._answer_server_request(message)

    def _answer_server_request(self, message):
        outcome = ("error", -32601, "method not found: " + message["method"])
        if self.responder:
            outcome = self.responder(self, message)
        if outcome is None:
            return
        reply = {"jsonrpc": "2.0", "id": message["id"]}
        if outcome[0] == "result":
            reply["result"] = outcome[1]
        else:
            reply["error"] = {"code": outcome[1], "message": outcome[2]}
        self.send_raw(reply)

    def send_raw(self, message):
        with self.lock:
            self.log.append(("out", message, time.time()))
        try:
            self.proc.stdin.write(Frame.encode(message))
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def notify(self, method, params=None, omit_params=False):
        message = {"jsonrpc": "2.0", "method": method}
        if not omit_params:
            message["params"] = params
        self.send_raw(message)

    def request(self, method, params=None, omit_params=False, timeout=DEFAULT_TIMEOUT, request_id=None):
        if request_id is None:
            request_id = self.next_id
            self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if not omit_params:
            message["params"] = params
        self.send_raw(message)
        return self.wait_response(request_id, timeout)

    def wait_response(self, request_id, timeout=DEFAULT_TIMEOUT):
        key = json.dumps(request_id)
        deadline = time.time() + timeout
        with self.cond:
            while key not in self.responses:
                left = deadline - time.time()
                if left <= 0 or self.eof:
                    return None
                self.cond.wait(min(left, 0.2))
            return self.responses[key]

    def wait_for(self, predicate, timeout=DEFAULT_TIMEOUT):
        deadline = time.time() + timeout
        with self.cond:
            while True:
                found = predicate(self)
                if found:
                    return found
                left = deadline - time.time()
                if left <= 0:
                    return None
                self.cond.wait(min(left, 0.2))

    def notifications_named(self, method):
        with self.lock:
            return [n for n in self.notifications if n["method"] == method]

    def requests_named(self, method):
        with self.lock:
            return [n for n in self.server_requests if n["method"] == method]

    def wait_exit(self, timeout=5.0):
        try:
            return self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            return None

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()
        try:
            self.proc.stdin.close()
        except Exception:
            pass
