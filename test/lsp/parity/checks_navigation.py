"""Hover, definition, references, highlight, rename and signature help (features.ts, run.ts)."""
import os

from plib import check, has_all, pkg_files, features_text, read, REPO

HOV, DEF, REF, REN, SIG = "hover", "definition", "references", "rename", "signature"

MAIN = """package app

import shapes

method main {
  Shape s = Circle{radius=2.0}
  println s.area()
  var c = Circle{radius=1.0}
  println c.radius()
  println c.area()
  var local = 5
  local = local + 1
  println local
}
"""


def nav(env):
    files = {"main.nv": MAIN, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]}
    return env.shared("nav", files)


def feat(env):
    return env.shared("features-hover", {"main.nv": features_text()})


def syntax(env):
    return env.shared("syntax", {"main.nv": read(os.path.join(REPO, "test", "syntax.nv"))})


# --------------------------------------------------------------------------------------------------- hover
@check(HOV, "local-inferred-integer", "a local initialised from a call shows its type (integer)")
def hover_local_integer(env):
    value = feat(env).hover("main.nv", "var total", 0, 6)
    return (value is not None and "integer" in value, str(value))


@check(HOV, "local-inferred-float", "inferred type of ratio is float (run.ts: var ratio = 3.5 * total)")
def hover_local_float(env):
    value = feat(env).hover("main.nv", "var ratio", 0, 6)
    return (value is not None and "float" in value, str(value))


@check(HOV, "local-inferred-literals", "string/bool/integer/float literal locals show their type")
def hover_literals(env):
    ses = env.session({"main.nv": "package a\n\nmethod main {\n  var s = \"x\"\n  var f = 2.5\n  var n = 5\n  var b = true\n  println s\n  println f\n  println n\n  println b\n}\n"})
    try:
        got = [ses.hover("main.nv", "  println " + n + "\n", 0, 10) or "" for n in "sfnb"]
        want = ["string", "float", "integer", "bool"]
        return all(w in g for w, g in zip(want, got)), str(got)
    finally:
        ses.close()


@check(HOV, "local-inferred-object", "a local built from Person{...} shows Person")
def hover_local_object(env):
    value = syntax(env).hover("main.nv", "var tom", 0, 6)
    return (value is not None and "Person" in value, str(value))


@check(HOV, "array-element-type", "an array literal of strings shows array<string> (TS infers the element type)")
def hover_array_type(env):
    value = feat(env).hover("main.nv", "var names", 0, 6)
    return (value is not None and "array<string>" in value, str(value))


@check(HOV, "constant-doc", "/// doc comment of a constant (run.ts: Greeting used by main.)")
def hover_const_doc(env):
    value = feat(env).hover("main.nv", "GREETING", 0, 1)
    return (value is not None and "Greeting used by main." in value, str(value))


@check(HOV, "method-block-doc", "/** */ doc comment of a method (run.ts: Entry point.)")
def hover_block_doc(env):
    value = feat(env).hover("main.nv", "method main", 0, 8)
    return (value is not None and "Entry point." in value, str(value))


@check(HOV, "method-signature", "a function shows its signature add(integer a, integer b): integer")
def hover_method(env):
    value = feat(env).hover("main.nv", "add(1, 2)", 0, 1)
    return (value is not None and "add(integer a, integer b): integer" in value, str(value))


@check(HOV, "accessor", "hover on tom.friends() shows the accessor / field (run.ts: friends())")
def hover_accessor(env):
    value = syntax(env).hover("main.nv", "tom.friends()", 0, 4)
    return (value is not None and "friends" in value, str(value))


@check(HOV, "enum-member", "an enum constant shows Color.RED")
def hover_enum_member(env):
    value = feat(env).hover("main.nv", "Color.RED", 0, 7)
    return (value is not None and "Color.RED" in value, str(value))


@check(HOV, "class-with-base", "a class shows its declaration with the base")
def hover_class(env):
    value = nav(env).hover("main.nv", "Circle{", 0, 1)
    return (value is not None and "Circle" in value and "Shape" in value, str(value))


@check(HOV, "member-of-container", "a method shows the class it belongs to (TS: Member of Counter)")
def hover_member_container(env):
    value = feat(env).hover("main.nv", "counter.value()", 0, 9)
    return (value is not None and "Counter" in value, str(value))


@check(HOV, "std-function", "json.stringify shows the std signature")
def hover_std(env):
    value = feat(env).hover("main.nv", "json.stringify", 0, 6)
    return (value is not None and "stringify" in value and "string" in value, str(value))


@check(HOV, "builtin-keyword", "println shows its keyword documentation")
def hover_keyword(env):
    value = feat(env).hover("main.nv", "println names", 0, 2)
    return (value is not None and "println" in value, str(value))


