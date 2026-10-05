import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); N = int(sys.argv[2]); mode = sys.argv[3]
root = f'{HERE}/size200'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B, env={'NOVUS_GC_STATS': '1'}); c.initialize(root, {'novus': {'check': {'mode': mode, 'novuscPath': HERE + '/fake-novusc.sh'}}})
c.open(path, text); time.sleep(0.5)
lines = text.split('\n'); v = 2
def r(label):
    x = c.rss(); print("  %-16s rss %6d kB hwm %6d threads %s fds %s" % (label, x['VmRSS'], x['VmHWM'], x['Threads'], x['fds']))
r('start')
lat = []
for i in range(N):
    if mode == 'onType':
        c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': 3, 'character': 0}, 'end': {'line': 3, 'character': 0}}, 'text': ' '}]}); v += 1
        time.sleep(0.01)
    else:
        c.notify('textDocument/didSave', {'textDocument': {'uri': uri}}); time.sleep(0.01)
    if i % 2500 == 2499:
        r('after %d' % (i + 1))
        m, ms = c.request('textDocument/hover', tdp(uri, 5, 10)); lat.append(ms)
time.sleep(2); r('idle 2s')
print("  hover during storm", ["%.1f" % x for x in lat])
rc = c.shutdown(); print("  exit", rc, ''.join(c.stderr_buf).strip()[-300:])
