"""Formatting parity: the cases of the formatter section of vscode-novus/src/test/run.ts."""
import os
import re

from lspclient import apply_edits
from plib import check, read, REPO

FMT = "format"

MESSY = 'package  x\nimport  json\n\n\n\nmethod   main{\n var x=Person{ name = "a" ,age=1 }\n if(x>1){println x}else{\nprintln  -1\n}\n\n}\nmethod b( integer a,integer b ):integer{\nreturn a+b\n}\n'
MESSY_EXPECTED = 'package x\nimport json\n\nmethod main {\n  var x = Person{name="a", age=1}\n  if (x > 1) { println x } else {\n    println -1\n  }\n}\nmethod b(integer a, integer b): integer {\n  return a + b\n}\n'

CASES = [
    ("based-list", 'define class A based B,C{\nprivate final array<Item>items:get,set\nabstract method buy():bool\n}\n', 'define class A based B, C {\n  private final array<Item> items: get, set\n  abstract method buy(): bool\n}\n'),
    ("call-with-map", 'method m {\nhttp.post("u",{\n"items":x,\n"sum":s\n})\n}\n', 'method m {\n  http.post("u", {\n    "items": x,\n    "sum": s\n  })\n}\n'),
    ("annotation-args", '@Deprecated{\ntext="a",\nsince="1"\n}\nmethod m{\n}\n', '@Deprecated{\n  text="a",\n  since="1"\n}\nmethod m {\n}\n'),
    ("literal-for-while", 'method m {\nvar p=Person{\nname="T",\nfriends=[]\n}\nfor(item in items){\nsum+item.price()\n}\nwhile(n>0){n=n-1}\n}\n', 'method m {\n  var p = Person{\n    name="T",\n    friends=[]\n  }\n  for (item in items) {\n    sum + item.price()\n  }\n  while (n > 0) { n = n - 1 }\n}\n'),
    ("operators-and-chain", 'method m {\nvar ok=a<b&&!done\nvar y=-x*(1+2)\nthis.text=text;\ntom.friends()\n.append(a,b)\n}\n', 'method m {\n  var ok = a < b && !done\n  var y = -x * (1 + 2)\n  this.text = text;\n  tom.friends()\n    .append(a, b)\n}\n'),
    ("comments", 'method m { // trailing\n  /* block */ println "x"   // note\n  /**\n   * doc\n   */\n  var z = 1\n}\n', 'method m { // trailing\n  /* block */ println "x" // note\n  /**\n   * doc\n   */\n  var z = 1\n}\n'),
    ("enum", 'define enum G{\nA("a"),\nB("b");\nprivate final str text:get\n}\n', 'define enum G {\n  A("a"),\n  B("b");\n  private final str text: get\n}\n'),
    ("interpolation-untouched", 'method main {\n  println "Starting ${NAME} with args: ${args}"\n  Key key = Key{field="value"}\n}\n', 'method main {\n  println "Starting ${NAME} with args: ${args}"\n  Key key = Key{field="value"}\n}\n'),
    ("unterminated-string-untouched", 'method m {\n  var s = "unterminated\n  println s\n}\n', 'method m {\n  var s = "unterminated\n  println s\n}\n'),
    ("else-on-own-line", 'method m {\n  if (a) {\n    x = 1\n  }\n  else {\n    x = 2\n  }\n}\n', 'method m {\n  if (a) {\n    x = 1\n  }\n  else {\n    x = 2\n  }\n}\n'),
]

TWO = {"tabSize": 2, "insertSpaces": True}


def session(env):
    return env.shared("format", {"main.nv": "package x\n"})


def fmt(env, text, options=TWO):
    return session(env).format("main.nv", text, options)


def make_case(cid, src, want):
    def run(env):
        out = fmt(env, src)
        if out != want:
            return False, "got %r want %r" % (out, want)
        again = fmt(env, out)
        return (again == out, "not idempotent: %r" % again)
    return run


for cid, src, want in CASES:
    check(FMT, "case-" + cid, "format %s (run.ts) and idempotent" % cid)(make_case(cid, src, want))


