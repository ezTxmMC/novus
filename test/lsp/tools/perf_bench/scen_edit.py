import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd()
for n in [int(x) for x in os.environ.get("SIZES","200,2000").split(",")]:
    root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
    c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
    c.open(path, text)
    c.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}}); c.wait_diag(uri, 0, 30)
    lines = text.split('\n'); rnd = random.Random(3); v = 2
    def ed(ln, ch, endch, s):
        global v
        c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': ch}, 'end': {'line': ln, 'character': endch}}, 'text': s}]}); v += 1
    # cheap request that needs no parse: an unknown method answers -32601 immediately
    _, base = c.request('nope/ping', {})
    for burst in (1, 10, 100):
        ts = []
        for rep in range(5):
            t0 = time.perf_counter()
            for i in range(burst):
                ln = rnd.randrange(len(lines)); L = lines[ln]
                ed(ln, len(L), len(L), 'x'); ed(ln, len(L), len(L) + 1, '')
            _, ms = c.request('nope/ping', {})
            ts.append((time.perf_counter() - t0) * 1000 / (2 * burst))
            time.sleep(0.4)
        print("%5d lines: %3d x2 didChange then ping: %.2f ms per didChange (ping alone %.2f)" % (n, burst, sorted(ts)[2], base))
    # wait for diagnostics: how much CPU after burst?
    c.shutdown()
