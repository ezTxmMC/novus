"""documentSymbol, workspace/symbol, folding and semantic tokens (features.ts, run.ts)."""
import os

from plib import check, pkg_files, features_text, read, REPO, FIXTURES

SYM, WSY, FOLD, TOK = "outline", "workspace-symbol", "folding", "semantic-tokens"

LEGEND_TYPES_TS = ["namespace", "type", "class", "enum", "interface", "parameter", "variable", "property", "enumMember", "function", "method", "decorator", "keyword"]
LEGEND_MODS_TS = ["declaration", "readonly", "abstract", "deprecated", "defaultLibrary", "modification"]

KINDS = {"File": 1, "Module": 2, "Package": 4, "Class": 5, "Method": 6, "Field": 8, "Constructor": 9, "Enum": 10, "Interface": 11, "Function": 12, "Variable": 13, "Constant": 14, "EnumMember": 22, "Property": 7}


def feat(env):
    return env.shared("features-struct", {"main.nv": features_text()})


def find(symbols, name, kind=None):
    for s in symbols or []:
        if s["name"] == name and (kind is None or s["kind"] == kind):
            return s
    return None


# ------------------------------------------------------------------------------------------------ outline
@check(SYM, "hierarchy-class-members", "a class lists its fields, constructor and methods as children")
def outline_children(env):
    syms = feat(env).symbols("main.nv")
    counter = find(syms, "Counter")
    names = [c["name"] for c in (counter or {}).get("children", [])]
    return (counter is not None and "value" in names and "construct" in names and "describe" in names and names.count("increment") == 2, str(names))


@check(SYM, "enum-constants", "an enum lists its constants as children (run.ts: Gender.MALE)")
def outline_enum(env):
    syms = feat(env).symbols("main.nv")
    color = find(syms, "Color")
    names = [(c["name"], c["kind"]) for c in (color or {}).get("children", [])]
    return (("RED", KINDS["EnumMember"]) in names and ("GREEN", KINDS["EnumMember"]) in names, str(names))


@check(SYM, "kinds", "kinds: function, class, interface, enum, constant")
def outline_kinds(env):
    syms = feat(env).symbols("main.nv")
    ok = (find(syms, "main", KINDS["Function"]) and find(syms, "Counter", KINDS["Class"]) and find(syms, "ICounter", KINDS["Interface"])
          and find(syms, "Color", KINDS["Enum"]) and find(syms, "GREETING", KINDS["Constant"]))
    return (bool(ok), str([(s["name"], s["kind"]) for s in syms]))


@check(SYM, "detail-signature", "methods carry their signature as detail, e.g. (integer a, integer b): integer")
def outline_detail(env):
    syms = feat(env).symbols("main.nv")
    add = find(syms, "add")
    return (add is not None and add.get("detail") == "(integer a, integer b): integer", str(add and add.get("detail")))


@check(SYM, "overloads-listed", "both main overloads of syntax.nv are listed (run.ts)")
def outline_overloads(env):
    ses = env.shared("syntax", {"main.nv": read(os.path.join(REPO, "test", "syntax.nv"))})
    syms = ses.symbols("main.nv")
    return (len([s for s in syms if s["name"] == "main"]) == 2, str([s["name"] for s in syms]))


@check(SYM, "selection-range", "selectionRange is the name, range spans the declaration")
def outline_ranges(env):
    syms = feat(env).symbols("main.nv")
    main = find(syms, "main")
    rng, sel = main["range"], main["selectionRange"]
    return (rng["end"]["line"] > sel["start"]["line"] and sel["start"]["line"] == sel["end"]["line"], str(main))


@check(SYM, "recovery-in-broken-file", "symbols are still produced for a file with syntax errors (run.ts errors.nv: class A declared)")
def outline_recovery(env):
    ses = env.session({"main.nv": read(os.path.join(FIXTURES, "errors.nv"))})
    try:
        syms = ses.symbols("main.nv")
        return (find(syms, "A", KINDS["Class"]) is not None and find(syms, "main") is not None, str([s["name"] for s in syms or []]))
    finally:
        ses.close()


@check(SYM, "deprecated-tag", "a deprecated method has the Deprecated symbol tag")
def outline_deprecated(env):
    ses = env.session({"main.nv": "package a\n\n@Deprecated{\n  text=\"x\",\n  since=\"1\"\n}\nmethod old() {\n}\n"})
    try:
        old = find(ses.symbols("main.nv"), "old")
        return (old is not None and 1 in (old.get("tags") or []), str(old))
    finally:
        ses.close()


# ------------------------------------------------------------------------------------- workspace symbols
def wsym(env):
    files = {"main.nv": "package app\n\nmethod main {\n}\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"], "extra.nv": "package app\n\nmethod circleHelper() {\n}\n"}
    return env.shared("wsym", files)


def query(ses, text, wait=3.0):
    """The answer of workspace/symbol; a first answer right after didOpen may be empty while the index builds, so
    an empty answer is asked again for up to `wait` seconds."""
    import time
    deadline = time.time() + wait
    while True:
        res = ses.c.request("workspace/symbol", {"query": text}).get("result") or []
        if res or time.time() > deadline:
            break
        time.sleep(0.05)
    return [(s["name"], s.get("containerName"), s["kind"]) for s in res]


