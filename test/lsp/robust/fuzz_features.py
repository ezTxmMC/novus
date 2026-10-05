#!/usr/bin/env python3
"""Drives the real novus-lsp with mutated repository files and asks every feature at many offsets.

usage: fuzz_features.py --bin <novus-lsp> --out <dir> [--jobs 8] [--step 0] [--seed 1] [--limit N] [--per-file-variants 6]

Invariants (a violation is a finding): the server never exits, every request is answered within 5 s, every
frame is valid JSON-RPC, a response is a result or a JSON-RPC error (-32603 internal error is reported too:
the guard saved the process but a feature crashed), ranges in results are well formed (start <= end, >= 0).
Findings are written to <out>/findings.jsonl; the text that triggered each one to <out>/repro/.
"""
import argparse
import hashlib
import json
import os
import random
import sys
import time
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lspclient import Server, uri_of, open_document  # noqa: E402
import invariants  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXTENSIONS = (".nv", ".nvh")
INVALID_MARKER_CHARS = ["\ue000", "\ue001", "\ue002", "\ue003", "\ue004", "\ue005"]
ODD_FRAGMENTS = ["\U0001F600", "中文", "\r\n", "\u0000", "é", "👨‍👩", "\"", "'", "{", "}", "(", ")", "/*", "//", "\\", " ", "﻿", "\t"]
FEATURES_AT = [
    ("textDocument/completion", lambda d, p: {"textDocument": d, "position": p, "context": {"triggerKind": 1}}),
    ("textDocument/hover", lambda d, p: {"textDocument": d, "position": p}),
    ("textDocument/definition", lambda d, p: {"textDocument": d, "position": p}),
    ("textDocument/references", lambda d, p: {"textDocument": d, "position": p, "context": {"includeDeclaration": True}}),
    ("textDocument/documentHighlight", lambda d, p: {"textDocument": d, "position": p}),
    ("textDocument/signatureHelp", lambda d, p: {"textDocument": d, "position": p}),
    ("textDocument/prepareRename", lambda d, p: {"textDocument": d, "position": p}),
    ("textDocument/rename", lambda d, p: {"textDocument": d, "position": p, "newName": "renamedX"}),
    ("textDocument/codeAction", lambda d, p: {"textDocument": d, "range": {"start": p, "end": p}, "context": {"diagnostics": []}}),
]
WHOLE_DOCUMENT = [
    ("textDocument/documentSymbol", lambda d, n: {"textDocument": d}),
    ("textDocument/foldingRange", lambda d, n: {"textDocument": d}),
    ("textDocument/semanticTokens/full", lambda d, n: {"textDocument": d}),
    ("textDocument/semanticTokens/range", lambda d, n: {"textDocument": d, "range": {"start": {"line": 0, "character": 0}, "end": {"line": max(n // 2, 1), "character": 3}}}),
    ("textDocument/formatting", lambda d, n: {"textDocument": d, "options": {"tabSize": 4, "insertSpaces": True}}),
    ("textDocument/rangeFormatting", lambda d, n: {"textDocument": d, "range": {"start": {"line": 0, "character": 0}, "end": {"line": max(n // 2, 1), "character": 0}}, "options": {"tabSize": 4, "insertSpaces": True}}),
    ("textDocument/documentLink", lambda d, n: {"textDocument": d}),
    ("workspace/symbol", lambda d, n: {"query": "a"}),
    ("completionItem/resolve", lambda d, n: {"label": "x", "kind": 3}),
]


ERRORS = {}
CHECKED = {}
LATENCY = {}
SLOW = []
SLOW_MS = 300
PEAK_RSS_KB = [0]
STATS = {"requests": 0, "nonempty": 0, "errors": 0, "max_ms": 0}


def rss_kb(pid):
    try:
        with open("/proc/%d/status" % pid) as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def collect_files():
    found = []
    for base, dirs, files in os.walk(REPO):
        dirs[:] = [x for x in dirs if x not in ("build", "node_modules", ".git")]
        for name in files:
            if name.endswith(EXTENSIONS) or name == "project.nv":
                found.append(os.path.join(base, name))
    return sorted(found)


def utf16_position(text, offset):
    """Line and UTF-16 character of a character offset."""
    line = text.count("\n", 0, offset)
    start = text.rfind("\n", 0, offset) + 1
    column = len(text[start:offset].encode("utf-16-le")) // 2
    return {"line": line, "character": column}


def positions_of(text, rng, count):
    offsets = {0, len(text)}
    for i in range(1, count):
        offsets.add(len(text) * i // count)
    for _ in range(count):
        offsets.add(rng.randrange(0, len(text) + 1))
    chosen = [utf16_position(text, o) for o in sorted(offsets)]
    chosen += [{"line": text.count("\n") + 5, "character": 0}, {"line": 0, "character": 100000}]
    return chosen


def make_variants(text, rng, per_file):
    variants = [("original", text)]
    size = len(text)
    step = max(size // per_file, 1)
    for cut in range(step, size, step):
        variants.append(("truncate@%d" % cut, text[:cut]))
    for index in range(per_file):
        if size == 0:
            break
        chars = list(text)
        for _ in range(rng.randint(1, 4)):
            at = rng.randrange(0, len(chars) + 1)
            kind = rng.randrange(3)
            if kind == 0:
                chars.insert(at, rng.choice(ODD_FRAGMENTS))
            elif kind == 1 and at < len(chars):
                del chars[at:at + rng.randint(1, 30)]
            elif at < len(chars):
                chars[at] = rng.choice(ODD_FRAGMENTS)
        variants.append(("mutate#%d" % index, "".join(chars)))
    for index in range(max(per_file // 2, 1)):
        chars = list(text)
        for _ in range(rng.randint(1, 3)):
            chars.insert(rng.randrange(0, len(chars) + 1), rng.choice(INVALID_MARKER_CHARS))
        variants.append(("invalid-utf8#%d" % index, "".join(chars)))
    return variants


def check_range(node, where, problems):
    if isinstance(node, dict):
        start, end = node.get("start"), node.get("end")
        if isinstance(start, dict) and isinstance(end, dict) and "line" in start and "line" in end:
            a = (start.get("line", 0), start.get("character", 0))
            b = (end.get("line", 0), end.get("character", 0))
            if min(a + b) < 0 or a > b:
                problems.append("malformed range %s at %s" % (json.dumps(node)[:120], where))
        for value in node.values():
            check_range(value, where, problems)
    elif isinstance(node, list):
        for value in node[:2000]:
            check_range(value, where, problems)


def judge(method, response):
    """Problems of one response (empty list = fine)."""
    if response is None:
        return ["timeout"]
    if "error" in response:
        error = response["error"]
        code = error.get("code") if isinstance(error, dict) else None
        if code == -32603:
            return ["internal-error: " + str(error.get("message"))[:160]]
        if not isinstance(error, dict) or not isinstance(code, int) or not isinstance(error.get("message"), str):
            return ["malformed error object"]
        return []
    problems = []
    check_range(response.get("result"), method, problems)
    return problems


def run_variant(server, uri, label, text, version, rng, findings, path, repro_dir):
    document = {"uri": uri}
    if version == 1:
        open_document(server, uri, text, version)
    else:
        message = {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": text}]}}
        server.send_with_markers(message)
    lines = text.count("\n") + 1
    issued = []
    for position in positions_of(text, rng, 6):
        for method, build in FEATURES_AT:
            params = build(document, position)
            issued.append((method, position, server.call(method, params), params))
    for method, build in WHOLE_DOCUMENT:
        params = build(document, lines)
        issued.append((method, None, server.call(method, params), params))
    doc = invariants.Doc(text)
    deep = not label.startswith("invalid-utf8")
    for method, position, request_id, params in issued:
        started = time.time()
        response = server.wait(request_id, 5.0)
        STATS["requests"] += 1
        if response is not None and response.get("result") not in (None, [], {}):
            STATS["nonempty"] += 1
        if response is not None and "error" in response:
            STATS["errors"] += 1
            kind = "%s %s %s" % (method, response["error"].get("code"), str(response["error"].get("message"))[:60])
            ERRORS[kind] = ERRORS.get(kind, 0) + 1
        elapsed_ms = int((time.time() - started) * 1000)
        STATS["max_ms"] = max(STATS["max_ms"], elapsed_ms)
        LATENCY.setdefault(method, []).append(elapsed_ms)
        if elapsed_ms > SLOW_MS:
            SLOW.append((elapsed_ms, os.path.relpath(path, REPO), label, method, len(text)))
        problem_list = judge(method, response)
        if response is not None and response.get("result"):
            CHECKED[method] = CHECKED.get(method, 0) + 1
        if deep and not problem_list and response is not None and "result" in response:
            problem_list = invariants.check(method, params, response["result"], doc, server.legend, position)
        if deep and method == "textDocument/formatting" and not problem_list and response is not None and response.get("result"):
            problem_list = idempotency_problems(server, uri, doc, response["result"], version)
        for problem in problem_list:
            record(findings, repro_dir, path, label, text, method, position, problem, time.time() - started)
        if not server.alive():
            return False
    return True


def idempotency_problems(server, uri, doc, edits, version):
    """Formats the formatted text again: it must give no edits. The document is restored afterwards."""
    formatted = invariants.formatted_text(doc, edits)
    if formatted is None or formatted == doc.text:
        return []
    server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version + 1000}, "contentChanges": [{"text": formatted}]})
    again = server.request("textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}}, 5.0)
    server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version + 2000}, "contentChanges": [{"text": doc.text}]})
    if again is None or "result" not in again:
        return ["formatting of the formatted text: %r" % (again,)]
    second = invariants.formatted_text(invariants.Doc(formatted), again["result"])
    if second is not None and second != formatted:
        return ["formatting is not idempotent"]
    return []


def record(findings, repro_dir, path, label, text, method, position, problem, elapsed):
    digest = hashlib.sha1(text.encode("utf-8", "surrogatepass")).hexdigest()[:12]
    name = "%s-%s.txt" % (os.path.basename(path), digest)
    with open(os.path.join(repro_dir, name), "w", encoding="utf-8", errors="surrogatepass", newline="") as handle:
        handle.write(text)
    findings.append({"file": os.path.relpath(path, REPO), "variant": label, "method": method, "position": position,
                     "problem": problem, "seconds": round(elapsed, 2), "repro": name})


def fuzz_files(args_tuple):
    binary, out, files, seed, per_file, worker = args_tuple
    work = os.path.join(out, "work%d" % worker)
    repro_dir = os.path.join(out, "repro")
    os.makedirs(work, exist_ok=True)
    os.makedirs(repro_dir, exist_ok=True)
    findings = []
    stats = {"files": 0, "variants": 0, "restarts": 0}
    server = None
    for path in files:
        rng = random.Random(seed ^ hash(path) & 0xFFFFFF)
        try:
            with open(path, "r", encoding="utf-8", errors="replace", newline="") as handle:
                text = handle.read()
        except OSError:
            continue
        if server is None or not server.alive():
            if server is not None:
                findings.append({"file": os.path.relpath(path, REPO), "problem": "SERVER EXITED before this file", "variant": "-", "method": "-"})
                server.close()
            server = Server(binary, REPO, env={"TMPDIR": work}, stderr_path=os.path.join(work, "stderr.log"))
            stats["restarts"] += 1
            server.initialize()
        uri = uri_of(path)
        stats["files"] += 1
        for number, (label, variant) in enumerate(make_variants(text, rng, per_file)):
            stats["variants"] += 1
            ok = run_variant(server, uri, label, variant, number + 1, rng, findings, path, repro_dir)
            if not ok:
                findings.append({"file": os.path.relpath(path, REPO), "variant": label, "method": "-", "problem": "SERVER EXITED during this variant", "repro": None})
                break
        PEAK_RSS_KB[0] = max(PEAK_RSS_KB[0], rss_kb(server.proc.pid))
        if server.violations:
            findings.append({"file": os.path.relpath(path, REPO), "variant": "-", "method": "-", "problem": "protocol violations: " + "; ".join(server.violations[:3])})
            server.violations.clear()
        if server.alive():
            server.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
    if server is not None:
        code, response = server.shutdown()
        if code != 0:
            findings.append({"file": "-", "variant": "-", "method": "shutdown", "problem": "unclean end, exit code %r" % (code,)})
        server.close()
    stats.update(STATS)
    stats["error_kinds"] = ERRORS
    stats["checked"] = CHECKED
    stats["latency"] = LATENCY
    stats["slow"] = SLOW
    stats["rss_kb"] = PEAK_RSS_KB[0]
    return findings, stats


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--stride", type=int, default=1, help="use every Nth repository file")
    parser.add_argument("--per-file-variants", type=int, default=6)
    parser.add_argument("--only", default="", help="substring filter on the path")
    parser.add_argument("--known", default="", help="file of known defects, one `method|start of the problem text` per line; they are counted, not failing")
    args = parser.parse_args()
    files = [f for f in collect_files() if args.only in f][::args.stride]
    if args.limit:
        files = files[:args.limit]
    os.makedirs(args.out, exist_ok=True)
    chunks = [(args.bin, args.out, files[i::args.jobs], args.seed, args.per_file_variants, i) for i in range(args.jobs)]
    all_findings, totals = [], {"files": 0, "variants": 0, "restarts": 0, "requests": 0, "nonempty": 0, "errors": 0}
    slowest = 0
    peak_rss = [0]
    all_slow = []
    all_checked = {}
    all_latency = {}
    error_kinds = {}
    started = time.time()
    with ProcessPoolExecutor(args.jobs) as pool:
        for findings, stats in pool.map(fuzz_files, chunks):
            all_findings += findings
            slowest = max(slowest, stats["max_ms"])
            for method, count in stats["checked"].items():
                all_checked[method] = all_checked.get(method, 0) + count
            peak_rss[0] = max(peak_rss[0], stats["rss_kb"])
            all_slow.extend(stats["slow"])
            for method, values in stats["latency"].items():
                all_latency.setdefault(method, []).extend(values)
            for kind, count in stats["error_kinds"].items():
                error_kinds[kind] = error_kinds.get(kind, 0) + count
            for key in [k for k in totals]:
                totals[key] += stats[key]
    known_rules = []
    if args.known:
        with open(args.known) as handle:
            known_rules = [tuple(line.rstrip("\n").split("|", 1)) for line in handle if "|" in line and not line.startswith("#")]
    known = [f for f in all_findings if any(f.get("method") == m and f["problem"].startswith(t) for m, t in known_rules)]
    all_findings = [f for f in all_findings if f not in known]
    if known:
        print("known findings (expected_failures): %d" % len(known))
    with open(os.path.join(args.out, "findings.jsonl"), "w") as handle:
        for finding in all_findings:
            handle.write(json.dumps(finding) + "\n")
    kinds = {}
    for finding in all_findings:
        key = (finding.get("method"), finding["problem"][:70])
        kinds[key] = kinds.get(key, 0) + 1
    print("files %d variants %d restarts %d requests %d nonempty %d errors %d slowest %d ms, in %.0fs; findings %d" % (
        totals["files"], totals["variants"], totals["restarts"], totals["requests"], totals["nonempty"], totals["errors"], slowest, time.time() - started, len(all_findings)))
    print("non-empty results checked by invariants: " + ", ".join("%s=%d" % (m.split("/")[-1], c) for m, c in sorted(all_checked.items())))
    print("peak server RSS %.0f MB" % (peak_rss[0] / 1024.0))
    for method, values in sorted(all_latency.items()):
        values.sort()
        print("  latency %-40s n=%-7d p50=%-4d p95=%-5d p99=%-5d max=%d ms" % (method, len(values), values[len(values) // 2], values[int(len(values) * 0.95)], values[int(len(values) * 0.99)], values[-1]))
    for entry in sorted(all_slow, reverse=True)[:15]:
        print("  slow %5d ms  %s [%s] %s (text %d chars)" % entry)
    for kind, count in sorted(error_kinds.items()):
        print("  answered error: %6d  %s" % (count, kind))
    for (method, problem), count in sorted(kinds.items(), key=lambda item: -item[1]):
        print("%6d  %s  %s" % (count, method, problem))
    return 1 if all_findings else 0


if __name__ == "__main__":
    sys.exit(main())
