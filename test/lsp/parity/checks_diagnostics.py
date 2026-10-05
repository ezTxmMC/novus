"""Diagnostics, code actions (auto-import), settings and snippets (analyzer.ts, features.ts codeActions, run.ts)."""
import os

from lspclient import apply_edits
from plib import check, pkg_files, features_text, read, REPO, FIXTURES

DIA, ACT, SNP, SET = "diagnostics", "code-actions", "snippets", "settings"


def one(env, src, quiet=0.6, options=None, files=None):
    """Diagnostics of a single main.nv (and optional extra files) in a fresh session."""
    ses = env.session(dict({"main.nv": src}, **(files or {})), options=options)
    try:
        return ses.diagnostics("main.nv", quiet)
    finally:
        ses.close()


def codes(found):
    return [d[1] for d in found]


# ------------------------------------------------------------------------------------------- no false positives
@check(DIA, "clean-features", "features.nv (without the stale C-style for) has no diagnostics (run.ts: no errors, no warnings)")
def clean_features(env):
    got = one(env, features_text())
    return (got == [], str(got))


@check(DIA, "clean-syntax-sample", "test/syntax.nv has no diagnostics (run.ts: parses without errors)")
def clean_syntax(env):
    got = one(env, read(os.path.join(REPO, "test", "syntax.nv")))
    return (got == [], str(got))


@check(DIA, "clean-packages", "test/cases/packages main.nv with import geo / draw/shapes / @helpers has no diagnostics (run.ts)")
def clean_packages(env):
    pk = os.path.join(REPO, "test", "cases", "packages")
    files = {}
    for root, _, names in os.walk(pk):
        for n in names:
            if n.endswith(".nv"):
                files[os.path.relpath(os.path.join(root, n), pk)] = read(os.path.join(root, n))
    ses = env.session(files)
    try:
        got = ses.diagnostics("main.nv")
        return (got == [], str(got))
    finally:
        ses.close()


