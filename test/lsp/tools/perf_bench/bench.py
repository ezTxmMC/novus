import re, random, time, sys, os
from lspclient import *

IDENT = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')

def positions(text, n, seed=1, kinds=('end','mid')):
    rnd = random.Random(seed)
    lines = text.split('\n')
    cand = []
    for ln, l in enumerate(lines):
        s = l.strip()
        if s.startswith('//') or s.startswith('*') or not s: continue
        for m in IDENT.finditer(l):
            cand.append((ln, m.start(), m.end(), m.group(0)))
    rnd.shuffle(cand)
    return cand[:n]

def decl_positions(text, n, seed=2):
    rnd = random.Random(seed)
    out = []
    for ln, l in enumerate(text.split('\n')):
        m = re.match(r'\s*(?:define\s+class|define\s+enum|method)\s+([A-Za-z_][A-Za-z0-9_]*)', l)
        if m: out.append((ln, m.start(1), m.end(1), m.group(1)))
    rnd.shuffle(out)
    return out[:n]

def tdp(uri, ln, ch): return {'textDocument': {'uri': uri}, 'position': {'line': ln, 'character': ch}}

def measure(c, uri, text, n=40, label=''):
    res = {}
    pos = positions(text, n)
    dp = decl_positions(text, max(5, n//4))
    t = []
    for ln, s, e, w in pos:
        p = tdp(uri, ln, e); p['context'] = {'triggerKind': 1}
        m, ms = c.request('textDocument/completion', p); t.append(ms)
    res['completion'] = t
    t = []
    for ln, s, e, w in pos:
        m, ms = c.request('textDocument/hover', tdp(uri, ln, (s+e)//2)); t.append(ms)
    res['hover'] = t
    t = []
    for ln, s, e, w in pos:
        m, ms = c.request('textDocument/definition', tdp(uri, ln, (s+e)//2)); t.append(ms)
    res['definition'] = t
    t = []
    for ln, s, e, w in dp:
        p = tdp(uri, ln, (s+e)//2); p['context'] = {'includeDeclaration': True}
        m, ms = c.request('textDocument/references', p); t.append(ms)
    res['references'] = t
    t = []
    for i in range(max(5, n//4)):
        m, ms = c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}}); t.append(ms)
    res['documentSymbol'] = t
    t = []
    for i in range(max(5, n//4)):
        m, ms = c.request('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}}); t.append(ms)
    res['semanticTokens/full'] = t
    t = []
    for i in range(max(5, n//4)):
        m, ms = c.request('textDocument/formatting', {'textDocument': {'uri': uri}, 'options': {'tabSize': 4, 'insertSpaces': True}}); t.append(ms)
    res['formatting'] = t
    return res

def report(res, prefix=''):
    for k, v in res.items():
        print("  %s%-22s %s" % (prefix, k, summarize(v)))
