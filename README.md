# Novus Language

Novus is a self-hosting programming language. Its compiler, `novusc`, is
written in Novus, compiles Novus to portable C, and compiles itself -
byte-identically (bootstrap fixpoint). There is no other implementation
anymore: a checked-in C snapshot of the compiler bootstraps everything, so
the only thing you need is a C compiler.

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

Linux / macOS (any `cc`: gcc, clang or `zig cc`):

```sh
scripts/bootstrap.sh          # or: make
build/novusc run examples/shapes/main.nv
build/novusc build examples/wordcount/main.nv -o wc && ./wc README.md
```

Windows (gcc from MinGW-w64 / MSYS2 in `PATH`, or `set NOVUS_CC=clang`):

```bat
scripts\bootstrap.cmd
build\novusc.exe run examples\shapes\main.nv
```

The bootstrap takes a few seconds: it compiles `bootstrap/novusc.c` (stage 0),
uses that to compile the current compiler sources (stage 1) and finally lets
the result compile itself (stage 2, `build/novusc`).

Prebuilt binaries for Linux, macOS and Windows are produced by the CI
workflow for every push and attached to tagged releases. To cross compile
all of them yourself from one machine, install [zig](https://ziglang.org)
and run `scripts/cross.sh` (output in `dist/`).

## Using novusc

```
novusc run <file.nv> [args...]     compile to a temporary binary and run it
novusc build <file.nv> [options]   compile to a native executable
novusc emit <file.nv> [-o out.c]   write the generated C (single file, runtime included)
novusc check <file.nv>             parse and analyze only
novusc version
```

Build options: `-o <path>`, `--cc <compiler>`, `--cflags <flags>`,
`--target <triple>` (cross compile through `zig cc -target`, e.g.
`--target x86_64-windows-gnu`), `--keep-c`, `--no-runtime`. The C compiler
defaults to `$NOVUS_CC`, then `cc` (`gcc` on Windows); `$NOVUS_CFLAGS` adds
flags. Programs are single, self-contained C files: `novusc emit` output can
be handed to any C compiler on any platform.

## Language

The showcase in [test/syntax.nv](test/syntax.nv) and the golden tests in
[test/cases/](test/cases/) cover the language: packages and file imports,
methods with overloading (by arity and parameter type, including
`method main(array<string> args)`), `var` with inference and typed
declarations (`integer x = 2.9` truncates), all control flow (`if`/`else if`,
`while`, `for..in` over arrays, map keys and strings, `break`/`continue`),
arrays, maps, strings with `${}` interpolation and escapes, classes with
fields (`private final string name: get, set`), constructors, `this`,
implicit `this` for fields and methods, object literals (`Person{name="Tom"}`), `based`
inheritance with polymorphism, interfaces, abstract classes, enums with
constructors, annotations (`@Deprecated{...}` warns at call time), top-level
constants, and the stdlib modules `os`, `path`, `json` and `http`.

Values are dynamically typed at run time; arrays, maps and objects are
passed by reference. Integers are 64 bit. Missing interface/abstract
implementations and unknown names are compile-time errors.

Builtins: `readFile`, `writeFile`, `fileExists`, `removeFile`, `readLine`,
`args`, `parseInt`, `parseFloat`, `chr`, `ord`, `typeOf`, `exec`, `env`,
`exit`, `platform`; statements `println`, `print`, `eprintln`.
Strings: `length`, `charAt`, `substring`, `indexOf`, `contains`,
`startsWith`, `endsWith`, `split`, `replace`, `trim`, `toUpper`, `toLower`.
Arrays: `length`, `append`, `pop`, `insert`, `remove`, `contains`,
`indexOf`, `join`, `clear`. Maps: `length`, `has`, `keys`, `values`,
`remove`, `get`.

Stdlib modules (`import os` etc.):

| Module | Functions |
| ------ | --------- |
| `os`   | `mkdir` (with parents), `rmdir`, `remove`, `removeAll`, `listDir`, `exists`, `isDir`, `isFile`, `rename`, `copy`, `fileSize`, `modified`, `readFile`, `writeFile`, `appendFile`, `cwd`, `chdir`, `temp`, `home`, `exec`, `output`, `env`, `setEnv`, `exit`, `platform`, `args`, `pid`, `time`, `clock`, `sleep`, `readLine` |
| `path` | `join`, `absolute` (cwd) / `absolute(p)`, `normalize`, `relative`, `dirname`, `basename`, `stem`, `extension`, `isAbsolute`, `exists`, `isDir`, `isFile`, `temp`, `separator` |
| `json` | `stringify`, `pretty`, `parse` (text or already structured data), `isValid`, `load(file)`, `save(value, dir, file)` (creates the directory) |
| `http` | `get`, `post`, `put`, `delete` (return the body, abort on transport errors), `request(method, url, body, headers)` -> `{status, ok, body, headers, error}`, `download(url, file)`. Maps/arrays are sent as JSON. Backed by the `curl` command line tool (ships with Windows 10+, macOS and most Linux distributions), so https just works. |

## Architecture

| Path                    | What                                                                          |
| ----------------------- | ----------------------------------------------------------------------------- |
| `bootstrap/novusc.c`    | Generated C of the compiler - the stage 0 snapshot (never edit by hand)      |
| `compiler/lexer/`      | Lexer (`tokens.nv`, `chars.nv`, `lexer.nv`)                                  |
| `compiler/ast/`        | The string-encoded s-expression AST (`sexp.nv`, `text.nv`)                   |
| `compiler/parser/`     | Parser: `tokens`, `types`, `expressions`, `statements`, `members`, `declarations` |
| `compiler/loader/`     | Program loading and `import "file.nv"` resolution (`paths.nv`, `loader.nv`)  |
| `compiler/codegen/`    | Novus -> C: `index`, `checks`, `modules`, `builtins`, `calls`, `expressions`, `statements`, `methods`, `program` |
| `compiler/runtime/`    | `runtime.nv`: `runtime/novus_rt.h` embedded as a string (generated by `tools/embed.nv`) |
| `compiler/driver/`     | The `novusc` command line (`cli.nv`) and build/run steps (`build.nv`)         |
| `compiler/main.nv`     | Entry point                                                                   |
| `runtime/novus_rt.h` embedded as a string (generated by `tools/embed.nv`)    |
| `compiler/main.nv`      | The `novusc` driver (`run`, `build`, `emit`, `check`)                         |
| `runtime/novus_rt.h`    | The C runtime every compiled program embeds (values, classes, os/path/json/http) |
| `scripts/`              | `bootstrap.sh`/`.cmd`/`.ps1`, `snapshot.sh`, `cross.sh`                       |
| `test/`                 | Golden tests (`run_tests.sh`) and the self-hosting ladder (`selfhost.sh`)     |
| `examples/`             | Example programs ([overview](examples/README.md))                             |
| `vscode-novus/`         | VS Code extension: highlighting, language server, run/build commands           |

## Self-hosting and hacking on the compiler

`test/selfhost.sh` (part of `make test`) verifies the ladder after every
change:

1. the snapshot `bootstrap/novusc.c` builds with a plain C compiler
2. it compiles the current `compiler/*.nv` sources
3. that compiler compiles itself
4. **fixpoint**: the second-generation compiler emits byte-identical C
   (stage 2 == stage 3), and that C is what is checked in as the snapshot

After changing anything under `compiler/` or `runtime/`, run
`make snapshot` (`scripts/snapshot.sh`): it re-embeds the runtime into
`compiler/runtime/runtime.nv`, rebuilds through the ladder, checks the fixpoint and
writes the new `bootstrap/novusc.c`. Commit the snapshot together with the
sources. The only rule: the snapshot must be able to compile the sources, so
when you add a builtin, regenerate the snapshot before the compiler sources
start using it (see [BOOTSTRAP.md](BOOTSTRAP.md)).

## Tests

```sh
make test              # golden tests + self-hosting ladder
test/run_tests.sh      # only the golden tests (filter: test/run_tests.sh classes)
```

## Editor support

A VS Code extension with syntax highlighting, a language server and run/build
commands lives in [vscode-novus/](vscode-novus/README.md).
