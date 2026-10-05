#!/usr/bin/env bash
# End-to-end checks of the user-facing features of novus-lsp through the protocol (Python 3, no packages needed):
#   a_expand.py       every snippet prefix typed in its context expands to code that passes `novusc check`
#   a_snippets.py     contexts, format (snippet / plain), ranking, indentation, tabs, CRLF, manifest and .nvh scopes
#   b_project.py      suggestions from the current project, auto-import edits, cache invalidation, .nvh components
#   c_dependencies.py suggestions from dependencies (fake $NOVUS_DEPS), manifest completion, fetch without network, std modules
#
#   test/lsp/e2e/run_all.sh            uses build/novus-lsp and build/novusc (NOVUS_LSP_BIN / NOVUSC override)
#
# A check that is expected to fail is named in KNOWN_BUGS (checks.py) and printed as "xfail"; the run fails on a check that
# fails without being listed and on a listed check that passes ("XPASS": remove the entry when the bug is fixed).
# Scratch projects go to $E2E_TMP (default: the system temporary directory); paths must not contain spaces.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
status=0
for script in a_expand a_snippets b_project c_dependencies; do
    echo "== $script"
    python3 "$HERE/$script.py" || status=1
done
rm -rf "$HERE/__pycache__"
exit "$status"
