#!/bin/sh
# A fake novusc for the protocol scenarios (DESIGN 9.5-2): `check <file>` and `deps`, driven by marker words in the files.
if [ "$1" = "deps" ]; then
    if [ -f FAIL_DEPS ]; then
        echo "error: cannot fetch dependencies"
        exit 1
    fi
    echo "fetched 2 modules"
    exit 0
fi
file="$2"
folder="$(dirname "$file")"
if grep -q WEIRD "$file"; then
    echo "the compiler lost its way"
    exit 3
fi
if grep -q LIBBROKEN "$file"; then
    echo "error: $folder/lib.nv:3: unknown member 'helper'"
    exit 1
fi
if grep -q BROKEN "$file"; then
    line="$(grep -n BROKEN "$file" | head -1 | cut -d: -f1)"
    echo "error: $file:$line: expected '}' but found 'BROKEN'"
    exit 1
fi
echo "ok: $file"
