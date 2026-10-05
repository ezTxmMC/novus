#!/usr/bin/env bash
# Real-editor message sequences against novus-lsp in production mode (see run.py); NOVUS_LSP_BIN selects the binary.
#   test/lsp/tools/editor_sim/run.sh [scenario-name-filter...]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
export NOVUS_LSP_BIN="${NOVUS_LSP_BIN:-$ROOT/build/novus-lsp}"
export PYTHONDONTWRITEBYTECODE=1
[ -x "$NOVUS_LSP_BIN" ] || { echo "no server binary at $NOVUS_LSP_BIN (make lsp)"; exit 2; }
python3 -u "$HERE/run.py" "$@" || exit 1
if [ $# -eq 0 ] && [ -d "$ROOT/vscode-novus/node_modules/vscode-languageserver-protocol" ]; then
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    cp -R "$ROOT/test/lsp/completion-basic/files/." "$WORK/"
    node "$HERE/vscode_jsonrpc.js" "$NOVUS_LSP_BIN" "$WORK"
fi
