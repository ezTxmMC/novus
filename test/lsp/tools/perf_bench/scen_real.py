import sys, os, time, random
from bench import *
B = sys.argv[1]; ROOT = sys.argv[2]; REL = sys.argv[3]
path = os.path.join(ROOT, REL); text = open(path).read(); uri = 'file://' + path
c = Client(B); c.initialize(ROOT, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.wait_diag(uri, 0, 30)
print("%s: %d lines %d bytes" % (REL, text.count('\n'), len(text)))
res = measure(c, uri, text, 60); print(" warm:"); report(res, '  ')
lines = text.split('\n'); rnd = random.Random(5); v = 2
after = {'completion': [], 'hover': [], 'semanticTokens/full': [], 'formatting': [], 'documentSymbol': []}
for ln, s, e, w in positions(text, 25, seed=9):
    for kind in after:
        eln = rnd.randrange(len(lines))
        c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': eln, 'character': len(lines[eln])}, 'end': {'line': eln, 'character': len(lines[eln])}}, 'text': ' '}]}); v += 1; lines[eln] += ' '
        if kind == 'completion': m, ms = c.request('textDocument/completion', dict(tdp(uri, ln, e), context={'triggerKind': 1}))
        elif kind == 'hover': m, ms = c.request('textDocument/hover', tdp(uri, ln, (s+e)//2))
        elif kind == 'semanticTokens/full': m, ms = c.request('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})
        elif kind == 'formatting': m, ms = c.request('textDocument/formatting', {'textDocument': {'uri': uri}, 'options': {'tabSize': 4, 'insertSpaces': True}})
        else: m, ms = c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
        after[kind].append(ms)
print(" cold (didChange right before):"); report(after, '  ')
print(" rss", c.rss()['VmRSS'], "kB")
c.shutdown()
