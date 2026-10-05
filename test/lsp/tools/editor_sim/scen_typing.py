"""Typing simulation: real files edited keystroke by keystroke with unbalanced brackets, quotes and comments; after
every keystroke the editor asks for completion, hover, signature help, symbols and tokens. Nothing may fail or
return a malformed result."""
import os
import random
import time

from harness import scenario, Session, make_workspace, REPO
from scen_flow import completion_items
from shapes import Doc, check_symbols, check_semantic_tokens, check_completion_item, check_hover, check_edits
from textmodel import Model, width

FRAGMENTS = ["{", "}", "(", ")", "[", "]", '"', "'", ".", ",", " ", "\n", "var x = ", "//", "/*", "*/", "${", "}", "\U0001F600", "é", "中",
             "method ", "define class ", "if (", "return ", "import ", "package ", "@", "<", ">", ":", "=", "Shape", "this.", "println", "\\", "\t", "\r\n"]
FILES = ["server/jobs/scheduler.nv", "base/text/lineindex.nv", "services/snippets/catalogue_flow.nv", "main.nv", "syntax/parsing/parser.nv"]


def typing_session(report, encoding, seed, steps):
    root = os.path.join(REPO, "lsp")
    s = Session("vscode", root, caps_edit=lambda caps: caps["general"].__setitem__("positionEncodings", [encoding]))
    c = s.client
    rng = random.Random(seed)
    problems, requests = [], 0
    try:
        legend = s.init_result["result"]["capabilities"]["semanticTokensProvider"]["legend"]
        for relative in FILES:
            path = os.path.join(root, relative)
            text = open(path, encoding="utf-8").read()
            model = Model(text, encoding)
            uri = s.uri(relative)
            c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 1, "text": text}})
            line_count = len(model.lines())
            line = rng.randrange(line_count)
            column = rng.randrange(width(model.lines()[line], encoding) + 1)
            for step in range(steps):
                fragment = rng.choice(FRAGMENTS)
                before = model.position(model.offset(line, column))
                model.apply((line, column), (line, column), fragment)
                after_offset = model.offset(line, column) + len(fragment)
                newpos = model.position(after_offset)
                c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2 + step}, "contentChanges": [
                    {"range": {"start": {"line": line, "character": column}, "end": {"line": line, "character": column}}, "text": fragment}]})
                line, column = newpos
                doc = Doc(model.text, encoding)
                pos = {"line": line, "character": column}
                tag = "%s step %d after %r" % (relative, step, fragment)
                trigger = {"triggerKind": 2, "triggerCharacter": fragment} if fragment in (".", "@", '"', "/", "<", ":") else {"triggerKind": 1}
                for method, params in (("textDocument/completion", {"textDocument": {"uri": uri}, "position": pos, "context": trigger}),
                                       ("textDocument/hover", {"textDocument": {"uri": uri}, "position": pos}),
                                       ("textDocument/signatureHelp", {"textDocument": {"uri": uri}, "position": pos}),
                                       ("textDocument/definition", {"textDocument": {"uri": uri}, "position": pos}),
                                       ("textDocument/documentSymbol", {"textDocument": {"uri": uri}}),
                                       ("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}),
                                       ("textDocument/foldingRange", {"textDocument": {"uri": uri}}),
                                       ("textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}})):
                    if method.endswith("formatting") and step % 5:
                        continue
                    requests += 1
                    r = c.request(method, params, timeout=20)
                    if r is None:
                        problems.append("%s: %s timed out or the server died" % (tag, method))
                        return problems, requests
                    if "error" in r:
                        problems.append("%s: %s -> error %r" % (tag, method, r["error"]))
                        continue
                    found = []
                    result = r.get("result")
                    if method.endswith("completion"):
                        for item in completion_items(r)[:40]:
                            check_completion_item(item, doc, line, column, found)
                    elif method.endswith("hover") and result:
                        check_hover(result, doc, found)
                    elif method.endswith("documentSymbol"):
                        check_symbols(result or [], doc, found)
                    elif method.endswith("semanticTokens/full"):
                        check_semantic_tokens((result or {}).get("data", []), doc, legend, found)
                    elif method.endswith("formatting"):
                        check_edits(result or [], doc, found, "formatting")
                    problems.extend("%s: %s: %s" % (tag, method, f) for f in found)
                if c.proc.poll() is not None:
                    problems.append("%s: the server died (exit %r)" % (tag, c.proc.poll()))
                    return problems, requests
            c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        response, code = s.shutdown()
        if code != 0:
            problems.append("exit code %r after the typing session" % (code,))
    finally:
        s.close()
    return problems, requests


@scenario("typing-simulation-utf16")
def typing_utf16(report):
    run(report, "utf-16", 101)


@scenario("typing-simulation-utf8")
def typing_utf8(report):
    run(report, "utf-8", 202)


def run(report, encoding, seed):
    problems, requests = typing_session(report, encoding, seed, 45)
    import re
    unique = list(dict.fromkeys(re.sub(r"step \d+ after '[^']*'", "step N", re.sub(r"\d+", "N", p))[:230] for p in problems))
    for p in unique[:12]:
        report.check(False, p)
    report.note("%s: %d requests, %d problems (%d distinct)" % (encoding, requests, len(problems), len(unique)))
