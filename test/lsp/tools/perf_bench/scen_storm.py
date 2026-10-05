import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); N = int(sys.argv[2])
root = f'{HERE}/size2000'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
def r(label):
    x = c.rss(); print("  %-18s rss %6d kB hwm %6d threads %s fds %s" % (label, x['VmRSS'], x['VmHWM'], x['Threads'], x['fds']))
r('start')
kinds = [('textDocument/hover', tdp(uri, 30, 10)), ('nope/unknown', {}), ('textDocument/hover', {'bad': 1}),
         ('textDocument/completion', dict(tdp('file:///nonexistent.nv', 1, 1), context={'triggerKind': 1})), ('textDocument/foldingRange', {'textDocument': {'uri': uri}}),
         ('workspace/symbol', {'query': 'Thing'}), ('textDocument/definition', tdp(uri, 40, 8))]
done = 0; t0 = time.perf_counter()
while done < N:
    ids = []
    for i in range(500):
        m, p = kinds[(done + i) % len(kinds)]
        rid = c.next_id; c.next_id += 1; ids.append(rid)
        c._send({'jsonrpc': '2.0', 'id': rid, 'method': m, 'params': p})
        if i % 7 == 0: c.notify('$/cancelRequest', {'id': rid})       # cancel of a request that is queued or already answered
        if i % 11 == 0: c.notify('$/cancelRequest', {'id': 99999999 + i})  # never tracked
    with c.cv:
        end = time.time() + 120
        while not all(i in c.responses for i in ids):
            c.cv.wait(1)
            if time.time() > end: print("TIMEOUT"); break
        errs = sum(1 for i in ids if 'error' in c.responses.get(i, (0, {}))[1])
        for i in ids: c.responses.pop(i, None)
    done += 500
    if done % 10000 == 0: r('after %d (errs %d of last 500)' % (done, errs))
print("  %d requests in %.1fs" % (N, time.perf_counter() - t0))
m, ms = c.request('textDocument/hover', tdp(uri, 30, 10)); print("  hover after storm %.2f ms" % ms)
c.shutdown()
