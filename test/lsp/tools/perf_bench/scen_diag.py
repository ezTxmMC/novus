import sys, os, time, random
from bench import *
B = sys.argv[1]; HERE = os.getcwd()
for n in (200, 2000, 10000):
    root = f'{HERE}/size{n}'; path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
    c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
    c.open(path, text); c.wait_diag(uri, 0, 30); time.sleep(1.2)
    lines = text.split('\n'); rnd = random.Random(3); v = 2
    def ed(ln, ch, endch, s):
        global v
        c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': ch}, 'end': {'line': ln, 'character': endch}}, 'text': s}]}); v += 1
    err = []; fix = []; semantic = []
    for rep in range(6):
        ln = rnd.randrange(5, len(lines) - 5); L = lines[ln]
        seq0 = c.diag_seq.get(uri, 0); te = time.perf_counter(); ed(ln, len(L), len(L), ' )')
        d = c.wait_diag(uri, seq0, 10); err.append((d - te) * 1000 if d else -1); time.sleep(0.5)
        seq0 = c.diag_seq.get(uri, 0); te = time.perf_counter(); ed(ln, len(L), len(L) + 2, '')
        d = c.wait_diag(uri, seq0, 10); fix.append((d - te) * 1000 if d else -1); time.sleep(0.5)
    print("%5d lines: didChange(syntax error) -> publish %s | fix -> publish %s" % (n, summarize(err), summarize(fix)))
    # an unknown-name (semantic) error
    for rep in range(4):
        ln = rnd.randrange(5, len(lines) - 5); L = lines[ln]
        seq0 = c.diag_seq.get(uri, 0); te = time.perf_counter(); ed(ln, 0, 0, '    var zz = unknownThing(1)\n')
        d = c.wait_diag(uri, seq0, 10); semantic.append((d - te) * 1000 if d else -1); time.sleep(0.5)
        seq0 = c.diag_seq.get(uri, 0); ed(ln, 0, 31, ''); c.wait_diag(uri, seq0, 10); time.sleep(0.5)
    print("       didChange(unknown name) -> publish %s" % summarize(semantic))
    c.shutdown()
