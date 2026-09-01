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

## Projects and dependencies

A directory with a `project.nv` is a project. The manifest has its own
explicit, line based syntax (values are quoted strings, `//` comments):

```nv
project "github.com/ezTxmMC/app"          // module path - others import it by this name
version "0.1.0"
main "main.nv"                            // entry file (default main.nv)
lib "lib.nv"                              // entry when imported as a dependency (default: main)
output "app"                              // executable name (default: last path segment)

require "github.com/user/geo" "v1.2.0"    // git tag, branch, commit hash or "latest"
require "github.com/user/colors"          // = latest
replace "github.com/user/geo" "../geo"    // develop against a local checkout
```

Inside a project the commands need no file argument: `novusc run`,
`novusc build` (uses `main`/`output`), `novusc check`, `novusc emit`.
`novusc init [module-path]` creates a manifest plus a hello world,
`novusc deps` fetches the dependencies (done automatically on build as
well), `novusc deps add <module> [version]` appends a `require`,
`novusc deps update` re-fetches everything.

Dependencies are git repositories, cloned with `git` into
`$NOVUS_DEPS` (default `~/.novus/deps/<module>@<version>`), transitively
through their own `project.nv`. Code imports a module by its path:

```nv
import "github.com/user/geo"                  // the module's entry file (lib)
import "github.com/user/geo/shapes/circle.nv" // a file inside the module
```

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
passed by reference. Integers are 64 bit and never allocated (tagged
pointers), objects are a single small block, so integer loops run in
constant memory and millions of objects cost ~100 bytes each. Missing
interface/abstract implementations and unknown names are compile-time
errors.

Statements `println`, `print`, `eprintln`.
Strings: `length`, `charAt`, `substring`, `indexOf`, `contains`,
`startsWith`, `endsWith`, `split`, `replace`, `trim`, `toUpper`, `toLower`.
Arrays: `length`, `append`, `pop`, `insert`, `remove`, `contains`,
`indexOf`, `join`, `clear`. Maps: `length`, `has`, `keys`, `values`,
`remove`, `get`.

## Standard library

The standard library lives in [std/](std/) - one Novus file per module,
embedded into `novusc`. A module is used through its name after
`import <module>`; its functions are namespaced (`strings.repeat(...)`),
so they never clash with your own. Functions marked `native "..."` are
implemented by the C runtime, everything else is plain Novus you can read.

| Module | What |
| ------ | ---- |
| [os](std/os.nv) | files and directories (`mkdir`, `listDir`, `removeAll`, `copy`, ...), processes (`exec`, `output`), environment, `time`/`clock`/`sleep`, `hasCommand`, `envOr` |
| [path](std/path.nv) | `join`, `absolute`, `normalize`, `relative`, `dirname`/`basename`/`stem`/`extension`, `withExtension`, `segments`, `exists`/`isDir`/`isFile` |
| [json](std/json.nv) | `stringify`, `pretty`, `parse`, `parseOr`, `isValid`, `load`, `save` |
| [http](std/http.nv) | `get`/`post`/`put`/`delete`, `request` -> `{status, ok, body, headers, error}`, `download`, `getJson`, `postJson` (driven by `curl`, https included) |
| [strings](std/strings.nv) | `repeat`, `padLeft`/`padRight`, `reverse`, `lines`, `words`, `count`, `lastIndexOf`, `capitalize`, `isDigit`/`isAlpha`/`isSpace`/`isNumeric`, `chars`, `stripPrefix`/`stripSuffix`, `truncate`, `compare` |
| [arrays](std/arrays.nv) | `sort`/`sortDesc`, `reverse`, `unique`, `range`, `slice`, `concat`, `sum`/`min`/`max`, `first`/`last`, `countOf`, `copy`, `chunk` |
| [maps](std/maps.nv) | `merge`, `fromPairs`, `invert`, `copy`, `entries`, `countValues` |
| [math](std/math.nv) | `sqrt`, `pow`, `floor`/`ceil`/`round`, trigonometry, `log`/`exp`, `abs`/`min`/`max`/`clamp`/`sign`, `gcd`/`lcm`, `powInt`, `isPrime`, `roundTo`, `toInt`/`toFloat` |
| [time](std/time.nv) | `now`, `clock`, `sleep`, `iso`, `format` (strftime), `parts`, `elapsedMs`, `duration` |
| [random](std/random.nv) | `seed`, `next`, `int`, `float`, `bool`, `pick`, `shuffle`, `string` |
| [fmt](std/fmt.nv) | `fixed`, `thousands`, `bytes`, `percent`, `table` |
| [log](std/log.nv) | `debug`/`info`/`warn`/`error` to stderr with levels and timestamps |
| [cli](std/cli.nv) | `parse(args)` -> positional arguments and `--options`, `option`, `flag`, `argument` |
| [base64](std/base64.nv) | `encode`, `decode` |
| [hash](std/hash.nv) | `fnv1a`, `crc32`, `bucket`, `hex` |
| [csv](std/csv.nv) | `parse`/`parseWith`, `stringify`/`stringifyWith` |
| [io](std/io.nv) | `readLine`, `readAll`, `readLines`, `write`, `writeErr`, `flush`, `prompt` |
| [test](std/test.nv) | `assert`, `assertEqual`, `report` |

Free builtins need no import: `readFile`, `writeFile`, `fileExists`,
`removeFile`, `readLine`, `args`, `parseInt`, `parseFloat`, `chr`, `ord`,
`typeOf`, `exec`, `env`, `exit`, `platform`.

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
| `compiler/std/`        | `stdlib.nv`: the `std/` modules embedded as strings (generated by `tools/embedstd.nv`) |
| `std/`                 | The standard library, one Novus module per file                              |
| `compiler/project/`    | `project.nv` manifests (`manifest.nv`) and git dependencies (`deps.nv`)      |
| `compiler/driver/`     | The `novusc` command line (`cli.nv`) and build/run steps (`build.nv`)         |
| `compiler/main.nv`     | Entry point                                                                   |
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
`make snapshot` (`scripts/snapshot.sh`): it re-embeds the runtime and the
standard library into `compiler/runtime/runtime.nv` and
`compiler/std/stdlib.nv`, rebuilds through the ladder, checks the fixpoint and
writes the new `bootstrap/novusc.c`. Commit the snapshot together with the
sources. The only rule: the snapshot must be able to compile the sources, so
when you add a builtin, regenerate the snapshot before the compiler sources
start using it (see [BOOTSTRAP.md](BOOTSTRAP.md)).

## Repository statistics

`make stats` (`novusc run tools/langstats.nv [dir] [--all] [--lines]`) prints
which languages make up the tree, GitHub-style - Novus included, generated
files and data/prose listed separately.

## Tests

```sh
make test              # golden tests + self-hosting ladder
test/run_tests.sh      # only the golden tests (filter: test/run_tests.sh classes)
```

## Editor support

A VS Code extension with syntax highlighting, a language server and run/build
commands lives in [vscode-novus/](vscode-novus/README.md).
