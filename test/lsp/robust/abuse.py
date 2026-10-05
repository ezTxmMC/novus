#!/usr/bin/env python3
"""Protocol abuse, hostile workspaces and concurrency scenarios against the real novus-lsp.

usage: abuse.py --bin <novus-lsp> --work <scratch dir without spaces> [--only substring] [--list]

Every scenario starts its own server in its own workspace under --work, and ends with a health check: the
process is alive, answers a documentSymbol within 5 s and ends with exit code 0 after shutdown + exit.
The result per scenario is `ok` or `FAIL <why>`; the exit code is the number of failures.
"""
import argparse
import json
import os
import shutil
import signal
import stat
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lspclient import Server, uri_of, open_document  # noqa: E402

SAMPLE = "package main\n\nimport io\n\nmethod main() {\n    var point = 1 + 2\n    io.println(point)\n    return\n}\n"
MANIFEST = 'project "demo"\nversion "1.0"\nmain "main.nv"\n'
REQUEST_LIMIT = 5.0
SCENARIOS = []
HEAVY = {"huge_single_line_5mb_code", "huge_5mb_tokens_one_line", "huge_100k_lines", "huge_100k_lines_one_method", "scan_of_document_in_root_directory",
         "dependency_replace_to_huge_directory", "memory_edit_loop", "open_close_churn_memory", "reader_flood_without_reading_stdout"}


def scenario(function):
    SCENARIOS.append(function)
    return function


class Context:
    def __init__(self, binary, work, name):
        self.binary = binary
        self.root = os.path.join(work, name)
        shutil.rmtree(self.root, ignore_errors=True)
        os.makedirs(self.root)
        self.name = name
        self.servers = []
        self.problems = []

    def write(self, relative, text, mode="w"):
        path = os.path.join(self.root, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, mode) as handle:
            handle.write(text)
        return path

    def server(self, env=None, root=None, stderr=True, **kwargs):
        merged = {"TMPDIR": os.path.join(self.root, "..", "tmp-" + self.name)}
        os.makedirs(merged["TMPDIR"], exist_ok=True)
        merged.update(env or {})
        server = Server(self.binary, root or self.root, env=merged,
                        stderr_path=os.path.join(self.root, "..", self.name + ".stderr.log") if stderr else None, **kwargs)
        self.servers.append(server)
        return server

    def expect(self, condition, message):
        if not condition:
            self.problems.append(message)
        return condition

    def request_ok(self, server, method, params, label=None, limit=REQUEST_LIMIT):
        """Sends a request, expects an answer in time; JSON-RPC errors other than -32603 are acceptable."""
        started = time.time()
        response = server.request(method, params, limit)
        took = time.time() - started
        label = label or method
        if response is None:
            self.problems.append("%s: no answer within %.0f s (alive=%s)" % (label, limit, server.alive()))
            return None
        if "error" in response and response["error"].get("code") == -32603:
            self.problems.append("%s: internal error: %s" % (label, response["error"].get("message")))
        return response

    def health(self, server, uri=None):
        if not self.expect(server.alive(), "server process is gone (exit code %r)" % (server.proc.poll(),)):
            return
        uri = uri or uri_of(os.path.join(self.root, "health.nv"))
        open_document(server, uri, SAMPLE, 1)
        response = server.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, REQUEST_LIMIT)
        self.expect(response is not None and "result" in response, "health: documentSymbol not answered: %r" % (response,))
        code, _ = server.shutdown(10.0)
        self.expect(code == 0, "health: shutdown+exit ended with %r" % (code,))
        self.expect(not server.violations, "protocol violations: %s" % server.violations[:3])

    def finish(self):
        for server in self.servers:
            server.close()
        return self.problems


def initialized(ctx, **kwargs):
    server = ctx.server(**kwargs)
    response = server.initialize()
    ctx.expect(response is not None and "result" in response, "initialize not answered: %r" % (response,))
    return server


# ---- (b) protocol abuse -----------------------------------------------------------------------------------
@scenario
def before_initialize(ctx):
    server = ctx.server()
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    for method, params in [("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}}),
                           ("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}}),
                           ("workspace/symbol", {"query": "x"}), ("shutdownless/unknown", {}), ("textDocument/formatting", None)]:
        response = ctx.request_ok(server, method, params)
        ctx.expect(response is not None and response.get("error", {}).get("code") == -32002,
                   "%s before initialize should answer -32002, got %r" % (method, response))
    server.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 1, "text": SAMPLE}})
    server.notify("$/cancelRequest", {"id": 1})
    server.notify("workspace/didChangeConfiguration", {"settings": {}})
    response = server.initialize()
    ctx.expect(response is not None and "result" in response, "initialize after the early requests failed: %r" % (response,))
    ctx.health(server)


@scenario
def duplicate_initialize(ctx):
    server = initialized(ctx)
    for _ in range(3):
        response = ctx.request_ok(server, "initialize", {"processId": None, "rootUri": uri_of(ctx.root), "capabilities": {}})
        ctx.expect(response is not None, "duplicate initialize not answered")
    server.notify("initialized", {})
    server.notify("initialized", {})
    ctx.health(server)


