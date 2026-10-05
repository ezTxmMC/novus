import sys, os, time
from bench import *
B, ROOT, OPEN = sys.argv[1], sys.argv[2], sys.argv[3]
extra = sys.argv[4:]
env = {}
for e in extra:
    k, v = e.split('=', 1); env[k] = v
c = Client(B, env=env)
c.initialize(ROOT, {'novus': {'check': {'mode': 'off'}}})
path = os.path.join(ROOT, OPEN); text = open(path).read(); uri = 'file://' + path
t1 = time.perf_counter()
c.open(path, text)
lines = text.split('\n')
ln = next((i for i, l in enumerate(lines) if l.strip().startswith('var ')), 5)
p = tdp(uri, ln, len(lines[ln]))
p['context'] = {'triggerKind': 1}
m, ms = c.request('textDocument/completion', p)
print("first completion %.0f ms (items %s)" % (ms, len(m['result']['items']) if m and m.get('result') and 'items' in m['result'] else '?'), "rss", c.rss()['VmRSS'])
d = c.wait_diag(uri, 0, 120)
print("first diagnostics %.0f ms after open" % ((d - t1)*1000 if d else -1))
m, ms = c.request('textDocument/completion', p); print("second completion %.1f ms" % ms)
# completion from empty prefix at statement start within a method
p2 = tdp(uri, ln, 8); p2['context'] = {'triggerKind': 1}
m, ms = c.request('textDocument/completion', p2); print("statement-start completion %.1f ms items %d" % (ms, len(m['result']['items'])))
m, ms = c.request('workspace/symbol', {'query': 'Thing'}); print("workspace/symbol Thing %.1f ms (%s)" % (ms, len(m['result']) if m and m.get('result') is not None else m))
m, ms = c.request('workspace/symbol', {'query': 'step1'}); print("workspace/symbol step1 %.1f ms (%s)" % (ms, len(m['result']) if m and m.get('result') is not None else m))
res = measure(c, uri, text, 40); report(res)
print("rss", c.rss())
# 45 s of steady requests to catch periodic rescans
lat = []; t0 = time.perf_counter()
while time.perf_counter() - t0 < 45:
    m, ms = c.request('textDocument/completion', p); lat.append(ms); time.sleep(0.02)
print("steady completion 45s:", summarize(lat), "slowest:", sorted(lat)[-3:])
print("rss", c.rss(), c.stderr_buf[:3])
c.shutdown()
