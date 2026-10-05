import sys, os, time, random
from bench import *
B = sys.argv[1]
HERE = os.getcwd()
def edit_params(uri, version, ln, ch, text):
    return {'textDocument': {'uri': uri, 'version': version}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': ch}, 'end': {'line': ln, 'character': ch}}, 'text': text}]}

for n in (200, 2000, 10000):
    root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
    print("== %d lines (%d bytes)" % (text.count('\n'), len(text)))
    c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
    t1 = time.perf_counter(); c.open(path, text)
    m, ms = c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
    print("  open -> first documentSymbol %.1f ms" % ms)
    d = c.wait_diag(uri, 0, 60); print("  open -> diagnostics %.1f ms" % ((d - t1) * 1000))
    res = measure(c, uri, text, 60); print(" warm:"); report(res, '  ')
    # after-edit (cold per edit): didChange of one char then the request
    lines = text.split('\n'); rnd = random.Random(5)
    after = {'completion': [], 'hover': [], 'definition': [], 'documentSymbol': [], 'semanticTokens/full': [], 'formatting': [], 'references': []}
    v = 2
    pos = positions(text, 25, seed=9); dp = decl_positions(text, 6, seed=3)
    for ln, s, e, w in pos:
        for kind in ('completion', 'hover', 'definition', 'documentSymbol', 'semanticTokens/full', 'formatting'):
            # a trailing-space edit at end of a random line keeps the text valid
            eln = rnd.randrange(len(lines)); 
            c.notify('textDocument/didChange', edit_params(uri, v, eln, len(lines[eln]), ' ')); v += 1
            c.notify('textDocument/didChange', edit_params(uri, v, eln, len(lines[eln]), '')) if False else None
            if kind == 'completion':
                p = tdp(uri, ln, e); p['context'] = {'triggerKind': 1}; m, ms = c.request('textDocument/completion', p)
            elif kind == 'hover': m, ms = c.request('textDocument/hover', tdp(uri, ln, (s+e)//2))
            elif kind == 'definition': m, ms = c.request('textDocument/definition', tdp(uri, ln, (s+e)//2))
            elif kind == 'documentSymbol': m, ms = c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
            elif kind == 'semanticTokens/full': m, ms = c.request('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})
            else: m, ms = c.request('textDocument/formatting', {'textDocument': {'uri': uri}, 'options': {'tabSize': 4, 'insertSpaces': True}})
            after[kind].append(ms)
            # re-sync client's view of lines: edited line now has trailing space
            lines[eln] = lines[eln] + ' '
    for ln, s, e, w in dp:
        eln = rnd.randrange(len(lines))
        c.notify('textDocument/didChange', edit_params(uri, v, eln, len(lines[eln]), ' ')); v += 1; lines[eln] += ' '
        p = tdp(uri, ln, (s+e)//2); p['context'] = {'includeDeclaration': True}
        m, ms = c.request('textDocument/references', p); after['references'].append(ms)
    print(" cold (one didChange immediately before each request):"); report(after, '  ')
    print("  rss", c.rss())
    c.shutdown()
