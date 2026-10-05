"""E2E (A): Java-style snippets through the protocol: where they are offered, in which format, with which indentation.

Usage: python3 test/lsp/e2e/a_snippets.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from checks import Checks, exit_with  # noqa: E402
from lspclient import Client, at, make_project  # noqa: E402
from snippet import expand  # noqa: E402

MANIFEST = 'project "x"\nversion "1.0"\nmain "main.nv"\n'
SKELETON = "package main\n\nmethod main() {\n    PREFIX|\n}\n"
CONTEXT_TEXTS = {
    "toplevel": "package main\n\nPREFIX|\n",
    "class": "package main\n\ndefine class A {\n    PREFIX|\n}\n",
    "statement": SKELETON,
    "expression": "package main\n\nmethod main() {\n    var x = PREFIX|\n}\n",
}
# Prefix -> scope in which a Java user expects it (the names of IntelliJ live templates or their Novus equivalent).
EXPECTED = {
    "toplevel": ["psvm", "main", "mainargs", "class", "enum", "interface", "import", "method", "const", "tryrun", "testmain", "async", "abstract", "impl", "package"],
    "class": ["ctor", "construct", "field", "method", "getter", "accessors", "absm"],
    "statement": ["sout", "soutv", "soutm", "soutp", "serr", "fori", "forir", "iter", "itar", "foreach", "itmap", "if", "ifelse", "ife",
                  "else", "while", "var", "thread", "virtual", "await", "sync", "syncl", "chan", "asserteq", "asserttrue", "exec",
                  "readfile", "writefile", "jsonparse", "httpget", "guard", "loop", "todo", "new", "obj"],
    "expression": ["new", "obj", "struct"],
}
FORBIDDEN = {  # prefix -> contexts in which it must not appear
    "sout": ["toplevel", "class", "expression"], "fori": ["toplevel", "class", "expression"], "psvm": ["class", "statement", "expression"],
    "ctor": ["toplevel", "statement", "expression"], "class": ["class", "statement", "expression"], "field": ["toplevel", "statement"],
}
# IntelliJ live templates that a Java user types out of habit and that Novus can express; none exists yet (idea, not a bug).
MISSING_JAVA_TEMPLATES = ["ifn (== nothing)", "inn (!= nothing)", "ritar (reverse array loop)", "souf (formatted print)",
                          "sleep (time.sleep)", "trycatch (tryRun + tryFailed in a method body)", "lazy", "mn / mx (math.min / math.max)"]


def labels_in(client, path, template, prefix=""):
    source, line, character = at(template.replace("PREFIX", prefix))
    client.change(path, source)
    return client.complete(path, line, character)


def check_matrix(client, path, checks):
    offered = {}
    for context, template in CONTEXT_TEXTS.items():
        offered[context] = {i["label"] for i in labels_in(client, path, template) if i.get("kind") == 15}
    for context, prefixes in EXPECTED.items():
        for prefix in prefixes:
            checks.check("offered %s in %s" % (prefix, context), prefix in offered[context], "missing")
    for prefix, contexts in FORBIDDEN.items():
        for context in contexts:
            checks.check("%s not offered in %s" % (prefix, context), prefix not in offered[context], "offered")
    return offered


def check_positions(client, path, checks):
    negatives = {
        "after dot": "package main\n\nmethod main() {\n    var s = \"a\"\n    s.sout|\n}\n",
        "after dot, empty": "package main\n\nmethod main() {\n    var s = \"a\"\n    s.|\n}\n",
        "inside string": "package main\n\nmethod main() {\n    var s = \"sout|\"\n}\n",
        "inside interpolation": "package main\n\nmethod main() {\n    var s = \"${sout|}\"\n}\n",
        "line comment": "package main\n\nmethod main() {\n    // sout|\n}\n",
        "block comment": "package main\n\nmethod main() {\n    /* sout| */\n}\n",
        "import line": "package main\n\nimport sout|\n",
        "type slot": "package main\n\nmethod main() {\n    var x: sout|\n}\n",
        "if condition": "package main\n\nmethod main() {\n    if (sout|) {\n    }\n}\n",
        "method name slot": "package main\n\nmethod sout|\n",
        "annotation": "package main\n\n@sout|\nmethod x() {\n}\n",
    }
    for name, source in negatives.items():
        text, line, character = at(source)
        client.change(path, text)
        found = [i["label"] for i in client.complete(path, line, character) if i.get("kind") == 15]
        checks.check("no snippet %s" % name, found == [], str(found))
    positives = {
        "after a semicolon": "package main\n\nmethod main() {\n    var c = 2; sout|\n}\n",
        "inside one-line block": "package main\n\nmethod main() {\n    if (true) { sout| }\n}\n",
        "nested 3 deep": "package main\n\nmethod main() {\n    while (true) {\n        if (true) {\n            sout|\n        }\n    }\n}\n",
        "uppercase prefix": "package main\n\nmethod main() {\n    SOUT|\n}\n",
    }
    for name, source in positives.items():
        text, line, character = at(source)
        client.change(path, text)
        found = [i["label"] for i in client.complete(path, line, character) if i.get("kind") == 15]
        checks.check("snippet %s" % name, "sout" in found, str(found))


def check_ranking(client, path, checks):
    for prefix in ("sout", "soutv", "fori", "iter", "while", "ifelse", "thread", "sync", "import", "method"):
        context = "toplevel" if prefix in ("import", "method") else "statement"
        items = labels_in(client, path, CONTEXT_TEXTS[context], prefix)
        first = items[0]["label"] if items else None
        checks.check("exact prefix %s ranks first" % prefix, first == prefix, "first is %r" % first)
        snippets = [i for i in items if i["label"] == prefix and i.get("kind") != 9]
        checks.check("no duplicate label %s (keyword or built-in dropped)" % prefix, len(snippets) == 1, str([i["kind"] for i in snippets]))


def check_format(client, path, checks):
    items = labels_in(client, path, CONTEXT_TEXTS["statement"], "soutv")
    item = [i for i in items if i["label"] == "soutv"][0]
    checks.check("soutv is a snippet item", item.get("insertTextFormat") == 2 and item.get("kind") == 15, str(item))
    checks.check("soutv adjustIndentation announced", item.get("insertTextMode") == 2, str(item.get("insertTextMode")))
    checks.check("soutv escapes the interpolation dollar", "\\${" in item["textEdit"]["newText"], item["textEdit"]["newText"])
    checks.check("soutv expands to the Novus interpolation", expand(item["textEdit"]["newText"]) == 'println "value = ${value}"', expand(item["textEdit"]["newText"]))
    items = labels_in(client, path, CONTEXT_TEXTS["class"], "accessors")
    item = [i for i in items if i["label"] == "accessors"][0]
    checks.check("choice placeholder in accessors", "${3|get,get\\, set,set|}" in item["textEdit"]["newText"], item["textEdit"]["newText"])
    items = labels_in(client, path, CONTEXT_TEXTS["statement"], "thread")
    item = [i for i in items if i["label"] == "thread" and i.get("kind") == 15][0]
    checks.check("thread snippet carries the import edit", [e["newText"] for e in item.get("additionalTextEdits", [])] == ["import thread\n\n"], str(item.get("additionalTextEdits")))
    checks.check("snippet carries detail", bool(item.get("detail")), "no detail")
    checks.check("snippet ranges cover the typed prefix", item["textEdit"]["range"]["end"]["character"] - item["textEdit"]["range"]["start"]["character"] == len("thread"), str(item["textEdit"]["range"]))


def check_indentation(root, path, checks):
    nested = "package main\n\nmethod main() {\n    while (true) {\n        fori|\n    }\n}\n"
    client = Client(root)
    client.open(path, "package main\n")
    text, line, character = at(nested)
    client.change(path, text)
    item = [i for i in client.complete(path, line, character) if i["label"] == "fori" and i.get("kind") == 15][0]
    lines = item["textEdit"]["newText"].split("\n")
    checks.check("fori body is indented one level in the snippet", lines[2].startswith("    $0") and lines[1] == "while (${1} < ${2:count}) {", str(lines))
    checks.check("continuation lines carry no tab characters by default", "\t" not in item["textEdit"]["newText"], repr(item["textEdit"]["newText"]))
    client.close()
    tabbed = Client(root, options={"novus": {"snippets": {"indent": "\t"}}})
    tabbed.open(path, "package main\n")
    tabbed.change(path, text)
    item = [i for i in tabbed.complete(path, line, character) if i["label"] == "fori" and i.get("kind") == 15][0]
    checks.check("novus.snippets.indent = tab is honoured", "\n\t$0\n" in item["textEdit"]["newText"], repr(item["textEdit"]["newText"]))
    tabbed.close()
    # A tab-indented document with the default settings: the snippet should follow the document (F3).
    plain = Client(root)
    plain.open(path, "package main\n")
    source = "package main\n\nmethod main() {\n\twhile (true) {\n\t\tfori|\n\t}\n}\n"
    text, line, character = at(source)
    plain.change(path, text)
    item = [i for i in plain.complete(path, line, character) if i["label"] == "fori" and i.get("kind") == 15][0]
    checks.check("tab-indented document gets tab-indented snippet lines", "\n\t$0" in item["textEdit"]["newText"], repr(item["textEdit"]["newText"]), "F3-doc-style")
    source = "package main\r\n\r\nmethod main() {\r\n    fori|\r\n}\r\n"
    text, line, character = at(source)
    plain.change(path, text)
    item = [i for i in plain.complete(path, line, character) if i["label"] == "fori" and i.get("kind") == 15][0]
    checks.check("CRLF document gets CRLF in a multi-line snippet", "\r\n" in item["textEdit"]["newText"], repr(item["textEdit"]["newText"]), "F3-doc-style")
    checks.check("CRLF range ends at the typed prefix", item["textEdit"]["range"]["end"] == {"line": 3, "character": 8}, str(item["textEdit"]["range"]))
    plain.close()


def check_plain_fallback(root, path, checks):
    nested = "package main\n\nmethod main() {\n    while (true) {\n        fori|\n    }\n}\n"
    client = Client(root, snippets=False)
    client.open(path, "package main\n")
    text, line, character = at(nested)
    client.change(path, text)
    items = client.complete(path, line, character)
    item = [i for i in items if i["label"] == "fori" and i.get("kind") == 15][0]
    body = item["textEdit"]["newText"]
    checks.check("no-snippet client: plain text format", item.get("insertTextFormat") in (None, 1), str(item.get("insertTextFormat")))
    checks.check("no-snippet client: no snippet syntax left", "$" not in body.replace("\\$", ""), repr(body))
    checks.check("no-snippet client: defaults are written out", body.startswith("var i = 0\nwhile (i < count) {"), repr(body))
    baked = item.get("insertTextMode") == 2 or body.split("\n")[1].startswith("        ")
    checks.check("no-snippet client: continuation lines follow the cursor indentation", baked, repr(body), "F2-plain-indent")
    checks.check("no-snippet client: no whitespace-only line", all(l.strip() or not l for l in body.split("\n")[1:]) and "    \n" not in body, repr(body), "F4-plain-leftovers")
    other = [i for i in items if i["label"] == "soutv" and i.get("kind") == 15]
    checks.check("no-snippet client: every snippet is plain", all("${" not in i["textEdit"]["newText"].replace("\\${", "") for i in items if i.get("kind") == 15), "snippet syntax in plain item")
    client.close()
    bare = Client(root, extra_caps={"textDocument": {"completion": {}}})
    bare.open(path, "package main\n")
    bare.change(path, text)
    item = [i for i in bare.complete(path, line, character) if i["label"] == "fori" and i.get("kind") == 15][0]
    body = item["textEdit"]["newText"]
    baked = item.get("insertTextMode") == 2 or body.split("\n")[1].startswith("        ")
    checks.check("bare client: continuation lines follow the cursor indentation", baked, repr(body), "F2-plain-indent")
    bare.close()


def check_scopes(root, path, checks):
    client = Client(root)
    manifest = root + "/project.nv"
    client.open(manifest, "")
    for prefix in ("require", "replace", "lib", "project", "manifestfull"):
        source, line, character = at('project "x"\nversion "1.0"\n%s|' % prefix)
        client.change(manifest, source)
        found = [i["label"] for i in client.complete(manifest, line, character) if i.get("kind") == 15]
        checks.check("manifest snippet %s" % prefix, prefix in found, str(found))
    source, line, character = at('project "x"\nrequire "|')
    client.change(manifest, source)
    found = [i["label"] for i in client.complete(manifest, line, character) if i.get("kind") == 15]
    checks.check("no manifest snippets inside a string", found == [], str(found))
    component = root + "/comp.nvh"
    client.open(component, "")
    header = "<?nv\nprop string label = \"x\"\nref integer count = 0\n"
    template = "?>\n<div>\n    <span>{count}</span>\nTEMPLATE</div>\n"
    probes = {"island": (header + "pr|\n" + template.replace("TEMPLATE", ""), ["prop"], True),
              "template element": (header + template.replace("TEMPLATE", "    nvi|\n"), ["nvif", "nvfor"], False),
              "template tag": (header + template.replace("<span>", "<span |>").replace("TEMPLATE", ""), ["click", "bind"], False)}
    for name, (source, expected, ok_now) in probes.items():
        text, line, character = at(source)
        client.change(component, text)
        found = [i["label"] for i in client.complete(component, line, character) if i.get("kind") == 15]
        checks.check("nvh %s offers %s" % (name, expected), all(e in found for e in expected), str(found), None if ok_now else "F1-nvh-scopes")
    text, line, character = at(header + template.replace("TEMPLATE", "    comp|\n"))
    client.change(component, text)
    found = [i["label"] for i in client.complete(component, line, character) if i.get("kind") == 15]
    checks.check("file-start snippet component is not offered inside a template", "component" not in found, str(found), "F1-nvh-scopes")
    client.close()


def main():
    root = make_project({"main.nv": "", "project.nv": MANIFEST, "comp.nvh": ""})
    path = root + "/main.nv"
    checks = Checks("a_snippets")
    client = Client(root)
    client.open(path, "package main\n")
    check_matrix(client, path, checks)
    check_positions(client, path, checks)
    check_ranking(client, path, checks)
    check_format(client, path, checks)
    client.close()
    check_indentation(root, path, checks)
    check_plain_fallback(root, path, checks)
    check_scopes(root, path, checks)
    print("Java live templates without a Novus snippet (ideas): " + "; ".join(MISSING_JAVA_TEMPLATES))
    exit_with(checks)


if __name__ == "__main__":
    main()
