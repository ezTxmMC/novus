import sys, os, time, random
from bench import *
B, root, rel = sys.argv[1], sys.argv[2], sys.argv[3]
path = os.path.join(root, rel); text = open(path).read(); uri = 'file://' + path
c = Client(B); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
c.open(path, text); c.wait_diag(uri, 0, 60)
lines = text.split('\n'); v = [2]
def edit(ln, ch, endch, s):
    c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': v[0]}, 'contentChanges': [{'range': {'start': {'line': ln, 'character': ch}, 'end': {'line': ln, 'character': endch}}, 'text': s}]}); v[0] += 1
def compl():
    ln = next(i for i, l in enumerate(lines) if l.strip().startswith('var names'))
    p = tdp(uri, ln, 12); p['context'] = {'triggerKind': 1}
    return c.request('textDocument/completion', p)[1]
compl(); print("warm completion %.1f ms" % compl())
# body-only edit
edit(10, 0, 0, '    '); print("body-only edit then completion: %.1f ms" % compl())
# declaration edit: add a method after the class header (line index of the 'construct' closing is known: insert before first '// Computes')
k = next(i for i, l in enumerate(lines) if l.strip().startswith('// Computes step 0'))
edit(k, 0, 0, '    method added(): integer {\n        return 1\n    }\n\n')
t0 = time.perf_counter(); ms = compl(); print("declaration edit (new method) then completion: %.1f ms" % ms)
print("   next completion %.1f ms" % compl())
# import edit
edit(2, 0, 0, 'import strings\n')
ms = compl(); print("import edit then completion: %.1f ms" % ms); print("   next completion %.1f ms" % compl())
# adding a new file to disk -> watched files notification
newp = os.path.join(root, 'pkg040', 'f_new.nv')
open(newp, 'w').write('package pkg040\n\nmethod freshHelper(): integer {\n    return 1\n}\n')
c.notify('workspace/didChangeWatchedFiles', {'changes': [{'uri': 'file://' + newp, 'type': 1}]})
ms = compl(); print("didChangeWatchedFiles(new file) then completion: %.1f ms" % ms); print("   next %.1f ms" % compl())
os.remove(newp)
c.notify('workspace/didChangeWatchedFiles', {'changes': [{'uri': 'file://' + newp, 'type': 3}]})
print("after delete: %.1f ms" % compl())
print("rss", c.rss()['VmRSS'])
c.shutdown()
