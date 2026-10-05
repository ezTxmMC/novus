#!/usr/bin/env bash
# Parity matrix of novus-lsp against the TypeScript server (test/lsp/parity/parity.py).
#   test/lsp/parity/run.sh                 uses build/novus-lsp (or NOVUS_LSP_BIN)
#   test/lsp/parity/run.sh --area hover    one area; --only <id-part> one check; --verbose lists passes too; --list lists checks
# Exit code 0 when the failing checks are exactly those of expected_failing.txt.
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
exec python3 "$ROOT/test/lsp/parity/parity.py" "$@"
