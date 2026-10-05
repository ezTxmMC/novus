import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd()
for n in (200, 2000, 10000):
    root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
    c = Client(B, env={'NOVUS_GC_STATS': '1'}); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
    r0 = c.rss()['VmRSS']
    c.open(path, text); c.wait_diag(uri, 0, 30)
    r1 = c.rss()['VmRSS']
    lines = text.split('\n'); rnd = random.Random(3); v = 2
    for i in range(40):
        ln = rnd.randrange(len(lines)); L = lines[ln]
        c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': len(L)}, 'end': {'line': ln, 'character': len(L)}}, 'text': ' '}]}); v += 1; lines[ln] += ' '
        c.request('textDocument/completion', dict(tdp(uri, ln, len(L)), context={'triggerKind': 1}))
    r2 = c.rss()
    time.sleep(1)
    c.request('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})
    r3 = c.rss()
    c.shutdown()
    print("%5d lines (%6d B): rss init %d kB, after open %d kB, after 40 edits+completion %d kB (hwm %d), after semTok %d kB (hwm %d) | %s" % (n, len(text), r0, r1, r2['VmRSS'], r2['VmHWM'], r3['VmRSS'], r3['VmHWM'], ''.join(c.stderr_buf).strip()))
