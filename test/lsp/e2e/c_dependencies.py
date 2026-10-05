"""E2E (C): suggestions from dependencies (project.nv require/replace, $NOVUS_DEPS cache) and from the std modules.

A fake dependency cache is built in a scratch directory: modules with their own project.nv and transitive requires, a
`latest` and a tagged directory (two versions of one module), a replace to a local directory with a nested replace, a module
without manifest (entry main.nv), packages and single files, and a module that is required but not fetched. No test
touches the network; the fetch command is pointed at a host name that cannot resolve.

Usage: python3 test/lsp/e2e/c_dependencies.py
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import depfixture  # noqa: E402
from checks import Checks, exit_with  # noqa: E402
from lspclient import ROOT, Client, NOVUSC, apply_edits, at, make_project, uri_of  # noqa: E402

MAIN = "package main\n\nIMPORTS\nmethod main() {\n    NAME|\n}\n"
GEOMETRY = 'import "github.com/acme/geometry"\n'


class Probe:
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

    def find(self, source, label):
        found = [i for i in self.items(source) if i["label"] == label]
        return found[0] if found else None

    def code(self, imports, name):
        return MAIN.replace("IMPORTS", imports).replace("NAME", name)


def compiles(project, deps, text):
    manifest = open(project + "/project.nv").read()
    open(project + "/project.nv", "w").write(re.sub(r'require "github.com/acme/missing" "v1.0.0"\n', "", manifest))
    open(project + "/main.nv", "w").write(text)
    environment = dict(os.environ, NOVUS_DEPS=deps)
    result = subprocess.run([NOVUSC, "check", "main.nv"], cwd=project, env=environment, capture_output=True, text=True, timeout=120)
    open(project + "/project.nv", "w").write(manifest)
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def check_import_paths(probe, checks):
    modules = probe.items('package main\n\nimport "|\n')
    labels = {i["label"]: i for i in modules}
    expected = ["github.com/acme/geometry", "github.com/acme/latestlib", "github.com/acme/localdep", "github.com/acme/mathx",
                "github.com/acme/missing", "github.com/acme/nested", "github.com/acme/plain", "github.com/acme/tagged"]
    checks.check("import \": every required module and the transitive ones", sorted(labels) == expected, str(sorted(labels)))
    checks.check("import \": version shown as description", labels["github.com/acme/geometry"].get("labelDetails", {}).get("description") == "v1.2.0", str(labels["github.com/acme/geometry"]))
    checks.check("import \": `latest` shown when no version is given", labels["github.com/acme/latestlib"]["labelDetails"]["description"] == "latest", "")
    checks.check("import \": not fetched is visible", labels["github.com/acme/missing"]["detail"] == "module, not fetched", labels["github.com/acme/missing"]["detail"])
    checks.check("import \": module of a nested replace", "github.com/acme/nested" in labels, "")
    sub = probe.items('package main\n\nimport "github.com/acme/geometry/|\n')
    names = {i["label"]: i["detail"] for i in sub}
    checks.check("import mod/: packages and the entry file", names.get("shapes") == "package folder" and names.get("util") == "package folder" and names.get("lib") == "file", str(names))
    checks.check("import mod/: a folder without sources is not a package", "internal" not in names, str(names), "F11-empty-folder")
    files = {i["label"] for i in probe.items('package main\n\nimport "github.com/acme/geometry/shapes/|\n')}
    checks.check("import mod/pkg/: files of the package", files == {"circle", "square"}, str(files))
    partial = probe.items('package main\n\nimport "github.com/acme/ge|"\n')
    checks.check("import \"partial: replaces the typed segment only", [(i["label"], i["textEdit"]["range"]["start"]["character"]) for i in partial] == [("geometry", 24)], str(partial))
    checks.check("import \"nothing fetched for the unfetched module's folders", probe.items('package main\n\nimport "github.com/acme/missing/|"\n') == [], "")
    std = probe.labels("package main\n\nimport |\n", "ma")
    checks.check("import without quote: std modules, folders and the quote", "math" in std and "maps" in std, str(std))
    quote = probe.find("package main\n\nimport |\n", '"')
    checks.check("import without quote: offers the opening quote for dependencies", quote is not None, "")


def check_symbols(probe, project, deps, checks):
    unimported = {
        "function": ("distance", "distance", 'import "github.com/acme/geometry"'),
        "class": ("Poin", "Point", 'import "github.com/acme/geometry"'),
        "constant": ("ORIGIN", "ORIGIN_NAME", 'import "github.com/acme/geometry"'),
        "package of the module": ("Squar", "Square", 'import "github.com/acme/geometry/shapes"'),
        "file of the module (as package folder)": ("shout", "util.shout", 'import "github.com/acme/geometry/util"'),
        "transitive module": ("sq", "sq", 'import "github.com/acme/mathx"'),
        "latest": ("newe", "newest", 'import "github.com/acme/latestlib"'),
        "tagged version": ("versionT", "versionTwo", 'import "github.com/acme/tagged"'),
        "replaced local directory": ("fromL", "fromLocal", 'import "github.com/acme/localdep"'),
        "nested replace": ("deepH", "deepHelper", 'import "github.com/acme/nested"'),
        "module without manifest": ("plainF", "plainFn", 'import "github.com/acme/plain"'),
    }
    for name, (typed, label, line) in unimported.items():
        source = probe.code("", typed)
        item = probe.find(source, label)
        checks.check("dependency symbol: %s offered" % name, item is not None, str(probe.labels(source)[:6]))
        if item is None:
            continue
        edits = [e["newText"] for e in item.get("additionalTextEdits", [])]
        checks.check("dependency symbol: %s imports with the right form" % name, edits and line in edits[0], str(edits))
        checks.check("dependency symbol: %s names its module" % name, item.get("labelDetails", {}).get("description", "").startswith("github.com/acme/") or item.get("labelDetails", {}).get("description") in ("shapes", "util"), str(item.get("labelDetails")))
    checks.check("version: the untagged older version is not read", probe.labels(probe.code("", "versionO"), "versionO") == ["versionTwo"] or "versionOne" not in probe.labels(probe.code("", "versionO")), "")
    checks.check("not fetched: no symbols and no hang", probe.labels(probe.code("", "Missi"), "Missi") == [], "")


def check_imported(probe, project, deps, checks):
    imports = GEOMETRY
    items = {i["label"]: i for i in probe.items(probe.code(imports, "Poi"))}
    point = items.get("Point")
    checks.check("imported dependency: class without import edit", point is not None and not point.get("additionalTextEdits"), str(point))
    members = probe.labels(probe.code(imports, "var p = Point(1.0, 2.0)\n    p."), None)
    checks.check("imported dependency: members of an instance", sorted(members) == ["label", "shift", "x", "y"], str(members))
    chained = probe.labels(probe.code(imports, "var p: Point = nothing\n    p.shift(1.0, 2.0)."), None)
    checks.check("imported dependency: members through a method chain", sorted(chained) == ["label", "shift", "x", "y"], str(chained))
    enum = probe.labels(probe.code(imports, "Axis."), None)
    checks.check("imported dependency: enum constants", sorted(enum) == ["HORIZONTAL", "VERTICAL", "values"], str(enum))
    function = probe.find(probe.code(imports, "dist"), "distance")
    checks.check("imported dependency: function detail is the signature", function["detail"] == "method distance(float x1, float y1, float x2, float y2): float", function["detail"])
    resolved = probe.client.resolve(function)
    checks.check("completion resolve: // comment becomes documentation", "Distance between two points." in str(resolved.get("documentation")), str(resolved.get("documentation")))
    resolved = probe.client.resolve(probe.find(probe.code(imports, "Poi"), "Point"))
    checks.check("completion resolve: /// comment becomes documentation", "A point of the plane." in str(resolved.get("documentation")), str(resolved.get("documentation")))
    shift = probe.find(probe.code(imports, "var p = Point(1.0, 2.0)\n    p.shi"), "shift")
    resolved = probe.client.resolve(shift)
    checks.check("completion resolve: documentation of a member", "Move the point by an offset." in str(resolved.get("documentation")), str(resolved.get("documentation")))
    unimported = probe.find(probe.code("", "newe"), "newest")
    resolved = probe.client.resolve(unimported)
    checks.check("completion resolve keeps the import edit and fills the documentation", resolved.get("additionalTextEdits") and "Latest of everything." in str(resolved.get("documentation")), str(resolved))
    other = probe.find(probe.code(imports, "Squar"), "Square")
    edits = [e["newText"] for e in other["additionalTextEdits"]]
    checks.check("second import of the same module goes to the project group", edits == ['\nimport "github.com/acme/geometry/shapes"'], str(edits))
    for typed, label, args in (("dist", "distance", "1.0, 2.0, 3.0, 4.0"), ("Poi", "Point", "1.0, 2.0"), ("Squar", "Square", ""), ("shout", "util.shout", '"a"'), ("sq", "sq", "2.0"),
                               ("versionT", "versionTwo", ""), ("fromL", "fromLocal", ""), ("deepH", "deepHelper", ""), ("newe", "newest", "")):
        text, line, character = at(probe.code("", typed))
        probe.client.change(probe.path, text)
        item = [i for i in probe.client.complete(probe.path, line, character) if i["label"] == label][0]
        accepted = apply_edits(text, [item["textEdit"]] + item.get("additionalTextEdits", []))
        accepted = accepted.replace("$1", args).replace("Square()", "Square{side=1.0}")
        ok, message = compiles(project, deps, accepted)
        checks.check("accepted dependency item %s compiles" % label, ok, message + "\n" + accepted)


def check_navigation(client, project, checks):
    path = project + "/main.nv"
    source = 'package main\n\nimport "github.com/acme/geometry"\nimport "github.com/acme/nothere"\nimport "github.com/acme/geometry/nopkg"\n\nmethod main() {\n    var d = distance(1.0, 2.0, 3.0, 4.0)\n}\n'
    client.open(path, source)
    diagnostics = client.diagnostics(path, 1.5) or []
    checks.check("unknown module in an import is reported", any(d["range"]["start"]["line"] == 3 and d["code"] == "unresolved-import" for d in diagnostics), str(diagnostics))
    checks.check("unknown package of a known module is reported", any(d["range"]["start"]["line"] == 4 and "nopkg" in d["message"] for d in diagnostics), str(diagnostics))
    hover = client.hover(path, 7, 14)
    value = hover["contents"]["value"] if hover else ""
    checks.check("hover on a dependency function: signature, doc and origin", "distance(float x1" in value and "Distance between two points." in value and "github.com/acme/geometry" in value, value)
    definition = client.definition(path, 7, 14)
    checks.check("definition jumps into the dependency cache", definition and definition[0]["uri"].endswith("geometry%40v1.2.0/lib.nv"), str(definition))
    links = client.request("textDocument/documentLink", {"textDocument": {"uri": uri_of(path)}})["result"]
    checks.check("document link of a dependency import", links and links[0]["target"].endswith("lib.nv"), str(links))


def check_manifest(client, project, deps, checks):
    manifest = project + "/project.nv"
    original = open(manifest).read()
    client.open(manifest, original)
    diagnostics = client.diagnostics(manifest, 1.5) or []
    hint = [d for d in diagnostics if d.get("code") == "dependency-not-fetched"]
    checks.check("required but not fetched: one hint on the require line", len(hint) == 1 and hint[0]["severity"] in (3, 4) and hint[0]["range"]["start"]["line"] == 8, str(diagnostics))
    actions = client.request("textDocument/codeAction", {"textDocument": {"uri": uri_of(manifest)}, "range": hint[0]["range"], "context": {"diagnostics": hint}})["result"]
    checks.check("code action: Fetch dependencies", any(a.get("command", {}).get("command") == "novus.fetchDependencies" for a in actions), str(actions))
    probe = Probe(client, project + "/main.nv")

    def seen(name):
        return probe.labels(probe.code("", name), name)
    client.change(manifest, original + 'require "github.com/acme/mathx" "v0.3.1"\n')
    checks.check("manifest edit: require added", seen("sq") == ["sq"], str(seen("sq")))
    client.change(manifest, original.replace('"github.com/acme/tagged" "v2"', '"github.com/acme/tagged" "v1"'))
    checks.check("manifest edit: version changed to v1", seen("versionO") == ["versionOne"] and "versionTwo" not in seen("version"), str(seen("version")))
    client.change(manifest, original.replace('"../localdep"', '"../nested"'))
    checks.check("manifest edit: replace retargeted", seen("fromL") == [] and seen("deepH") != [], "")
    client.change(manifest, original.replace('"../localdep"', '"../doesnotexist"'))
    diagnostics = client.diagnostics(manifest, 1.0) or []
    checks.check("replace to a missing directory is an error", any(d.get("code") == "replace-target-missing" and d["severity"] == 1 for d in diagnostics), str(diagnostics))
    client.change(manifest, original)
    checks.check("not fetched: no symbol before the fetch", seen("missingFn") == [], str(seen("missingFn")))
    os.makedirs(deps + "/github.com/acme/missing@v1.0.0", exist_ok=True)
    open(deps + "/github.com/acme/missing@v1.0.0/lib.nv", "w").write("package missing\n\nmethod missingFn(): integer {\n    return 1\n}\n")
    client.request("workspace/executeCommand", {"command": "novus.rerunChecks", "arguments": []})
    time.sleep(0.5)
    checks.check("dependency fetched outside the editor is found after novus.rerunChecks", seen("missingFn") == ["missingFn"], str(seen("missingFn")), "F8-rerun-modules")
    client.change(manifest, original + "\n")
    checks.check("dependency fetched outside the editor is found after a manifest edit", seen("missingFn") == ["missingFn"], str(seen("missingFn")))


def check_manifest_completion(client, project, checks):
    manifest = project + "/project.nv"
    probe = Probe(client, manifest)
    items = probe.items('project "x"\nrequire "|')
    labels = [i["label"] for i in items]
    checks.check("require \": cached modules", "github.com/acme/geometry" in labels and "github.com/acme/mathx" in labels, str(labels))
    checks.check("require \": no empty-label item", "" not in labels, str(labels), "F9-empty-require")
    checks.check("require \": does not echo the typed text as required", not [i for i in probe.items('project "x"\nrequire "github.com/|') if i["label"] == "github.com/"], "", "F9-empty-require")
    versions = probe.labels('project "x"\nrequire "github.com/acme/geometry" "|')
    checks.check("require version slot: cached versions and latest", "v1.2.0" in versions and "latest" in versions, str(versions), "F10-version-slot")
    entry = probe.labels('project "x"\nversion "1"\nmain "|')
    checks.check("main \": source files of the project", "main.nv" in entry, str(entry), "F10-version-slot")
    required = probe.labels('project "x"\nrequire "github.com/acme/geometry" "v1.2.0"\nreplace "|')
    checks.check("replace \": the required modules", required == ["github.com/acme/geometry"], str(required))
    directories = probe.labels('project "x"\nrequire "github.com/acme/geometry" "v1.2.0"\nreplace "github.com/acme/geometry" "../|')
    checks.check("replace dir: sibling directories", "localdep" in directories and "nested" in directories, str(directories))


def check_fetch_no_hang(checks):
    root = make_project({"project.nv": 'project "x"\nversion "1"\nmain "main.nv"\nrequire "nonexistent.invalid/acme/lib" "v1.0.0"\n',
                         "main.nv": 'package main\n\nimport "nonexistent.invalid/acme/lib"\n\nmethod main() {\n    lib|\n}\n'})
    cache = make_project({"x": ""})
    client = Client(root, env={"NOVUS_DEPS": cache}, options={"novus": {"check": {"novuscPath": NOVUSC}}})
    path = root + "/main.nv"
    client.open(path, open(path).read())
    started = time.time()
    text, line, character = at(open(path).read())
    client.change(path, text)
    items = client.complete(path, line, character)
    checks.check("unfetched dependency: completion answers at once", time.time() - started < 5, "%.1fs" % (time.time() - started))
    checks.check("unfetched dependency: no item invented", not [i for i in items if i.get("kind") in (3, 7)], str([i["label"] for i in items][:5]))
    started = time.time()
    client.request("workspace/executeCommand", {"command": "novus.fetchDependencies", "arguments": []}, timeout=60)
    time.sleep(2)
    messages = [m for m in client.notes if m["method"] == "window/showMessage"]
    checks.check("fetch of an unreachable host fails with a message and without hanging", messages and "failed" in messages[-1]["params"]["message"] and time.time() - started < 40, str(messages))
    client.close()


def check_std(checks):
    root = make_project({"main.nv": "", "project.nv": 'project "x"\nversion "1.0"\nmain "main.nv"\n'})
    client = Client(root)
    path = root + "/main.nv"
    client.open(path, "package main\n")
    modules = sorted(f[:-3] for f in os.listdir(os.path.join(ROOT, "std")) if f.endswith(".nv"))
    probe = Probe(client, path)
    checks.check("std: the module list of the import line is complete", sorted(probe.labels("package main\n\nimport |\n", None)) != [] and all(m in probe.labels("package main\n\nimport |\n") for m in modules), "")
    missing_docs = 0
    total = 0
    for module in modules:
        source = "package main\n\nimport %s\n\nmethod main() {\n    %s.|\n}\n" % (module, module)
        items = probe.items(source)
        public = set(re.findall(r"^(?:async )?method (\w+)", open(os.path.join(ROOT, "std", module + ".nv")).read(), re.M))
        got = {i["label"] for i in items}
        checks.check("std %s: every function of the module source is offered" % module, public <= got, str(sorted(public - got)))
        checks.check("std %s: every item has a signature" % module, all(i.get("detail", "").startswith("method ") or i.get("kind") != 3 for i in items), "")
        for item in items:
            total += 1
            fenced = client.resolve(item).get("documentation", {}).get("value", "")
            if not re.sub(r"```.*?```", "", fenced, flags=re.S).strip():
                missing_docs += 1
        item = [i for i in items if i["kind"] == 3][0]
        text, line, character = at("package main\n\nimport %s\n\nmethod main() {\n    %s.%s|\n}\n" % (module, module, item["label"]))
        client.change(path, text)
        hover = client.hover(path, line, character - 1)
        checks.check("std %s: hover on %s shows a signature" % (module, item["label"]), hover and "```novus" in hover["contents"]["value"], str(hover))
    checks.check("std: every function has documentation text", missing_docs == 0, "%d of %d have none" % (missing_docs, total), "F16-std-docs")
    unimported = probe.items("package main\n\nmethod main() {\n    strings.|\n}\n")
    item = [i for i in unimported if i["label"] == "trim"] or unimported[:1]
    checks.check("std: members of an unimported module come with the import edit", item and item[0].get("additionalTextEdits") and item[0]["additionalTextEdits"][0]["newText"].startswith("import strings"), str(item[:1]))
    client.close()


def main():
    checks = Checks("c_dependencies")
    project, deps = depfixture.build()
    plain = deps + "/github.com/acme/plain@latest"
    os.makedirs(plain)
    open(plain + "/main.nv", "w").write("package plain\n\n// A module without manifest: its entry is main.nv.\nmethod plainFn(): integer {\n    return 1\n}\n")
    manifest = open(project + "/project.nv").read()
    open(project + "/project.nv", "w").write(manifest + 'require "github.com/acme/plain"\n')
    client = Client(project, env={"NOVUS_DEPS": deps})
    probe = Probe(client, project + "/main.nv")
    check_import_paths(probe, checks)
    check_symbols(probe, project, deps, checks)
    check_imported(probe, project, deps, checks)
    check_navigation(client, project, checks)
    check_manifest_completion(client, project, checks)
    check_manifest(client, project, deps, checks)
    client.close()
    check_fetch_no_hang(checks)
    check_std(checks)
    exit_with(checks)


if __name__ == "__main__":
    main()