@scenario
def change_for_unopened_document(ctx):
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "never_opened.nv"))
    server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": "package main\n"}]})
    server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3}, "contentChanges": [{"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 3}}, "text": "x"}]})
    server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    server.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
    server.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
    for method in ("textDocument/hover", "textDocument/completion", "textDocument/definition", "textDocument/documentSymbol", "textDocument/formatting",
                   "textDocument/semanticTokens/full", "textDocument/references", "textDocument/rename", "textDocument/foldingRange"):
        params = {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}, "context": {"includeDeclaration": True}, "newName": "x", "options": {"tabSize": 4, "insertSpaces": True}}
        ctx.request_ok(server, method, params)
    ctx.health(server)


@scenario
def wrong_versions_and_ranges(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    open_document(server, uri, SAMPLE, 5)
    zero = {"line": 0, "character": 0}
    cases = [
        {"textDocument": {"uri": uri, "version": 1}, "contentChanges": [{"text": "package main\n"}]},
        {"textDocument": {"uri": uri, "version": -7}, "contentChanges": [{"text": "package main\n"}]},
        {"textDocument": {"uri": uri, "version": 9223372036854775807}, "contentChanges": [{"text": "package main\n"}]},
        {"textDocument": {"uri": uri, "version": 1e30}, "contentChanges": [{"text": "package main\n"}]},
        {"textDocument": {"uri": uri, "version": "seven"}, "contentChanges": [{"text": "package main\n"}]},
        {"textDocument": {"uri": uri}, "contentChanges": [{"text": "package main\n"}]},
        {"textDocument": {"uri": uri, "version": 6}, "contentChanges": []},
        {"textDocument": {"uri": uri, "version": 7}, "contentChanges": [{"range": {"start": {"line": 99, "character": 0}, "end": {"line": 100, "character": 5}}, "text": "zz"}]},
        {"textDocument": {"uri": uri, "version": 8}, "contentChanges": [{"range": {"start": {"line": 3, "character": 0}, "end": {"line": 1, "character": 0}}, "text": "zz"}]},
        {"textDocument": {"uri": uri, "version": 9}, "contentChanges": [{"range": {"start": {"line": 0, "character": 10 ** 9}, "end": {"line": 0, "character": 10 ** 12}}, "text": "zz"}]},
        {"textDocument": {"uri": uri, "version": 10}, "contentChanges": [{"range": {"start": {"line": -1, "character": -1}, "end": {"line": 0, "character": 0}}, "text": "zz"}]},
        {"textDocument": {"uri": uri, "version": 11}, "contentChanges": [{"range": {"start": zero, "end": {"line": 0, "character": 3}}, "rangeLength": -5, "text": "\U0001F600"}]},
        {"textDocument": {"uri": uri, "version": 12}, "contentChanges": [{"range": {"start": {"line": 0, "character": 1}, "end": {"line": 0, "character": 2}}, "text": ""}]},
        {"textDocument": {"uri": uri, "version": 13}, "contentChanges": [{"range": {"start": zero}, "text": "a"}, {"text": 5}, {}, None, "x", [1]]},
        {"textDocument": {"uri": uri, "version": 14}, "contentChanges": "nope"},
        {"textDocument": "nope", "contentChanges": []},
        {"textDocument": None, "contentChanges": None},
        None, [], "x", 5,
    ]
    for case in cases:
        server.notify("textDocument/didChange", case)
    # a request in the middle of the garbage must still work
    ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": uri}})
    # a position in the middle of an emoji (UTF-16 surrogate pair)
    server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 20}, "contentChanges": [{"text": "package main\n// \U0001F600 \u4e2d\n"}]})
    for column in range(0, 12):
        ctx.request_ok(server, "textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 1, "character": column}}, "hover col %d" % column)
        ctx.request_ok(server, "textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 1, "character": column}}, "completion col %d" % column)
    ctx.health(server, uri)


@scenario
def burst_didchange_with_cancels(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    open_document(server, uri, SAMPLE, 1)
    last_request = None
    started = time.time()
    for version in range(2, 2002):
        text = SAMPLE + "// edit %d\n" % version
        server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": text}]})
        if version % 3 == 0:
            request_id = server.call("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}})
            if version % 6 == 0:
                server.notify("$/cancelRequest", {"id": request_id})
            last_request = request_id
        if version % 7 == 0:
            server.notify("$/cancelRequest", {"id": request_id if last_request else 9999999})
    response = server.wait(last_request, 20.0)
    took = time.time() - started
    ctx.expect(response is not None, "last completion of the burst was not answered in 20 s")
    ctx.expect(took < 10.0, "burst of 2000 didChange took %.1f s to drain (budget 10 s)" % took)
    final = ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": uri}})
    ctx.health(server, uri)


def timed_features(ctx, server, uri, label, limit=15.0):
    params_at = {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 5}}
    calls = [("textDocument/documentSymbol", {"textDocument": {"uri": uri}}), ("textDocument/foldingRange", {"textDocument": {"uri": uri}}),
             ("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}), ("textDocument/hover", params_at),
             ("textDocument/completion", params_at), ("textDocument/definition", params_at),
             ("textDocument/documentHighlight", params_at), ("textDocument/signatureHelp", params_at),
             ("textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}}),
             ("textDocument/references", dict(params_at, context={"includeDeclaration": True})),
             ("textDocument/prepareRename", params_at), ("textDocument/codeAction", {"textDocument": {"uri": uri}, "range": {"start": params_at["position"], "end": params_at["position"]}, "context": {"diagnostics": []}}),
             ("textDocument/documentLink", {"textDocument": {"uri": uri}})]
    for column in (7, 9):
        at_import = {"textDocument": {"uri": uri}, "position": {"line": 2, "character": column}}
        calls.append(("textDocument/completion", at_import))
        calls.append(("textDocument/definition", at_import))
        calls.append(("textDocument/hover", at_import))
    for method, params in calls:
        started = time.time()
        response = server.request(method, params, limit)
        took = time.time() - started
        if response is None:
            ctx.problems.append("%s: %s no answer within %.0f s (alive=%s)" % (label, method, limit, server.alive()))
            if not server.alive():
                return False
            continue
        if "error" in response and response["error"].get("code") == -32603:
            ctx.problems.append("%s: %s internal error: %s" % (label, method, response["error"].get("message")))
        if took > REQUEST_LIMIT:
            ctx.problems.append("%s: %s took %.1f s (> %.0f s)" % (label, method, took, REQUEST_LIMIT))
    return True


def open_big(ctx, name, text, label):
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, name))
    started = time.time()
    open_document(server, uri, text, 1)
    first = server.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, 30.0)
    ctx.expect(first is not None, "%s: first request after didOpen not answered in 30 s" % label)
    ctx.expect(time.time() - started < 10.0, "%s: didOpen+documentSymbol took %.1f s" % (label, time.time() - started))
    if first is not None:
        if timed_features(ctx, server, uri, label):
            ctx.health(server, uri)
    return server


@scenario
def huge_single_line_5mb(ctx):
    line = "package main\n\nmethod main() {\n    var s = \"" + "x" * (5 * 1024 * 1024) + "\"\n    return\n}\n"
    open_big(ctx, "big.nv", line, "5 MB string literal")


@scenario
def huge_single_line_5mb_code(ctx):
    line = "package main\n\nmethod main() {\n    var s = 1" + " + 1" * (1250000) + "\n    return\n}\n"
    open_big(ctx, "bigexpr.nv", line, "5 MB binary expression")


@scenario
def huge_5mb_tokens_one_line(ctx):
    line = "package main\nmethod a() { " + "a = b; " * 700000 + "}\n"
    open_big(ctx, "bigtokens.nv", line, "5 MB of statements on one line")


@scenario
def huge_100k_lines(ctx):
    body = "".join("method m%d(int a): int {\n    var b = a + %d\n    return b\n}\n" % (i, i) for i in range(25000))
    open_big(ctx, "lines.nv", "package main\n\n" + body, "100k lines, 25000 methods")


@scenario
def huge_100k_lines_one_method(ctx):
    body = "".join("    var v%d = %d\n" % (i, i) for i in range(100000))
    open_big(ctx, "lines2.nv", "package main\n\nmethod main() {\n" + body + "}\n", "100k lines in one method")


NESTED = {
    "1000 parens": lambda n: "(" * n + "1" + ")" * n,
    "1000 brackets": lambda n: "[" * n + "1" + "]" * n,
    "1000 unary": lambda n: "!" * n + "true",
    "1000 negations": lambda n: "-" * n + "1",
    "1000 calls": lambda n: "f(" * n + "1" + ")" * n,
    "1000 member chain": lambda n: "a" + ".b" * n,
    "1000 index chain": lambda n: "a" + "[0]" * n,
    "1000 call chain": lambda n: "a" + ".b()" * n,
    "1000 lambdas": lambda n: "lambda () => " * n + "1",
    "1000 ternary": lambda n: "true ? 1 : " * n + "2",
    "1000 maps": lambda n: "{\"a\": " * n + "1" + "}" * n,
    "1000 casts": lambda n: "(int) " * n + "1",
    "1000 strings interp": lambda n: "\"${" * n + "1" + "}\"" * n,
}


@scenario
def deeply_nested_expressions(ctx):
    server = initialized(ctx)
    for index, (label, build) in enumerate(NESTED.items()):
        text = "package main\n\nmethod main() {\n    var x = " + build(1000) + "\n    return\n}\n"
        uri = uri_of(ctx.write("nest%d.nv" % index, text))
        open_document(server, uri, text, 1)
        if not server.alive():
            ctx.problems.append("%s: server died on didOpen" % label)
            return
        if not timed_features(ctx, server, uri, label):
            ctx.problems.append("%s: server died" % label)
            return
    ctx.health(server)


@scenario
def deeply_nested_statements(ctx):
    server = initialized(ctx)
    shapes = {
        "1000 blocks": ("{\n" * 1000, "}\n" * 1000),
        "1000 ifs": ("if (true) {\n" * 1000, "}\n" * 1000),
        "1000 whiles": ("while (true) {\n" * 1000, "}\n" * 1000),
        "1000 fors": ("for (var i = 0; i < 1; i = i + 1) {\n" * 1000, "}\n" * 1000),
        "1000 else-if": ("if (a) {\n} else " * 1000, "{\n}\n"),
        "1000 try": ("try {\n" * 1000, "} catch (e) {\n}\n" * 1000),
        "1000 match": ("match x {\n  1 => {\n" * 1000, "}\n}\n" * 1000),
    }
    for index, (label, (head, tail)) in enumerate(shapes.items()):
        text = "package main\n\nmethod main() {\n" + head + "    return\n" + tail + "}\n"
        uri = uri_of(ctx.write("stmt%d.nv" % index, text))
        open_document(server, uri, text, 1)
        if not timed_features(ctx, server, uri, label):
            ctx.problems.append("%s: server died" % label)
            return
    nested_class = "".join("define class C%d {\n" % i for i in range(1000)) + "}\n" * 1000
    uri = uri_of(ctx.write("classes.nv", "package main\n" + nested_class))
    open_document(server, uri, "package main\n" + nested_class, 1)
    timed_features(ctx, server, uri, "1000 nested classes")
    ctx.health(server)


@scenario
def deeply_nested_markup(ctx):
    server = initialized(ctx)
    text = "package main\n\nmethod page() {\n    return " + "<div>" * 1000 + "x" + "</div>" * 1000 + "\n}\n"
    uri = uri_of(ctx.write("page.nvh", text))
    open_document(server, uri, text, 1, "nvh")
    timed_features(ctx, server, uri, "1000 nested tags")
    text2 = "<" * 5000 + "{" * 5000 + "@" * 100
    uri2 = uri_of(ctx.write("page2.nvh", text2))
    open_document(server, uri2, text2, 1, "nvh")
    timed_features(ctx, server, uri2, "nvh junk")
    ctx.health(server)


@scenario
def symlink_cycle_in_workspace(ctx):
    ctx.write("project.nv", MANIFEST)
    ctx.write("main.nv", SAMPLE)
    ctx.write("a/inner.nv", "package a\n\nmethod inner() {\n    return\n}\n")
    os.symlink(ctx.root, os.path.join(ctx.root, "a", "loop"))
    os.symlink("../b", os.path.join(ctx.root, "a", "b1"))
    os.makedirs(os.path.join(ctx.root, "b"), exist_ok=True)
    os.symlink("../a", os.path.join(ctx.root, "b", "a1"))
    os.symlink(os.path.join(ctx.root, "nowhere"), os.path.join(ctx.root, "dangling.nv"))
    os.symlink("main.nv", os.path.join(ctx.root, "self_link.nv"))
    os.symlink("/", os.path.join(ctx.root, "rootlink"))
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    timed_features(ctx, server, uri, "symlink cycle")
    response = ctx.request_ok(server, "workspace/symbol", {"query": "inner"})
    ctx.expect(response is not None and "result" in response, "workspace/symbol in a symlink cycle workspace")
    ctx.health(server, uri)


@scenario
def unreadable_directory_and_files(ctx):
    ctx.write("project.nv", MANIFEST)
    ctx.write("main.nv", SAMPLE)
    ctx.write("secret/hidden.nv", "package secret\n")
    ctx.write("noread.nv", "package main\n")
    ctx.write("pkg/unreadable.nv", "package pkg\n")
    os.chmod(os.path.join(ctx.root, "secret"), 0)
    os.chmod(os.path.join(ctx.root, "noread.nv"), 0)
    os.chmod(os.path.join(ctx.root, "pkg"), stat.S_IRUSR | stat.S_IWUSR)  # no x: listable but not enterable
    try:
        server = initialized(ctx)
        uri = uri_of(os.path.join(ctx.root, "main.nv"))
        open_document(server, uri, SAMPLE, 1)
        timed_features(ctx, server, uri, "unreadable directory")
        unreadable = uri_of(os.path.join(ctx.root, "noread.nv"))
        ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": unreadable}})
        ctx.request_ok(server, "textDocument/hover", {"textDocument": {"uri": unreadable}, "position": {"line": 0, "character": 0}})
        ctx.request_ok(server, "workspace/symbol", {"query": "hidden"})
        ctx.health(server, uri)
    finally:
        for name in ("secret", "noread.nv", "pkg"):
            os.chmod(os.path.join(ctx.root, name), 0o755)


def dependency_workspace(ctx, manifest, deps_files=None, env_deps="auto"):
    ctx.write("project.nv", manifest)
    text = "package main\n\nimport geo\n\nmethod main() {\n    var far = distance(1.0, 2.0)\n    var p: Point = nothing\n    return\n}\n"
    ctx.write("main.nv", text)
    for relative, content in (deps_files or {}).items():
        ctx.write(os.path.join("deps", relative), content)
    env = {}
    if env_deps == "auto":
        env["NOVUS_DEPS"] = os.path.join(ctx.root, "deps")
    elif env_deps is not None:
        env["NOVUS_DEPS"] = env_deps
    server = initialized(ctx, env=env)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, text, 1)
    for column in (7, 20, 30):
        ctx.request_ok(server, "textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": column}}, "completion@%d" % column)
    ctx.request_ok(server, "textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 16}})
    ctx.request_ok(server, "textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 16}})
    ctx.request_ok(server, "textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 8}})
    ctx.request_ok(server, "textDocument/codeAction", {"textDocument": {"uri": uri}, "range": {"start": {"line": 2, "character": 0}, "end": {"line": 2, "character": 10}}, "context": {"diagnostics": []}})
    ctx.request_ok(server, "workspace/symbol", {"query": "dist"})
    ctx.health(server, uri)
    return server


@scenario
def deps_missing(ctx):
    dependency_workspace(ctx, 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n')


@scenario
def deps_novus_deps_nowhere(ctx):
    dependency_workspace(ctx, 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n', env_deps="/nonexistent/dir/deps")


@scenario
def deps_novus_deps_is_a_file(ctx):
    ctx.write("afile", "x")
    dependency_workspace(ctx, 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n', env_deps=os.path.join(ctx.root, "afile"))


@scenario
def deps_novus_deps_empty_string(ctx):
    dependency_workspace(ctx, 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n', env_deps="")


@scenario
def deps_malformed_dependency_manifest(ctx):
    good = 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n'
    for index, bad in enumerate(['require\n', 'project "x\nversion', '\x00\x00\x00', 'replace "a" "../../../../../..//"\nrequire "github.com/acme/geo" "v1.0.0"\n',
                                 'require "github.com/acme/geo" "v1.0.0"\nrequire "github.com/acme/geo" "v1.0.0"\n',
                                 'project "a"\nrequire "github.com/acme/geo" "v1.0.0"\n' * 2000, '\xff\xfe' * 100]):
        ctx.name = "deps_malformed_dependency_manifest%d" % index
        dependency_workspace(ctx, good, {"github.com/acme/geo@v1.0.0/project.nv": bad, "github.com/acme/geo@v1.0.0/lib.nv": "method distance(float a, float b): float {\n return a\n}\ndefine class Point {\n float x\n}\n"})


@scenario
def deps_dependency_requires_itself_cycle(ctx):
    manifest = 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n'
    dep = 'project "github.com/acme/geo"\nversion "1.0"\nrequire "github.com/acme/geo" "v1.0.0"\nrequire "github.com/acme/other" "v1.0.0"\n'
    other = 'project "github.com/acme/other"\nversion "1.0"\nrequire "github.com/acme/geo" "v1.0.0"\n'
    dependency_workspace(ctx, manifest, {"github.com/acme/geo@v1.0.0/project.nv": dep, "github.com/acme/geo@v1.0.0/lib.nv": "method distance(float a, float b): float {\n return a\n}\n",
                                         "github.com/acme/other@v1.0.0/project.nv": other, "github.com/acme/other@v1.0.0/o.nv": "package other\n"})


@scenario
def deps_dependency_dir_is_symlink_cycle(ctx):
    manifest = 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\n'
    ctx.write("deps/github.com/acme/placeholder", "")
    os.symlink(os.path.join(ctx.root, "deps"), os.path.join(ctx.root, "deps", "github.com", "acme", "geo@v1.0.0"))
    dependency_workspace(ctx, manifest)


@scenario
def deps_replace_points_outside_and_to_files(ctx):
    manifest = 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\nreplace "github.com/acme/geo" "/etc"\nrequire "x/y" "v1"\nreplace "x/y" "/dev/null"\nreplace "z" "../../.."\nreplace "w" "/proc/self"\n'
    dependency_workspace(ctx, manifest)


@scenario
def manifest_garbage(ctx):
    for index, bad in enumerate(["", "\x00", "\xff\xfe\xfd", "project", 'project "' + "x" * 100000 + '"', "require " * 50000, "{" * 5000, '\r\n' * 20000, 'project "demo"\nmain "../../../../etc/passwd"\n', 'main "' + "a/" * 3000 + 'main.nv"\n']):
        ctx.name = "manifest_garbage%d" % index
        sub = Context(ctx.binary, os.path.dirname(ctx.root), ctx.name)
        sub.write("project.nv", bad)
        sub.write("main.nv", SAMPLE)
        server = initialized(sub)
        uri = uri_of(os.path.join(sub.root, "main.nv"))
        open_document(server, uri, SAMPLE, 1)
        mani = uri_of(os.path.join(sub.root, "project.nv"))
        open_document(server, mani, bad, 1)
        timed_features(sub, server, mani, "manifest garbage %d" % index)
        timed_features(sub, server, uri, "main with garbage manifest %d" % index)
        sub.health(server, uri)
        ctx.problems += ["#%d: %s" % (index, p) for p in sub.finish()]


@scenario
def root_uri_variants(ctx):
    for index, root_uri in enumerate([None, "", "not a uri", "file:///nonexistent/abc", "file://" + ctx.root + "/missing.nv", "http://example.com/x", "file:///", "file:///proc", "file://" + "/a" * 3000, 17, {"a": 1}]):
        server = ctx.server(root=ctx.root)
        server.send({"jsonrpc": "2.0", "id": 100 + index, "method": "initialize", "params": {"processId": None, "rootUri": root_uri, "rootPath": None, "workspaceFolders": [{"uri": root_uri, "name": "x"}, None, 3] if index % 2 else None, "capabilities": {}}})
        response = server.wait(100 + index, 20.0)
        ctx.expect(response is not None, "initialize with rootUri %r not answered in 20 s" % (root_uri,))
        server.notify("initialized", {})
        if response is not None:
            uri = uri_of(ctx.write("f%d.nv" % index, SAMPLE))
            open_document(server, uri, SAMPLE, 1)
            ctx.request_ok(server, "workspace/symbol", {"query": "m"}, "workspace/symbol, rootUri %r" % (root_uri,))
            ctx.request_ok(server, "textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 8}})
            code, _ = server.shutdown(10)
            ctx.expect(code == 0, "rootUri %r: exit code %r" % (root_uri, code))
        elif not server.alive():
            ctx.problems.append("rootUri %r killed the server" % (root_uri,))


@scenario
def uri_variants(ctx):
    server = initialized(ctx)
    uris = ["", "file:///", "file:///%", "file:///%zz", "file:///a%00b.nv", "file:///a b.nv", "untitled:Untitled-1", "http://x/y.nv", "file://host/share/x.nv", "FILE:///x.nv",
            "file:///" + "a" * 100000 + ".nv", "file:///%E4%B8%AD%E6%96%87.nv", "file:///%ff%fe.nv", "file:///x.nv?q=1#frag", "file:///C:/x.nv", "file:///" + ".." + "/.." * 500 + "/etc/passwd", None, 5, [], {}]
    for uri in uris:
        server.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "novus", "version": 1, "text": SAMPLE}})
        for method in ("textDocument/documentSymbol", "textDocument/hover", "textDocument/definition", "textDocument/formatting", "textDocument/completion"):
            ctx.request_ok(server, method, {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}, "options": {"tabSize": 4, "insertSpaces": True}}, "%s uri=%r" % (method, str(uri)[:40]))
        server.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
    ctx.health(server)


METHODS = ["textDocument/completion", "completionItem/resolve", "textDocument/hover", "textDocument/signatureHelp", "textDocument/definition", "textDocument/references",
           "textDocument/documentHighlight", "textDocument/documentSymbol", "workspace/symbol", "textDocument/foldingRange", "textDocument/documentLink",
           "textDocument/rename", "textDocument/prepareRename", "textDocument/formatting", "textDocument/rangeFormatting", "textDocument/codeAction",
           "textDocument/semanticTokens/full", "textDocument/semanticTokens/range", "workspace/executeCommand", "shutdownx", "workspace/didChangeConfiguration",
           "workspace/didChangeWatchedFiles", "workspace/didChangeWorkspaceFolders", "textDocument/didOpen", "textDocument/didChange", "textDocument/didSave", "textDocument/didClose", "$/cancelRequest", "$/setTrace"]
BAD_PARAMS = [None, [], {}, "x", 5, True, {"textDocument": None}, {"textDocument": 5}, {"textDocument": {"uri": 5}}, {"textDocument": {"uri": "file:///x.nv"}, "position": None},
              {"textDocument": {"uri": "file:///x.nv"}, "position": "1:2"}, {"textDocument": {"uri": "file:///x.nv"}, "position": {"line": "1", "character": "2"}},
              {"textDocument": {"uri": "file:///x.nv"}, "position": {"line": -1, "character": -1}}, {"textDocument": {"uri": "file:///x.nv"}, "position": {"line": 1e300, "character": 1e300}},
              {"textDocument": {"uri": "file:///x.nv"}, "position": {"line": 9223372036854775807, "character": 9223372036854775807}},
              {"textDocument": {"uri": "file:///x.nv"}, "position": {"line": 1.5, "character": 2.5}}, {"textDocument": {"uri": "file:///x.nv"}, "range": {"start": {"line": 5, "character": 0}, "end": {"line": 1, "character": 0}}},
              {"textDocument": {"uri": "file:///x.nv"}, "range": None, "options": None, "newName": None, "context": None}, {"query": None}, {"query": 5}, {"query": "x" * 100000},
              {"command": None}, {"command": "nope", "arguments": 5}, {"command": "novus.fetchDependencies", "arguments": [None, 5, {}]}, {"command": "novus.rerunChecks", "arguments": "x"},
              {"textDocument": {"uri": "file:///x.nv"}, "newName": "", "position": {"line": 0, "character": 0}}, {"textDocument": {"uri": "file:///x.nv"}, "newName": "a b c\n!", "position": {"line": 0, "character": 8}},
              {"textDocument": {"uri": "file:///x.nv"}, "newName": 5, "position": {"line": 0, "character": 8}}, {"changes": None, "settings": None}, {"changes": [None, 5, {"uri": 5, "type": "x"}]},
              {"event": {"added": None, "removed": [5]}}, {"id": None}, {"id": {}}, {"label": None}, {"label": 5, "data": 5}]


@scenario
def malformed_params_for_every_method(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("x.nv", SAMPLE))
    open_document(server, uri, SAMPLE, 1)
    answered = 0
    for method in METHODS:
        for params in BAD_PARAMS:
            if method == "textDocument/didClose" or method == "textDocument/didOpen":
                server.notify(method, params)
                continue
            if method == "$/cancelRequest":
                server.notify(method, params)
                continue
            response = ctx.request_ok(server, method, params, "%s params=%s" % (method, json.dumps(params)[:70]), limit=REQUEST_LIMIT)
            answered += response is not None
            if not server.alive():
                ctx.problems.append("server died on %s %s" % (method, json.dumps(params)[:100]))
                return
    ctx.health(server)


@scenario
def malformed_message_shapes(ctx):
    server = initialized(ctx)
    messages = [
        {"jsonrpc": "2.0", "id": None, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": 1.5, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": {"a": 1}, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": [], "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": True, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": "a" * 100000, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": 10 ** 30, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "2.0", "id": -5, "method": "textDocument/hover", "params": {}},
        {"jsonrpc": "1.0", "id": 2, "method": "textDocument/hover"},
        {"id": 3, "method": "textDocument/hover"},
        {"jsonrpc": "2.0", "id": 4, "method": 5},
        {"jsonrpc": "2.0", "id": 5, "method": ""},
        {"jsonrpc": "2.0", "id": 6},
        {"jsonrpc": "2.0", "id": 7, "result": 1},
        {"jsonrpc": "2.0", "id": 8, "error": {"code": 1, "message": "x"}},
        {"jsonrpc": "2.0", "id": 99999, "result": None},
        {"jsonrpc": "2.0", "method": "$/cancelRequest", "params": {"id": {"x": 1}}},
        [], [{"jsonrpc": "2.0", "id": 9, "method": "textDocument/hover"}], 5, "x", None, True,
    ]
    for message in messages:
        server.send(message)
    server.send_raw(b"Content-Length: 4\r\n\r\nnull")
    server.send_raw(b"Content-Length: 2\r\n\r\n{}")
    server.send_raw(b"Content-Length: 0\r\n\r\n")
    server.send_raw(b"Content-Type: application/vscode-jsonrpc; charset=utf-8\r\nContent-Length: 2\r\n\r\n{}")
    server.send_raw(b"content-length: 2\r\n\r\n{}")
    server.send_raw(b"Content-Length: 2\n\n{}")
    time.sleep(1.0)
    ctx.expect(server.alive(), "server died on malformed message shapes")
    ctx.health(server)


@scenario
def invalid_utf8_and_odd_json_bodies(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    bodies = [
        b'{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"' + uri.encode() + b'","languageId":"novus","version":1,"text":"package main\\n// \xff\xfe\xc0\xaf bad \xed\xa0\x80\\n"}}}',
        b'{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"' + uri.encode() + b'","languageId":"novus","version":2,"text":"package main\\n// \\ud800 lone \\udc00 surrogates \\u0000 nul\\n"}}}',
        b'{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"' + uri.encode() + b'","version":3},"contentChanges":[{"text":"\xf0\x9f\x98"}]}}',
        b'\xef\xbb\xbf{"jsonrpc":"2.0","id":501,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"' + uri.encode() + b'"}}}',
        b'{"jsonrpc":"2.0","id":502,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"' + uri.encode() + b'"}}} trailing',
        b'{"jsonrpc":"2.0","id":503,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"' + uri.encode() + b'"}}}{"x":1}',
        b'{"a":' * 5000, b"[" * 100000, b"{" * 100000, b'"' * 10, b"\x00" * 100, b"1e999999999", b'{"jsonrpc":"2.0","id":504,"method":"textDocument/hover","params":' + b'[' * 3000 + b']' * 3000 + b'}',
        b'{"jsonrpc":"2.0","id":505,"method":"textDocument/hover","params":' + b'{"a":' * 3000 + b'1' + b'}' * 3000 + b'}',
        b'{"jsonrpc":"2.0","id":506,"method":"textDocument/hover","params":{"textDocument":{"uri":"' + b"\\u00e9" * 200000 + b'"}}}',
        b'{"jsonrpc":"2.0","id":507,"id":508,"method":"a","method":"textDocument/hover"}',
    ]
    for body in bodies:
        server.send_raw(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        time.sleep(0.05)
        if not server.alive():
            ctx.problems.append("server died on body %r" % (body[:80],))
            return
    ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": uri}})
    ctx.health(server)


@scenario
def requests_after_shutdown(ctx):
    server = initialized(ctx)
    ctx.expect(server.request("shutdown") is not None, "shutdown not answered")
    for method in ("textDocument/hover", "shutdown", "initialize", "workspace/symbol"):
        response = server.request(method, {})
        ctx.expect(response is not None and response.get("error", {}).get("code") == -32600, "%s after shutdown should be -32600, got %r" % (method, response))
    server.notify("textDocument/didOpen", {"textDocument": {"uri": "file:///z.nv", "languageId": "novus", "version": 1, "text": SAMPLE}})
    server.notify("exit")
    try:
        code = server.proc.wait(10)
    except subprocess.TimeoutExpired:
        ctx.problems.append("no exit after shutdown+exit")
        return
    ctx.expect(code == 0, "exit code after shutdown+exit was %r" % code)


@scenario
def many_open_documents(ctx):
    server = initialized(ctx)
    ctx.write("project.nv", MANIFEST)
    for index in range(3000):
        open_document(server, uri_of(os.path.join(ctx.root, "d%d.nv" % index)), SAMPLE.replace("main", "main%d" % index), 1)
    started = time.time()
    response = ctx.request_ok(server, "workspace/symbol", {"query": "main"}, limit=20.0)
    ctx.expect(time.time() - started < REQUEST_LIMIT, "workspace/symbol with 3000 open documents took %.1f s" % (time.time() - started))
    ctx.request_ok(server, "textDocument/references", {"textDocument": {"uri": uri_of(os.path.join(ctx.root, "d1.nv"))}, "position": {"line": 5, "character": 8}, "context": {"includeDeclaration": True}}, limit=20.0)
    for index in range(3000):
        server.notify("textDocument/didClose", {"textDocument": {"uri": uri_of(os.path.join(ctx.root, "d%d.nv" % index))}})
    ctx.health(server)


@scenario
def many_files_in_workspace(ctx):
    for index in range(4000):
        ctx.write("pkg%d/f.nv" % (index % 40), "package pkg%d\n\nmethod fn%d(int a): int {\n    return a\n}\n" % (index % 40, index)) if index < 40 else ctx.write("pkg%d/f%d.nv" % (index % 40, index), "package pkg%d\n\nmethod fn%d(int a): int {\n    return a\n}\n" % (index % 40, index))
    ctx.write("project.nv", MANIFEST)
    ctx.write("main.nv", SAMPLE)
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    started = time.time()
    ctx.request_ok(server, "workspace/symbol", {"query": "fn1"}, limit=20.0)
    ctx.expect(time.time() - started < REQUEST_LIMIT, "workspace/symbol over 4000 files took %.1f s" % (time.time() - started))
    started = time.time()
    ctx.request_ok(server, "textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}}, limit=20.0)
    ctx.expect(time.time() - started < REQUEST_LIMIT, "completion over 4000 files took %.1f s" % (time.time() - started))
    ctx.health(server, uri)


# ---- (c) concurrency ------------------------------------------------------------------------------------------
SLOW_NOVUSC = """#!/bin/sh
# fake novusc: `check` sleeps (SLOW_SECONDS), reports one error per run; `deps` sleeps too
sleep ${SLOW_SECONDS:-3}
echo "ran $@" >> "$(dirname "$0")/runs.log"
if [ "$1" = "deps" ]; then echo "fetched 0 modules"; exit 0; fi
echo "error: $2:3: expected '}' but found 'SLOW'"
exit 1
"""


def slow_checker(ctx, seconds=3):
    path = ctx.write("fake-novusc.sh", SLOW_NOVUSC)
    os.chmod(path, 0o755)
    ctx.write("project.nv", MANIFEST)
    ctx.write("main.nv", SAMPLE)
    server = ctx.server(env={"SLOW_SECONDS": str(seconds)})
    response = server.initialize(options={"novus": {"check": {"novuscPath": path}}})
    ctx.expect(response is not None and "result" in response, "initialize")
    return server, path


@scenario
def didchange_storm_during_running_check(ctx):
    server, _ = slow_checker(ctx, 3)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    time.sleep(0.5)
    started = time.time()
    for version in range(2, 1502):
        server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": SAMPLE + "// %d\n" % version}]})
        if version % 100 == 0:
            server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    answer = ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": uri}})
    ctx.expect(time.time() - started < 5.0, "main loop was blocked %.1f s by a running check" % (time.time() - started))
    time.sleep(7)
    ctx.expect(len(server.drain_diagnostics()) > 0, "no diagnostics were published while checks ran")
    ctx.health(server, uri)


@scenario
def shutdown_during_running_check(ctx):
    server, path = slow_checker(ctx, 10)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    time.sleep(1.0)
    started = time.time()
    code, response = server.shutdown(10.0)
    ctx.expect(response is not None, "shutdown not answered during a running check")
    ctx.expect(code == 0, "exit code %r (None = still running after 10 s)" % (code,))
    ctx.expect(time.time() - started < 4.0, "shutdown+exit took %.1f s with a running 10 s check" % (time.time() - started))
    ctx.expect(not server.violations, "violations %s" % server.violations)


@scenario
def shutdown_during_running_deps_fetch(ctx):
    server, path = slow_checker(ctx, 10)
    response = ctx.request_ok(server, "workspace/executeCommand", {"command": "novus.fetchDependencies", "arguments": []})
    time.sleep(0.5)
    started = time.time()
    code, response = server.shutdown(10.0)
    ctx.expect(code == 0 and time.time() - started < 4.0, "shutdown during deps fetch: code %r after %.1f s" % (code, time.time() - started))


@scenario
def kill_checker_children_after_exit(ctx):
    server, path = slow_checker(ctx, 6)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    time.sleep(1.0)
    server.shutdown(10.0)
    time.sleep(0.5)
    left = subprocess.run(["pgrep", "-f", path], capture_output=True, text=True).stdout.split()
    # a sleeping fake novusc may live on until its sleep ends; that is a leak of a child process, report as a finding
    ctx.expect(not left, "fake novusc child process still running after the server ended: pids %s" % left)
    subprocess.run(["pkill", "-f", path])


@scenario
def stdin_closed_mid_frame(ctx):
    variants = {
        "mid-header": b"Content-Len",
        "header-no-body": b"Content-Length: 100\r\n\r\n",
        "mid-body": b'Content-Length: 100\r\n\r\n{"jsonrpc":"2.0","id":2,',
        "mid-body-large": b"Content-Length: 5000000\r\n\r\n" + b"x" * 1000000,
        "mid-crlf": b"Content-Length: 2\r\n\r",
        "after-body-garbage": b'Content-Length: 2\r\n\r\n{}garbage',
    }
    for name, tail in variants.items():
        server = initialized(ctx)
        server.send_raw(tail)
        server.proc.stdin.close()
        try:
            code = server.proc.wait(10)
        except subprocess.TimeoutExpired:
            ctx.problems.append("%s: server still running 10 s after stdin was closed" % name)
            server.kill()
            continue
        ctx.expect(code in (0, 1), "%s: exit code %r" % (name, code))
        ctx.expect(code != -11 and code != -6, "%s: crashed with signal, code %r" % (name, code))


@scenario
def stdin_closed_after_open_documents_with_check(ctx):
    server, path = slow_checker(ctx, 5)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    time.sleep(0.5)
    server.proc.stdin.close()
    started = time.time()
    try:
        code = server.proc.wait(10)
    except subprocess.TimeoutExpired:
        ctx.problems.append("server did not end within 10 s after stdin EOF during a check")
        return
    ctx.expect(time.time() - started < 4.0, "EOF with running check took %.1f s to end the server" % (time.time() - started))
    ctx.expect(code == 1, "exit code after EOF without shutdown should be 1, got %r" % code)
    subprocess.run(["pkill", "-f", path])


@scenario
def stdout_closed_while_server_writes(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    open_document(server, uri, SAMPLE, 1)
    server.proc.stdout.close()
    for _ in range(50):
        server.call("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        if not server.alive():
            break
    time.sleep(1.5)
    server.send({"jsonrpc": "2.0", "id": 9999, "method": "shutdown"})
    server.notify("exit")
    try:
        code = server.proc.wait(10)
    except subprocess.TimeoutExpired:
        ctx.problems.append("server did not end after stdout was closed and shutdown+exit sent (hang writing to a closed pipe?)")
        return
    ctx.expect(code not in (-11, -6), "server crashed with signal on closed stdout: %r" % code)


@scenario
def signals_term_and_int(ctx):
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        server = initialized(ctx)
        server.proc.send_signal(sig)
        time.sleep(0.5)
        ctx.expect(server.proc.poll() != -11, "signal %s made the server crash with SIGSEGV" % sig)
        server.kill()


@scenario
def cancel_storm_and_dead_ids(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    open_document(server, uri, SAMPLE, 1)
    ids = []
    for _ in range(3000):
        ids.append(server.call("textDocument/references", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}, "context": {"includeDeclaration": True}}))
        server.notify("$/cancelRequest", {"id": ids[-1]})
        server.notify("$/cancelRequest", {"id": ids[-1] + 100000})
    last = server.wait(ids[-1], 20.0)
    ctx.expect(last is not None, "last request after cancel storm not answered")
    for request_id in ids[-6:-1]:
        response = server.wait(request_id, 5.0)
        ctx.expect(response is not None, "request %s not answered" % request_id)
    ctx.health(server, uri)


@scenario
def reader_flood_without_reading_stdout(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.write("main.nv", SAMPLE))
    open_document(server, uri, SAMPLE, 1)
    # the client sends 20000 requests; its reader thread keeps up, the server must neither deadlock nor drop answers
    count = 20000
    ids = [server.call("textDocument/foldingRange", {"textDocument": {"uri": uri}}) for _ in range(count)]
    started = time.time()
    response = server.wait(ids[-1], 60.0)
    ctx.expect(response is not None, "20000 queued requests: last not answered in 60 s")
    ctx.expect(time.time() - started < 20, "draining 20000 queued requests took %.1f s" % (time.time() - started))
    time.sleep(2)
    missing = [i for i in ids if i not in server.answered_ids]
    ctx.expect(not missing, "%d of %d requests never answered (first %s)" % (len(missing), count, missing[:3]))
    ctx.health(server, uri)


def rss_mb(server):
    try:
        with open("/proc/%d/status" % server.proc.pid) as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return -1.0


@scenario
def settings_garbage(ctx):
    options_list = [None, 5, "x", [], {"novus": 5}, {"novus": {"check": 5}}, {"novus": {"check": {"novuscPath": 5}}}, {"novus": {"check": {"novuscPath": "/nonexistent/novusc"}}},
                    {"novus": {"check": {"novuscPath": "/bin/false"}}}, {"novus": {"check": {"novuscPath": "/bin/cat"}}}, {"novus": {"check": {"novuscPath": "/dev/null"}}},
                    {"novus": {"completion": {"maxItems": -5}}}, {"novus": {"completion": {"maxItems": 0}}}, {"novus": {"completion": {"maxItems": 10 ** 18}}}, {"novus": {"completion": {"maxItems": "many"}}},
                    {"novus": {"completion": {"snippets": "yes"}}}, {"novus": {"format": {"indent": -1}}}, {"novus": {"diagnostics": {"own": None, "pureline": [1]}}},
                    {"novus": {"check": {"novuscPath": "x" * 100000}}}, {"novus": {"check": {"debounceMs": -1, "onSave": 5}}}, {"novus": {"trace": {}}}]
    for index, options in enumerate(options_list):
        server = ctx.server()
        response = server.initialize(options=options)
        ctx.expect(response is not None and "result" in response, "initialize with options %s" % json.dumps(options)[:80])
        uri = uri_of(ctx.write("main.nv", SAMPLE))
        open_document(server, uri, SAMPLE, 1)
        server.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
        ctx.request_ok(server, "textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 10}}, "completion, options %s" % json.dumps(options)[:60])
        server.notify("workspace/didChangeConfiguration", {"settings": options})
        server.notify("workspace/didChangeConfiguration", {"settings": {"novus": {"completion": {"maxItems": 3}}}})
        ctx.request_ok(server, "textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}})
        code, _ = server.shutdown(10)
        ctx.expect(code == 0, "options %s: exit code %r" % (json.dumps(options)[:60], code))


@scenario
def watched_files_and_folders(ctx):
    ctx.write("project.nv", MANIFEST)
    ctx.write("main.nv", SAMPLE)
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, SAMPLE, 1)
    changes = []
    for index in range(5000):
        changes.append({"uri": uri_of(os.path.join(ctx.root, "gen%d.nv" % index)), "type": 1 + index % 3})
    changes += [{"uri": "garbage", "type": 1}, {"uri": None, "type": 9}, {"uri": uri_of(os.path.join(ctx.root, "project.nv")), "type": 2}, {"uri": uri_of(ctx.root), "type": 3}, {"uri": uri_of("/"), "type": 3}]
    server.notify("workspace/didChangeWatchedFiles", {"changes": changes})
    for _ in range(50):
        server.notify("workspace/didChangeWatchedFiles", {"changes": changes[:200]})
    for index in range(100):
        server.notify("workspace/didChangeWorkspaceFolders", {"event": {"added": [{"uri": uri_of(os.path.join(ctx.root, "f%d" % index)), "name": "f"}, {"uri": "bad", "name": 5}], "removed": [{"uri": uri_of(ctx.root), "name": "x"}]}})
    ctx.write("project.nv", "garbage {{{ \x00")
    server.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": uri_of(os.path.join(ctx.root, "project.nv")), "type": 2}]})
    timed_features(ctx, server, uri, "after watched-file flood")
    ctx.health(server, uri)


@scenario
def execute_command_variants(ctx):
    server, path = slow_checker(ctx, 1)
    for params in [{"command": "novus.fetchDependencies"}, {"command": "novus.fetchDependencies", "arguments": ["x", 5, None]}, {"command": "novus.rerunChecks"}, {"command": "novus.rerunChecks", "arguments": [{"uri": 5}]},
                   {"command": "novus.rerunChecks", "arguments": [uri_of(os.path.join(ctx.root, "main.nv"))]}, {"command": "unknown.command"}, {"command": ""}, {}]:
        for _ in range(3):
            ctx.request_ok(server, "workspace/executeCommand", params, "executeCommand %s" % json.dumps(params)[:70])
    for _ in range(100):
        server.call("workspace/executeCommand", {"command": "novus.rerunChecks"})
        server.call("workspace/executeCommand", {"command": "novus.fetchDependencies"})
    time.sleep(2)
    ctx.health(server)
    subprocess.run(["pkill", "-f", path])


@scenario
def memory_edit_loop(ctx):
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "big.nv"))
    base = "package main\n\n" + "".join("method m%d(int a): int {\n    var b = a + %d\n    return b\n}\n" % (i, i) for i in range(1500))
    open_document(server, uri, base, 1)
    ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": uri}})
    samples = []
    for round_ in range(6):
        for step in range(200):
            version = 2 + round_ * 200 + step
            text = base + "// edit %d\n" % version
            server.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": text}]})
            if step % 20 == 0:
                server.call("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}})
                server.call("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": 6, "character": 9}})
        ctx.request_ok(server, "textDocument/documentSymbol", {"textDocument": {"uri": uri}}, limit=20)
        samples.append(rss_mb(server))
    ctx.expect(samples[-1] < 400, "RSS after 1200 edits of a 90 KB document: %s MB (budget 150 MB for the repository)" % ["%.0f" % x for x in samples])
    ctx.expect(samples[-1] < samples[1] * 2.0 + 50, "RSS grows with the number of edits: %s MB" % ["%.0f" % x for x in samples])
    ctx.health(server, uri)


@scenario
def open_close_churn_memory(ctx):
    server = initialized(ctx)
    samples = []
    for round_ in range(5):
        for index in range(1000):
            uri = uri_of(os.path.join(ctx.root, "d%d.nv" % index))
            open_document(server, uri, SAMPLE * 20, 1)
            if index % 100 == 0:
                server.call("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
            server.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        ctx.request_ok(server, "workspace/symbol", {"query": "m"})
        samples.append(rss_mb(server))
    ctx.expect(samples[-1] < samples[0] * 2.0 + 50, "RSS grows with open/close churn: %s MB" % ["%.0f" % x for x in samples])
    ctx.health(server)


# ---- scenarios that document known defects (listed in test/lsp/robust/expected_failures.txt) ----------------
@scenario
def scan_of_document_in_root_directory(ctx):
    server = initialized(ctx)
    uri = "file:///robust_probe.nv"
    open_document(server, uri, SAMPLE, 1)
    response = ctx.request_ok(server, "workspace/symbol", {"query": "main"}, limit=REQUEST_LIMIT)
    ctx.expect(response is not None, "workspace/symbol with a document directly below / is blocked by an unbounded walk of the file system")
    ctx.health(server, uri_of(os.path.join(ctx.root, "health.nv")))


@scenario
def dependency_replace_to_huge_directory(ctx):
    root_relative = os.path.relpath("/", ctx.root)
    ctx.write("project.nv", 'project "demo"\nversion "1.0"\nmain "main.nv"\nrequire "github.com/acme/geo" "v1.0.0"\nreplace "github.com/acme/geo" "%s/usr"\n' % root_relative)
    text = "package main\n\nimport geo\n\nmethod main() {\n    var far = di\n}\n"
    ctx.write("main.nv", text)
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "main.nv"))
    open_document(server, uri, text, 1)
    response = ctx.request_ok(server, "textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 16}}, limit=REQUEST_LIMIT)
    ctx.expect(response is not None, "the first completion after `replace` to a large directory is blocked (the dependency index has no budget, G-23)")


@scenario
def semantic_tokens_scale_many_locals(ctx):
    text = "package main\n\nmethod main() {\n" + "".join("    var v%d = 1\n" % i for i in range(8000)) + "}\n"
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "locals.nv"))
    open_document(server, uri, text, 1)
    started = time.time()
    response = ctx.request_ok(server, "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}, limit=30.0)
    ctx.expect(response is not None and time.time() - started < REQUEST_LIMIT, "semanticTokens/full of 8000 locals in one method took %.1f s (quadratic in the locals of a callable)" % (time.time() - started))


@scenario
def semantic_tokens_scale_long_line(ctx):
    text = "package main\n\nmethod main() {\n    var s = 1" + " + 1" * 80000 + "\n}\n"
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "line.nv"))
    open_document(server, uri, text, 1)
    started = time.time()
    response = ctx.request_ok(server, "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}, limit=30.0)
    ctx.expect(response is not None and time.time() - started < REQUEST_LIMIT, "semanticTokens/full of one 320 KB line took %.1f s (quadratic in the line length)" % (time.time() - started))


@scenario
def semantic_tokens_scale_many_methods(ctx):
    text = "package main\n\n" + "".join("method m%d(int a): int {\n    var b = a + %d\n    return b\n}\n" % (i, i) for i in range(4000))
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "methods.nv"))
    open_document(server, uri, text, 1)
    started = time.time()
    response = ctx.request_ok(server, "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}, limit=30.0)
    ctx.expect(response is not None and time.time() - started < REQUEST_LIMIT, "semanticTokens/full of 4000 small methods took %.1f s (quadratic in the declarations)" % (time.time() - started))


@scenario
def directory_uri_as_document(ctx):
    server = initialized(ctx)
    uri = uri_of(ctx.root)
    open_document(server, uri, SAMPLE, 1)
    for method in ("hover", "definition", "references", "documentHighlight", "prepareRename", "rename", "semanticTokens/full"):
        params = {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}, "context": {"includeDeclaration": True}, "newName": "zz"}
        ctx.request_ok(server, "textDocument/" + method, params, "%s on a document whose uri is an existing directory" % method)


@scenario
def completion_items_have_labels_for_unterminated_require(ctx):
    text = 'project "demo"\nversion "0.1.0"\nmain "main.nv"\n\nrequire "'
    ctx.write("project.nv", text)
    server = initialized(ctx)
    uri = uri_of(os.path.join(ctx.root, "project.nv"))
    open_document(server, uri, text, 1)
    response = ctx.request_ok(server, "textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 9}})
    items = (response or {}).get("result", {}) or {}
    for item in (items.get("items") if isinstance(items, dict) else items) or []:
        ctx.expect(item.get("label"), "completion item with an empty label: %s" % json.dumps(item)[:140])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=False)
    parser.add_argument("--work", required=False)
    parser.add_argument("--only", default="")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--quick", action="store_true", help="skip the scenarios that take minutes (HEAVY)")
    parser.add_argument("--xfail", default="", help="file with the names of scenarios that are known to fail (one per line, # comments)")
    args = parser.parse_args()
    if args.list:
        for function in SCENARIOS:
            print(function.__name__)
        return 0
    if " " in (args.work or ""):
        print("--work must not contain spaces (project.nv)")
        return 2
    failures = 0
    expected = set()
    if args.xfail:
        with open(args.xfail) as handle:
            expected = {line.split("#")[0].strip() for line in handle if line.split("#")[0].strip()}
    for function in SCENARIOS:
        if args.only not in function.__name__:
            continue
        if args.quick and function.__name__ in HEAVY:
            continue
        ctx = Context(os.path.abspath(args.bin), os.path.abspath(args.work), function.__name__)
        started = time.time()
        try:
            function(ctx)
        except Exception as error:  # noqa: BLE001
            ctx.problems.append("scenario raised %s: %s" % (type(error).__name__, error))
        problems = ctx.finish()
        known = function.__name__ in expected
        verdict = "ok" if not problems else ("xfail" if known else "FAIL")
        if known and not problems:
            verdict = "XPASS (remove it from the xfail list)"
        print("%-48s %s (%.1fs)" % (function.__name__, verdict, time.time() - started), flush=True)
        for problem in problems[:12]:
            print("      - " + problem)
        if len(problems) > 12:
            print("      ... %d more" % (len(problems) - 12))
        failures += bool(problems) and not known
    return failures


if __name__ == "__main__":
    sys.exit(main())
