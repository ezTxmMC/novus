import sys, os, time, random, re
from bench import *
B = sys.argv[1]; HERE = os.getcwd(); n = int(sys.argv[2]); kind = sys.argv[3]; N = int(sys.argv[4])
root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B, env={'NOVUS_GC_STATS': '2'}); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.wait_diag(uri, 0, 30)
lines = text.split('\n'); rnd = random.Random(3); v = 2
for i in range(N):
    ln = rnd.randrange(len(lines)); L = lines[ln]
    c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': len(L)}, 'end': {'line': ln, 'character': len(L)}}, 'text': ' '}]}); v += 1; lines[ln] += ' '
    if kind == 'sem': c.request('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})
    elif kind == 'compl': c.request('textDocument/completion', dict(tdp(uri, ln, len(L)), context={'triggerKind': 1}))
    elif kind == 'none': c.request('nope/ping', {})
    if (i + 1) % (N // 5) == 0: x = c.rss(); print("  %s edit %4d rss %7d kB" % (kind, i + 1, x['VmRSS']))
c.shutdown(); err = [l for l in ''.join(c.stderr_buf).split('\n') if l.strip()]
print(len(err), 'gc lines'); 
for l in err[::max(1, len(err)//8)]: print('  ', l)
print(err[-1])
