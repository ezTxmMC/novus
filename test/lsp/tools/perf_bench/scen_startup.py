import sys, time, os
sys.path.insert(0, os.path.dirname(__file__))
from lspclient import *
B = sys.argv[1]
xs = []; rs = []
for i in range(15):
    c = Client(B); t0 = time.perf_counter()
    msg, ms = c.initialize(None)
    xs.append((time.perf_counter()-c.t_spawn)*1000)
    rs.append(c.rss().get('VmRSS'))
    c.shutdown()
print("startup spawn->initialize response (no root):", summarize(xs), "RSS kB", rs[-1])
xs = []
for i in range(15):
    c = Client(B)
    msg, ms = c.initialize(REPO_ROOT)
    xs.append((time.perf_counter()-c.t_spawn)*1000)
    c.shutdown()
print("startup spawn->initialize response (root=repo):", summarize(xs))
