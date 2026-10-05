import sys, os, time, glob
from bench import *
B = sys.argv[1]; R = REPO_ROOT
sets = {'compiler+std (this repository, ~9k lines, 70 files)': glob.glob(R + '/compiler/*/*.nv') + glob.glob(R + '/compiler/*.nv') + glob.glob(R + '/std/*.nv'),
        'lsp/ (32k lines, 286 files)': glob.glob(R + '/lsp/**/*.nv', recursive=True)}
for label, files in sets.items():
    c = Client(B, env={'NOVUS_GC_STATS': '1'}); c.initialize(R, {'novus': {'check': {'mode': 'off'}}})
    t0 = time.perf_counter()
    for f in files: c.open(f, open(f).read())
    c.request('workspace/symbol', {'query': 'a'})
    for f in files[:50]:
        c.request('textDocument/documentSymbol', {'textDocument': {'uri': 'file://' + f}}); c.request('textDocument/semanticTokens/full', {'textDocument': {'uri': 'file://' + f}})
    time.sleep(3)
    x = c.rss(); print("%s: open all %d files: rss %d kB hwm %d kB (%.1fs)" % (label, len(files), x['VmRSS'], x['VmHWM'], time.perf_counter() - t0))
    c.shutdown(); print('  ', ''.join(c.stderr_buf).strip())
