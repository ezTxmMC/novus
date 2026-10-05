"""Every feature on real source files in both encodings: results are validated the way a strict client reads them."""
import os
import random
import re
import time

from harness import scenario, Session, make_workspace, REPO
from scen_flow import completion_items
from shapes import Doc, check_symbols, check_semantic_tokens, check_edits, check_completion_item, check_hover, uri_path

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SAMPLE_FILES = ["server/jobs/scheduler.nv", "projects/workspace/workspace.nv", "features/candidates/documentation.nv", "syntax/parsing/parser.nv",
                "server/rpc/router.nv", "services/snippets/catalogue_flow.nv", "base/text/lineindex.nv", "server/protocol/params.nv", "main.nv"]


def first_n(items, n):
    return list(items)[:n]


def location_problems(result, label, project_root, problems, encoding):
    locations = result if isinstance(result, list) else ([result] if result else [])
    for loc in locations:
        uri = loc.get("uri") or loc.get("targetUri")
        rng = loc.get("range") or loc.get("targetSelectionRange")
        path = uri_path(uri) if uri else None
        if not uri or rng is None:
            problems.append("%s: location without uri/range: %r" % (label, loc))
            continue
        if path and os.path.isfile(path):
            with open(path, encoding="utf-8", errors="replace") as handle:
                problem = Doc(handle.read(), encoding).valid_range(rng)
            if problem:
                problems.append("%s: %s in %s" % (label, problem, os.path.basename(path)))
        elif path:
            problems.append("%s: location points to a file that does not exist: %s" % (label, path))


