"""E2E (B): suggestions from the current project: other files and packages, members with inferred receiver types, locals,
auto-import edits, cache invalidation after edits of other files, and .nvh components.

Usage: python3 test/lsp/e2e/b_project.py
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from checks import Checks, exit_with  # noqa: E402
from lspclient import Client, NOVUSC, ROOT, apply_edits, at, make_project, uri_of  # noqa: E402
from projectfixture import FILES  # noqa: E402

HEAD = "package main\n\nimport geo\n\n"
BODY = "method main() {\n%s\n}\n"


class Probe:
    """One open document of a project; `at` texts carry a | cursor."""

    def __init__(self, client, path):
        self.client = client
        self.path = path
        self.opened = False

    def items(self, source):
        text, line, character = at(source)
        if self.opened:
            self.client.change(self.path, text)
        else:
            self.client.open(self.path, text)
            self.opened = True
        return self.client.complete(self.path, line, character)

    def labels(self, source, prefix=None):
        return [i["label"] for i in self.items(source) if prefix is None or i["label"].startswith(prefix)]

    def accept(self, source, label):
        """The document after accepting the item `label`: the main edit plus the additional edits."""
        text, _, _ = at(source)
        item = [i for i in self.items(source) if i["label"] == label][0]
        edits = [item["textEdit"]] + item.get("additionalTextEdits", [])
        return apply_edits(text, edits)


def compiles(root, entry="main.nv"):
    result = subprocess.run([NOVUSC, "check", entry], cwd=root, capture_output=True, text=True, timeout=120)
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def check_other_files(probe, root, checks):
    body = lambda line: "package main\n\nimport os\n\n" + BODY % line
    cases = {
        "class of another package": ("    Circ|", "Circle", "Circle", "import geo"),
        "enum of another package": ("    var k = Kin|", "Kind", "Kind", "import geo"),
        "function of another package, qualified": ("    descr|", "geo.describe", "geo.describe", "import geo"),
        "nested package folder": ("    Ring|", "Ring", "Ring", "import geo/shapes"),
        "second package": ("    Us|", "User", "User", "import models"),
        "root file by @file": ("    helperF|", "helperFn", "helperFn", "import @helper"),
        "class of a root file": ("    HelperB|", "HelperBox", "HelperBox", "import @helper"),
        "folder named like a package": ("    tool|", "tools.toolRun", "tools.toolRun", "import tools"),
    }
    for name, (line, label, _, importline) in cases.items():
        labels = probe.labels(body(line))
        checks.check("project symbol: %s offered" % name, label in labels, str(labels[:8]))
        if label not in labels:
            continue
        accepted = probe.accept(body(line), label)
        checks.check("project symbol: %s adds %r" % (name, importline), importline in accepted.split("\n"), accepted)
        checks.check("project symbol: %s keeps `import os`" % name, "import os" in accepted.split("\n"), accepted)
    labels = probe.labels(body("    hyph|"))
    checks.check("folder with a hyphen cannot be imported and is not offered", "hyph" not in labels, str(labels))
    docs = {i["label"]: i for i in probe.items(body("    Circ|"))}
    checks.check("class item documents its declaration", "A round figure." in str(docs["Circle"].get("documentation")), str(docs["Circle"].get("documentation")))
    checks.check("class item has an object-literal twin", "Circle{}" in docs, str(list(docs)))
    # Compile what the user gets by accepting: class, function, @file and nested package imports.
    for line, label, args in (("    var c = Circ|", "Circle", "1.0"), ("    var d = descr|", "geo.describe", "Circle(1.0)"),
                              ("    var h = helperF|", "helperFn", ""), ("    var r = Ri|", "Ring", "")):
        accepted = probe.accept(body(line), label)
        accepted = accepted.replace("$1", args).replace("Ring()", "Ring{inner=1.0}")
        directory = make_project(FILES)
        open(directory + "/main.nv", "w").write(accepted)
        ok, message = compiles(directory)
        checks.check("accepted %s compiles" % label, ok, message + "\n" + accepted)


def check_import_forms(probe, checks):
    prefix = "package main\n\n"
    cases = {
        "no import yet": (prefix + BODY % "    Circ|", ["package main", "", "import geo", "", "method main() {"]),
        "after std import": (prefix + "import os\n\n" + BODY % "    Circ|", ["import os", "", "import geo", ""]),
        "sorted into the project group": (prefix + "import os\n\nimport models\n\n" + BODY % "    Circ|", ["import geo", "import models"]),
        "no package line": (BODY % "    helperF|", ["import @helper", "", "method main() {"]),
    }
    for name, (source, expected) in cases.items():
        label = "helperFn" if "helperF" in source else "Circle"
        lines = probe.accept(source, label).split("\n")
        checks.check("import placement: %s" % name, any(lines[i:i + len(expected)] == expected for i in range(len(lines))), "\n".join(lines))
    lines = probe.accept(prefix.replace("\n", "\r\n") + (BODY % "    Circ|").replace("\n", "\r\n"), "Circle").split("\n")
    checks.check("import edit follows CRLF", all(l.endswith("\r") for l in lines[:-1]), repr(lines[:6]))
    already = probe.items(HEAD + BODY % "    Circ|")
    circle = [i for i in already if i["label"] == "Circle"][0]
    checks.check("imported package: no import edit", not circle.get("additionalTextEdits"), str(circle.get("additionalTextEdits")))
    checks.check("imported package: class detail is its declaration", circle.get("detail") == "define class Circle based Shape", str(circle.get("detail")))


def check_members(probe, checks):
    members = lambda source: sorted(probe.labels(source))
    expect = {
        "constructor call": (HEAD + BODY % "    var c = Circle(1.0)\n    c.|", ["area", "grow", "label", "radius"]),
        "method chain": (HEAD + BODY % "    var c = Circle(1.0).grow(2.0)\n    c.|", ["area", "grow", "label", "radius"]),
        "typed parameter": (HEAD + "method run(Circle c) {\n    c.|\n}\n", ["area", "grow", "label", "radius"]),
        "array<T> loop variable": (HEAD + "method run(array<Circle> cs) {\n    for (x in cs) {\n        x.|\n    }\n}\n", ["area", "grow", "label", "radius"]),
        "declared interface": (HEAD + BODY % "    var s: Shape = nothing\n    s.|", ["area"]),
        "enum constants": (HEAD + BODY % "    Kind.|", ["ROUND", "SQUARE", "values"]),
        "package qualifier": (HEAD + BODY % "    geo.|", ["PI", "describe"]),
        "this": (HEAD + "define class Box {\n    integer size\n    method grow(integer by) {\n        this.|\n    }\n}\n", ["grow", "size"]),
    }
    for name, (source, wanted) in expect.items():
        got = members(source)
        checks.check("members: %s" % name, got == wanted, "%s != %s" % (got, wanted))
    got = probe.labels("package main\n\nimport geo\nimport models\n\n" + BODY % "    var u = makeUser(\"a\")\n    u.|")
    checks.check("members: result of a function from another package", sorted(got) == ["age", "greet", "name"], str(sorted(got)))
    string_members = probe.labels(HEAD + BODY % "    var s = \"abc\"\n    s.|")
    checks.check("members: string receiver gets string methods only", "toUpper" in string_members and "push" not in string_members, str(string_members))
    literal = probe.labels(HEAD + BODY % "    var xs = [Circle(1.0)]\n    for (x in xs) {\n        x.|\n    }")
    checks.check("members: element of an array literal", sorted(literal) == ["area", "grow", "label", "radius"], str(len(literal)) + " items", "F13-literal-elements")
    qualifier = probe.items("package main\n\n" + BODY % "    models.|")
    checks.check("members: unimported project package gets its functions with the import edit", any(i["label"] == "makeUser" and i.get("additionalTextEdits") for i in qualifier), str([i["label"] for i in qualifier][:5]), "F14-unimported-package")
    item = [i for i in probe.items(HEAD + BODY % "    var c = Circle(1.0)\n    c.gr|") if i["label"] == "grow"][0]
    checks.check("members: method inserts a call with a tabstop", item["textEdit"]["newText"] == "grow($1)" and item.get("insertTextFormat") == 2, str(item["textEdit"]))
    checks.check("members: method detail is its signature", item["detail"] == "method grow(float by): Circle", item["detail"])


def check_locals(probe, checks):
    head = HEAD + "method run(integer count, string name) {\n"
    got = probe.labels(head + "    var total = 1\n    for (item in [1, 2]) {\n        var inner = 2\n        in|\n    }\n    var later = 3\n}\n")
    checks.check("locals: loop-body local visible", "inner" in got, str(got))
    got = probe.labels(head + "    var total = 1\n    later|\n    var later = 3\n}\n")
    checks.check("locals: a variable declared later is not offered", "later" not in got, str(got))
    got = probe.labels(head + "    if (count > 0) {\n        var scoped = 1\n    }\n    scop|\n}\n")
    checks.check("locals: a variable of a closed block is not offered", "scoped" not in got, str(got))
    items = probe.items(head + "    var total = 1\n    cou|\n}\n")
    local = [i for i in items if i["label"] == "count"]
    checks.check("locals: parameter ranks in bucket 0", local and local[0]["sortText"].startswith("0"), str(local))
    checks.check("locals: parameter detail", local and local[0]["detail"] == "parameter count: integer", str(local))
    items = probe.items(head + "    var total = 1\n    tot|\n}\n")
    checks.check("locals: variable detail shows the inferred type", [i["detail"] for i in items if i["label"] == "total"] == ["var total: integer"], str([i["detail"] for i in items if i["label"] == "total"]))
    items = probe.items(HEAD + "define class Box {\n    integer size\n    method grow(integer size) {\n        si|\n    }\n}\n")
    sizes = [i["label"] for i in items if i["label"] == "size"]
    checks.check("shadowing: the parameter hides the field", len(sizes) == 1, "%d items named size" % len(sizes), "F15-shadowed-field")


def check_invalidation(root, client, checks):
    main = root + "/main.nv"
    shape = root + "/geo/shape.nv"
    probe = Probe(client, main)
    source = HEAD + BODY % "    Squa|"
    original = open(shape).read()
    checks.check("invalidation: starts without Square", probe.labels(source, "Squa") == [], "")
    client.open(shape, original)
    client.change(shape, original + "\ndefine class Square {\n    float side\n}\n")
    checks.check("invalidation: edit of an open buffer of another file", "Square" in probe.labels(source, "Squa"), "")
    client.change(shape, original)
    checks.check("invalidation: undoing the edit removes it", probe.labels(source, "Squa") == [], str(probe.labels(source, "Squa")))
    client.notify("textDocument/didClose", {"textDocument": {"uri": uri_of(shape)}})
    time.sleep(0.2)
    open(shape, "w").write(original + "\ndefine class Square {\n    float side\n}\n")
    client.watched(shape, 2)
    checks.check("invalidation: didChangeWatchedFiles after a disk write", "Square" in probe.labels(source, "Squa"), "")
    extra = root + "/geo/extra.nv"
    open(extra, "w").write("package geo\n\ndefine class Hexagon {\n    float side\n}\n")
    client.watched(extra, 1)
    checks.check("invalidation: a created file of an imported package", "Hexagon" in probe.labels(HEAD + BODY % "    Hexa|", "Hexa"), "")
    os.remove(extra)
    client.watched(extra, 3)
    checks.check("invalidation: a deleted file", probe.labels(HEAD + BODY % "    Hexa|", "Hexa") == [], "")
    os.makedirs(root + "/brand")
    open(root + "/brand/b.nv", "w").write("package brand\n\nmethod brandName(): string {\n    return \"x\"\n}\n")
    client.watched(root + "/brand/b.nv", 1)
    checks.check("invalidation: a new package folder", "brand.brandName" in probe.labels(HEAD + BODY % "    brandN|", "brand"), "")
    fresh = root + "/fresh/f.nv"
    client.open(fresh, "package fresh\n\ndefine class Fresh {\n}\n")
    checks.check("invalidation: a package that exists only as an unsaved buffer", "Fresh" in probe.labels(HEAD + BODY % "    Fres|", "Fres"), "")
    client.open(shape, original)
    client.change(shape, original.replace("method grow(float by)", "method grow2(float by)"))
    members = probe.labels(HEAD + BODY % "    var c = Circle(1.0)\n    c.|")
    checks.check("invalidation: renamed method of another file shows in members", "grow2" in members and "grow" not in members, str(members))
    client.notify("textDocument/didClose", {"textDocument": {"uri": uri_of(shape)}})


def check_stale_disk(checks):
    root = make_project(FILES)
    client = Client(root)
    probe = Probe(client, root + "/main.nv")
    source = HEAD + BODY % "    Squa|"
    probe.labels(source)
    shape = root + "/geo/shape.nv"
    text = open(shape).read()
    time.sleep(1.2)
    open(shape, "w").write(text + "\ndefine class Square {\n    float side\n}\n")
    time.sleep(1.2)
    checks.check("a disk change without notification is seen after a second", "Square" in probe.labels(source, "Squa"), "")
    client.close()


def check_nvh(checks):
    root = os.path.join(make_project({"x": ""}), "web")
    shutil.copytree(os.path.join(ROOT, "examples", "web"), root)
    client = Client(root)
    main = root + "/main.nv"
    text = open(main).read()
    probe = Probe(client, main)
    marker = 'web.page("/", Home())'
    labels = probe.labels(text.replace(marker, "var h = Hom|"), "Hom")
    checks.check("nvh: component class of an imported folder", "Home" in labels, str(labels))
    items = probe.items(text.replace(marker, "var c = Count|"))
    counter = [i for i in items if i["label"] == "Counter"]
    checks.check("nvh: component of a folder that is not imported gets an import edit", counter and [e["newText"] for e in counter[0].get("additionalTextEdits", [])] == ["import components\n"], str(counter))
    members = probe.labels(text.replace(marker, "var h = Home()\n    h.|"))
    checks.check("nvh: component members include its own method", "changed" in members, str(members))
    checks.check("nvh: runtime members nv* of NvhComponent are hidden", not [m for m in members if m.startswith("nv")], str([m for m in members if m.startswith("nv")]), "F12-nvh-template")
    page = root + "/pages/Todos.nvh"
    page_probe = Probe(client, page)
    todos = open(page).read()
    anchor = "method add() {"
    checks.check("nvh header: ref of the component", "draft" in page_probe.labels(todos.replace(anchor, anchor + "\n    dr|"), "dr"), "")
    checks.check("nvh header: global of another file (@state)", "nextTodo" in page_probe.labels(todos.replace(anchor, anchor + "\n    nextT|"), "next"), "")
    checks.check("nvh header: statement snippet inside a method", "sout" in page_probe.labels(todos.replace(anchor, anchor + "\n    sout|"), "sout"), "")
    checks.check("nvh header: element of a global array literal", "id" in page_probe.labels(todos.replace(anchor, anchor + "\n    for (t in todos) {\n        t.|\n    }")) and len(page_probe.labels(todos.replace(anchor, anchor + "\n    for (t in todos) {\n        t.|\n    }"))) < 10, "unknown type lists every builtin member", "F13-literal-elements")
    got = page_probe.labels(todos.replace('<button disabled={draft.trim() == ""}>', "<button disabled={dr|}>"), "dr")
    checks.check("nvh template: expression completes refs", "draft" in got, str(got), "F12-nvh-template")
    got = page_probe.labels(todos.replace('bind="draft"', 'bind="dr|"'), "dr")
    checks.check("nvh template: bind= completes refs", "draft" in got, str(got), "F12-nvh-template")
    got = page_probe.labels(todos.replace("<TodoItem todo={t}", "<TodoItem to|"), "to")
    checks.check("nvh template: component prop names", "todo" in got, str(got), "F12-nvh-template")
    client.close()


def main():
    checks = Checks("b_project")
    root = make_project(FILES)
    client = Client(root)
    probe = Probe(client, root + "/main.nv")
    check_other_files(probe, root, checks)
    check_import_forms(probe, checks)
    check_members(probe, checks)
    check_locals(probe, checks)
    client.close()
    client = Client(make_project(FILES))
    check_invalidation(client.root, client, checks)
    client.close()
    check_stale_disk(checks)
    check_nvh(checks)
    exit_with(checks)


if __name__ == "__main__":
    main()
