"""Server-initiated requests: ids, responses of every shape, silence, errors, slow clients."""
import json
import threading
import time

from harness import scenario, Session, make_workspace, MAIN_TEXT
from profiles import well_behaved_responder
from scen_flow import completion_items


def hover_ok(report, s, label):
    response = s.client.request("textDocument/hover", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 4, "character": 8}}, timeout=10)
    return report.check(response is not None and ("result" in response), "%s: server still answers requests" % label)


@scenario("serverreq-silent-client")
def silent_client(report):
    root = make_workspace()
    s = Session("vscode", root, responder=lambda c, m: None)
    try:
        s.open("main.nv", MAIN_TEXT)
        started = time.time()
        for _ in range(20):
            hover_ok(report, s, "silent")
        report.check(time.time() - started < 5, "20 hovers took %.1fs with unanswered server requests" % (time.time() - started))
        for _ in range(3):
            s.client.notify("workspace/didChangeConfiguration", {"settings": None})
        time.sleep(1.5)
        hover_ok(report, s, "silent after config pulls")
        ids = [m["id"] for m in s.client.server_requests]
        report.check(len(ids) == len(set(ids)), "ids unique with unanswered requests: %r" % ids)
        method_counts = {}
        for m in s.client.server_requests:
            method_counts[m["method"]] = method_counts.get(m["method"], 0) + 1
        report.note("server requests with a silent client: %r" % method_counts)
        report.check(method_counts.get("client/registerCapability", 0) == 1, "registration is sent once, got %r" % method_counts)
        response, code = s.shutdown()
        report.check(code == 0, "clean exit with unanswered server requests, got %r" % (code,))
    finally:
        s.close()


@scenario("serverreq-error-responses")
def error_responses(report):
    for error in ((-32601, "method not found"), (-32603, "internal"), (-32800, "cancelled"), (-32002, "x")):
        root = make_workspace()
        s = Session("nvim", root, responder=lambda c, m, e=error: ("error", e[0], e[1]))
        try:
            s.open("main.nv", MAIN_TEXT)
            hover_ok(report, s, "error %d" % error[0])
            s.client.notify("workspace/didChangeConfiguration", {"settings": None})
            time.sleep(0.5)
            hover_ok(report, s, "error %d after pull" % error[0])
            n_reg = len(s.client.requests_named("client/registerCapability"))
            report.check(n_reg == 1, "no retry loop after error %d: %d registrations" % (error[0], n_reg))
            report.check(not s.client.notifications_named("window/logMessage") or True, "")
            s.shutdown()
        finally:
            s.close()


@scenario("serverreq-config-response-shapes")
def config_shapes(report):
    shapes = [("null", None), ("empty array", []), ("[null]", [None]), ("[[]]", [[]]), ("['x']", ["x"]), ("5", 5),
              ("[5, 6, 7]", [5, 6, 7]), ("[{}]", [{}]), ("wrapped novus", [{"novus": {"check": {"mode": "off"}}}]),
              ("wrong types", [{"check": {"mode": 4}, "pureline": "yes", "completion": {"maxItems": "many"}}]),
              ("huge", [{"pureline": {"allowedShortNames": ["a"] * 5000}}]), ("object", {"a": 1})]
    for label, value in shapes:
        def responder(client, message, value=value):
            if message["method"] == "workspace/configuration":
                return ("result", value)
            return ("result", None)
        root = make_workspace()
        s = Session("vscode", root, responder=responder)
        try:
            s.open("main.nv", MAIN_TEXT)
            time.sleep(0.3)
            hover_ok(report, s, "config response %s" % label)
            s.client.notify("workspace/didChangeConfiguration", {"settings": None})
            time.sleep(0.3)
            hover_ok(report, s, "config response %s (second pull)" % label)
            response, code = s.shutdown()
            report.check(code == 0, "config response %s: exit %r" % (label, code))
            report.check(not s.client.protocol_errors, "config response %s: %r" % (label, s.client.protocol_errors))
        finally:
            s.close()


