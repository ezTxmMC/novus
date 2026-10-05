#!/usr/bin/env bash
# Performance and memory scenarios of novus-lsp (lsp/DESIGN.md 8.9 and 9.2, Wave 5). Not part of `make test`: the
# runs take minutes and their numbers depend on the machine. Needs python3.
#
#   test/lsp/tools/perf_bench/run.sh BINARY WORKDIR            budgets + the quick scenarios
#   test/lsp/tools/perf_bench/run.sh BINARY WORKDIR NAME...    only these scenarios (scen_NAME.py)
#
# WORKDIR must not contain a space (project.nv cannot hold one); the workloads are generated into it on first use.
# budgets.py exits 1 when a budget of 8.9 fails that known_failures.txt does not list; the scenarios only print.
# scen_session.py runs 10000 edit pairs (about 5 minutes) and scen_huge.py 70 s, so both are left out of the default.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
BINARY="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
WORK="$2"
shift 2
python3 "$HERE/prepare.py" "$WORK" > /dev/null || exit 1
cd "$WORK" || exit 1
if [ $# -eq 0 ]; then
    python3 "$HERE/budgets.py" "$BINARY" || exit 1
    set -- startup sizes edit diag deps checks storm openclose rebuild block
fi
for name in "$@"; do
    echo "== $name"
    case "$name" in
        index) python3 "$HERE/scen_index.py" "$BINARY" "$WORK/syn500" pkg040/f05.nv ;;
        rebuild) python3 "$HERE/scen_rebuild.py" "$BINARY" "$WORK/syn500" pkg040/f05.nv ;;
        refs) python3 "$HERE/scen_refs.py" "$BINARY" "$WORK/syn500" pkg040/f05.nv step1 limit ;;
        real) python3 "$HERE/scen_real.py" "$BINARY" "$(cd "$HERE/../../../.." && pwd)" compiler/nvh/nvh.nv ;;
        session) python3 -u "$HERE/scen_session.py" "$BINARY" "$WORK/size2000" main.nv 10000 ;;
        huge) python3 "$HERE/scen_huge.py" "$BINARY" huge10k 70 ;;
        huge2) python3 "$HERE/scen_huge2.py" "$BINARY" huge10k ;;
        checks) python3 "$HERE/scen_checks.py" "$BINARY" 3000 onSave ;;
        storm) python3 "$HERE/scen_storm.py" "$BINARY" 30000 ;;
        mem2 | block) python3 "$HERE/scen_$name.py" "$BINARY" 2000 ;;
        leak) python3 "$HERE/scen_leak.py" "$BINARY" 2000 sem 300 ;;
        *) python3 "$HERE/scen_$name.py" "$BINARY" ;;
    esac
done
