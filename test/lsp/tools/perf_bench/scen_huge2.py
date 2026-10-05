import sys, os, time
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); root = HERE + '/' + sys.argv[2]
path = root + '/main.nv'; uri = 'file://' + path
for label, line in (('empty prefix', '    var x = '), ('prefix f', '    var x = f'), ('prefix fn12x', '    var x = fn12x'), ('prefix zz (no match)', '    var x = zzq')):
    text = 'package main\n\nimport dir000\n\nmethod main() {\n' + line + '\n}\n'
    c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}}); c.open(path, text)
    p = dict(tdp(uri, 5, len(line)), context={'triggerKind': 1})
    first = c.request('textDocument/completion', p)[1]
    ts = []; n = 0
    for i in range(8):
        m, ms = c.request('textDocument/completion', p); ts.append(ms); n = len(m['result']['items'])
    print("%-22s first %.0f ms, warm p50 %.1f ms max %.1f (items %d) rss %d" % (label, first, pct(ts, 50), max(ts), n, c.rss()['VmRSS']))
    c.shutdown()
