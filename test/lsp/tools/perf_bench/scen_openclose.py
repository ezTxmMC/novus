import sys, os, time, glob
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); root = HERE + '/syn500'
files = sorted(glob.glob(root + '/pkg*/f*.nv'))
c = Client(B, env={'NOVUS_GC_STATS': '1'}); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
def r(label):
    x = c.rss(); print("  %-22s rss %6d kB hwm %6d threads %s fds %s" % (label, x['VmRSS'], x['VmHWM'], x['Threads'], x['fds']))
r('start'); lat = []
for rnd in range(int(os.environ.get("ROUNDS","6"))):
    t0 = time.perf_counter()
    for f in files[:300]:
        text = open(f).read(); uri = 'file://' + f
        c.open(f, text)
        m, ms = c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}}); lat.append(ms)
        c.notify('textDocument/didClose', {'textDocument': {'uri': uri}})
    r('round %d (%.1fs)' % (rnd + 1, time.perf_counter() - t0))
print("  open+documentSymbol:", summarize(lat))
# open all 300 at once (editor with many tabs)
for f in files[:300]: c.open(f, open(f).read())
r('300 open at once')
m, ms = c.request('textDocument/completion', dict(tdp('file://' + files[7], 20, 8), context={'triggerKind': 1})); print("  completion with 300 docs open %.1f ms" % ms)
m, ms = c.request('workspace/symbol', {'query': 'Thing'}); print("  workspace/symbol %.1f ms" % ms)
time.sleep(2); r('after 2s')
c.shutdown(); print(''.join(c.stderr_buf).strip())