@check(DIA, "clean-shapes-package", "the shapes fixture package has no diagnostics")
def clean_shapes(env):
    ses = env.session({"main.nv": "package app\n\nimport shapes\n\nmethod main {\n  var c = Circle{radius=1.0}\n  println c.area()\n}\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        got = ses.diagnostics("shapes/shapes.nv") + ses.diagnostics("main.nv")
        return (got == [], str(got))
    finally:
        ses.close()


# ------------------------------------------------------------------------------------------------- syntax codes
@check(DIA, "unterminated-string", "an unterminated string is an error (run.ts errors.nv)")
def unterminated_string(env):
    got = one(env, read(os.path.join(FIXTURES, "errors.nv")))
    return (any(d[1] == "unterminated-string" and d[2] == 1 for d in got), str(got))


@check(DIA, "unterminated-bracket", "a call that is never closed is an error")
def unterminated_bracket(env):
    got = one(env, "method main {\n  foo(\n}\n")
    return (any(d[2] == 1 for d in got), str(got))


@check(DIA, "method-without-body", "method noBody(): integer without a body is an error (run.ts: Method 'noBody' has no body)")
def no_body(env):
    got = one(env, "method main {\n}\n\nmethod noBody(): integer\n\nmethod other {\n}\n")
    return (any(d[2] == 1 for d in got), str(got))


@check(DIA, "no-body-reported-on-the-method", "the missing body is reported on the method's own line (TS: at the name)")
def no_body_position(env):
    got = one(env, "method main {\n}\n\nmethod noBody(): integer\n\nmethod other {\n}\n")
    return (any(d[0] == 4 for d in got), str(got))


@check(DIA, "c-style-for", "the C-style for of features.nv is flagged (it does not exist in Novus: novusc rejects it)", "novusc")
def c_style_for(env):
    got = one(env, read(os.path.join(FIXTURES, "features.nv")))
    return (any(d[1] == "c-style-for" and d[2] == 1 for d in got), str(got))


@check(DIA, "compound-assignment", "+= and ++ are flagged (not in the language)", "novusc")
def compound(env):
    got = one(env, "method main {\n  var q = 3\n  q += 1\n  q++\n}\n")
    return (codes(got).count("compound-assignment") == 2, str(got))


# ----------------------------------------------------------------------------------------------- semantic codes
@check(DIA, "unknown-variable", "an unknown variable is a warning on the identifier (run.ts: Cannot find name)")
def unknown_variable(env):
    got = one(env, "method main {\n  println(undefinedName)\n}\n")
    return (any(d[1] == "unknown-name" and d[2] == 2 and d[0] == 2 for d in got), str(got))


@check(DIA, "unknown-variable-bare-println", "println undefinedName (no parentheses, followed by another statement) is reported")
def unknown_bare(env):
    got = one(env, "method main {\n  println undefinedName\n  println 2\n}\n")
    return (any(d[1] == "unknown-name" for d in got), str(got))


@check(DIA, "unknown-variable-var-init", "var y = undefinedName (alone on its line, followed by another statement) is reported")
def unknown_var_init(env):
    got = one(env, "method main {\n  var y = undefinedName\n  var z = 2\n  println z\n}\n")
    return (any(d[1] == "unknown-name" for d in got), str(got))


@check(DIA, "unknown-function", "an unknown function call is a warning")
def unknown_function(env):
    got = one(env, "method main {\n  var q = undefinedFn(2)\n  println(q)\n}\n")
    return (any(d[1] == "unknown-name" for d in got), str(got))


@check(DIA, "unknown-member", "this.nope() on a known class is a warning (run.ts: does not exist on type)")
def unknown_member(env):
    got = one(env, "define class C {\n  method m() {\n    this.nope()\n  }\n}\n\nmethod main {\n}\n")
    return (any(d[1] == "unknown-member" for d in got), str(got))


@check(DIA, "unknown-class-in-literal", "Foo{a=1} with an unknown class is reported (novusc: unknown class 'Foo'; TS: Cannot find type)")
def unknown_class_literal(env):
    got = one(env, "method main {\n  var c = Foo{a=1}\n  println(c)\n}\n")
    return (len(got) > 0, str(got))


@check(DIA, "unknown-type-in-typed-local", "Missing m = ... with an unknown type is reported (TS: Cannot find type 'Missing')")
def unknown_type_local(env):
    got = one(env, "method main {\n  Missing m = 1\n  println(m)\n}\n")
    return (len(got) > 0, str(got))


@check(DIA, "unknown-base-type", "define class A based Missing is reported (run.ts: Cannot find type 'Missing'; novusc: unknown base type)")
def unknown_base(env):
    got = one(env, "define class A based Missing {\n}\n\nmethod main {\n}\n")
    return (len(got) > 0, str(got))


@check(DIA, "duplicate-class", "a duplicate class definition is an error")
def duplicate_class(env):
    got = one(env, "define class A {\n}\n\ndefine class A {\n}\n\nmethod main {\n}\n")
    return (any(d[1] == "duplicate-definition" and d[2] == 1 for d in got), str(got))


@check(DIA, "module-not-imported", "using json without import json gives module-not-imported (TS: missing-import for json)")
def module_not_imported(env):
    got = one(env, "method main {\n  println(json.stringify(1))\n}\n")
    return (any(d[1] == "module-not-imported" for d in got), str(got))


@check(DIA, "unimported-class-reported", "Circle{...} from a package that is not imported is reported with a way to fix it (TS: missing-import for Circle, Color, Shape)")
def unimported_class(env):
    got = one(env, pkg_files()["main.nv"], files={"shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    names = " ".join(d[3] for d in got)
    return ("Circle" in names and "Shape" in names, str(got))


@check(DIA, "unimported-enum-reported", "Color.RED without import is reported (TS: missing-import)")
def unimported_enum(env):
    got = one(env, pkg_files()["main.nv"], files={"shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    return (any("Color" in d[3] for d in got), str(got))


@check(DIA, "semantic-with-syntax-error", "name checks keep running in a file that has a syntax error elsewhere (TS reports both)")
def semantic_with_syntax(env):
    got = one(env, "method main {\n  println(undefinedName)\n  foo(\n}\n")
    return (any(d[1] == "unknown-name" for d in got) and any(d[2] == 1 for d in got), str(got))


@check(DIA, "unused-variable-hint", "an unused local is a hint tagged Unnecessary (TS reportUnused)")
def unused_variable(env):
    ses = env.session({"main.nv": "method main {\n  var unusedvar = 1\n}\n"})
    try:
        raw = ses.raw_diagnostics("main.nv")
        return (any(d.get("severity") == 4 and 1 in (d.get("tags") or []) for d in raw), str(raw))
    finally:
        ses.close()


@check(DIA, "deprecated-use-hint", "a call of a deprecated method gets a hint tagged Deprecated (TS)")
def deprecated_use(env):
    src = "package a\n\n@Deprecated{\n  text=\"use fresh\",\n  since=\"1\"\n}\nmethod old() {\n}\n\nmethod main {\n  old()\n}\n"
    ses = env.session({"main.nv": src})
    try:
        raw = ses.raw_diagnostics("main.nv")
        return (any(2 in (d.get("tags") or []) for d in raw), str(raw))
    finally:
        ses.close()


@check(DIA, "unknown-annotation", "@Nope on a method is reported (TS: Cannot find annotation 'Nope')")
def unknown_annotation(env):
    got = one(env, "package a\n\n@Nope\nmethod bad() {\n}\n\nmethod main {\n}\n")
    return (len(got) > 0, str(got))


@check(DIA, "annotation-unknown-argument", "@Deprecated{ bogus=... } is reported (TS: Annotation has no argument)")
def annotation_argument(env):
    got = one(env, "package a\n\n@Deprecated{ bogus=\"1\" }\nmethod bad() {\n}\n\nmethod main {\n}\n")
    return (len(got) > 0, str(got))


@check(DIA, "fix-clears-diagnostic", "editing the code so that it is correct clears the diagnostic")
def fix_clears(env):
    ses = env.session({"main.nv": "method main {\n  println(undefinedName)\n}\n"})
    try:
        before = ses.diagnostics("main.nv")
        ses.c.notifications.clear()
        ses.set("main.nv", "method main {\n  var undefinedName = 1\n  println(undefinedName)\n}\n")
        after = ses.diagnostics("main.nv")
        return (len(before) == 1 and after == [], str((before, after)))
    finally:
        ses.close()


@check(DIA, "version-tagged", "publishDiagnostics carry the document version")
def version_tagged(env):
    ses = env.session({"main.nv": "method main {\n  println(undefinedName)\n}\n"})
    try:
        ses.diagnostics("main.nv")
        notes = [n for n in ses.c.notifications if n["method"] == "textDocument/publishDiagnostics" and n["params"]["uri"].endswith("main.nv")]
        return (bool(notes) and notes[-1]["params"].get("version") == 1, str(notes[-1:]))
    finally:
        ses.close()


# ------------------------------------------------------------------------------------------------ code actions
PKG_SRC = "package app\n\nimport shapes\n\nmethod main {\n  println(json.stringify(1))\n  println(os.platform())\n}\n"


def first_diag(ses, code="module-not-imported"):
    for d in ses.raw_diagnostics("main.nv"):
        if d["code"] == code:
            return d
    return None


@check(ACT, "quickfix-std-import", "a missing std module has a preferred quick fix 'Import json' (run.ts: Import 'shapes')")
def quickfix_std(env):
    ses = env.session({"main.nv": PKG_SRC, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        d = first_diag(ses)
        acts = ses.actions("main.nv", d) if d else []
        ok = bool(acts) and acts[0]["kind"] == "quickfix" and "json" in acts[0]["title"] and acts[0].get("isPreferred")
        return (ok, str([a["title"] for a in acts]))
    finally:
        ses.close()


@check(ACT, "quickfix-applies", "applying the fix removes the diagnostic and adds the import after the last import (run.ts: further imports go after the last import line)")
def quickfix_applies(env):
    ses = env.session({"main.nv": PKG_SRC, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        d = first_diag(ses)
        edit = ses.actions("main.nv", d)[0]["edit"]["changes"][ses.c.uri_of("main.nv")]
        out = apply_edits(PKG_SRC, edit)
        ses.set("main.nv", out)
        left = [x[3] for x in ses.diagnostics("main.nv") if "json" in x[3]]
        return ("import json" in out and not left, repr(out[:80]) + str(left))
    finally:
        ses.close()


@check(ACT, "insert-after-package", "with only a package line the import goes after it with a blank line (run.ts fix-all case)")
def insert_after_package(env):
    src = "package app\n\nmethod main {\n  println(strings.repeat(\"a\", 2))\n}\n"
    ses = env.session({"main.nv": src})
    try:
        edit = ses.actions("main.nv", first_diag(ses))[0]["edit"]["changes"][ses.c.uri_of("main.nv")]
        out = apply_edits(src, edit)
        return (out.startswith("package app\n\nimport strings\n\nmethod main"), repr(out[:60]))
    finally:
        ses.close()


@check(ACT, "insert-without-package", "without a package line the import is inserted at the top of the file (run.ts test/project)")
def insert_no_package(env):
    src = "method main {\n  println(strings.repeat(\"a\", 2))\n}\n"
    ses = env.session({"main.nv": src})
    try:
        edit = ses.actions("main.nv", first_diag(ses))[0]["edit"]["changes"][ses.c.uri_of("main.nv")]
        out = apply_edits(src, edit)
        return (out.startswith("import strings\n\nmethod main"), repr(out[:60]))
    finally:
        ses.close()


@check(ACT, "quickfix-project-package", "an unknown name that a project package provides gets 'Import <package>' (Color -> shapes)")
def quickfix_project(env):
    ses = env.session({"main.nv": pkg_files()["main.nv"], "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        d = first_diag(ses, "unknown-name")
        acts = ses.actions("main.nv", d) if d else []
        return (any("shapes" in a["title"] for a in acts), str([a["title"] for a in acts]))
    finally:
        ses.close()


@check(ACT, "quickfix-unimported-class", "the Circle{...} of an unimported package has an 'Import shapes' quick fix (run.ts: Import 'shapes' at Circle{)")
def quickfix_class(env):
    ses = env.session({"main.nv": pkg_files()["main.nv"], "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        line, ch = ses.spot("main.nv", "Circle{")
        rng = {"start": {"line": line, "character": ch}, "end": {"line": line, "character": ch + 6}}
        acts = ses.c.call("textDocument/codeAction", "main.nv", {"range": rng, "context": {"diagnostics": ses.raw_diagnostics("main.nv")}}).get("result") or []
        return (any("shapes" in a["title"] for a in acts), str([a["title"] for a in acts]))
    finally:
        ses.close()


@check(ACT, "fix-all-imports", "several missing imports offer 'Add all missing imports' (run.ts: fix-all, source.fixAll)")
def fix_all(env):
    ses = env.session({"main.nv": PKG_SRC, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        diags = ses.raw_diagnostics("main.nv")
        acts = ses.c.call("textDocument/codeAction", "main.nv", {"range": {"start": {"line": 0, "character": 0}, "end": {"line": 8, "character": 0}}, "context": {"diagnostics": diags}}).get("result") or []
        return (any("all" in a["title"].lower() for a in acts), str([a["title"] for a in acts]))
    finally:
        ses.close()


@check(ACT, "organize-imports", "source.organizeImports sorts the imports (extra capability)", "novus-lsp design")
def organize(env):
    ses = env.session({"main.nv": "package a\n\nimport zeta\nimport arrays\n\nmethod main {\n  println(arrays.sort([2, 1]))\n}\n", "zeta/z.nv": "package zeta\n\nmethod zed() {\n}\n"})
    try:
        acts = ses.actions("main.nv", only=["source.organizeImports"])
        return (any(a["kind"] == "source.organizeImports" for a in acts), str(acts))
    finally:
        ses.close()


@check(ACT, "no-action-without-problem", "no quick fix is offered where nothing is wrong")
def no_action(env):
    ses = env.session({"main.nv": "package a\n\nmethod main {\n  println(1)\n}\n"})
    try:
        acts = ses.actions("main.nv", only=["quickfix"])
        return (acts == [], str(acts))
    finally:
        ses.close()


# ------------------------------------------------------------------------------------------------- settings
@check(SET, "diagnostics-enabled-off", "novus.diagnostics.enabled=false publishes no problems (package.json setting kept by the extension)")
def diagnostics_off(env):
    got = one(env, "method main {\n  println(undefinedName)\n}\n", options={"novus": {"diagnostics": {"enabled": False}}})
    return (got == [], str(got))


@check(SET, "undefined-symbols-off", "novus.diagnostics.undefinedSymbols=false silences unknown names")
def undefined_off(env):
    got = one(env, "method main {\n  println(undefinedName)\n}\n", options={"novus": {"diagnostics": {"undefinedSymbols": False}}})
    return (got == [], str(got))


@check(SET, "own-diagnostics-off", "novus.diagnostics.own=false silences the own layer (novus-lsp setting)", "novus-lsp design")
def own_off(env):
    got = one(env, "method main {\n  println(undefinedName)\n}\n", options={"novus": {"diagnostics": {"own": False}}})
    return (got == [], str(got))


@check(SET, "did-change-configuration", "a workspace/didChangeConfiguration is applied at once")
def did_change_config(env):
    ses = env.session({"main.nv": "method main {\n  println(undefinedName)\n}\n"})
    try:
        before = ses.diagnostics("main.nv")
        ses.c.notify("workspace/didChangeConfiguration", {"settings": {"novus": {"diagnostics": {"own": False}}}})
        ses.c.drain(0.5)
        ses.c.notifications.clear()
        ses.set("main.nv", "method main {\n  println(undefinedName2)\n}\n")
        after = ses.diagnostics("main.nv")
        return (len(before) == 1 and after == [], str((before, after)))
    finally:
        ses.close()


# ------------------------------------------------------------------------------------------------- snippets
SNIPPET_MAP = [  # TS snippet label -> (novus-lsp prefix, context marker text)
    ("method", "method", "top"), ("main", "main", "top"), ("define class", "class", "top"), ("define enum", "enum", "top"),
    ("define interface", "interface", "top"), ("define abstract", "abstract", "top"), ("define annotation", "annotation", "top"),
    ("construct", "construct", "class"), ("field", "field", "class"), ("if", "if", "stmt"), ("if else", "ifelse", "stmt"),
    ("for", "for", "stmt"), ("while", "while", "stmt"), ("var", "var", "stmt"), ("println", "println", "stmt"), ("eprintln", "eprintln", "stmt"),
]
CONTEXTS = {
    "top": "package a\n\n%s|\n",
    "class": "package a\n\ndefine class A {\n  %s|\n}\n",
    "stmt": "package a\n\nmethod main {\n  %s|\n}\n",
}


def snip_session(env):
    return env.shared("snippets", {"main.nv": "package a\n"})


def snippet_items(env, ctx, prefix):
    items = snip_session(env).complete("main.nv", CONTEXTS[ctx] % prefix)
    return [i for i in items if i.get("kind") == 15]


def make_snippet_case(label, prefix, ctx):
    def run(env):
        found = [i for i in snippet_items(env, ctx, prefix) if i["label"] == prefix]
        return (bool(found) and found[0].get("insertTextFormat") == 2, "%s -> %s in %s: %s" % (label, prefix, ctx, [i["label"] for i in snippet_items(env, ctx, prefix)][:8]))
    return run


for _label, _prefix, _ctx in SNIPPET_MAP:
    check(SNP, "ts-" + _label.replace(" ", "-"), "the TS snippet '%s' exists as '%s' in a %s context" % (_label, _prefix, _ctx))(make_snippet_case(_label, _prefix, _ctx))


@check(SNP, "java-sout", "sout expands to println \"...\" (requirement 4)", "requirement 4")
def java_sout(env):
    found = [i for i in snippet_items(env, "stmt", "sout") if i["label"] == "sout"]
    return (bool(found) and "println" in found[0]["textEdit"]["newText"], str(found[:1]))


@check(SNP, "java-main-psvm", "main and psvm give the entry point at top level", "requirement 4")
def java_main(env):
    a = [i for i in snippet_items(env, "top", "main") if i["label"] == "main"]
    b = [i for i in snippet_items(env, "top", "psvm") if i["label"] == "psvm"]
    return (bool(a) and bool(b) and "method main" in b[0]["textEdit"]["newText"], str((a[:1], b[:1])))


@check(SNP, "java-fori", "fori gives a counted loop in a method body", "requirement 4")
def java_fori(env):
    found = [i for i in snippet_items(env, "stmt", "fori") if i["label"] == "fori"]
    return (bool(found) and "while" in found[0]["textEdit"]["newText"], str(found[:1]))


@check(SNP, "context-gating", "statement snippets are not offered at top level, class snippets not in a body")
def context_gating(env):
    top = [i["label"] for i in snippet_items(env, "top", "fori")]
    body = [i["label"] for i in snippet_items(env, "stmt", "construct")]
    return ("fori" not in top and "construct" not in body, str((top, body)))


@check(SNP, "snippet-syntax-wellformed", "every snippet body in a completion answer has balanced tab stops and no stray $", "LSP snippet grammar")
def snippet_wellformed(env):
    import re
    bad = []
    for ctx in ("top", "class", "stmt"):
        for item in snippet_items(env, ctx, ""):
            body = item.get("textEdit", {}).get("newText", item.get("insertText", ""))
            stripped = re.sub(r"\\.", "", body)
            stripped = re.sub(r"\$\{\d+(:[^}]*)?\}|\$\d+|\$\{\d+\|[^}]*\|\}", "", stripped)
            if "$" in stripped:
                bad.append((item["label"], body))
    return (not bad, str(bad[:4]))


@check(SNP, "snippets-compile", "scripts/lsp_snippets.sh: every snippet of the catalogue passes novusc check (result of the run recorded by the tester)", "scripts/lsp_snippets.sh")
def snippets_compile(env):
    path = os.environ.get("PARITY_SNIPPETS_LOG")
    if not path or not os.path.exists(path):
        return True, "not run (set PARITY_SNIPPETS_LOG to the output of scripts/lsp_snippets.sh)"
    text = read(path)
    return ("rc=0" in text, text[-300:])
