import subprocess, threading, json, time, os, sys, queue, statistics

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))

def pct(xs, p):
    if not xs: return 0.0
    s = sorted(xs); k = min(len(s)-1, int(round((p/100.0)*(len(s)-1))))
    return s[k]

def summarize(xs):
    return "n=%d p50=%.1fms p95=%.1fms max=%.1fms" % (len(xs), pct(xs,50), pct(xs,95), max(xs) if xs else 0)

class Client:
    def __init__(self, binary, env=None, cwd=None):
        e = dict(os.environ)
        if env: e.update(env)
        self.t_spawn = time.perf_counter()
        self.p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=e, cwd=cwd)
        self.pid = self.p.pid
        self.next_id = 1
        self.pending = {}
        self.cv = threading.Condition()
        self.responses = {}
        self.diags = {}      # uri -> (time, version?, list)
        self.diag_seq = {}
        self.notifs = []
        self.stderr_buf = []
        self.alive = True
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._err, daemon=True).start()

    def _err(self):
        for line in self.p.stderr:
            self.stderr_buf.append(line.decode('utf-8', 'replace'))

    def _read(self):
        f = self.p.stdout
        while True:
            length = None
            while True:
                line = f.readline()
                if not line:
                    with self.cv:
                        self.alive = False; self.cv.notify_all()
                    return
                line = line.strip()
                if line == b'': break
                if line.lower().startswith(b'content-length:'):
                    length = int(line.split(b':')[1])
            body = f.read(length)
            t = time.perf_counter()
            try: msg = json.loads(body)
            except Exception: continue
            with self.cv:
                if 'id' in msg and 'method' not in msg:
                    self.responses[msg['id']] = (t, msg)
                elif msg.get('method') == 'textDocument/publishDiagnostics':
                    u = msg['params']['uri']
                    self.diags[u] = (t, msg['params']['diagnostics'])
                    self.diag_seq[u] = self.diag_seq.get(u, 0) + 1
                elif 'id' in msg and 'method' in msg:
                    # server->client request: answer null
                    self._send({'jsonrpc':'2.0','id':msg['id'],'result':None})
                else:
                    self.notifs.append((t, msg))
                self.cv.notify_all()

    def _send(self, obj):
        data = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        try:
            self.p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(data) + data)
            self.p.stdin.flush()
        except BrokenPipeError:
            pass

    def notify(self, method, params):
        self._send({'jsonrpc':'2.0','method':method,'params':params})

    def request(self, method, params, timeout=120):
        i = self.next_id; self.next_id += 1
        t0 = time.perf_counter()
        self._send({'jsonrpc':'2.0','id':i,'method':method,'params':params})
        with self.cv:
            end = t0 + timeout
            while i not in self.responses:
                if not self.alive: return None, (time.perf_counter()-t0)*1000
                rem = end - time.perf_counter()
                if rem <= 0: return None, timeout*1000
                self.cv.wait(rem)
            t, msg = self.responses.pop(i)
        return msg, (t - t0)*1000

    def wait_diag(self, uri, after_seq, timeout=30):
        t0 = time.perf_counter()
        with self.cv:
            while self.diag_seq.get(uri, 0) <= after_seq:
                rem = t0 + timeout - time.perf_counter()
                if rem <= 0: return None
                self.cv.wait(rem)
            return self.diags[uri][0]

    def rss(self):
        d = {}
        try:
            for l in open('/proc/%d/status' % self.pid):
                k, _, v = l.partition(':')
                if k in ('VmRSS','VmHWM','VmSize','Threads'):
                    d[k] = int(v.split()[0])
            d['fds'] = len(os.listdir('/proc/%d/fd' % self.pid))
        except Exception: pass
        return d

    def initialize(self, root, options=None, caps=None):
        params = {'processId': None, 'rootUri': 'file://' + root if root else None,
                  'capabilities': caps if caps is not None else {'textDocument': {'completion': {'completionItem': {'snippetSupport': True, 'labelDetailsSupport': True}}}}}
        if options is not None: params['initializationOptions'] = options
        if root: params['workspaceFolders'] = [{'uri': 'file://' + root, 'name': 'w'}]
        msg, ms = self.request('initialize', params)
        self.notify('initialized', {})
        return msg, ms

    def open(self, path, text, version=1):
        self.notify('textDocument/didOpen', {'textDocument': {'uri': 'file://' + path, 'languageId': 'novus', 'version': version, 'text': text}})

    def shutdown(self):
        try:
            self.request('shutdown', None, timeout=10)
            self.notify('exit', None)
            self.p.wait(timeout=10)
        except Exception:
            self.p.kill()
        return self.p.returncode

    def kill(self):
        try: self.p.kill()
        except Exception: pass
