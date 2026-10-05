#!/usr/bin/env bash
# Verifies the snippet catalogue of novus-lsp (lsp/DESIGN.md 7.5 and 7.6).
#
#   scripts/lsp_snippets.sh              compile every snippet, then diff the VS Code export
#   scripts/lsp_snippets.sh --no-export  compile only
#   scripts/lsp_snippets.sh --update     regenerate vscode-novus/snippets/novus.json, then compile
#
# The tool test/lsp/tools/snippets_tool writes one verification project per snippet; every
# project must pass `novusc check` and, unless it is a project.nv or .nvh snippet, `novusc build`
# (the C compiler is the gate: `break` outside a loop passes check). The exported
# VS Code file must equal the committed one, so the catalogue stays the single source of truth.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUSC="${NOVUSC:-$ROOT/build/novusc}"
EXPORTED="$ROOT/vscode-novus/snippets/novus.json"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export NOVUS_CFLAGS="${NOVUS_CFLAGS:--O0}"
export NOVUSC
CHECK_EXPORT=1
UPDATE=0
for argument in "$@"; do
    case "$argument" in
        --no-export) CHECK_EXPORT=0 ;;
        --update) UPDATE=1 ;;
        *) echo "usage: $0 [--no-export | --update]" >&2; exit 2 ;;
    esac
done

jobs_count() {
    nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4
}

# One verification project: arguments are the id, the entry file and "build" or "check".
cat > "$WORK/check_one.sh" << 'EOF'
#!/usr/bin/env bash
id="$1" entry="$2" mode="$3"
dir="$CASES/$id/p"
cd "$dir" || { echo "missing project directory" > "$RESULTS/$id.fail"; exit 0; }
if ! output="$("$NOVUSC" check "$entry" 2>&1)"; then
    printf 'check: %s\n' "$output" > "$RESULTS/$id.fail"
    exit 0
fi
if [ "$mode" = build ]; then
    if ! output="$("$NOVUSC" build "$entry" -o "$CASES/$id/out.bin" 2>&1)"; then
        printf 'build: %s\n' "$output" > "$RESULTS/$id.fail"
    fi
fi
exit 0
EOF
chmod +x "$WORK/check_one.sh"

TOOL="$WORK/snippets_tool"
if ! "$NOVUSC" build "$ROOT/test/lsp/tools/snippets_tool/main.nv" -o "$TOOL" > "$WORK/build.log" 2>&1; then
    echo "FAIL: cannot build snippets_tool"
    cat "$WORK/build.log"
    exit 1
fi

export CASES="$WORK/cases" RESULTS="$WORK/results"
mkdir -p "$RESULTS"
if ! "$TOOL" verify "$CASES"; then
    echo "FAIL: the verification projects were not written"
    exit 1
fi
xargs -P "$(jobs_count)" -L1 "$WORK/check_one.sh" < "$CASES/INDEX.txt"

total="$(wc -l < "$CASES/INDEX.txt" | tr -d ' ')"
failed=0
for failure in "$RESULTS"/*.fail; do
    [ -e "$failure" ] || continue
    failed=$((failed + 1))
    echo "FAIL snippet $(basename "$failure" .fail)"
    head -8 "$failure" | sed 's/^/    /'
done
echo "$total snippets verified, $failed failed"

status=0
[ "$failed" = 0 ] || status=1
if [ "$UPDATE" = 1 ]; then
    "$TOOL" export "$EXPORTED" || status=1
    echo "updated $EXPORTED"
elif [ "$CHECK_EXPORT" = 1 ]; then
    "$TOOL" export "$WORK/novus.json" || status=1
    if ! diff -u "$EXPORTED" "$WORK/novus.json" > "$WORK/export.diff"; then
        echo "FAIL: $EXPORTED is stale: run scripts/lsp_snippets.sh --update"
        head -20 "$WORK/export.diff"
        status=1
    fi
fi
exit "$status"
