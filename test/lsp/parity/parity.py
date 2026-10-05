#!/usr/bin/env python3
"""Parity matrix of novus-lsp against the TypeScript language server (vscode-novus/src/server).

    python3 test/lsp/parity/parity.py [--bin PATH] [--area NAME] [--list] [--verbose]

Exit code 0 when every check behaves as expected.txt says: a check listed in expected_failing.txt must fail (a known
gap), every other check must pass."""
import argparse
import importlib
import os
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plib  # noqa: E402

MODULES = ["checks_completion", "checks_navigation", "checks_structure", "checks_format", "checks_diagnostics", "checks_protocol", "checks_perf"]


def load_expected(path):
    wanted = {}
    if not os.path.exists(path):
        return wanted
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#"):
            name, _, why = line.partition(" ")
            wanted[name] = why.strip()
    return wanted


def run_one(env, entry):
    started = time.time()
    attempts = 2 if entry["area"] == "performance" else 1  # a budget is timed: one retry against a noisy machine
    try:
        for _ in range(attempts):
            ok, detail = entry["fn"](env)
            if ok:
                break
    except Exception as exc:  # a crash of the check or of the server is a failure of the check
        ok, detail = False, "EXCEPTION %s: %s" % (type(exc).__name__, exc)
        if os.environ.get("PARITY_TRACE"):
            traceback.print_exc()
    return bool(ok), str(detail), time.time() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=os.environ.get("NOVUS_LSP_BIN") or os.path.join(plib.REPO, "build", "novus-lsp"))
    parser.add_argument("--area")
    parser.add_argument("--only")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    for name in MODULES:
        if os.path.exists(os.path.join(plib.HERE, name + ".py")):
            importlib.import_module(name)
    entries = [e for e in plib.REGISTRY if (not args.area or e["area"] == args.area) and (not args.only or args.only in e["id"])]
    if args.list:
        for e in entries:
            print(e["id"], "-", e["title"])
        return 0
    expected = load_expected(os.path.join(plib.HERE, "expected_failing.txt"))
    env = plib.Env(args.bin)
    stats = {}
    problems = []
    try:
        for entry in entries:
            ok, detail, secs = run_one(env, entry)
            gap = entry["id"] in expected
            status = "pass" if ok else ("GAP " if gap else "FAIL")
            if ok and gap:
                status = "XPASS"
                problems.append("%s passes now: remove it from expected_failing.txt" % entry["id"])
            if not ok and not gap:
                problems.append("%s fails: %s" % (entry["id"], detail))
            row = stats.setdefault(entry["area"], {"pass": 0, "gap": 0, "fail": 0})
            row["pass" if ok else ("gap" if gap else "fail")] += 1
            if not ok or args.verbose:
                print("%-5s %-44s %s" % (status, entry["id"], entry["title"] if ok else detail[:300]))
            sys.stdout.flush()
    finally:
        env.cleanup()
    print_table(stats)
    unknown = [k for k in expected if k not in {e["id"] for e in plib.REGISTRY}]
    problems += ["expected_failing.txt names an unknown check: %s" % k for k in unknown]
    for line in problems:
        print("PROBLEM", line)
    return 1 if problems else 0


def print_table(stats):
    print("\n%-14s %5s %5s %5s %7s" % ("area", "pass", "gap", "fail", "score"))
    totals = {"pass": 0, "gap": 0, "fail": 0}
    for area in sorted(stats):
        row = stats[area]
        total = row["pass"] + row["gap"] + row["fail"]
        print("%-14s %5d %5d %5d %6.0f%%" % (area, row["pass"], row["gap"], row["fail"], 100.0 * row["pass"] / total))
        for key in totals:
            totals[key] += row[key]
    total = sum(totals.values())
    if total:
        print("%-14s %5d %5d %5d %6.0f%%" % ("TOTAL", totals["pass"], totals["gap"], totals["fail"], 100.0 * totals["pass"] / total))


if __name__ == "__main__":
    sys.exit(main())
