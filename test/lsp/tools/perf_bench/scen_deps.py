import sys, os, time
from bench import *
B = sys.argv[1]; HERE = os.getcwd()
root = HERE + '/depproj'; env = {'NOVUS_DEPS': HERE + '/deps3'}
path = root + '/main.nv'; text = open(path).read(); uri = 'file://' + path
c = Client(B, env=env); c.initialize(root, {'novus': {'check': {'mode': 'off'}}})
t1 = time.perf_counter(); c.open(path, text)
lines = text.split('\n')
def comp(ln, ch, label):
    p = tdp(uri, ln, ch); p['context'] = {'triggerKind': 1}
    m, ms = c.request('textDocument/completion', p)
    r = m['result']; n = len(r['items']) if isinstance(r, dict) else len(r or [])
    print("  %-40s %7.1f ms  %d items" % (label, ms, n)); return ms
# put the cursor at the end of 'var t = Lib0T' -> prefix completion reaching into the dependency
ln = 8
comp(ln, len('    var t = L'), "first completion (prefix 'L' into deps)")
comp(ln, len('    var t = L'), "same again (warm)")
comp(9, len('    var u = Lib1'), "prefix Lib1")
# member completion on a dep type
c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': 2}, 'contentChanges': [{'range': {'start': {'line': 10, 'character': 0}, 'end': {'line': 10, 'character': 0}}, 'text': '    t.\n'}]})
comp(10, 6, "member completion t.")
# import path completion
c.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': 3}, 'contentChanges': [{'range': {'start': {'line': 5, 'character': 0}, 'end': {'line': 5, 'character': 0}}, 'text': 'import "github.com/\n'}]})
comp(5, len('import "github.com/'), "import path completion")
d = c.wait_diag(uri, 0, 30); print("  diagnostics after open: %.0f ms" % ((d - t1) * 1000 if d else -1))
m, ms = c.request('workspace/symbol', {'query': 'Lib2Thing'}); print("  workspace/symbol %.1f ms (%d)" % (ms, len(m['result'] or [])))
# definition into dependency
m, ms = c.request('textDocument/definition', tdp(uri, 8, len('    var t = Lib0T'))); print("  definition into dep %.1f ms" % ms, bool(m['result']))
print("  rss", c.rss())
c.shutdown()
