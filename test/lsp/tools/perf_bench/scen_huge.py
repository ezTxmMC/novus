import sys, os, time
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); root = HERE + '/' + sys.argv[2]
path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B); t0 = time.perf_counter(); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text)
m, ms = c.request('textDocument/completion', dict(tdp(uri, 5, 12), context={'triggerKind': 1})); print("first completion %.0f ms (items %d)" % (ms, len(m['result']['items'])), c.rss()['VmRSS'], 'kB')
d = c.wait_diag(uri, 0, 60); print("first diagnostics %.0f ms" % ((d - t0) * 1000))
lat = []; t0 = time.perf_counter()
while time.perf_counter() - t0 < float(sys.argv[3]):
    m, ms = c.request('textDocument/completion', dict(tdp(uri, 5, 12), context={'triggerKind': 1})); lat.append((time.perf_counter() - t0, ms)); time.sleep(0.02)
print("steady:", summarize([x[1] for x in lat]))
print("spikes > 10ms at t=", [("%.0fs" % t, "%.0fms" % ms) for t, ms in lat if ms > 10])
print("rss", c.rss()); c.shutdown()