def exercise(report, encoding, root=None, files=None):
    root = root or os.path.join(REPO, "lsp")
    def edit(caps):
        caps["general"]["positionEncodings"] = [encoding]
    s = Session("vscode", root, caps_edit=edit)
    c = s.client
    problems = []
    rng = random.Random(7)
    counts = {}
    try:
        legend = s.init_result["result"]["capabilities"]["semanticTokensProvider"]["legend"]
        for relative in (files or SAMPLE_FILES):
            path = os.path.join(root, relative)
            if not os.path.exists(path):
                continue
            text = open(path, encoding="utf-8").read()
            doc = Doc(text, encoding)
            uri = s.open(relative, text)
            tag = relative
            r = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
            check_symbols(r.get("result") or [], doc, problems_for(problems, tag + " documentSymbol"))
            r = c.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
            counts["tokens"] = counts.get("tokens", 0) + len((r.get("result") or {}).get("data", [])) // 5
            check_semantic_tokens((r.get("result") or {}).get("data", []), doc, legend, problems_for(problems, tag + " semanticTokens"))
            r = c.request("textDocument/semanticTokens/range", {"textDocument": {"uri": uri}, "range": {"start": {"line": 3, "character": 0}, "end": {"line": 20, "character": 0}}})
            check_semantic_tokens((r.get("result") or {}).get("data", []), doc, legend, problems_for(problems, tag + " semanticTokens/range"))
            r = c.request("textDocument/foldingRange", {"textDocument": {"uri": uri}})
            for fold in r.get("result") or []:
                if not (0 <= fold["startLine"] <= fold["endLine"] < len(doc.lines)):
                    problems.append("%s folding range %r outside the document" % (tag, fold))
            r = c.request("textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}})
            check_edits(r.get("result") or [], doc, problems_for(problems, tag + " formatting"), tag + " formatting")
            r = c.request("textDocument/documentLink", {"textDocument": {"uri": uri}})
            for link in r.get("result") or []:
                problem = doc.valid_range(link.get("range"))
                if problem:
                    problems.append("%s documentLink: %s" % (tag, problem))
            r = c.request("textDocument/codeAction", {"textDocument": {"uri": uri}, "range": {"start": {"line": 0, "character": 0}, "end": {"line": min(5, len(doc.lines) - 1), "character": 0}}, "context": {"diagnostics": [], "triggerKind": 1}})
            for action in r.get("result") or []:
                if not isinstance(action.get("title"), str):
                    problems.append("%s codeAction without title" % tag)
            positions = []
            for number, line in enumerate(doc.lines):
                for match in IDENT.finditer(line):
                    positions.append((number, match.start(), match.group(0)))
            for number, start, word in rng.sample(positions, min(40, len(positions))):
                from textmodel import width
                column = width(doc.lines[number][:start], encoding)
                pos = {"line": number, "character": column + min(1, len(word))}
                params = {"textDocument": {"uri": uri}, "position": pos}
                counts["positions"] = counts.get("positions", 0) + 1
                r = c.request("textDocument/hover", params)
                if r.get("result"):
                    check_hover(r["result"], doc, problems_for(problems, "%s hover %s" % (tag, word)))
                if "error" in r:
                    problems.append("%s hover error %r" % (tag, r["error"]))
                r = c.request("textDocument/definition", params)
                if "error" in r:
                    problems.append("%s definition error %r at %s" % (tag, r["error"], word))
                location_problems(r.get("result"), "%s definition %s" % (tag, word), root, problems, encoding)
                r = c.request("textDocument/references", {**params, "context": {"includeDeclaration": True}})
                if "error" in r:
                    problems.append("%s references error %r at %s" % (tag, r["error"], word))
                location_problems(r.get("result"), "%s references %s" % (tag, word), root, problems, encoding)
                counts["references"] = counts.get("references", 0) + len(r.get("result") or [])
                r = c.request("textDocument/documentHighlight", params)
                for h in r.get("result") or []:
                    problem = doc.valid_range(h.get("range"))
                    if problem:
                        problems.append("%s highlight %s: %s" % (tag, word, problem))
                r = c.request("textDocument/prepareRename", params)
                if r.get("result") and isinstance(r["result"], dict) and "range" in r["result"]:
                    problem = doc.valid_range(r["result"]["range"])
                    if problem:
                        problems.append("%s prepareRename %s: %s" % (tag, word, problem))
                r = c.request("textDocument/signatureHelp", params)
                if "error" in r:
                    problems.append("%s signatureHelp error %r" % (tag, r["error"]))
                end_pos = {"line": number, "character": column + width(word, encoding)}
                r = c.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": end_pos, "context": {"triggerKind": 1}})
                if "error" in r:
                    problems.append("%s completion error %r after %s" % (tag, r["error"], word))
                    continue
                items = completion_items(r)
                counts["items"] = counts.get("items", 0) + len(first_n(items, 60))
                for item in first_n(items, 60):
                    check_completion_item(item, doc, number, end_pos["character"], problems_for(problems, "%s completion after %s" % (tag, word)))
            c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        s.shutdown()
    finally:
        s.close()
    return problems, counts


def problems_for(sink, tag):
    class Collector(list):
        def append(self, item):
            sink.append("%s: %s" % (tag, item))
    return Collector()


@scenario("shapes-real-sources-utf16")
def shapes_utf16(report):
    run_shapes(report, "utf-16")


@scenario("shapes-real-sources-utf8")
def shapes_utf8(report):
    run_shapes(report, "utf-8")


UNICODE_MAIN = (
    'package main\n\nimport geo\n\n// \u4e2d\u6587 \U0001F389 comment before code\nmethod main() {\n'
    '    var label = "\U0001F600\u00e9" + "\u4e2d\u6587"; var count = 3 // \U0001F600\n'
    '    println("\U0001F600\u00e9" + count + label)\n'
    '    var shape = Shape("\U0001F600", 2.5); println(shape.name + "\U0001F468\u200d\U0001F469\u200d\U0001F467" + count)\n'
    '    var again = describe(shape) + "e\u0301" + label\n'
    '    println(again.length())\n}\n')


@scenario("shapes-unicode-multibyte")
def shapes_unicode(report):
    for encoding in ("utf-16", "utf-8"):
        root = make_workspace(files={"main.nv": UNICODE_MAIN})
        problems, counts = exercise(report, encoding, root=root, files=["main.nv", "geo/shape.nv"])
        for p in list(dict.fromkeys(re.sub(r"\d+", "N", p)[:200] for p in problems))[:10]:
            report.check(False, "unicode %s: %s" % (encoding, p))
        report.note("unicode %s: %r, %d problems" % (encoding, counts, len(problems)))


@scenario("shapes-crlf-documents")
def shapes_crlf(report):
    for encoding in ("utf-16", "utf-8"):
        root = make_workspace(files={"main.nv": UNICODE_MAIN.replace("\n", "\r\n")})
        problems, counts = exercise(report, encoding, root=root, files=["main.nv"])
        for p in list(dict.fromkeys(re.sub(r"\d+", "N", p)[:200] for p in problems))[:10]:
            report.check(False, "crlf %s: %s" % (encoding, p))
        report.note("crlf %s: %r, %d problems" % (encoding, counts, len(problems)))


def run_shapes(report, encoding):
    problems, counts = exercise(report, encoding)
    unique = {}
    for p in problems:
        key = re.sub(r"[A-Za-z_]+(?= hover| definition| references| highlight| prepareRename| completion)", "", p)[:160]
        unique.setdefault(re.sub(r"\d+", "N", p)[:200], p)
    for p in list(unique.values())[:15]:
        report.check(False, p)
    report.note("%s: %r, %d problems (%d distinct)" % (encoding, counts, len(problems), len(unique)))
