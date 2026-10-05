import sys, os, time, re
from bench import *
B = sys.argv[1]; ROOT = sys.argv[2]; REL = sys.argv[3]; names = sys.argv[4:]
path = os.path.join(ROOT, REL); text = open(path).read(); uri = 'file://' + path
c = Client(B); c.initialize(ROOT, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.wait_diag(uri, 0, 30)
lines = text.split('\n')
for name in names:
    pos = None
    for ln, l in enumerate(lines):
        m = re.search(r'\b' + re.escape(name) + r'\b', l)
        if m: pos = (ln, m.start() + 1); break
    if not pos: print("name not found", name); continue
    ts = []
    for k in range(4):
        p = tdp(uri, pos[0], pos[1]); p['context'] = {'includeDeclaration': True}
        m, ms = c.request('textDocument/references', p); ts.append(ms)
    n = len(m['result']) if m and m.get('result') else m
    rn = []
    for k in range(2):
        p = tdp(uri, pos[0], pos[1]); p['newName'] = name + 'Renamed'
        m2, ms2 = c.request('textDocument/rename', p); rn.append(ms2)
    cnt = sum(len(v) for v in m2['result']['changes'].values()) if m2 and m2.get('result') and 'changes' in m2['result'] else (m2['error']['message'] if m2 and 'error' in m2 else '?')
    h = c.request('textDocument/documentHighlight', tdp(uri, pos[0], pos[1]))[1]
    print("%-14s references x4: %s ms (n=%s) | rename x2: %s ms (edits %s) | highlight %.1f ms" % (name, ["%.0f" % t for t in ts], n, ["%.0f" % t for t in rn], cnt, h))
print("rss", c.rss())
c.shutdown()
