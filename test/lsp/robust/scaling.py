#!/usr/bin/env python3
"""Scaling probe: how the time of every feature grows with the size of the document.

usage: scaling.py --bin <novus-lsp> --work <dir> --shape <name> --sizes 1000,2000,4000 [--limit 30]
Shapes: methods (N small methods, one per 4 lines), vars (N statements in one method), expr (N terms in one
binary expression on one line), stmts (N statements on ONE line), string (a string literal of N bytes),
comment (a comment of N bytes), lines (N empty lines).
For every size a fresh server is started, the document opened, and each feature timed (first call only; limit seconds).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lspclient import Server, uri_of, open_document  # noqa: E402

SHAPES = {
    "methods": lambda n: "package main\n\n" + "".join("method m%d(int a): int {\n    var b = a + %d\n    return b\n}\n" % (i, i) for i in range(n)),
    "vars": lambda n: "package main\n\nmethod main() {\n" + "".join("    var v%d = %d\n" % (i, i) for i in range(n)) + "}\n",
    "expr": lambda n: "package main\n\nmethod main() {\n    var s = 1" + " + 1" * n + "\n    return\n}\n",
    "stmts": lambda n: "package main\nmethod a() { " + "a = b; " * n + "}\n",
    "string": lambda n: "package main\n\nmethod main() {\n    var s = \"" + "x" * n + "\"\n    return\n}\n",
    "comment": lambda n: "package main\n// " + "x" * n + "\nmethod main() {\n}\n",
    "lines": lambda n: "package main\n" + "\n" * n + "method main() {\n}\n",
}


def features(uri, line, column):
    at = {"textDocument": {"uri": uri}, "position": {"line": line, "character": column}}
    whole = {"textDocument": {"uri": uri}}
    return [("didOpen+documentSymbol", "textDocument/documentSymbol", whole), ("foldingRange", "textDocument/foldingRange", whole),
            ("semanticTokens", "textDocument/semanticTokens/full", whole), ("hover", "textDocument/hover", at), ("completion", "textDocument/completion", at),
            ("definition", "textDocument/definition", at), ("highlight", "textDocument/documentHighlight", at), ("signatureHelp", "textDocument/signatureHelp", at),
            ("formatting", "textDocument/formatting", dict(whole, options={"tabSize": 4, "insertSpaces": True})), ("references", "textDocument/references", dict(at, context={"includeDeclaration": True})),
            ("prepareRename", "textDocument/prepareRename", at), ("rename", "textDocument/rename", dict(at, newName="zz")),
            ("codeAction", "textDocument/codeAction", {"textDocument": {"uri": uri}, "range": {"start": at["position"], "end": at["position"]}, "context": {"diagnostics": []}}),
            ("documentLink", "textDocument/documentLink", whole), ("workspace/symbol", "workspace/symbol", {"query": "m"})]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--shape", required=True)
    parser.add_argument("--sizes", required=True)
    parser.add_argument("--limit", type=float, default=30.0)
    parser.add_argument("--only", default="")
    parser.add_argument("--skip", default="", help="comma separated feature labels to leave out (semanticTokens is quadratic and blocks the server)")
    parser.add_argument("--at", default="start", help="start | middle | end: where the position requests point")
    args = parser.parse_args()
    os.makedirs(args.work, exist_ok=True)
    for size in [int(x) for x in args.sizes.split(",")]:
        text = SHAPES[args.shape](size)
        server = Server(os.path.abspath(args.bin), args.work, env={"TMPDIR": args.work}, stderr_path=os.path.join(args.work, "stderr.log"))
        server.initialize()
        uri = uri_of(os.path.join(args.work, "big.nv"))
        started = time.time()
        open_document(server, uri, text, 1)
        row = []
        lines = text.count("\n")
        line = {"start": min(3, lines), "middle": lines // 2, "end": max(lines - 3, 0)}[args.at]
        for label, method, params in features(uri, line, 8):
            if args.only and args.only not in label:
                continue
            if label in args.skip.split(","):
                continue
            t0 = time.time()
            response = server.request(method, params, args.limit)
            took = time.time() - t0 if label != "didOpen+documentSymbol" else time.time() - started
            state = "TIMEOUT" if response is None else ("err%s" % response["error"]["code"] if "error" in response else "")
            row.append("%s=%.2f%s" % (label, took, state))
            if response is None:
                break
        print("size %-8d bytes %-9d " % (size, len(text)) + " ".join(row), flush=True)
        server.close()


if __name__ == "__main__":
    main()