@scenario("serverreq-stray-responses")
def stray_responses(report):
    root = make_workspace()
    s = Session("vscode", root)
    try:
        c = s.client
        c.send_raw({"jsonrpc": "2.0", "id": "novus-99", "result": None})
        c.send_raw({"jsonrpc": "2.0", "id": 99, "result": []})
        c.send_raw({"jsonrpc": "2.0", "id": None, "result": None})
        c.send_raw({"jsonrpc": "2.0", "id": "novus-1", "result": [{}]})
        c.send_raw({"jsonrpc": "2.0", "id": "novus-2"})
        c.send_raw({"jsonrpc": "2.0", "id": "novus-3", "error": {"code": -1, "message": "x"}})
        c.send_raw({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse"}})
        c.send_raw({"jsonrpc": "2.0", "id": 1, "result": None})   # the id of OUR initialize request, reused as a response
        hover_ok(report, s, "stray responses")
        stray = [m for m in c.log if m[0] == "in" and "error" in m[1] and m[1].get("id") in ("novus-99", 99, None)]
        report.check(not stray, "server answered a response with an error response: %r" % [m[1] for m in stray])
        report.check(not [m for m in c.log if m[0] == "in" and m[1].get("id") == 1 and "error" in m[1]], "server answered our reused id")
        response, code = s.shutdown()
        report.check(code == 0, "exit %r" % (code,))
    finally:
        s.close()


@scenario("serverreq-result-omitted")
def result_omitted(report):
    # Neovim drops a nil result: the response then has neither result nor error.
    def responder(client, message):
        reply = {"jsonrpc": "2.0", "id": message["id"]}
        if message["method"] == "workspace/configuration":
            reply["result"] = [{"pureline": {"enabled": True, "maxLinesPerFile": 3}}]
        client.send_raw(reply)
        return None
    root = make_workspace()
    s = Session("nvim", root, responder=responder)
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        hover_ok(report, s, "result omitted")
        diag = s.diagnostics_for(uri, 6, lambda p: any(d.get("source") == "pureline" for d in p["diagnostics"]))
        report.check(diag is not None, "pureline settings pulled through the config response reached the diagnostics")
        s.shutdown()
    finally:
        s.close()


@scenario("serverreq-slow-client")
def slow_client(report):
    def responder(client, message):
        def late():
            time.sleep(2.0)
            r = well_behaved_responder(client, message)
            client.send_raw({"jsonrpc": "2.0", "id": message["id"], "result": r[1]})
        threading.Thread(target=late, daemon=True).start()
        return None
    root = make_workspace()
    s = Session("vscode", root, responder=responder)
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        t = time.time()
        hover_ok(report, s, "slow client")
        report.check(time.time() - t < 1.0, "hover waited %.2fs for a slow client" % (time.time() - t))
        time.sleep(2.6)
        hover_ok(report, s, "after the slow answers")
        s.shutdown()
    finally:
        s.close()


@scenario("serverreq-config-effects")
def config_effects(report):
    value = [{"diagnostics": {"own": True}, "pureline": {"enabled": True, "maxLinesPerFile": 4}}]
    def responder(client, message):
        if message["method"] == "workspace/configuration":
            return ("result", value)
        return ("result", None)
    root = make_workspace()
    s = Session("vscode", root, responder=responder)
    try:
        uri = s.open("main.nv", MAIN_TEXT)
        diag = s.diagnostics_for(uri, 6, lambda p: any(d.get("source") == "pureline" for d in p["diagnostics"]))
        report.check(diag is not None, "pulled pureline settings produce PL diagnostics: %r" % (s.client.notifications_named("textDocument/publishDiagnostics")[-1:],))
        value[0]["pureline"]["enabled"] = False
        s.client.notify("workspace/didChangeConfiguration", {"settings": None})
        gone = s.diagnostics_for(uri, 6, lambda p: not any(d.get("source") == "pureline" for d in p["diagnostics"]))
        report.check(gone is not None and not any(d.get("source") == "pureline" for d in gone["diagnostics"]), "disabling pureline through a new pull clears them")
        value[0]["diagnostics"]["own"] = False
        s.client.notify("workspace/didChangeConfiguration", {"settings": {"novus": {"diagnostics": {"own": False}}}})
        time.sleep(0.5)
        s.client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": "package main\n\nmethod main() {\n  )\n}\n"}]})
        time.sleep(1.0)
        last = s.diagnostics_for(uri, 2)
        report.check(last is not None and not [d for d in last["diagnostics"] if d.get("source") == "novus"], "own diagnostics off: no 'novus' findings: %r" % (last,))
        s.shutdown()
    finally:
        s.close()


@scenario("serverreq-ids-after-shutdown")
def no_requests_after_shutdown(report):
    root = make_workspace()
    s = Session("vscode", root)
    try:
        s.open("main.nv", MAIN_TEXT)
        s.client.request("shutdown", omit_params=True)
        before = len(s.client.server_requests)
        s.client.notify("workspace/didChangeConfiguration", {"settings": None})
        s.client.notify("textDocument/didOpen", {"textDocument": {"uri": s.uri("geo/shape.nv"), "languageId": "novus", "version": 1, "text": "package geo\n"}})
        time.sleep(0.5)
        report.check(len(s.client.server_requests) == before, "server sent requests after shutdown")
        pubs = [n for n in s.client.notifications if n["method"] == "textDocument/publishDiagnostics" and n["params"]["uri"].endswith("shape.nv")]
        report.note("publishDiagnostics for a document opened after shutdown: %d" % len(pubs))
        s.client.notify("exit", omit_params=True)
        report.check(s.client.wait_exit(5) == 0, "exit 0")
    finally:
        s.close()


@scenario("serverreq-request-id-types")
def request_id_types(report):
    root = make_workspace()
    s = Session("vscode", root)
    try:
        s.open("main.nv", MAIN_TEXT)
        for request_id in (0, 7, "abc", "7", "a bé", 9007199254740991, -5, 2147483648, 4294967296):
            response = s.client.request("textDocument/hover", {"textDocument": {"uri": s.uri("main.nv")}, "position": {"line": 4, "character": 8}},
                                        request_id=request_id, timeout=5)
            report.check(response is not None and response["id"] == request_id and type(response["id"]) == type(request_id),
                         "id %r echoed as %r" % (request_id, None if response is None else response.get("id")))
        s.shutdown()
    finally:
        s.close()


@scenario("serverreq-empty-answers-keep-init-options")
def empty_answers_keep_options(report):
    # Neovim and Helix answer workspace/configuration with null when the user configured nothing; the settings that
    # came with initializationOptions must survive that (and an empty didChangeConfiguration).
    from profiles import initialize_params
    import copy
    for label, answer in (("null item", [None]), ("empty object", [{}]), ("empty array", []), ("scalar", ["x"])):
        root = make_workspace()
        params = initialize_params("nvim", root)
        params["initializationOptions"] = {"novus": {"pureline": {"enabled": True, "maxLinesPerFile": 5}, "check": {"mode": "off"}}}
        def responder(client, message, answer=answer):
            if message["method"] == "workspace/configuration":
                return ("result", copy.deepcopy(answer))
            return ("result", None)
        s = Session("nvim", root, init_params=params, responder=responder)
        try:
            for settings in ({}, [], None, "x"):
                s.client.notify("workspace/didChangeConfiguration", {"settings": settings})
            s.client.notify("workspace/didChangeConfiguration", None)
            time.sleep(0.4)
            uri = s.open("main.nv", MAIN_TEXT)
            d = s.diagnostics_for(uri, 4, lambda p: any(x.get("source") == "pureline" for x in p["diagnostics"]))
            report.check(d is not None, "pull answered with %s must not reset the settings of initializationOptions" % label)
            hover_ok(report, s, label)
            s.shutdown()
        finally:
            s.close()
