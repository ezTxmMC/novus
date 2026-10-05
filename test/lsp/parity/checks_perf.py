"""Budgets of lsp/DESIGN.md 8.9 and scaling of the requests an editor sends on every keystroke."""
import os
import time

from lspclient import Client
from plib import check, deep_merge, OPTIONS_QUIET, read, REPO

PERF = "performance"
COMPLETION_P95_MS = 50.0
FIRST_COMPLETION_MS = 400.0
MEMORY_LIMIT_MB = 150
SEMTOK_SCALING_LIMIT = 6.0


def open_repo(env, root, rel):
    client = Client(env.binary, os.path.join(REPO, root), options=OPTIONS_QUIET)
    text = read(os.path.join(REPO, root, rel))
    client.open(rel, text)
    client.diagnostics(rel, 0.4)
    return client, text


def statement_positions(text, count):
    lines = text.split("\n")
    spots = [(i, len(l) - len(l.lstrip())) for i, l in enumerate(lines) if l.startswith("        ") and l.strip()]
    return spots[:count]


def p95_of(samples):
    ordered = sorted(samples)
    return ordered[max(0, int(len(ordered) * 0.95) - 1)]


def completion_samples(client, rel, text, count=40):
    samples = []
    for line, col in statement_positions(text, count):
        started = time.time()
        client.at("textDocument/completion", rel, line, col)
        samples.append((time.time() - started) * 1000)
    return samples


@check(PERF, "completion-p95-compiler", "statement completion p95 < 50 ms in the compiler/ project (DESIGN 8.9)", "DESIGN 8.9")
def completion_compiler(env):
    client, text = open_repo(env, "compiler", "codegen/expressions.nv")
    try:
        samples = completion_samples(client, "codegen/expressions.nv", text)
        value = p95_of(samples)
        return (value < COMPLETION_P95_MS, "p95 %.1f ms over %d requests" % (value, len(samples)))
    finally:
        client.close()


@check(PERF, "completion-p95-lsp-project", "statement completion p95 < 50 ms in the 200-file lsp/ project (DESIGN 8.9)", "DESIGN 8.9")
def completion_lsp(env):
    client, text = open_repo(env, "lsp", "features/completion/engine.nv")
    try:
        samples = completion_samples(client, "features/completion/engine.nv", text)
        value = p95_of(samples)
        return (value < COMPLETION_P95_MS, "p95 %.1f ms (median %.1f) over %d requests" % (value, sorted(samples)[len(samples) // 2], len(samples)))
    finally:
        client.close()


@check(PERF, "first-completion", "the first completion after opening the lsp/ project < 400 ms (DESIGN 8.9)", "DESIGN 8.9")
def first_completion(env):
    client = Client(env.binary, os.path.join(REPO, "lsp"), options=OPTIONS_QUIET)
    try:
        rel = "features/completion/engine.nv"
        text = read(os.path.join(REPO, "lsp", rel))
        client.open(rel, text)
        line, col = statement_positions(text, 1)[0]
        started = time.time()
        client.at("textDocument/completion", rel, line, col)
        value = (time.time() - started) * 1000
        return (value < FIRST_COMPLETION_MS, "%.0f ms" % value)
    finally:
        client.close()


@check(PERF, "memory", "resident memory < 150 MB after working in the lsp/ project (DESIGN 8.9)", "DESIGN 8.9")
def memory(env):
    client, text = open_repo(env, "lsp", "features/completion/engine.nv")
    try:
        completion_samples(client, "features/completion/engine.nv", text, 20)
        client.call("textDocument/semanticTokens/full", "features/completion/engine.nv")
        client.at("textDocument/references", "features/completion/engine.nv", 20, 12, {"context": {"includeDeclaration": True}})
        with open("/proc/%d/status" % client.proc.pid) as status:
            rss = int(status.read().split("VmRSS:")[1].split()[0]) // 1024
        return (rss < MEMORY_LIMIT_MB, "%d MB" % rss)
    finally:
        client.close()


def generated(count):
    parts = ["package big\n\nmethod main {\n  println(f0(1))\n}\n"]
    for i in range(count):
        parts.append("\n// helper number %d\nmethod f%d(integer a): integer {\n  var total = a * 2\n  var names = [\"a\", \"b\", \"c\"]\n  for (name in names) {\n    if (name == \"b\") {\n      total = total + %d\n    } else {\n      total = total - 1\n    }\n  }\n  return total\n}\n" % (i, i, i))
    return "".join(parts)


def semtok_ms(env, count):
    src = generated(count)
    ses = env.session({"main.nv": "package big\n"})
    try:
        ses.set("main.nv", src)
        ses.c.drain(0.8)
        started = time.time()
        ses.c.call("textDocument/semanticTokens/full", "main.nv")
        return (time.time() - started) * 1000, len(src)
    finally:
        ses.close()


@check(PERF, "semantic-tokens-scaling", "semanticTokens/full grows about linearly with the file (4x the size costs < 6x the time)", "scaling")
def semtok_scaling(env):
    small, small_bytes = semtok_ms(env, 100)
    large, large_bytes = semtok_ms(env, 400)
    ratio = large / max(small, 1.0)
    return (ratio < SEMTOK_SCALING_LIMIT, "%.0f ms for %d KB, %.0f ms for %d KB: ratio %.1f" % (small, small_bytes // 1024, large, large_bytes // 1024, ratio))


@check(PERF, "semantic-tokens-large-file", "semanticTokens/full of a 380 KB file < 1 s (the editor asks after every edit)", "scaling")
def semtok_large(env):
    value, size = semtok_ms(env, 1600)
    return (value < 1000.0, "%.0f ms for %d KB" % (value, size // 1024))


@check(PERF, "edit-round-trip-50kb", "an edit of a 50 KB file and the next documentSymbol answer in < 40 ms (parse budget 15 ms, DESIGN 8.9)", "DESIGN 8.9")
def edit_round_trip(env):
    src = generated(200)
    ses = env.session({"main.nv": "package big\n"})
    try:
        ses.set("main.nv", src)
        ses.c.drain(0.8)
        samples = []
        for i in range(5):
            started = time.time()
            ses.set("main.nv", src + "\n" * (i + 1))
            ses.c.call("textDocument/documentSymbol", "main.nv")
            samples.append((time.time() - started) * 1000)
        value = sorted(samples)[len(samples) // 2]
        return (value < 40.0, "median %.0f ms for %d KB" % (value, len(src) // 1024))
    finally:
        ses.close()


@check(PERF, "hover-latency", "hover answers < 20 ms in the lsp/ project", "DESIGN 8.9")
def hover_latency(env):
    client, text = open_repo(env, "lsp", "features/completion/engine.nv")
    try:
        lines = text.split("\n")
        line = next(i for i, l in enumerate(lines) if "SourcePlan(" in l)
        col = lines[line].index("SourcePlan") + 2
        samples = []
        for _ in range(10):
            started = time.time()
            client.at("textDocument/hover", "features/completion/engine.nv", line, col)
            samples.append((time.time() - started) * 1000)
        value = p95_of(samples)
        return (value < 20.0, "p95 %.1f ms" % value)
    finally:
        client.close()
