import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); n = int(sys.argv[2]); bin2 = B
root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.wait_diag(uri, 0, 30); time.sleep(1.5)
lines = text.split('\n'); rnd = random.Random(2); v = 2
lat = []
for rep in range(5):
    ln = rnd.randrange(5, len(lines) - 5); L = lines[ln]
    c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': len(L)}, 'end': {'line': ln, 'character': len(L)}}, 'text': ' )'}]}); v += 1
    lines[ln] = L + ' )'
    t0 = time.perf_counter(); seq0 = c.diag_seq.get(uri, 0)
    while time.perf_counter() - t0 < 1.5:
        ts = time.perf_counter()
        m, ms = c.request('textDocument/hover', tdp(uri, ln, 4))
        lat.append((ts - t0, ms, 'error' in m if m else True))
        time.sleep(0.005)
    c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': len(L)}, 'end': {'line': ln, 'character': len(L) + 2}}, 'text': ''}]}); v += 1; lines[ln] = L
    time.sleep(1.2)
slow = [(round(t, 2), round(ms)) for t, ms, e in lat if ms > 20]
print("%5d lines: hover while a diagnostics run is due: %s; errors %d; slow(>20ms) %s" % (n, summarize([x[1] for x in lat]), sum(1 for x in lat if x[2]), slow[:10]))
c.shutdown()
