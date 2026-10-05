import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); n = int(sys.argv[2])
root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B, env={'NOVUS_GC_STATS': '1'}); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.wait_diag(uri, 0, 30)
def r(label): x = c.rss(); print("  %-28s rss %7d kB hwm %7d kB" % (label, x['VmRSS'], x['VmHWM']))
r('after open+diag')
for k, (m, p) in enumerate([('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})] * 6):
    c.request(m, p); 
    if k in (0, 1, 5): r('semanticTokens #%d' % (k + 1))
for k in range(6):
    c.request('textDocument/formatting', {'textDocument': {'uri': uri}, 'options': {'tabSize': 4, 'insertSpaces': True}})
    if k in (0, 1, 5): r('formatting #%d' % (k + 1))
for k in range(6):
    c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
r('documentSymbol x6')
time.sleep(2); r('idle 2s')
c.shutdown(); print(''.join(c.stderr_buf).strip())
