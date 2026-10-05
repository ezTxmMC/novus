"""Completion parity: every context of vscode-novus/src/server/completion.ts and its tests (run.ts)."""
from plib import check, has_all, pkg_files, features_text, read, REPO, FIXTURES
import os

CTX = "completion"
BODY = "package app\n\nimport shapes\n\nmethod main {\n  %s\n}\n"
LIB = "package app\n\nimport shapes\n\nGLOBALV = 1\n\nmethod add(integer a, integer b): integer {\n  return a + b\n}\n\ndefine class K {\n  private final integer fld: get\n  method m(integer p) {\n    var loc = 1\n    %s\n  }\n}\n"


def pkg_session(env):
    files = {"main.nv": "package app\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]}
    return env.shared("completion-pkg", files)


@check(CTX, "toplevel-keywords", "top level offers import/define/var/method/main/private/final (TS TOP_LEVEL_KEYWORDS)")
def toplevel_keywords(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\n|\n")
    return has_all(labels, ["import", "define", "var", "private", "final", "method", "main"])


@check(CTX, "toplevel-package-keyword", "top level of a file without package offers package")
def toplevel_package(env):
    labels = pkg_session(env).labels("main.nv", "|\n")
    return has_all(labels, ["package"])


@check(CTX, "class-body-keywords", "class body offers method/construct/abstract/private/final (TS CLASS_BODY_KEYWORDS)")
def class_body(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\ndefine class A {\n  |\n}\n")
    return has_all(labels, ["method", "construct", "abstract", "private", "final"])


@check(CTX, "class-body-after-modifier", "class body after a modifier still offers method/construct")
def class_body_mod(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\ndefine class A {\n  private |\n}\n")
    return has_all(labels, ["method"])


@check(CTX, "statement-keywords", "statement offers var/if/else/for/while/return/println/print/eprintln/break/continue/sync/await/thread/virtual")
def statement_keywords(env):
    labels = pkg_session(env).labels("main.nv", BODY % "|")
    return has_all(labels, ["var", "if", "else", "for", "while", "return", "println", "print", "eprintln", "break", "continue", "sync", "await", "thread", "virtual"])


@check(CTX, "statement-symbols", "statement offers locals, parameters, this, fields, methods, globals")
def statement_symbols(env):
    labels = pkg_session(env).labels("main.nv", LIB % "|")
    return has_all(labels, ["loc", "p", "this", "fld", "m", "add", "GLOBALV", "K"])


@check(CTX, "statement-builtins", "statement offers the built-in functions (TS builtinGlobals)")
def statement_builtins(env):
    labels = pkg_session(env).labels("main.nv", BODY % "|")
    return has_all(labels, ["readFile", "writeFile", "parseInt", "chr", "ord", "typeOf", "args", "fileExists"])


@check(CTX, "expression-literals", "expression offers true and false")
def expression_literals(env):
    labels = pkg_session(env).labels("main.nv", BODY % "var q = |")
    return has_all(labels, ["true", "false"])


@check(CTX, "expression-locals-after-eq", "after '=' locals and functions are offered, keywords are not")
def expression_locals(env):
    labels = pkg_session(env).labels("main.nv", LIB % "var q = |")
    ok, detail = has_all(labels, ["loc", "p", "add"])
    return ok and "while" not in labels, detail + " (while leaked: %s)" % ("while" in labels)


@check(CTX, "prefix-filter", "a typed prefix narrows the list (ab| lists abc)")
def prefix_filter(env):
    labels = pkg_session(env).labels("main.nv", BODY % "var abc = 1\n  ab|")
    return ("abc" in labels, str(labels[:10]))


@check(CTX, "member-object", "after tom. the fields and methods incl. getter accessors (run.ts: friends, name)")
def member_object(env):
    src = read(os.path.join(REPO, "test", "syntax.nv"))
    ses = env.shared("syntax", {"main.nv": src})
    marked = src.replace("  tom.friends().append(fabi, jason)", "  tom.|friends().append(fabi, jason)", 1)
    labels = ses.labels("main.nv", marked, ".")
    return has_all(labels, ["friends", "name", "age"])


@check(CTX, "member-typed-local", "after a typed local c. the class members of its type")
def member_typed_local(env):
    labels = pkg_session(env).labels("main.nv", BODY % "Circle c = Circle{radius=1.0}\n  c.|", ".")
    return has_all(labels, ["area", "radius"])


@check(CTX, "member-getter-and-overloads", "counter. lists increment (both overloads) and value (run.ts features.nv)")
def member_features(env):
    ses = env.shared("features", {"main.nv": features_text()})
    marked = features_text().replace("println counter.value()", "println counter.|value()", 1)
    items = ses.complete("main.nv", marked, ".")
    labels = [i["label"] for i in items]
    return (labels.count("increment") == 2 and "value" in labels, str(labels))


@check(CTX, "member-enum-constants", "Color. lists the enum constants")
def member_enum(env):
    labels = pkg_session(env).labels("main.nv", BODY % "var c = Color.|", ".")
    return has_all(labels, ["RED", "GREEN"])


@check(CTX, "member-std-module", "json. lists the functions of the std module")
def member_module(env):
    labels = pkg_session(env).labels("main.nv", BODY % "json.|", ".")
    return has_all(labels, ["parse", "stringify", "save"])


@check(CTX, "member-this", "this. lists fields and methods of the enclosing class")
def member_this(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\ndefine class A {\n  private final integer n: get\n  method m() {\n    this.|\n  }\n}\n", ".")
    return has_all(labels, ["n", "m"])


@check(CTX, "member-call-chain", "a call result is typed: c.child().| lists members of the returned class (TS memberItemsFromAst)")
def member_chain(env):
    src = "package app\n\ndefine class B {\n  private final integer deep: get\n}\n\ndefine class A {\n  method child(): B {\n    return B{deep=1}\n  }\n}\n\nmethod main {\n  var a = A{}\n  a.child().|\n}\n"
    return has_all(pkg_session(env).labels("main.nv", src, "."), ["deep"])


@check(CTX, "member-array-result", "tom.friends().| lists the array methods (syntax.nv)")
def member_array_result(env):
    src = read(os.path.join(REPO, "test", "syntax.nv"))
    ses = env.shared("syntax", {"main.nv": src})
    marked = src.replace("  tom.friends().append(fabi, jason)", "  tom.friends().|append(fabi, jason)", 1)
    return has_all(ses.labels("main.nv", marked, "."), ["append", "length"])


@check(CTX, "member-builtin-types", "string and array values offer their builtin methods", "novus-lsp design")
def member_builtin(env):
    ses = pkg_session(env)
    a = has_all(ses.labels("main.nv", BODY % "var s = \"x\"\n  s.|", "."), ["length", "substring", "split"])
    b = has_all(ses.labels("main.nv", BODY % "var a = [1]\n  a.|", "."), ["append", "length", "join"])
    return a[0] and b[0], a[1] + b[1]


@check(CTX, "annotation-names", "after @ builtin and user annotations (Deprecated, Abstract, Interface, Marker)")
def annotation_names(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\nimport shapes\n\n@|\nmethod x() {\n}\n", "@")
    return has_all(labels, ["Deprecated", "Abstract", "Interface", "Marker"])


@check(CTX, "annotation-builtin-args", "@Deprecated{ | } suggests text and since (run.ts)")
def annotation_builtin_args(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\n@Deprecated{ | }\nmethod old() {\n}\n")
    return has_all(labels, ["text", "since"])


@check(CTX, "annotation-user-args", "@Marker{ | } suggests the methods of a user annotation (run.ts: note)")
def annotation_user_args(env):
    labels = pkg_session(env).labels("main.nv" if False else "main.nv", "package app\n\nimport shapes\n\n@Marker{ | }\nmethod t() {\n}\n")
    return has_all(labels, ["note"])


@check(CTX, "objlit-fields", "Circle{ | } suggests the fields, inserted with a trailing =")
def objlit_fields(env):
    items = pkg_session(env).complete("main.nv", BODY % "var c = Circle{ | }")
    ok = [i["label"] for i in items] == ["radius"] and items[0].get("textEdit", {}).get("newText", items[0].get("insertText")) == "radius="
    return ok, str([(i["label"], i.get("textEdit")) for i in items])


@check(CTX, "objlit-multiline", "multi-line Circle{ <newline> | } suggests the fields")
def objlit_multi(env):
    return has_all(pkg_session(env).labels("main.nv", BODY % "var c = Circle{\n    |\n  }"), ["radius"])


@check(CTX, "objlit-no-repeat", "an already assigned field is not suggested again (run.ts)")
def objlit_no_repeat(env):
    src = "package app\n\nimport shapes\n\ndefine class Pair {\n  private final integer left: get\n  private final integer right: get\n}\n\nmethod main {\n  var p = Pair{ left=1, | }\n}\n"
    labels = pkg_session(env).labels("main.nv", src)
    return ("left" not in labels and "right" in labels, str(labels))


@check(CTX, "objlit-not-method-body", "the start of a method body is not mistaken for an initializer (run.ts)")
def objlit_not_body(env):
    return has_all(pkg_session(env).labels("main.nv", "package shapes\n\nmethod main {\n  |\n}\n"), ["var"])


@check(CTX, "objlit-class-snippet", "class completion offers a Circle{...} item whose filterText is Circle (run.ts)")
def objlit_snippet(env):
    items = pkg_session(env).complete("main.nv", BODY % "var c = Cir|")
    brace = [i for i in items if i["label"].startswith("Circle{")]
    plain = [i for i in items if i["label"] == "Circle"]
    return (bool(brace) and brace[0].get("filterText") == "Circle" and bool(plain), str([(i["label"], i.get("filterText")) for i in items]))


@check(CTX, "objlit-snippet-prefilled", "the Circle{...} item pre-fills the field names: Circle{radius=${1}} (run.ts)")
def objlit_prefilled(env):
    items = pkg_session(env).complete("main.nv", BODY % "var c = Cir|")
    brace = [i for i in items if i["label"].startswith("Circle{")]
    text = (brace[0].get("textEdit") or {}).get("newText", "") if brace else ""
    return ("radius" in text, "snippet is %r" % text)


@check(CTX, "import-std-and-project", "import | lists std modules and project packages (run.ts: shapes, json)")
def import_lists(env):
    return has_all(pkg_session(env).labels("main.nv", "package app\n\nimport |\n"), ["shapes", "json"])


@check(CTX, "import-prefix", "import js| narrows to json")
def import_prefix(env):
    return has_all(pkg_session(env).labels("main.nv", "package app\n\nimport js|\n"), ["json"])


@check(CTX, "import-nested-folder", "import draw/| lists the sub-packages (import draw/shapes form)")
def import_nested(env):
    pk = os.path.join(REPO, "test", "cases", "packages")
    files = {}
    for root, _, names in os.walk(pk):
        for n in names:
            if n.endswith(".nv"):
                files[os.path.relpath(os.path.join(root, n), pk)] = read(os.path.join(root, n))
    ses = env.shared("packages", files)
    return has_all(ses.labels("main.nv", "package main\n\nimport draw/|\n"), ["shapes"])


@check(CTX, "import-at-root-files", "import @| lists only root files, not folders (import @helpers form)")
def import_at(env):
    pk = os.path.join(REPO, "test", "cases", "packages")
    files = {}
    for root, _, names in os.walk(pk):
        for n in names:
            if n.endswith(".nv"):
                files[os.path.relpath(os.path.join(root, n), pk)] = read(os.path.join(root, n))
    ses = env.shared("packages", files)
    labels = ses.labels("main.nv", "package main\n\nimport @|\n")
    return ("helpers" in labels and "geo" not in labels and "draw" not in labels, str(labels))


@check(CTX, "based-types", "based | offers class types only")
def based_types(env):
    labels = pkg_session(env).labels("main.nv", "package app\n\nimport shapes\n\ndefine class A based |\n")
    return ("Shape" in labels and "Circle" in labels and "integer" not in labels, str(labels))


@check(CTX, "type-positions", "return type, parameter type and generic argument offer types")
def type_positions(env):
    ses = pkg_session(env)
    out = []
    for marked in ["package app\n\nimport shapes\n\nmethod m(): |\n", "package app\n\nimport shapes\n\nmethod m(|) {\n}\n", "package app\n\nimport shapes\n\nmethod m() {\n  array<|\n}\n"]:
        out.append(has_all(ses.labels("main.nv", marked), ["integer", "string", "Circle"]))
    return all(o[0] for o in out), str([o[1] for o in out if not o[0]])


@check(CTX, "accessor-get-set", "a field's ': |' offers get and set")
def accessor(env):
    return has_all(pkg_session(env).labels("main.nv", "package app\n\ndefine class A {\n  private final string name: |\n}\n"), ["get", "set"])


@check(CTX, "define-kinds", "define | offers class/enum/interface/abstract/annotation")
def define_kinds(env):
    return has_all(pkg_session(env).labels("main.nv", "package app\n\ndefine |\n"), ["class", "enum", "interface", "abstract", "annotation"])


@check(CTX, "silent-in-string-and-comment", "no completion inside a string literal or a comment")
def silent(env):
    ses = pkg_session(env)
    a = ses.labels("main.nv", BODY % "println \"ab|c\"")
    b = ses.labels("main.nv", BODY % "// ab|c")
    return (a == [] and b == [], "string: %s comment: %s" % (a[:5], b[:5]))


@check(CTX, "interpolation-expression", "inside ${...} of a string expression completion works")
def interpolation(env):
    return has_all(pkg_session(env).labels("main.nv", BODY % "var abc = 1\n  println \"${ab|}\""), ["abc"])


@check(CTX, "autoimport-class", "an unimported class carries an additionalTextEdit 'import shapes' and labelDetails (run.ts)")
def autoimport_class(env):
    ses = env.session({"main.nv": "package app\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        items = ses.complete("main.nv", "package app\n\nmethod main {\n  var c = Cir|\n}\n")
        circle = [i for i in items if i["label"] == "Circle"]
        ok = bool(circle) and "import shapes" in json_text(circle[0].get("additionalTextEdits")) and "shapes" in json_text(circle[0].get("labelDetails")) + circle[0].get("detail", "")
        return ok, str(circle[:1])
    finally:
        ses.close()


@check(CTX, "autoimport-expression", "expression completion offers Color with its import edit (run.ts)")
def autoimport_expr(env):
    ses = env.session({"main.nv": "package app\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        items = ses.complete("main.nv", "package app\n\nmethod main {\n  var c = Col|\n}\n")
        color = [i for i in items if i["label"] == "Color"]
        return (bool(color) and bool(color[0].get("additionalTextEdits")), str(color[:1]))
    finally:
        ses.close()


@check(CTX, "autoimport-brace-item", "Circle{...} of another package auto-imports too (run.ts)")
def autoimport_brace(env):
    ses = env.session({"main.nv": "package app\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        items = ses.complete("main.nv", "package app\n\nmethod main {\n  var c = Cir|\n}\n")
        brace = [i for i in items if i["label"].startswith("Circle{")]
        return (bool(brace) and "import shapes" in json_text(brace[0].get("additionalTextEdits")), str(brace[:1]))
    finally:
        ses.close()


@check(CTX, "autoimport-import-edit-text", "the import edit lands after the package line (run.ts: after package, blank line)")
def autoimport_text(env):
    from lspclient import apply_edits
    ses = env.session({"main.nv": "package app\n", "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        src = "package app\n\nmethod main {\n  var c = Cir\n}\n"
        items = ses.complete("main.nv", src.replace("Cir", "Cir|"))
        circle = [i for i in items if i["label"] == "Circle"][0]
        out = apply_edits(src, circle["additionalTextEdits"])
        return (out.startswith("package app\n\nimport shapes\n\nmethod main"), repr(out[:60]))
    finally:
        ses.close()


def json_text(value):
    import json
    return json.dumps(value) if value is not None else ""