@check(HOV, "primitive-type", "integer shows its documentation")
def hover_primitive(env):
    value = feat(env).hover("main.nv", "integer truncated", 0, 2)
    return (value is not None and "integer" in value, str(value))


@check(HOV, "builtin-annotation", "@Interface shows the built-in annotation documentation")
def hover_annotation(env):
    value = feat(env).hover("main.nv", "@Interface", 0, 3)
    return (value is not None and "Interface" in value, str(value))


@check(HOV, "deprecated-note", "a deprecated method's hover carries a deprecation note")
def hover_deprecated(env):
    ses = env.session({"main.nv": "package a\n\n/// Old way.\n@Deprecated{\n  text=\"use fresh\",\n  since=\"1\"\n}\nmethod old() {\n}\n\nmethod main {\n  old()\n}\n"})
    try:
        value = ses.hover("main.nv", "old()", 1, 1)
        return (value is not None and "eprecated" in value and "Old way." in value, str(value))
    finally:
        ses.close()


@check(HOV, "inside-interpolation", "hover works on a name inside ${...}")
def hover_interpolation(env):
    value = feat(env).hover("main.nv", "${ratio}", 0, 3)
    return (value is not None and "ratio" in value, str(value))


@check(HOV, "unimported-class", "hover on a class of a not yet imported package resolves it (run.ts: members of an unimported class)")
def hover_unimported(env):
    ses = env.session({"main.nv": pkg_files()["main.nv"], "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        value = ses.hover("main.nv", "s.area()", 0, 2)
        return (value is not None and "area" in value, str(value))
    finally:
        ses.close()


# ----------------------------------------------------------------------------------------------- definition
@check(DEF, "local", "a local use jumps to its var declaration")
def def_local(env):
    got = nav(env).definition("main.nv", "println local", 0, 10)
    return (got == [("main.nv", 10)], str(got))


@check(DEF, "class-in-literal", "Person{ jumps to the class (run.ts)")
def def_class_literal(env):
    ses = syntax(env)
    got = ses.definition("main.nv", "Person{", 0, 1)
    return (len(got) == 1 and got[0][0] == "main.nv", str(got))


@check(DEF, "class-cross-file", "Circle{ in main.nv jumps into shapes/shapes.nv")
def def_class_cross(env):
    got = nav(env).definition("main.nv", "Circle{", 0, 1)
    return (got == [("shapes/shapes.nv", 7)], str(got))


@check(DEF, "type-position", "a type in a typed local (Shape s) jumps to the interface")
def def_type_position(env):
    got = nav(env).definition("main.nv", "Shape s", 0, 1)
    return (got == [("shapes/shapes.nv", 2)], str(got))


@check(DEF, "member-typed-receiver", "s.area jumps to the member of the declared type")
def def_member(env):
    got = nav(env).definition("main.nv", "s.area()", 0, 3)
    return (len(got) == 1 and got[0][0] == "shapes/shapes.nv", str(got))


@check(DEF, "accessor-to-field", "c.radius() jumps to the field with the accessor (TS: accessorOf)")
def def_accessor(env):
    got = nav(env).definition("main.nv", "c.radius()", 0, 3)
    return (len(got) == 1 and got[0][0] == "shapes/shapes.nv", str(got))


@check(DEF, "enum-constant", "Color.RED jumps to the enum constant")
def def_enum_constant(env):
    got = feat(env).definition("main.nv", "Color.RED", 0, 7)
    return (len(got) == 1 and got[0][0] == "main.nv", str(got))


@check(DEF, "function", "add(1, 2) jumps to the method")
def def_function(env):
    got = feat(env).definition("main.nv", "add(1, 2)", 0, 1)
    return (len(got) == 1, str(got))


@check(DEF, "overload", "an overloaded method jumps to a declaration of that name")
def def_overload(env):
    got = feat(env).definition("main.nv", "counter.increment(5)", 0, 9)
    return (len(got) >= 1, str(got))


@check(DEF, "unimported-class", "go to definition works before the import is added (run.ts)")
def def_unimported(env):
    ses = env.session({"main.nv": pkg_files()["main.nv"], "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]})
    try:
        got = ses.definition("main.nv", "Circle{", 0, 1)
        return (len(got) == 1 and got[0][0] == "shapes/shapes.nv", str(got))
    finally:
        ses.close()


@check(DEF, "std-null", "a std function has no source location (empty answer, no crash)")
def def_std(env):
    got = feat(env).definition("main.nv", "json.stringify", 0, 6)
    return (isinstance(got, list), str(got))


@check(DEF, "interpolation", "definition works on a name inside ${...}")
def def_interpolation(env):
    got = feat(env).definition("main.nv", "${ratio}", 0, 3)
    return (len(got) == 1, str(got))


# ----------------------------------------------------------------------------------------------- references
@check(REF, "local-with-declaration", "references of a local include the declaration")
def ref_local(env):
    got = nav(env).references("main.nv", "var local", 0, 5, True)
    return (len(got) == 4, str(got))


@check(REF, "local-without-declaration", "includeDeclaration=false drops the declaration")
def ref_local_nodecl(env):
    got = nav(env).references("main.nv", "var local", 0, 5, False)
    return (len(got) == 3, str(got))


@check(REF, "class-cross-file", "references of Circle span main.nv and shapes.nv")
def ref_class(env):
    got = nav(env).references("main.nv", "Circle{", 0, 1)
    files = {g[0] for g in got}
    return (files == {"main.nv", "shapes/shapes.nv"} and len(got) >= 3, str(got))


@check(REF, "method-cross-file", "references of a method are found across files")
def ref_method(env):
    got = nav(env).references("main.nv", "c.area()", 0, 3)
    files = {g[0] for g in got}
    return ("main.nv" in files and "shapes/shapes.nv" in files, str(got))


@check(REF, "unopened-file", "references reach files that are not open (TS indexes the whole workspace)")
def ref_unopened(env):
    files = {"main.nv": MAIN, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"], "util.nv": "package app\n\nimport shapes\n\nmethod twice(): float {\n  var k = Circle{radius=3.0}\n  return k.area() * 2.0\n}\n"}
    ses = env.session(files, open_all=False)
    try:
        ses.open("main.nv")
        got = ses.references("main.nv", "Circle{", 0, 1)
        return ("shapes/shapes.nv" in {g[0] for g in got}, str(got))
    finally:
        ses.close()


@check(REF, "outside-program-closure", "references reach a workspace file that no program imports (TS: every indexed file)")
def ref_outside_closure(env):
    files = {"main.nv": MAIN, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"], "other/o.nv": "package other\n\nimport shapes\n\nmethod useIt(): float {\n  var k = Circle{radius=3.0}\n  return k.area()\n}\n"}
    ses = env.session(files)
    try:
        got = ses.references("main.nv", "Circle{", 0, 1)
        return ("other/o.nv" in {g[0] for g in got}, str(got))
    finally:
        ses.close()


@check(REF, "highlight", "documentHighlight marks a local's reads and writes")
def highlight(env):
    ses = nav(env)
    res = ses.at("textDocument/documentHighlight", "main.nv", *ses.spot("main.nv", "var local", 0, 5)) or []
    kinds = sorted(h.get("kind") for h in res)
    return (len(res) == 4 and 3 in kinds and 2 in kinds, str(res))


# ----------------------------------------------------------------------------------------------------- rename
@check(REN, "prepare-local", "prepareRename returns the identifier range and placeholder")
def prepare_local(env):
    ses = nav(env)
    res = ses.at("textDocument/prepareRename", "main.nv", *ses.spot("main.nv", "var local", 0, 5))
    return (bool(res) and res.get("placeholder") == "local", str(res))


@check(REN, "prepare-refused-on-keyword", "prepareRename on println / a builtin answers null")
def prepare_builtin(env):
    ses = nav(env)
    res = ses.at("textDocument/prepareRename", "main.nv", *ses.spot("main.nv", "println local", 0, 2))
    return (res is None, str(res))


@check(REN, "local", "renaming a local edits every occurrence in the scope")
def rename_local(env):
    got = nav(env).rename("main.nv", "var local", "renamed", 0, 5)
    return (got is not None and len(got.get("main.nv", [])) == 4, str(got))


@check(REN, "class-cross-file", "renaming a class edits all files")
def rename_class(env):
    got = nav(env).rename("main.nv", "Circle{", "Round", 0, 1)
    return (got is not None and set(got) == {"main.nv", "shapes/shapes.nv"}, str(got and {k: len(v) for k, v in got.items()}))


@check(REN, "method-interface-and-impl", "renaming s.area edits the interface member and the implementation (a project method family)")
def rename_family(env):
    got = nav(env).rename("main.nv", "c.area()", "surface", 0, 3)
    if not got:
        return False, str(got)
    shapes = got.get("shapes/shapes.nv", [])
    return (len(shapes) >= 2 and len(got.get("main.nv", [])) == 2, str({k: len(v) for k, v in got.items()}))


@check(REN, "conflict-refused", "renaming a local to a name that is taken in its scope is refused")
def rename_conflict(env):
    got = nav(env).rename("main.nv", "var local", "c", 0, 5)
    return (not got, str(got))


@check(REN, "keyword-refused", "renaming to a keyword or invalid identifier is refused")
def rename_keyword(env):
    ses = nav(env)
    a = ses.rename("main.nv", "var local", "while", 0, 5)
    b = ses.rename("main.nv", "var local", "1bad", 0, 5)
    return (not a and not b, str((a, b)))


@check(REN, "unopened-file", "renaming edits files that are not open")
def rename_unopened(env):
    files = {"main.nv": MAIN, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"]}
    ses = env.session(files, open_all=False)
    try:
        ses.open("main.nv")
        got = ses.rename("main.nv", "Circle{", "Round", 0, 1)
        return (got is not None and "shapes/shapes.nv" in got, str(got and list(got)))
    finally:
        ses.close()


@check(REN, "outside-program-closure", "renaming also edits a workspace file that no program imports (a rename must not leave it broken)")
def rename_outside_closure(env):
    files = {"main.nv": MAIN, "shapes/shapes.nv": pkg_files()["shapes/shapes.nv"], "other/o.nv": "package other\n\nimport shapes\n\nmethod useIt(): float {\n  var k = Circle{radius=3.0}\n  return k.area()\n}\n"}
    ses = env.session(files)
    try:
        got = ses.rename("main.nv", "Circle{", "Round", 0, 1)
        return (got is not None and "other/o.nv" in got, str(got and list(got)))
    finally:
        ses.close()


# ------------------------------------------------------------------------------------------- signature help
SIG_BASE = "package f\n\ndefine class Counter {\n  private final integer value: get, set\n  construct(integer value) {\n    this.value = value\n  }\n  method increment() {\n    this.value = this.value + 1\n  }\n  method increment(integer by) {\n    this.value = this.value + by\n  }\n}\n\nmethod add(integer a, integer b): integer {\n  return a + b\n}\n\nmethod main {\n  var counter = Counter{value=0}\n  %s\n}\n"


def sig(env, body):
    return env.shared("sig", {"main.nv": "package f\n"}).signature("main.nv", SIG_BASE % body)


@check(SIG, "free-first", "add( shows add(integer a, integer b): integer, parameter 0")
def sig_first(env):
    got = sig(env, "add(|")
    return (got is not None and got[1] == 0 and "add(integer a, integer b)" in got[2][0], str(got))


@check(SIG, "free-second", "after the comma the active parameter is 1")
def sig_second(env):
    got = sig(env, "add(1, |")
    return (got is not None and got[1] == 1, str(got))


@check(SIG, "closed-call-none", "after the closing parenthesis there is no signature help")
def sig_closed(env):
    return (sig(env, "add(1, 2)|") is None, str(sig(env, "add(1, 2)|")))


@check(SIG, "member-overloads", "counter.increment( lists both overloads and activates the one with a parameter")
def sig_overloads(env):
    got = sig(env, "counter.increment(|")
    return (got is not None and len(got[2]) == 2 and got[0] == 1, str(got))


@check(SIG, "constructor", "Counter( shows the constructor")
def sig_ctor(env):
    got = sig(env, "var k = Counter(|")
    return (got is not None and "Counter(integer value)" in got[2][0], str(got))


@check(SIG, "nested-call", "the inner call wins inside add(add(1, |")
def sig_nested(env):
    got = sig(env, "add(add(1, |")
    return (got is not None and got[1] == 1, str(got))


@check(SIG, "std-method", "json.save( shows the std signature (run.ts)")
def sig_std(env):
    src = read(os.path.join(REPO, "test", "syntax.nv"))
    ses = env.shared("syntax", {"main.nv": src})
    got = ses.signature("main.nv", src.replace("  json.save(personObject, peoplePath, \"person.json\")", "  json.save(|personObject, peoplePath, \"person.json\")", 1))
    return (got is not None and "save(" in got[2][0], str(got))


@check(SIG, "builtin-function", "parseInt( shows the built-in")
def sig_builtin(env):
    got = sig(env, "var n = parseInt(|")
    return (got is not None and "parseInt" in got[2][0], str(got))


@check(SIG, "multiline-call", "a call that continues on the next line keeps the signature help")
def sig_multiline(env):
    got = sig(env, "add(1,\n    |")
    return (got is not None and got[1] == 1, str(got))


@check(SIG, "not-in-string", "no signature help inside a string literal")
def sig_string(env):
    return (sig(env, "println \"add(|\"") is None, str(sig(env, "println \"add(|\"")))


@check(SIG, "documentation", "the documentation of a documented method is part of the signature")
def sig_doc(env):
    src = "package f\n\n/// Adds two.\nmethod add(integer a, integer b): integer {\n  return a + b\n}\n\nmethod main {\n  add(|\n}\n"
    ses = env.shared("sig", {"main.nv": "package f\n"})
    text = src
    from plib import mark
    t, line, ch = mark(text)
    ses.set("main.nv", t)
    res = ses.at("textDocument/signatureHelp", "main.nv", line, ch)
    doc = str(res["signatures"][0].get("documentation")) if res else ""
    return ("Adds two." in doc, doc)
