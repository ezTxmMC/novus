#!/usr/bin/env python3
"""The budgets of lsp/DESIGN.md 8.9, measured on this repository with the real server binary.
Usage: python3 budgets.py SERVER_BINARY
Budgets listed in known_failures.txt are reported as XFAIL and do not fail the run (they fail today; remove the line
when the budget holds). Exit code 1 when a budget that is not listed fails."""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import REPO_ROOT, Client, pct, positions, tdp

FIRST_COMPLETION_MS = 400
WARM_COMPLETION_P95_MS = 50
PARSE_50KB_MS = 15
MEMORY_MB = 150
PARSE_FILE = 'compiler/nvh/nvh.nv'
OPTIONS = {'novus': {'check': {'mode': 'off'}}}


def known_failures():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'known_failures.txt')
    if not os.path.exists(path):
        return []
    return [line.split()[0] for line in open(path) if line.strip() and not line.startswith('#')]


def open_document(client, relative):
    path = os.path.join(REPO_ROOT, relative)
    text = open(path).read()
    client.open(path, text)
    return 'file://' + path, text


def first_completion(binary):
    client = Client(binary)
    client.initialize(REPO_ROOT, OPTIONS)
    uri, _ = open_document(client, 'compiler/main.nv')
    params = dict(tdp(uri, 20, 5), context={'triggerKind': 1})
    _, millis = client.request('textDocument/completion', params)
    client.shutdown()
    return millis


def warm_completion_p95(binary):
    client = Client(binary)
    client.initialize(REPO_ROOT, OPTIONS)
    uri, text = open_document(client, PARSE_FILE)
    client.request('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
    times = []
    for line, start, end, _ in positions(text, 200):
        _, millis = client.request('textDocument/completion', dict(tdp(uri, line, end), context={'triggerKind': 1}))
        times.append(millis)
    client.shutdown()
    return pct(times, 95)


def parse_cost(binary):
    client = Client(binary)
    client.initialize(REPO_ROOT, OPTIONS)
    uri, text = open_document(client, PARSE_FILE)
    request = ('textDocument/documentSymbol', {'textDocument': {'uri': uri}})
    client.request(*request)
    warm = pct([client.request(*request)[1] for _ in range(10)], 50)
    cold = []
    for version in range(2, 22):
        insert = version % 2 == 0
        edit_end = 0 if insert else 1
        client.notify('textDocument/didChange', {'textDocument': {'uri': uri, 'version': version}, 'contentChanges': [{'range': {'start': {'line': 0, 'character': 0}, 'end': {'line': 0, 'character': edit_end}}, 'text': ' ' if insert else ''}]})
        cold.append(client.request(*request)[1])
    client.shutdown()
    return pct(cold, 50) - warm


def repository_memory(binary):
    client = Client(binary)
    client.initialize(REPO_ROOT, OPTIONS)
    files = glob.glob(REPO_ROOT + '/compiler/**/*.nv', recursive=True) + glob.glob(REPO_ROOT + '/std/*.nv')
    for path in files:
        client.open(path, open(path).read())
    client.request('workspace/symbol', {'query': 'a'})
    for path in files[:50]:
        client.request('textDocument/semanticTokens/full', {'textDocument': {'uri': 'file://' + path}})
    peak = client.rss()['VmHWM'] / 1024.0
    client.shutdown()
    return peak


def main():
    binary = os.path.abspath(sys.argv[1])
    checks = [('first-completion', 'first completion after opening compiler/main.nv', first_completion, FIRST_COMPLETION_MS, 'ms'),
              ('warm-completion', 'completion p95 warm on %s' % PARSE_FILE, warm_completion_p95, WARM_COMPLETION_P95_MS, 'ms'),
              ('parse-50kb', 'lex + parse of a 50 KB file (didChange cost)', parse_cost, PARSE_50KB_MS, 'ms'),
              ('memory', 'peak RSS with the whole repository open', repository_memory, MEMORY_MB, 'MB')]
    expected = known_failures()
    failed = 0
    for key, label, measure, limit, unit in checks:
        value = measure(binary)
        verdict = 'ok' if value < limit else ('XFAIL' if key in expected else 'FAIL')
        failed += 1 if verdict == 'FAIL' else 0
        print('%-6s %-18s %-52s %8.1f %s (budget < %d %s)' % (verdict, key, label, value, unit, limit, unit))
    sys.exit(1 if failed else 0)


main()