@check(WSY, "substring-case-insensitive", "query 'circ' finds Circle and circleHelper (case-insensitive substring)")
def wsym_substring(env):
    got = [n for n, _, _ in query(wsym(env), "circ")]
    return ("Circle" in got and "circleHelper" in got, str(got))


@check(WSY, "container-name", "members report their container (area in Circle)")
def wsym_container(env):
    got = query(wsym(env), "area")
    return (("area", "Circle", KINDS["Method"]) in got, str(got))


@check(WSY, "unopened-files", "symbols of files that were never opened are found (TS indexes every .nv)")
def wsym_unopened(env):
    got = [n for n, _, _ in query(wsym(env), "Shape")]
    return ("Shape" in got, str(got))


@check(WSY, "empty-query-lists-all", "an empty query lists the project's symbols")
def wsym_empty(env):
    got = [n for n, _, _ in query(wsym(env), "")]
    return ("Circle" in got and "Color" in got and "area" in got, str(got[:20]))


@check(WSY, "no-open-document", "a freshly started server with no open document still answers workspace/symbol (TS scans the folders at start)")
def wsym_no_open(env):
    files = {"main.nv": "package app\n\nmethod main {\n}\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]}
    ses = env.session(files, open_all=False)
    try:
        got = query(ses, "Circle", wait=2.0)
        return (any(n == "Circle" for n, _, _ in got), str(got))
    finally:
        ses.close()


@check(WSY, "no-open-document-with-manifest", "the same with a project.nv: the folder is scanned at start")
def wsym_no_open_manifest(env):
    files = {"project.nv": "project \"x\"\nmain \"main.nv\"\n", "main.nv": "package app\n\nmethod main {\n}\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]}
    ses = env.session(files, open_all=False)
    try:
        got = query(ses, "Circle", wait=2.0)
        return (any(n == "Circle" for n, _, _ in got), str(got))
    finally:
        ses.close()


@check(WSY, "kinds", "kinds: class, enum, interface")
def wsym_kinds(env):
    got = query(wsym(env), "")
    return (("Circle", "shapes", KINDS["Class"]) in got and ("Color", "shapes", KINDS["Enum"]) in got and ("Shape", "shapes", KINDS["Interface"]) in got, str(got[:12]))


# ---------------------------------------------------------------------------------------------- folding
FOLD_SRC = """package f

import json
import os
import path

// line comment one
// line comment two
// line comment three

/* block
   comment */

@Deprecated{
  text="x",
  since="1"
}
method old {
  var arr = [
    1,
    2
  ]
  var m = {
    "a": 1,
    "b": 2
  }
  var o = Person{
    name="a",
    age=1
  }
  if (arr.length() > 1) {
    println 1
  } else {
    println 2
  }
}

define class Person {
  private final string name: get

  construct(string name) {
    this.name = name
  }
}
"""


def folds(env):
    ses = env.shared("fold", {"main.nv": FOLD_SRC})
    return ses.folds("main.nv")


def has_fold(got, start_text, kind=None):
    line = next(i for i, l in enumerate(FOLD_SRC.split("\n")) if l.startswith(start_text))
    return any(f[0] == line and (kind is None or f[2] == kind) for f in got)


@check(FOLD, "method-and-class-bodies", "method and class bodies fold")
def fold_bodies(env):
    got = folds(env)
    return (has_fold(got, "method old") and has_fold(got, "define class Person") and has_fold(got, "  construct"), str(got))


@check(FOLD, "blocks", "if/else blocks fold")
def fold_blocks(env):
    got = folds(env)
    return (has_fold(got, "  if (arr") and has_fold(got, "  } else {"), str(got))


@check(FOLD, "block-comment", "a block comment folds as a comment")
def fold_block_comment(env):
    return (has_fold(folds(env), "/* block", "comment"), str(folds(env)))


@check(FOLD, "line-comment-run", "consecutive // lines fold as one comment")
def fold_line_comments(env):
    return (has_fold(folds(env), "// line comment one", "comment"), str(folds(env)))


@check(FOLD, "imports", "the import block folds as imports")
def fold_imports(env):
    return (has_fold(folds(env), "import json", "imports"), str(folds(env)))


@check(FOLD, "object-literal", "a multi-line Person{...} literal folds")
def fold_object(env):
    return (has_fold(folds(env), "  var o = Person{"), str(folds(env)))


@check(FOLD, "map-literal", "a multi-line map literal folds")
def fold_map(env):
    return (has_fold(folds(env), "  var m = {"), str(folds(env)))


@check(FOLD, "array-literal", "a multi-line array literal folds")
def fold_array(env):
    return (has_fold(folds(env), "  var arr = ["), str(folds(env)))


@check(FOLD, "annotation-arguments", "a multi-line @Deprecated{...} folds")
def fold_annotation(env):
    return (has_fold(folds(env), "@Deprecated{"), str(folds(env)))


# ------------------------------------------------------------------------------------------ semantic tokens
def decode(ses, rel, text):
    legend = ses.c.init["result"]["capabilities"]["semanticTokensProvider"]["legend"]
    data = ses.c.call("textDocument/semanticTokens/full", rel).get("result", {}).get("data", [])
    lines = text.split("\n")
    line = ch = 0
    out = []
    for i in range(0, len(data), 5):
        dl, dc, ln, ty, mo = data[i:i + 5]
        line += dl
        ch = dc if dl else ch + dc
        mods = [legend["tokenModifiers"][b] for b in range(len(legend["tokenModifiers"])) if mo >> b & 1]
        out.append((line, lines[line][ch:ch + ln], legend["tokenTypes"][ty], mods))
    return out


def toks(env):
    return decode(feat(env), "main.nv", features_text())


def tok(tokens, text, line_text=None):
    lines = features_text().split("\n")
    for line, t, ty, mods in tokens:
        if t == text and (line_text is None or line_text in lines[line]):
            return ty, mods
    return None


@check(TOK, "legend-superset", "the legend contains every token type and modifier of the TS legend")
def tok_legend(env):
    legend = feat(env).c.init["result"]["capabilities"]["semanticTokensProvider"]["legend"]
    missing = [t for t in LEGEND_TYPES_TS if t not in legend["tokenTypes"]] + [m for m in LEGEND_MODS_TS if m not in legend["tokenModifiers"]]
    return (not missing, str(missing))


@check(TOK, "count", "more than 50 tokens for syntax.nv (run.ts)")
def tok_count(env):
    src = read(os.path.join(REPO, "test", "syntax.nv"))
    ses = env.shared("syntax", {"main.nv": src})
    return (len(decode(ses, "main.nv", src)) > 50, "")


@check(TOK, "declarations", "class, interface, enum, enum member, function, method, parameter, property and variable kinds")
def tok_kinds(env):
    t = toks(env)
    want = [("Counter", "class", "define class Counter"), ("ICounter", "interface", "define class Counter"), ("Color", "enum", "define enum Color"),
            ("RED", "enumMember", "RED(\"red\")"), ("main", "function", "method main"), ("describe", "method", "method describe"),
            ("by", "parameter", "integer by"), ("value", "property", "private final integer value"), ("total", "variable", "var total")]
    bad = []
    for text, ty, where in want:
        got = tok(t, text, where)
        if not got or got[0] != ty:
            bad.append((text, ty, got))
    return (not bad, str(bad))


@check(TOK, "decorator", "@Interface is a decorator token")
def tok_decorator(env):
    got = tok(toks(env), "Interface", "@Interface")
    return (got is not None and got[0] == "decorator", str(got))


@check(TOK, "keywords", "var, method, define, if, return are keyword tokens")
def tok_keywords(env):
    t = toks(env)
    bad = [w for w in ("var", "method", "define", "if", "return") if not (tok(t, w) and tok(t, w)[0] == "keyword")]
    return (not bad, str(bad))


@check(TOK, "modifier-declaration", "declaration sites carry the declaration modifier")
def tok_declaration(env):
    got = tok(toks(env), "total", "var total")
    return (got is not None and "declaration" in got[1], str(got))


@check(TOK, "modifier-readonly", "a final constant is readonly")
def tok_readonly(env):
    got = tok(toks(env), "GREETING", "private final GREETING")
    return (got is not None and "readonly" in got[1], str(got))


@check(TOK, "modifier-abstract", "an interface method is abstract")
def tok_abstract(env):
    got = tok(toks(env), "increment", "  increment()")
    return (got is not None and "abstract" in got[1], str(got))


@check(TOK, "modifier-default-library", "primitive types are defaultLibrary")
def tok_default_library(env):
    got = tok(toks(env), "integer", "integer value")
    return (got is not None and "defaultLibrary" in got[1], str(got))


@check(TOK, "modifier-modification", "an assignment target is marked modification")
def tok_modification(env):
    got = tok(toks(env), "value", "this.value = value")
    return (got is not None and "modification" in got[1], str(got))


@check(TOK, "interpolation", "names inside ${...} get tokens")
def tok_interpolation(env):
    t = toks(env)
    got = [x for x in t if x[1] == "ratio" and x[2] == "variable"]
    return (len(got) >= 2, str(got))


@check(TOK, "deprecated-modifier", "uses of a deprecated method carry the deprecated modifier")
def tok_deprecated(env):
    src = "package a\n\n@Deprecated{\n  text=\"x\",\n  since=\"1\"\n}\nmethod old() {\n}\n\nmethod main {\n  old()\n}\n"
    ses = env.session({"main.nv": src})
    try:
        t = decode(ses, "main.nv", src)
        got = [x for x in t if x[1] == "old"]
        return (all("deprecated" in x[3] for x in got) and len(got) == 2, str(got))
    finally:
        ses.close()


@check(TOK, "range-request", "semanticTokens/range answers (extra capability)", "novus-lsp design")
def tok_range(env):
    ses = feat(env)
    res = ses.c.call("textDocument/semanticTokens/range", "main.nv", {"range": {"start": {"line": 0, "character": 0}, "end": {"line": 20, "character": 0}}}).get("result")
    return (bool(res and res.get("data")), "")
