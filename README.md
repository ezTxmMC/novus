# Novus Language

Novus is a self-hosting programming language: a C++ interpreter bootstraps a
compiler that is written in Novus itself and compiles Novus to C - including
its own sources, byte-identically (bootstrap fixpoint).

```nv
package main

method main {
    var names = ["Ada", "Grace"]
    for (n in names) {
        println "Hello, ${n}!"
    }
}
```

## Quick start

```sh
cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VCPKG/scripts/buildsystems/vcpkg.cmake
cmake --build build

./build/novus run examples/shapes/main.nv        # interpret
./build/novus build examples/wordcount/main.nv -o wc   # compile to native via C
./wc some.txt
```

## Language

The full feature set lives in [test/syntax.nv](test/syntax.nv), which runs as a
golden test: packages & imports, typed methods with overloading (incl.
`method main(array<str> args)`), `var` with inference, all control flow
(`if`/`else if`, `while`, `for..in`, `break`/`continue`), arrays, maps,
strings with `${}` interpolation and escapes, classes with fields
(`private final string name: get, set`), constructors, `this`,
object literals (`Person{name="Tom"}`), `based` inheritance with
polymorphism, interfaces, abstract classes, enums with constructors,
annotations (`@Deprecated{...}` warns at call time), and stdlib modules
(`json`, `path`) plus builtins (`readFile`, `writeFile`, `args`, `chr`,
`ord`, `parseInt`, `typeOf`).

## Architecture

| Path                              | What                                                                                 |
| --------------------------------- | ------------------------------------------------------------------------------------ |
| `src/`                            | C++ host: lexer, parser, tree-walking interpreter, `run`/`build` CLI                 |
| `tools/lexcore.nv`                | Novus lexer, written in Novus                                                        |
| `tools/parsecore.nv`              | Novus parser -> s-expression AST, written in Novus                                   |
| `tools/gencore.nv`                | C code generator, written in Novus                                                   |
| `tools/novusc.nv`                 | The Novus compiler as a program (`files -> C` on stdout)                             |
| `tools/runtimec.nv` + `emitrt.nv` | The small C runtime (`novus_rt.h`) generated programs use                            |
| `examples/`                       | Example projects ([overview](examples/README.md))                                    |
| `test/`                           | Golden-file suite (`./test/run_tests.sh`) and bootstrap check (`./test/selfhost.sh`) |

`novus build` runs the Novus-written pipeline in-process and hands the
generated C to `cc`. Compiled programs are limited to the self-hosting
subset for now (see [examples/README.md](examples/README.md)).

## Self-hosting

`./test/selfhost.sh` verifies the bootstrap ladder after every change:

1. the Novus lexer, compiled by the pipeline, tokenizes identically
2. the Novus parser, compiled by the pipeline, parses identically
3. the compiler compiles itself; the native binary emits identical C
4. **fixpoint**: `novusc` (native) compiles its own four source files to the
   byte-identical C it was built from - stage 2 == stage 3

## Tests

```sh
./test/selfhost.sh    # bootstrap stages 1-4
```

## Editor support

A VS Code extension with syntax highlighting, a language server and a run
command lives in [vscode-novus/](vscode-novus/README.md).
