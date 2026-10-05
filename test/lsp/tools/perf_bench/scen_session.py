import sys, os, time, random
from bench import *
B, root, rel = sys.argv[1], sys.argv[2], sys.argv[3]
N = int(sys.argv[4]) if len(sys.argv) > 4 else 10000
check = sys.argv[5] if len(sys.argv) > 5 else 'off'
env = {}
if check != 'off':
    env['NOVUS_CHECK_FAKE'] = '1'
opts = {'novus': {'check': {'mode': check}}}
if check != 'off':
    opts['novus']['check']['novuscPath'] = os.path.join(os.getcwd(), 'fake-novusc.sh')
path = os.path.join(root, rel); text = open(path).read(); uri = 'file://' + path
c = Client(B, env=env); c.initialize(root, opts)
c.open(path, text)
lines = text.split('\n'); rnd = random.Random(7); v = 2
def ed(ln, ch, endch, s):
    global v
    c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': ch}, 'end': {'line': ln, 'character': endch}}, 'text': s}]}); v += 1
samples = []; lat = {'completion': [], 'hover': [], 'edit+diag': [], 'fix+diag': []}
t0 = time.perf_counter()
seq = c.diag_seq.get(uri, 0)
for i in range(N):
    ln = rnd.randrange(len(lines)); L = lines[ln]
    # type 'x' at end of the line then delete it again (net zero, so the file stays the same size)
    ed(ln, len(L), len(L), 'x'); ed(ln, len(L), len(L) + 1, '')
    if i % 10 == 0:
        p = tdp(uri, ln, len(L)); p['context'] = {'triggerKind': 1}
        m, ms = c.request('textDocument/completion', p); lat['completion'].append(ms)
        m, ms = c.request('textDocument/hover', tdp(uri, ln, max(0, len(L)//2))); lat['hover'].append(ms)
    if i % 100 == 99:
        # a stray ')' makes a syntax error: the diagnostics must change, then the fix must clear them again
        seq0 = c.diag_seq.get(uri, 0); te = time.perf_counter(); ed(ln, len(L), len(L), ' )')
        d = c.wait_diag(uri, seq0, 5)
        lat['edit+diag'].append((d - te) * 1000 if d else 5000.0)
        seq0 = c.diag_seq.get(uri, 0); te = time.perf_counter(); ed(ln, len(L), len(L) + 2, '')
        d = c.wait_diag(uri, seq0, 5)
        lat['fix+diag'].append((d - te) * 1000 if d else 5000.0)
    if i % 500 == 0:
        r = c.rss(); samples.append((i, r.get('VmRSS'), r.get('VmHWM'), r.get('Threads'), r.get('fds'), time.perf_counter() - t0))
for s in samples: print("edit %5d rss=%6d kB hwm=%6d kB threads=%s fds=%s t=%.0fs" % s)
for k, v_ in lat.items(): print(k, summarize(v_))
print("final", c.rss(), "stderr", c.stderr_buf[:3])
c.shutdown()
