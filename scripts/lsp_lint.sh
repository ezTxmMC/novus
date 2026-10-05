#!/usr/bin/env bash
# Structure lint of novus-lsp (lsp/DESIGN.md 9.4). Bash + awk, no other dependency.
#
#   scripts/lsp_lint.sh           lint lsp/; exit code 1 when a rule fails (warnings do not fail)
#   scripts/lsp_lint.sh DIR       lint another tree laid out like lsp/ (it needs packages.txt and
#                                 contract_names.txt of its own; used by the lint's own test)
#
# Before matching, string literals (also multi-line ones) and // and /* */ comments are blanked;
# rules 6-11 look only at the code that remains, rules 3 and 5 at declarations and import lines.
#
# Rules (numbers as in DESIGN 9.4, 12 and 13 are additions):
#  1 more than 10 .nv/.nvh files in a folder (project.nv does not count)
#  2 a file or a class above 200 lines (a function above 50 lines is only a warning)
#  3 a class/enum/interface/free method/constant defined twice, a std-prefixed class (Toml Yaml Nvh),
#    an internal name (not in contract_names.txt) without the package prefix of LSP-R10
#  4 a package folder named like a std module or a compiler package
#  5 imports outside the declared dependencies of packages.txt (maybe is exempt), a cycle in
#    packages.txt, the string import "novus/compiler/std" outside projects/stdlib, lsp/main.nv
#    importing more than server/wiring
#  6 stdout written outside rpc/writer.nv and platform/stdio.nv (println, print, io.write, os.exec, os.output)
#  7 a local variable or parameter named like an imported std module
#  8 else or else-if (exemption: a trailing comment  // pureline:allow-else <reason>)
#  9 a one-letter local or parameter other than i j k
# 10 a call other than stdoutWriteFrame has get remove length inside a sync body
# 11 thread f(...) where f does not start with runGuarded
# 12 the package declaration does not name the folder
# 13 a contract name is missing from its (existing) package or is defined in another one
# Notes only: a test source of test/cases/lsp_*/ above 200 lines (lint of the real tree only; tests are exempt).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LSP="${1:-$ROOT/lsp}"
LSP="$(cd "$LSP" && pwd)"
for needed in packages.txt contract_names.txt; do
    if [ ! -f "$LSP/$needed" ]; then
        echo "lsp_lint: $LSP/$needed is missing" >&2
        exit 2
    fi
done
STD_MODULES="arrays base64 bits bytes cli config crypto csv fmt hash http io json log maps math net os path properties random strings test thread time toml unicode web yaml zlib"
COMPILER_PACKAGES="ast codegen driver lexer loader nvh parser project runtime std"
failures=0

fail() {
    echo "$1"
    failures=$((failures + 1))
}

# Rules 1 and 4: folders.
while IFS= read -r dir; do
    count="$(find "$dir" -maxdepth 1 -type f \( -name '*.nv' -o -name '*.nvh' \) ! -name project.nv | wc -l | tr -d ' ')"
    [ "$count" -gt 10 ] && fail "${dir#"$LSP"/}: [rule 1] $count source files in one folder (limit 10)"
    [ "$dir" = "$LSP" ] && continue
    name="$(basename "$dir")"
    for reserved in $STD_MODULES $COMPILER_PACKAGES; do
        [ "$name" = "$reserved" ] && fail "${dir#"$LSP"/}: [rule 4] folder is named like the std module or compiler package '$reserved'"
    done
done < <(find "$LSP" -type d | LC_ALL=C sort)

FILES="$(find "$LSP" -type f \( -name '*.nv' -o -name '*.nvh' \) | LC_ALL=C sort)"
if [ -z "$FILES" ]; then
    echo "lsp_lint: no sources under $LSP"
    exit $((failures > 0))
fi

