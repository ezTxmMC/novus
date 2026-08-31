#!/usr/bin/env bash
# Self-hosting milestones: compile the Novus lexer (stage 1), parser
# (stage 2) and full compiler (stage 3) to C with the Novus-written
# pipeline, verifying native behavior matches the interpreted original.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOVUS="$ROOT/build/novus"
WORK="${TMPDIR:-/tmp}/novus_selfhost_$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

"$NOVUS" run "$ROOT/tools/emitrt.nv" > "$WORK/novus_rt.h"

# ---------- Stage 1: the lexer lexes, compiled by Novus ----------
DEMO='x = 1 + 2'

cat > "$WORK/ref1.nv" << EOF
package main

import "$ROOT/tools/lexcore.nv"

method main {
    for (t in lex("$DEMO")) {
        println t
    }
}
EOF
"$NOVUS" run "$WORK/ref1.nv" > "$WORK/ref1.txt"

cat > "$WORK/gen1.nv" << EOF
package main

import "$ROOT/tools/gencore.nv"

method main {
    var lexSource = readFile("$ROOT/tools/lexcore.nv")
    var driver = "
method main {
    for (t in lex(\"$DEMO\")) {
        println t
    }
    return 0
}"
    println generateC(lexSource + driver)
}
EOF
"$NOVUS" run "$WORK/gen1.nv" > "$WORK/lexer.c"
gcc "$WORK/lexer.c" -o "$WORK/lexer"
"$WORK/lexer" > "$WORK/native1.txt"
diff "$WORK/ref1.txt" "$WORK/native1.txt"
echo "STAGE 1 OK: native lexer identical ($(wc -l < "$WORK/native1.txt") tokens)"

# ---------- Stage 2: the parser parses, compiled by Novus ----------
cat > "$WORK/demo.nv" << 'EOF'
method main {
    var i = 0
    while (i < 3) {
        println twice(i)
        i = i + 1
    }
    return 0
}

method twice(integer x): integer {
    return x * 2
}
EOF

cat > "$WORK/ref2.nv" << EOF
package main

import "$ROOT/tools/parsecore.nv"

method main {
    for (m in parseProgram(readFile("$WORK/demo.nv"))) {
        println m
    }
}
EOF
"$NOVUS" run "$WORK/ref2.nv" > "$WORK/ref2.txt"

cat > "$WORK/gen2.nv" << EOF
package main

import "$ROOT/tools/gencore.nv"

method main {
    var lexSource = readFile("$ROOT/tools/lexcore.nv")
    var parseSource = readFile("$ROOT/tools/parsecore.nv")
    var demo = readFile("$WORK/demo.nv")
    var driver = "
method main {
    for (m in parseProgram(" + chr(34) + demo + chr(34) + ")) {
        println m
    }
    return 0
}"
    println generateC(lexSource + parseSource + driver)
}
EOF
"$NOVUS" run "$WORK/gen2.nv" > "$WORK/parser.c"
gcc "$WORK/parser.c" -o "$WORK/parser"
"$WORK/parser" > "$WORK/native2.txt"
diff "$WORK/ref2.txt" "$WORK/native2.txt"
echo "STAGE 2 OK: native parser identical ($(wc -l < "$WORK/native2.txt") methods)"

# ---------- Stage 3: the compiler compiles, compiled by Novus ----------
cat > "$WORK/ref3.nv" << EOF
package main

import "$ROOT/tools/gencore.nv"

method main {
    println generateC(readFile("$WORK/demo.nv"))
}
EOF
"$NOVUS" run "$WORK/ref3.nv" > "$WORK/ref3.txt"

cat > "$WORK/gen3.nv" << EOF
package main

import "$ROOT/tools/gencore.nv"

method main {
    var lexSource = readFile("$ROOT/tools/lexcore.nv")
    var parseSource = readFile("$ROOT/tools/parsecore.nv")
    var genSource = readFile("$ROOT/tools/gencore.nv")
    var demo = readFile("$WORK/demo.nv")
    var driver = "
method main {
    println generateC(" + chr(34) + demo + chr(34) + ")
    return 0
}"
    println generateC(lexSource + parseSource + genSource + driver)
}
EOF
"$NOVUS" run "$WORK/gen3.nv" > "$WORK/compiler.c"
gcc "$WORK/compiler.c" -o "$WORK/compiler"
"$WORK/compiler" > "$WORK/native3.txt"
diff "$WORK/ref3.txt" "$WORK/native3.txt"
echo "STAGE 3 OK: native compiler emits identical C ($(wc -l < "$WORK/native3.txt") lines)"

# Bonus: compile & run the C that the NATIVE compiler produced
cp "$WORK/native3.txt" "$WORK/out_by_native.c"
gcc "$WORK/out_by_native.c" -o "$WORK/out_by_native"
"$WORK/out_by_native" > "$WORK/final.txt"
echo "BONUS: program compiled by the native Novus compiler prints:"
cat "$WORK/final.txt"

# ---------- Stage 4: the fixpoint ----------
ALL="$ROOT/tools/lexcore.nv $ROOT/tools/parsecore.nv $ROOT/tools/gencore.nv $ROOT/tools/novusc.nv"

# C1: the compiler, compiled by the interpreted pipeline
"$NOVUS" run "$ROOT/tools/novusc.nv" $ALL > "$WORK/c1.c"
gcc "$WORK/c1.c" -o "$WORK/novusc1"

# C2: the compiler, compiled by its own native binary
"$WORK/novusc1" $ALL > "$WORK/c2.c"
diff "$WORK/c1.c" "$WORK/c2.c"
echo "STAGE 4a OK: interpreted and native compiler produce identical C"

# C3: once more, compiled by the second-generation binary
gcc "$WORK/c2.c" -o "$WORK/novusc2"
"$WORK/novusc2" $ALL > "$WORK/c3.c"
diff "$WORK/c2.c" "$WORK/c3.c"
echo "STAGE 4 FIXPOINT REACHED: novusc compiles itself byte-identically ($(wc -l < "$WORK/c3.c") lines of C)"
