"""E2E (A): every snippet prefix, typed in its context, expands through the protocol to code that passes `novusc check`.

Usage: python3 test/lsp/e2e/a_expand.py [--show-ok]
The expansion is what a client does: the textEdit, the additionalTextEdits (imports), tabstops replaced by their default
text, continuation lines indented like the cursor line when the item asks for adjustIndentation. The verification
wrapper declares the statement prelude of DESIGN 7.5, so defaults such as `condition` and `items` resolve.
Exit code 1 when a snippet does not compile in the context where the server offers it, except the entries of NEEDS_FIXTURE
(their default text names a type or a file that only the fixtures of scripts/lsp_snippets.sh declare).
"""
import concurrent.futures
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from lspclient import Client, NOVUSC, apply_edits, at, make_project  # noqa: E402
from checks import Checks  # noqa: E402
from snippet import expand  # noqa: E402

PRELUDE = ["var items = [1, 2]", "var names = {\"a\": 1}", "var count = 3", "var text = \"t\"", "var done = false",
           "var condition = true", "var value = 1", "var tasks = []", "var lock = 0", "var task = 0", "var parsed = {}"]
HEAD = "package main\n\nmethod work() {\n}\n\ndefine class Point {\n    integer x\n    integer y\n}\n\n"
STATEMENT = HEAD + "method main(array<string> args) {\n" + "".join("    %s\n" % line for line in PRELUDE) + "    PREFIX|\n}\n"
EXPRESSION = HEAD + "method main(array<string> args) {\n" + "".join("    %s\n" % line for line in PRELUDE) + "    var made = PREFIX|\n}\n"
TOPLEVEL = "package main\n\nPREFIX|\n\nmethod main() {\n}\n"
CLASS = "package main\n\ndefine class Box {\n    integer width\n    string label\n\n    PREFIX|\n}\n\nmethod main() {\n}\n"
CONTEXTS = {"toplevel": TOPLEVEL, "class": CLASS, "statement": STATEMENT, "expression": EXPRESSION}


def adjust(text, indent):
    lines = text.split("\n")
    return "\n".join([lines[0]] + [(indent + line) if line.strip() else line for line in lines[1:]])


def line_indent(text, line):
    row = text.split("\n")[line]
    return row[:len(row) - len(row.lstrip())]


def run_check(directory):
    result = subprocess.run([NOVUSC, "check", "main.nv"], cwd=directory, capture_output=True, text=True, timeout=120)
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def expansions(client, path, template, prefix, results):
    source, line, character = at(template.replace("PREFIX", prefix))
    client.change(path, source)
    items = client.complete(path, line, character)
    exact = [i for i in items if i["label"] == prefix and i.get("kind") == 15]
    if not exact:
        results.append((prefix, None, "snippet not offered"))
        return
    item = exact[0]
    edit = item["textEdit"]
    body = expand(edit["newText"])
    if item.get("insertTextMode") == 2:
        body = adjust(body, line_indent(source, line))
    edits = [{"range": edit["range"], "newText": body}] + item.get("additionalTextEdits", [])
    results.append((prefix, apply_edits(source, edits), ""))


NEEDS_FIXTURE = {
    "toplevel": ["classb", "extends", "impl", "implements", "importf", "importfile", "importm", "importmod", "importn", "importnested"],
    "class": ["absm", "abstractm"],
    "statement": ["asserteq", "assertequal"],
}
ELSE_WORDS = ["else", "elif", "elseif"]


def run(checks, show_ok=False):
    root = make_project({"main.nv": "", "project.nv": 'project "x"\nversion "1.0"\nmain "main.nv"\n'})
    client = Client(root)
    path = root + "/main.nv"
    client.open(path, "package main\n")
    work = []
    for name, template in CONTEXTS.items():
        source, line, character = at(template.replace("PREFIX", ""))
        client.change(path, source)
        labels = [i["label"] for i in client.complete(path, line, character) if i.get("kind") == 15 and not i["label"].startswith("@")]
        results = []
        for prefix in labels:
            expansions(client, path, template, prefix, results)
        for prefix, text, error in results:
            if text is None:
                checks.check("expand %s/%s offered" % (name, prefix), False, error)
                continue
            directory = make_project({"main.nv": text, "project.nv": 'project "x"\nversion "1.0"\nmain "main.nv"\n'})
            work.append((name, prefix, directory, text))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        outcomes = list(pool.map(lambda w: run_check(w[2]), work))
    for (name, prefix, directory, text), (ok, message) in zip(work, outcomes):
        label = "expand %s/%s compiles" % (name, prefix)
        if prefix in NEEDS_FIXTURE.get(name, []):
            continue
        known = "F5-else-anywhere" if name == "statement" and prefix in ELSE_WORDS else None
        checks.check(label, ok, "%s\n--- %s\n%s" % (message, directory, text), known)
        if ok and show_ok:
            print("ok   %s/%s" % (name, prefix))
    client.close()
    return len(work)


def main():
    checks = Checks("a_expand")
    count = run(checks, "--show-ok" in sys.argv)
    print("%d snippet expansions checked" % count)
    return checks.finish()


if __name__ == "__main__":
    sys.exit(main())
