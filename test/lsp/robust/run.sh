#!/usr/bin/env bash
# Robustness tests of novus-lsp (final phase, tester "robust"):
#   test/lsp/robust/run.sh [--full]   NOVUS_LSP_BIN=<binary> (default build/novus-lsp), WORK=<scratch dir without spaces>
#
# quick (default, about 1 minute): fuzz_features.py over every 12th repository file with the semantic invariants of
#   invariants.py, plus the scenarios of abuse.py except the HEAVY ones.
# --full: every file and every scenario (about 15 minutes; the known defects of expected_failures.txt are minutes each).
# Defects that are known on this tree are listed in expected_failures.txt and known_findings.txt: they are reported as
# xfail and do not fail the run; a listed scenario that passes prints XPASS.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${NOVUS_LSP_BIN:-$ROOT/build/novus-lsp}"
WORK="${WORK:-$(mktemp -d /tmp/novus-lsp-robust.XXXXXX)}"
mkdir -p "$WORK"
case "$WORK" in *" "*) echo "WORK must not contain spaces" >&2; exit 2 ;; esac
MODE_FUZZ=(--stride 12 --per-file-variants 3)
MODE_ABUSE=(--quick)
if [ "${1:-}" = "--full" ]; then
    MODE_FUZZ=(--per-file-variants 6)
    MODE_ABUSE=()
fi
status=0
python3 "$HERE/fuzz_features.py" --bin "$BIN" --out "$WORK/fuzz" --jobs "${JOBS:-8}" --known "$HERE/known_findings.txt" "${MODE_FUZZ[@]}" | grep -v 'latency\|answered error\|  slow' || true
[ "${PIPESTATUS[0]}" -eq 0 ] || status=1
python3 "$HERE/abuse.py" --bin "$BIN" --work "$WORK/abuse" --xfail "$HERE/expected_failures.txt" "${MODE_ABUSE[@]}" || status=1
exit $status
