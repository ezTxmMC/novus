"""Defects found by the simulations that are not fixed yet. Each check runs `report.defect`: it is expected to fail
(listed in known_defects.txt); when the defect is fixed the check says so and the run fails until the entry is removed."""
import time

from harness import scenario, Session, make_workspace, MAIN_TEXT
from scen_flow import completion_items


@scenario("known-completion-vscode-command-for-other-clients")
def vscode_command_for_other_clients(report):
    text = "package main\n\nimport geo\n\nmethod main() {\n    var shape = Shape(\"a\", 1.5)\n    describe\n}\n"
    for editor in ("nvim", "helix"):
        root = make_workspace()
        s = Session(editor, root)
        try:
            commands = s.init_result["result"]["capabilities"]["executeCommandProvider"]["commands"]
            uri = s.open("main.nv", text)
            resp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 6, "character": 12}, "context": {"triggerKind": 1}})
            foreign = sorted({i["command"]["command"] for i in completion_items(resp) if i.get("command") and i["command"]["command"] not in commands})
            report.defect("D05-vscode-command", not foreign, "%s: completion items carry client commands the server does not list in executeCommandProvider: %r" % (editor, foreign))
            if foreign:
                answer = s.client.request("workspace/executeCommand", {"command": foreign[0], "arguments": []})
                report.note("%s: executing %s on the server answers %r" % (editor, foreign[0], (answer or {}).get("error")))
            s.shutdown()
        finally:
            s.close()


@scenario("known-check-failures-are-invisible")
def check_failures_invisible(report):
    # The user gets no message when the configured novusc does not exist or a check times out (only stderr).
    from scen_robust import session_with_check
    root = make_workspace()
    s = session_with_check(root, "/nonexistent/novusc")
    try:
        s.open("main.nv", MAIN_TEXT)
        time.sleep(1.0)
        messages = s.client.notifications_named("window/showMessage") + s.client.notifications_named("window/logMessage")
        report.defect("D07-silent-check-setup", len(messages) > 0, "a configured novusc path that does not exist is reported to the client (none of %d messages)" % len(messages))
        s.shutdown()
    finally:
        s.close()


@scenario("known-helix-documented-config-is-ignored")
def helix_config(report):
    # website/src/content/projects/editor.mdx documents `[language-server.novus-lsp.config] check = { mode = "onSave" }` for
    # Helix: Helix sends that table verbatim as initializationOptions, without the `novus` wrapper, and answers
    # workspace/configuration (section "novus") with null.
    from profiles import initialize_params
    root = make_workspace()
    params = initialize_params("helix", root)
    params["initializationOptions"] = {"pureline": {"enabled": True, "maxLinesPerFile": 5}}
    s = Session("helix", root, init_params=params, responder=lambda c, m: ("result", [None] if m["method"] == "workspace/configuration" else None))
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        found = s.diagnostics_for(uri, 3, lambda p: any(d.get("source") == "pureline" for d in p["diagnostics"]))
        report.defect("D09-unwrapped-init-options", found is not None, "initializationOptions without the 'novus' wrapper (the documented Helix setup) are applied")
        s.shutdown()
    finally:
        s.close()


@scenario("known-kinds-ignore-client-valueset")
def kinds_valueset(report):
    # LSP: without completionItemKind.valueSet / symbolKind.valueSet a client only knows the kinds of the first version
    # (completion 1-18, symbols 1-18); the server has to map the others (Struct 22, EnumMember 22, ...).
    def edit(caps):
        caps["textDocument"]["completion"].pop("completionItemKind", None)
        caps["textDocument"]["documentSymbol"].pop("symbolKind", None)
    root = make_workspace()
    s = Session("vscode", root, caps_edit=edit)
    try:
        uri = s.open("geo/shape.nv")
        resp = s.client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        kinds = set()
        def walk(items):
            for item in items:
                kinds.add(item["kind"])
                walk(item.get("children") or [])
        walk(resp.get("result") or [])
        report.defect("D10-kinds-valueset", max(kinds) <= 18, "symbol kinds %r for a client without symbolKind.valueSet" % sorted(kinds))
        main = s.open("main.nv", "package main\n\nimport geo\n\nmethod main() {\n    S\n}\n")
        comp = s.client.request("textDocument/completion", {"textDocument": {"uri": main}, "position": {"line": 5, "character": 5}})
        ckinds = sorted({i.get("kind") for i in completion_items(comp) if i.get("kind")})
        report.defect("D10-kinds-valueset", max(ckinds) <= 18, "completion kinds %r for a client without completionItemKind.valueSet" % ckinds)
        s.shutdown()
    finally:
        s.close()


@scenario("known-trigger-characters-in-expressions")
def trigger_characters(report):
    root = make_workspace()
    s = Session("vscode", root)
    try:
        for code, trigger, label in (("    var a = 4 /", "/", "division"), ('    var m = {"a":', ":", "map literal"), ("    var a = 1\n    if (a <", "<", "comparison")):
            text = "package main\n\nimport geo\n\nmethod main() {\n" + code + "\n}\n"
            lines = code.split("\n")
            uri = s.open("main.nv", text, version=len(label))
            resp = s.client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 4 + len(lines), "character": len(lines[-1])}, "context": {"triggerKind": 2, "triggerCharacter": trigger}})
            report.defect("D11-trigger-noise", len(completion_items(resp)) == 0, "typing '%s' in a %s opens a popup with %d unrelated items" % (trigger, label, len(completion_items(resp))))
        s.shutdown()
    finally:
        s.close()
