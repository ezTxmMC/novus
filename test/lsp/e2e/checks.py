"""Tiny check registry for the e2e scripts.

check(name, ok, detail, known=None): `known` names an entry of KNOWN_BUGS; the check is then expected to fail (XFAIL,
does not fail the run) and an unexpected pass (XPASS) fails the run, so that the entry is removed together with the fix.
"""
import sys

KNOWN_BUGS = {
    "F1-nvh-scopes": "nvh_template / nvh_tag snippet scopes are never produced (lsp/features/completion/context.nv MarkupContextRule, lsp/features/candidates/request.nv CANDIDATES_SNIPPET_SCOPES)",
    "F2-plain-indent": "no insertTextMode and no baked-in indentation for clients without snippet or insertTextMode support (lsp/features/candidates/lexicalsources.nv snippetItem, completionWithoutSnippets)",
    "F3-doc-style": "snippet indentation and line ends do not follow the document (tabs, CRLF); only novus.snippets.indent decides",
    "F4-plain-leftovers": "plain-text fallback leaves a whitespace-only line where $0 was",
    "F5-else-anywhere": "else/elif/elseif snippets are offered where the previous statement is not an if block",
    "F8-rerun-modules": "novus.rerunChecks does not rebuild the module table, so an externally fetched dependency stays invisible (lsp/server/lifecycle/commands.nv rerunChecks)",
    "F9-empty-require": "require \" offers an empty-label item and echoes the typed text as 'required' (lsp/features/candidates/modulesources.nv moduleItems)",
    "F10-version-slot": "no completion for the version slot of require, nor for main/lib file names",
    "F11-empty-folder": "a dependency folder without .nv sources is offered as package folder",
    "F12-nvh-template": "no completion inside .nvh templates ({expr}, bind=, @click=, component tags and props); nv* runtime members leak into component members",
    "F13-literal-elements": "element type of an array/map literal is not inferred (for (x in [Circle(1.0)]) x. gives every builtin member)",
    "F14-unimported-package": "members after the qualifier of a project package that is not imported come without import edit (builtin members instead)",
    "F15-shadowed-field": "a field shadowed by a parameter is offered next to it under the same bare name",
    "F16-std-docs": "265 of 473 std functions have no documentation text and internal helpers are listed",
}


class Checks:
    def __init__(self, title):
        self.title = title
        self.passed = 0
        self.failed = []
        self.xfailed = []
        self.xpassed = []

    def check(self, name, ok, detail="", known=None):
        if known is not None:
            assert known in KNOWN_BUGS, known
            if ok:
                self.xpassed.append(name)
                print("XPASS %s (%s is fixed: remove it from KNOWN_BUGS and the check)" % (name, known))
                return
            self.xfailed.append(name)
            print("xfail %s [%s] %s" % (name, known, detail[:200]))
            return
        if ok:
            self.passed += 1
            return
        self.failed.append(name)
        print("FAIL  %s: %s" % (name, detail[:400]))

    def finish(self):
        print("%s: %d passed, %d failed, %d expected failures, %d unexpected passes" % (
            self.title, self.passed, len(self.failed), len(self.xfailed), len(self.xpassed)))
        return 1 if self.failed or self.xpassed else 0


def exit_with(*all_checks):
    code = 0
    for checks in all_checks:
        code |= checks.finish()
    sys.exit(code)