@check(FMT, "messy-normalised", "a messy input is normalised (run.ts MESSY)")
def messy(env):
    out = fmt(env, MESSY)
    return (out == MESSY_EXPECTED, repr(out))


@check(FMT, "tabs-honoured", "insertSpaces=false indents with tabs (run.ts)")
def tabs(env):
    out = fmt(env, "method m {\nprintln 1\n}\n", {"tabSize": 4, "insertSpaces": False})
    return (out == "method m {\n\tprintln 1\n}\n", repr(out))


@check(FMT, "tabsize-four", "tabSize 4 indents with four spaces")
def tabsize(env):
    out = fmt(env, "method m {\nprintln 1\n}\n", {"tabSize": 4, "insertSpaces": True})
    return (out == "method m {\n    println 1\n}\n", repr(out))


@check(FMT, "named-argument-spacing-setting", "the setting novus.format.namedArgumentSpacing=spaces gives Key{a = 1} (run.ts)")
def named_spacing(env):
    ses = env.session({"main.nv": "package x\n"}, options={"novus": {"format": {"namedArgumentSpacing": "spaces"}}})
    try:
        out = ses.format("main.nv", "var x = K{a=1}\n")
        return (out == "var x = K{a = 1}\n", repr(out))
    finally:
        ses.close()


@check(FMT, "max-blank-lines-setting", "novus.format.maxBlankLines limits consecutive blank lines (run.ts: 3 blank lines -> 1)")
def max_blank(env):
    out = fmt(env, "method a {\n}\n\n\n\nmethod b {\n}\n")
    return (out == "method a {\n}\n\nmethod b {\n}\n", repr(out))


@check(FMT, "minimal-edit", "a full format of an almost formatted file is a few small edits, not a whole-document replace (run.ts)")
def minimal_edit(env):
    src = "method main {\nprintln 1\n}\n\n\n\nmethod b {\n  println 2\n}\n"
    ses = session(env)
    ses.set("main.nv", src)
    edits = ses.c.call("textDocument/formatting", "main.nv", {"options": TWO}).get("result")
    out = apply_edits(src, edits)
    total = sum(e["range"]["end"]["line"] - e["range"]["start"]["line"] + 1 for e in edits)
    return (out == "method main {\n  println 1\n}\n\nmethod b {\n  println 2\n}\n" and total <= 4, str(edits))


@check(FMT, "range-formatting", "range formatting only touches the selected line (run.ts)")
def range_format(env):
    src = "method main {\nprintln 1\n}\n\n\n\nmethod b {\n  println 2\n}\n"
    ses = session(env)
    ses.set("main.nv", src)
    edits = ses.c.call("textDocument/rangeFormatting", "main.nv", {"range": {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 0}}, "options": TWO}).get("result")
    ok = bool(edits) and len(edits) == 1 and edits[0]["range"]["start"]["line"] == 1 and edits[0]["newText"].strip() == ""
    return (ok and apply_edits(src, edits).split("\n")[1] == "  println 1", str(edits))


def tokens_of(text):
    return re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", text))


def sample_case(rel, unit):
    def run(env):
        text = read(os.path.join(REPO, rel))
        opts = {"tabSize": unit, "insertSpaces": True}
        out = fmt(env, text, opts)
        if out is None:
            return False, "no answer"
        orig = [l for l in text.split("\n") if l.strip()]
        now = [l for l in out.split("\n") if l.strip()]
        changed = sum(1 for a, b in zip(orig, now) if a != b) + abs(len(orig) - len(now))
        idem = fmt(env, out, opts) == out
        same_tokens = tokens_of(out) == tokens_of(text)
        return (idem and same_tokens and changed <= 3, "idempotent=%s tokens-kept=%s changed-lines=%d" % (idem, same_tokens, changed))
    return run


for _rel, _unit in [("test/syntax.nv", 2), ("compiler/driver/cli.nv", 4), ("std/json.nv", 4), ("lsp/features/hover/hover.nv", 4)]:
    check(FMT, "sample-" + os.path.basename(_rel), "%s: idempotent, only whitespace changes, already canonical (run.ts)" % _rel, "TS run.ts")(sample_case(_rel, _unit))
