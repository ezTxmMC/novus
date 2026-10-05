"""Helpers of the parity driver: scratch workspaces, a session with convenience calls, the check registry.

A check is a function `fn(env) -> (ok, detail)`. `env` gives scratch workspaces (`env.session(files)`), the repository
root and the server binary. A check listed in expected_failing.txt is a known gap: it must fail, and a pass is reported
as XPASS so that the list is kept honest."""
import json
import os
import shutil
import tempfile
import time

from lspclient import Client, apply_edits, offset_to_pos, pos_of

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FIXTURES = os.path.join(REPO, "vscode-novus", "test", "fixtures")
OPTIONS_QUIET = {"novus": {"check": {"mode": "off"}}}

REGISTRY = []


def deep_merge(base, extra):
    out = json.loads(json.dumps(base))
    for key, value in extra.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = deep_merge(out[key], value)
        else:
            out[key] = value
    return out


def check(area, cid, title, oracle="TS"):
    """Registers a check; `oracle` says where the expectation comes from."""
    def wrap(fn):
        REGISTRY.append({"area": area, "id": area + "." + cid, "title": title, "oracle": oracle, "fn": fn})
        return fn
    return wrap


def mark(text, marker="|"):
    """Splits a text with one cursor marker into (text, line, character)."""
    idx = text.index(marker)
    src = text[:idx] + text[idx + len(marker):]
    line, ch = offset_to_pos(src, idx)
    return src, line, ch


class Session:
    """One server process on a scratch workspace."""

    def __init__(self, env, files, options=None, extra_env=None):
        self.env = env
        self.dir = tempfile.mkdtemp(prefix="parity", dir=env.scratch)
        for rel, text in files.items():
            path = os.path.join(self.dir, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
        merged = deep_merge(OPTIONS_QUIET, options or {})
        self.c = Client(env.binary, self.dir, env=extra_env, options=merged)
        self.files = dict(files)
        self.text = {}

    def open(self, rel, text=None):
        text = self.files[rel] if text is None else text
        self.text[rel] = text
        self.c.open(rel, text)
        return text

    def set(self, rel, text):
        self.text[rel] = text
        if self.c.uri_of(rel) in self.c.versions:
            self.c.change(rel, text)
        else:
            self.c.open(rel, text)

    def at(self, method, rel, line, ch, extra=None):
        return self.c.at(method, rel, line, ch, extra).get("result")

    def spot(self, rel, needle, occ=0, shift=0):
        return pos_of(self.text[rel], needle, occ, shift)

    def hover(self, rel, needle, occ=0, shift=0):
        res = self.at("textDocument/hover", rel, *self.spot(rel, needle, occ, shift))
        return res["contents"]["value"] if res else None

    def definition(self, rel, needle, occ=0, shift=0):
        res = self.at("textDocument/definition", rel, *self.spot(rel, needle, occ, shift)) or []
        return [(r["uri"].replace(self.c.uri + "/", ""), r["range"]["start"]["line"]) for r in res]

    def references(self, rel, needle, occ=0, shift=0, decl=True):
        extra = {"context": {"includeDeclaration": decl}}
        res = self.at("textDocument/references", rel, *self.spot(rel, needle, occ, shift), extra) or []
        return sorted((r["uri"].replace(self.c.uri + "/", ""), r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in res)

    def rename(self, rel, needle, new, occ=0, shift=0):
        res = self.at("textDocument/rename", rel, *self.spot(rel, needle, occ, shift), {"newName": new})
        if not res:
            return None
        return {k.replace(self.c.uri + "/", ""): v for k, v in res["changes"].items()}

    def complete(self, rel, marked, trigger=None):
        src, line, ch = mark(marked)
        self.set(rel, src)
        ctx = {"triggerKind": 2, "triggerCharacter": trigger} if trigger else {"triggerKind": 1}
        res = self.at("textDocument/completion", rel, line, ch, {"context": ctx})
        if res is None:
            return []
        return res["items"] if isinstance(res, dict) else res

    def labels(self, rel, marked, trigger=None):
        return [i["label"] for i in self.complete(rel, marked, trigger)]

    def signature(self, rel, marked):
        src, line, ch = mark(marked)
        self.set(rel, src)
        res = self.at("textDocument/signatureHelp", rel, line, ch)
        if not res:
            return None
        return res["activeSignature"], res["activeParameter"], [s["label"] for s in res["signatures"]]

    def diagnostics(self, rel, quiet=0.6):
        found = self.c.diagnostics(rel, quiet) or []
        return [(d["range"]["start"]["line"] + 1, d["code"], d["severity"], d["message"]) for d in found]

    def raw_diagnostics(self, rel, quiet=0.6):
        return self.c.diagnostics(rel, quiet) or []

    def symbols(self, rel):
        return self.c.call("textDocument/documentSymbol", rel).get("result")

    def folds(self, rel):
        res = self.c.call("textDocument/foldingRange", rel).get("result") or []
        return sorted((f["startLine"], f["endLine"], f.get("kind")) for f in res)

    def format(self, rel, text, options=None):
        self.set(rel, text)
        res = self.c.call("textDocument/formatting", rel, {"options": options or {"tabSize": 2, "insertSpaces": True}}).get("result")
        if res is None:
            return None
        return apply_edits(text, res)

    def actions(self, rel, diagnostic=None, only=None, whole=False):
        diags = [diagnostic] if diagnostic else []
        rng = diagnostic["range"] if diagnostic else {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}}
        context = {"diagnostics": diags}
        if only:
            context["only"] = only
        return self.c.call("textDocument/codeAction", rel, {"range": rng, "context": context}).get("result") or []

    def close(self):
        self.c.close()
        shutil.rmtree(self.dir, ignore_errors=True)


class Env:
    def __init__(self, binary):
        self.binary = binary
        self.scratch = tempfile.mkdtemp(prefix="parity-scratch")
        self.cache = {}

    def session(self, files, open_all=True, options=None, extra_env=None):
        ses = Session(self, files, options, extra_env)
        if open_all:
            for rel in files:
                if rel.endswith(".nv") or rel.endswith(".nvh"):
                    ses.open(rel)
        return ses

    def shared(self, key, files, options=None):
        """A session that many checks of one area reuse; the checks only read or re-set the main document."""
        if key not in self.cache:
            self.cache[key] = self.session(files, options=options)
        return self.cache[key]

    def cleanup(self):
        for ses in self.cache.values():
            ses.close()
        shutil.rmtree(self.scratch, ignore_errors=True)


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def features_text():
    """features.nv of the TS fixtures without the C-style for loop, which novusc rejects (a stale fixture)."""
    text = read(os.path.join(FIXTURES, "features.nv"))
    return text.replace("  for (var i = 0; i < 3; i = i + 1) {\n    println i\n  }\n", "")


def pkg_files():
    base = os.path.join(FIXTURES, "pkg")
    return {"main.nv": read(base + "/main.nv"), "shapes/shapes.nv": read(base + "/shapes/shapes.nv"), "helper.nv": read(base + "/helper.nv")}


def has_all(labels, wanted):
    missing = [w for w in wanted if w not in labels]
    return (not missing, "missing %s in %s" % (missing, labels[:30]))