# Everything else is one awk pass over the config files and all sources.
# shellcheck disable=SC2086
output="$(LC_ALL=C awk -v root="$LSP" -v std="$STD_MODULES" -v compilerPackages="$COMPILER_PACKAGES" '
function report(rule, message) {
    print rel ":" FNR ": [rule " rule "] " message
    failed++
}
function reportAt(file, line, rule, message) {
    print file ":" line ": [rule " rule "] " message
    failed++
}
function warn(message) {
    print rel ":" FNR ": warning: " message
}
function trim(s) {
    sub(/^[ \t]+/, "", s)
    sub(/[ \t]+$/, "", s)
    return s
}
function leafOf(path,   n, parts) {
    n = split(path, parts, "/")
    return parts[n]
}
function blank(s,   n, i, c, pair, out) {
    n = length(s)
    out = ""
    for (i = 1; i <= n; i++) {
        c = substr(s, i, 1)
        if (inBlock) {
            if (c == "*" && substr(s, i + 1, 1) == "/") { inBlock = 0; out = out "  "; i++ } else { out = out " " }
            continue
        }
        if (inString) {
            if (c == "\\") { out = out "  "; i++; continue }
            if (c == "\"") { inString = 0; out = out "\""; continue }
            out = out " "
            continue
        }
        if (c == "\"") { inString = 1; out = out "\""; continue }
        pair = substr(s, i, 2)
        if (pair == "//") { break }
        if (pair == "/*") { inBlock = 1; out = out "  "; i++; continue }
        out = out c
    }
    return out
}
function countChar(s, ch,   n, i, total) {
    n = length(s)
    total = 0
    for (i = 1; i <= n; i++) {
        if (substr(s, i, 1) == ch) { total++ }
    }
    return total
}
function capitalize(s) {
    return toupper(substr(s, 1, 1)) substr(s, 2)
}
function arityOf(code,   openAt, closeAt, inner, i, depthAngle, c, commas) {
    openAt = index(code, "(")
    closeAt = 0
    for (i = length(code); i > openAt; i--) {
        if (substr(code, i, 1) == ")") { closeAt = i; break }
    }
    inner = trim(substr(code, openAt + 1, closeAt - openAt - 1))
    if (inner == "") { return 0 }
    depthAngle = 0
    commas = 0
    for (i = 1; i <= length(inner); i++) {
        c = substr(inner, i, 1)
        if (c == "<") { depthAngle++ }
        if (c == ">") { depthAngle-- }
        if (c == "," && depthAngle == 0) { commas++ }
    }
    return commas + 1
}
function checkLocalName(name, what) {
    if (length(name) == 1 && name != "i" && name != "j" && name != "k") {
        report(9, what " \"" name "\" is a one-letter name (only i j k are allowed)")
    }
    if (name in importedStd) {
        report(7, what " \"" name "\" is named like the imported std module " name)
    }
}
function checkParameters(code,   openAt, closeAt, inner, i, c, depthAngle, piece, words, count) {
    openAt = index(code, "(")
    closeAt = 0
    for (i = length(code); i > openAt; i--) {
        if (substr(code, i, 1) == ")") { closeAt = i; break }
    }
    if (openAt == 0 || closeAt == 0) { return }
    inner = substr(code, openAt + 1, closeAt - openAt - 1)
    depthAngle = 0
    piece = ""
    for (i = 1; i <= length(inner) + 1; i++) {
        c = substr(inner, i, 1)
        if (c == "<") { depthAngle++ }
        if (c == ">") { depthAngle-- }
        if ((c == "," && depthAngle == 0) || i == length(inner) + 1) {
            piece = trim(piece)
            count = split(piece, words, /[ \t]+/)
            if (count >= 2) { checkLocalName(words[count], "parameter") }
            piece = ""
            continue
        }
        piece = piece c
    }
}
function checkLocals(code,   rest, name) {
    if (match(code, /^[ \t]*var +[A-Za-z_][A-Za-z0-9_]*/)) {
        rest = substr(code, RSTART, RLENGTH)
        sub(/^[ \t]*var +/, "", rest)
        checkLocalName(rest, "variable")
    }
    if (match(code, /(^|[^A-Za-z0-9_])for *\( *[A-Za-z_][A-Za-z0-9_]* +in /)) {
        rest = substr(code, RSTART, RLENGTH)
        sub(/^.*for *\( */, "", rest)
        sub(/ +in $/, "", rest)
        checkLocalName(rest, "loop variable")
    }
    if (match(code, /^[ \t]*[A-Za-z_][A-Za-z0-9_<>,]* +[A-Za-z_][A-Za-z0-9_]* *=[^=]/)) {
        rest = trim(substr(code, RSTART, RLENGTH))
        sub(/ *=[^=]$/, "", rest)
        split(rest, words, /[ \t]+/)
        if (!(words[1] in notTypes)) { checkLocalName(words[2], "variable") }
    }
}
function checkSyncCalls(text,   name) {
    while (match(text, /[A-Za-z_][A-Za-z0-9_]* *\(/)) {
        name = substr(text, RSTART, RLENGTH)
        sub(/ *\($/, "", name)
        text = substr(text, RSTART + RLENGTH)
        if (!(name in syncAllowed) && !(name in notCalls)) {
            report(10, "call to " name "() inside a sync body (only stdoutWriteFrame, has, get, remove and length are allowed)")
        }
    }
}
function declare(kind, name, key, line) {
    if (key in definedAt) {
        report(3, kind " " name " is already defined at " definedAt[key])
    }
    definedAt[key] = rel ":" line
    definedIn[name] = leaf
}
function checkPrefix(kind, name, prefix) {
    if (name in contractPackage) {
        if (contractPackage[name] != leaf) {
            report(13, "contract name " name " belongs to package " contractPackage[name] ", not " leaf)
        }
        return
    }
    if (index(name, prefix) != 1) {
        report(3, "internal " kind " " name " lacks the package prefix " prefix)
    }
}
function checkDeclarations(code,   name, arity, key) {
    if (match(code, /^define +(abstract +)?(class|interface|enum|annotation) +[A-Za-z_][A-Za-z0-9_]*/)) {
        name = substr(code, RSTART, RLENGTH)
        sub(/^.* /, "", name)
        declare("type", name, "type " name, FNR)
        if (name ~ /^(Toml|Yaml|Nvh)/) { report(3, "type " name " collides with the std classes by its prefix") }
        checkPrefix("type", name, capitalize(leaf))
        if (code ~ /^define +(abstract +)?class /) { blockKind = "class"; blockName = name; blockStart = FNR }
        else { blockKind = "type"; blockName = name; blockStart = FNR }
        return
    }
    if (match(code, /^(async +)?method +[A-Za-z_][A-Za-z0-9_]* *\(/)) {
        name = substr(code, RSTART, RLENGTH)
        sub(/^(async +)?method +/, "", name)
        sub(/ *\($/, "", name)
        arity = arityOf(code)
        declare("method", name, "method " name "/" arity, FNR)
        if (!(rel == "main.nv" && name == "main")) { checkPrefix("method", name, leaf) }
        return
    }
    if (match(code, /^(private +)?final +[A-Za-z_][A-Za-z0-9_]* *=/)) {
        name = substr(code, RSTART, RLENGTH)
        sub(/^(private +)?final +/, "", name)
        sub(/ *=$/, "", name)
        declare("constant", name, "const " name, FNR)
        checkPrefix("constant", name, toupper(leaf) "_")
    }
}
function checkImport(line,   target, quoted, resolved) {
    target = trim(substr(line, 8))
    quoted = (substr(target, 1, 1) == "\"")
    if (quoted) {
        gsub(/"/, "", target)
        if (target == "novus/compiler/std") {
            if (rel !~ /^projects\/stdlib\//) { report(5, "the import \"novus/compiler/std\" is allowed only in projects/stdlib") }
            return
        }
        report(5, "unknown string import \"" target "\"")
        return
    }
    if (target in stdSet) { importedStd[target] = 1; return }
    if (!(target in packageByPath)) {
        report(5, "import " target " names neither a std module nor a package of packages.txt")
        return
    }
    resolved = packageByPath[target]
    if (rel == "main.nv") {
        if (target != "server/wiring") { report(5, "lsp/main.nv may import only server/wiring, not " target) }
        return
    }
    if (resolved != "maybe" && resolved != leaf && index(" " deps[leaf] " ", " " resolved " ") == 0) {
        report(5, "package " leaf " imports " resolved ", which is not a declared dependency in packages.txt")
    }
}
function sortedKeys(source, sorted,   key, count, i, j, hold) {
    count = 0
    for (key in source) {
        sorted[++count] = key
    }
    for (i = 2; i <= count; i++) {
        hold = sorted[i]
        for (j = i - 1; j >= 1 && sorted[j] > hold; j--) { sorted[j + 1] = sorted[j] }
        sorted[j + 1] = hold
    }
    return count
}
function checkCycles(   leafName, remaining, changed, parts, i, n, ready, dep, names, count) {
    for (leafName in deps) { remaining[leafName] = 1 }
    changed = 1
    while (changed) {
        changed = 0
        for (leafName in remaining) {
            n = split(deps[leafName], parts, " ")
            ready = 1
            for (i = 1; i <= n; i++) {
                dep = parts[i]
                if (dep != "" && dep != "maybe" && (dep in remaining)) { ready = 0 }
            }
            if (ready) { delete remaining[leafName]; changed = 1 }
        }
    }
    count = sortedKeys(remaining, names)
    for (i = 1; i <= count; i++) {
        print "packages.txt: [rule 5] package " names[i] " is part of a dependency cycle"
        failed++
    }
}
function endOfFile(   key) {
    if (FILENAME == "") { return }
    if (fileLines > 200) {
        reportAt(rel, fileLines, 2, "file has " fileLines " lines (limit 200)")
    }
}

BEGIN {
    n = split(std, parts, " ")
    for (i = 1; i <= n; i++) { stdSet[parts[i]] = 1 }
    split("if while for return sync thread await", words, " ")
    for (i in words) { notCalls[words[i]] = 1 }
    split("stdoutWriteFrame has get remove length", words, " ")
    for (i in words) { syncAllowed[words[i]] = 1 }
    split("var return if else while for break continue import package method private final println print eprintln in true false", words, " ")
    for (i in words) { notTypes[words[i]] = 1 }
    failed = 0
}

FILENAME == root "/packages.txt" {
    if ($0 == "") { next }
    colon = index($0, ":")
    path = substr($0, 1, colon - 1)
    packageLeaf = leafOf(path)
    packageByPath[path] = packageLeaf
    packagePath[packageLeaf] = path
    deps[packageLeaf] = trim(substr($0, colon + 1))
    next
}
FILENAME == root "/contract_names.txt" {
    if ($0 == "") { next }
    split($0, pair, " ")
    contractPackage[pair[1]] = pair[2]
    next
}

FNR == 1 {
    endOfFile()
    rel = substr(FILENAME, length(root) + 2)
    folder = rel
    if (sub(/\/[^\/]*$/, "", folder) == 0) { folder = "" }
    leaf = (folder == "") ? "main" : leafOf(folder)
    expectedLeaf = leaf
    inString = 0; inBlock = 0; depth = 0; fileLines = 0
    blockKind = ""; funcName = ""; funcStart = 0; awaitFirst = ""; syncDepth = -1; sawPackage = 0
    delete importedStd
    if (leaf != "main") { havePackage[leaf] = 1 }
}

{
    fileLines++
    startsInString = inString
    original = $0
    code = blank($0)
    opens = countChar(code, "{")
    closes = countChar(code, "}")

    if (!startsInString && original ~ /^import /) { checkImport(original) }
    if (!sawPackage && code ~ /^package +[A-Za-z_]/) {
        sawPackage = 1
        declared = trim(substr(code, 9))
        if (declared != expectedLeaf) { report(12, "package declaration \"" declared "\" does not name the folder (" expectedLeaf ")") }
    }
    if (depth == 0) { checkDeclarations(code) }

    if (code ~ /(^|[^A-Za-z0-9_.])else($|[^A-Za-z0-9_])/ && original !~ /\/\/ *pureline:allow-else/) {
        report(8, "else is not allowed (guard clauses instead; exemption: // pureline:allow-else <reason>)")
    }
    if (rel != "server/rpc/writer.nv" && rel != "base/platform/stdio.nv") {
        if (code ~ /(^|[^A-Za-z0-9_.])(println|print)($|[^A-Za-z0-9_])/ || code ~ /(^|[^A-Za-z0-9_])io\.write *\(/ || code ~ /(^|[^A-Za-z0-9_])os\.(exec|output) *\(/) {
            report(6, "stdout is written here; only MessageWriter and platform.stdio may do that")
        }
    }
    if (code ~ /^[ \t]*((async|abstract) +)?(method|construct)([ \t(])/) { checkParameters(code) }
    if (depth > 0) { checkLocals(code) }

    # threads: the target must start with runGuarded (checked at the end, the target may be defined later)
    probe = code
    while (match(probe, /(^|[^A-Za-z0-9_.])thread +[A-Za-z_][A-Za-z0-9_]* *\(/)) {
        threadName = substr(probe, RSTART, RLENGTH)
        sub(/^.*thread +/, "", threadName)
        sub(/ *\($/, "", threadName)
        threadUses[++threadCount] = threadName
        threadAt[threadCount] = rel ":" FNR
        probe = substr(probe, RSTART + RLENGTH)
    }

    # sync bodies
    if (syncDepth < 0 && code ~ /(^|[^A-Za-z0-9_.])sync *[({]/) {
        syncDepth = depth
        syncText = code
        brace = index(syncText, "{")
        if (brace > 0) { checkSyncCalls(substr(syncText, brace + 1)) }
    } else if (syncDepth >= 0) {
        checkSyncCalls(code)
    }

    # first statement of a function (rule 11) and function length (warning)
    if (awaitFirst != "" && code ~ /[^ \t]/) {
        firstGuarded[awaitFirst] = (code ~ /runGuarded/)
        awaitFirst = ""
    }
    if (funcName == "" && code ~ /^[ \t]*(async +)?method +[A-Za-z_][A-Za-z0-9_]* *\(.*\{ *$/) {
        funcName = code
        sub(/^[ \t]*(async +)?method +/, "", funcName)
        sub(/ *\(.*$/, "", funcName)
        funcStart = FNR
        funcDepth = depth
        awaitFirst = funcName
    }

    depth += opens - closes
    if (syncDepth >= 0 && depth <= syncDepth) { syncDepth = -1 }
    if (funcName != "" && depth <= funcDepth && FNR > funcStart) {
        if (FNR - funcStart + 1 > 50) { warn("function " funcName " has " (FNR - funcStart + 1) " lines (review above 30, split above 50)") }
        funcName = ""
    }
    if (blockKind != "" && depth == 0 && (opens > 0 || closes > 0 || FNR > blockStart)) {
        if (blockKind == "class" && FNR - blockStart + 1 > 200) {
            reportAt(rel, blockStart, 2, "class " blockName " has " (FNR - blockStart + 1) " lines (limit 200)")
        }
        blockKind = ""
    }
}

END {
    endOfFile()
    checkCycles()
    for (i = 1; i <= threadCount; i++) {
        name = threadUses[i]
        if (!(name in firstGuarded)) {
            print threadAt[i] ": [rule 11] thread target " name " is not a free method of lsp/"
            failed++
        } else if (firstGuarded[name] == 0) {
            print threadAt[i] ": [rule 11] thread target " name " does not start with runGuarded"
            failed++
        }
    }
    contractCount = sortedKeys(contractPackage, contractNames)
    for (c = 1; c <= contractCount; c++) {
        name = contractNames[c]
        packageName = contractPackage[name]
        if ((packageName in havePackage) && !(name in definedIn) && name !~ /^Bootstrap/) {
            print "contract_names.txt: [rule 13] " name " (package " packageName ") is not defined in lsp/"
            failed++
        }
        if ((name in definedIn) && definedIn[name] != packageName && !(packageName == "wiring" && name ~ /^Bootstrap/)) {
            print "contract_names.txt: [rule 13] " name " is defined in package " definedIn[name] ", the contract says " packageName
            failed++
        }
    }
    print "lsp_lint: " failed " failures" > "/dev/stderr"
    exit (failed > 0)
}
' "$LSP/packages.txt" "$LSP/contract_names.txt" $FILES)"
status=$?

# Test sources are exempt from the 200-line limit (DESIGN 9.4, amendment 4): a long one is only mentioned as a note.
lint_test_sources() {
    [ "$LSP" = "$ROOT/lsp" ] || return 0
    local file lines
    for file in "$ROOT"/test/cases/lsp_*/*.nv; do
        [ -f "$file" ] || continue
        lines="$(wc -l < "$file" | tr -d ' ')"
        if [ "$lines" -gt 200 ]; then
            echo "${file#"$ROOT"/}: note: $lines lines in a test source (tests are exempt from the 200-line limit)"
        fi
    done
}

[ -n "$output" ] && echo "$output"
lint_test_sources
if [ "$status" != 0 ] || [ "$failures" != 0 ]; then
    echo "lsp_lint: FAILED"
    exit 1
fi
echo "lsp_lint: clean"
