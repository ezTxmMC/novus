#!/usr/bin/env bash
# Functions of test/run_lsp_corpus.sh that test/lsp/corpus/selftest.sh also exercises; source this file, do not run it.
# A summary file holds lines `summary <key> <n>`; a golden holds `== <key> <n>` (exact), `>= <key> <n>` (floor) and `#` comments.

# A count that grows with the repository is a floor in the golden; everything else must match exactly.
corpus_is_floor_key() {
    case "$1" in
        syntax.files.*|compare.checked|compare.accepted|compare.closure-files|compare.needs-network|diagnose.files|fuzz.files|fuzz.cases) return 0 ;;
    esac
    return 1
}

# corpus_write_golden <golden> <summary>
corpus_write_golden() {
    local golden="$1" summary="$2" key value
    {
        echo "# Counters of test/run_lsp_corpus.sh (lsp/DESIGN.md 9.5-4 (c)). \`== key n\`: exact, \`>= key n\`: floor."
        echo "# Regenerate with test/run_lsp_corpus.sh --update after reviewing why a counter moved."
        while read -r _ key value; do
            if corpus_is_floor_key "$key"; then echo ">= $key $value"; else echo "== $key $value"; fi
        done < <(LC_ALL=C sort "$summary")
    } > "$golden"
}

# The number of .nv and .nvh files below the root that the tools should have visited, found without the tools' own walker:
# dot directories, build, dist, out and node_modules are left out like the walker does (symlinked directories are not followed).
corpus_expected_files() {
    (cd "$1" && find . -type d \( -name '.?*' -o -name build -o -name dist -o -name out -o -name node_modules \) -prune \
        -o \( -type f -o -type l \) \( -name '*.nv' -o -name '*.nvh' \) -print | wc -l | tr -d ' ')
}

# corpus_add_skipped <summary> <skipped-key> <expected-files> <counted-key-pattern>: appends `summary <skipped-key> <n>` where n
# is the expected file count minus the sum of the counters that match the awk regular expression, so a file that a tool left
# out (above 2 MiB, deeper than 64 levels) shows up as a nonzero counter that the golden pins to 0.
corpus_add_skipped() {
    local summary="$1" key="$2" expected="$3" pattern="$4" seen
    seen="$(awk -v p="$pattern" '$1 == "summary" && $2 ~ p { total += $3 } END { print total + 0 }' "$summary")"
    echo "summary $key $((expected - seen))" >> "$summary"
}

# corpus_compare_golden <golden> <summary>: every golden line must be well formed, every golden key must appear in the
# summary (and the other way round) with an allowed value. Prints what is wrong and returns 1.
corpus_compare_golden() {
    local golden="$1" summary="$2" status=0 line operator key expected extra actual
    [ -f "$golden" ] || { echo "corpus: $golden is missing (run with --update)"; return 1; }
    while IFS= read -r line; do
        case "$line" in "#"*|"") continue ;; esac
        read -r operator key expected extra <<< "$line"
        if ! corpus_golden_line_is_valid "$operator" "$key" "$expected" "$extra"; then
            echo "corpus: malformed golden line '$line' (expected '== key n' or '>= key n')"
            status=1
            continue
        fi
        actual="$(awk -v k="$key" '$2 == k { print $3 }' "$summary")"
        corpus_check_counter "$operator" "$key" "$expected" "$actual" || status=1
    done < "$golden"
    corpus_check_unlisted "$golden" "$summary" || status=1
    corpus_check_duplicates "$golden" || status=1
    return "$status"
}

corpus_golden_line_is_valid() {
    case "$1" in "=="|">=") ;; *) return 1 ;; esac
    case "$2" in ""|*[!a-z0-9.-]*) return 1 ;; esac
    case "$3" in ""|*[!0-9]*) return 1 ;; esac
    [ -z "$4" ]
}

corpus_check_counter() {
    local operator="$1" key="$2" expected="$3" actual="$4"
    if [ -z "$actual" ]; then
        echo "corpus: counter $key is in the golden but missing from this run"
        return 1
    fi
    case "$actual" in *[!0-9]*)
        echo "corpus: counter $key is '$actual', which is not a number"
        return 1 ;;
    esac
    if [ "$operator" = "==" ] && [ "$actual" -ne "$expected" ]; then
        echo "corpus: counter $key is $actual, the golden says exactly $expected"
        return 1
    fi
    if [ "$operator" = ">=" ] && [ "$actual" -lt "$expected" ]; then
        echo "corpus: counter $key dropped to $actual, the golden floor is $expected"
        return 1
    fi
    return 0
}

corpus_check_unlisted() {
    local golden="$1" summary="$2" status=0 key
    while read -r _ key _; do
        if ! awk -v k="$key" '$2 == k { found = 1 } END { exit !found }' "$golden"; then
            echo "corpus: counter $key is not in the golden (run with --update)"
            status=1
        fi
    done < "$summary"
    return "$status"
}

corpus_check_duplicates() {
    local twice
    twice="$(awk '$1 == "==" || $1 == ">=" { print $2 }' "$1" | sort | uniq -d)"
    [ -z "$twice" ] && return 0
    echo "corpus: the golden lists a counter twice: $twice"
    return 1
}
