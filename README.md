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

Which C compiler is used matters: `gcc -O2` optimizes the generated code
noticeably better than clang (8 ms vs 25 ms on a ten million iteration loop),
so `novusc` picks `gcc` when it is installed. Override with `NOVUS_CC`.

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
constants, concurrency (`thread`, `virtual`, `async`, `await`, `sync`) and
the stdlib modules `os`, `path`, `json` and `http`.

Values are dynamically typed at run time, but the compiler proves which
locals are always integers and generates them as unboxed 64-bit values, so
counting loops and numeric code compile to plain C. Arrays, maps and objects
are passed by reference. Integers are 64 bit and never allocated (tagged
pointers), a boxed value is 16 bytes, objects are one block of value, header
and field slots (32 bytes for a one-field class), string literals are static
values the compiler lays down rather than something the runtime boxes, and
maps are hash indexed but always iterate in key order. Missing
interface/abstract implementations, unknown names and unknown fields are
errors.

Memory is managed by a mark-sweep garbage collector built into the runtime
(`runtime/nv_memory.h`): allocation is a pointer increment in a per-thread
buffer, a collection runs once as much has been allocated as was live after
the previous one (at least 8 MB), stacks are scanned conservatively, and
regions that come up empty go back to the operating system - so a program's
memory tracks what it is actually using instead of growing with everything
it ever allocated. `NOVUS_GC_MIN=<MB>` (per thread that is running, so
that more threads do not mean more pauses) and `NOVUS_GC_GROWTH=<percent>` tune
the pacing, `NOVUS_GC_STATS=1` prints a summary at exit and `NOVUS_GC=off`
disables collection.

Statements `println`, `print`, `eprintln`.
Strings: `length`, `charAt`, `substring`, `indexOf`, `contains`,
`startsWith`, `endsWith`, `split`, `replace`, `trim`, `toUpper`, `toLower`.
Arrays: `length`, `append`, `pop`, `insert`, `remove`, `contains`,
`indexOf`, `join`, `clear`. Maps: `length`, `has`, `keys`, `values`,
`remove`, `get`.

## Concurrency

`thread f(...)` runs `f` on an operating system thread. `virtual f(...)` runs
it on a virtual thread: a stack of its own, a hundred kilobytes reserved and
only the pages it touches ever resident, that a small pool of carrier threads
(one per processor by default) runs. Both hand back a task, and `await` is
the value it ends up with.

```nv
import thread

method render(integer frame): string {
    return "frame ${frame}"
}

async method load(string name): string {   // its calls start on a virtual
    return readFile(name)                  // thread and hand back a task
}

method main {
    var tasks = []
    for (n in [1, 2, 3]) {
        tasks.append(virtual render(n))
    }
    println thread.joinAll(tasks)          // ["frame 1", "frame 2", "frame 3"]

    println await load("notes.txt")
    println await thread render(9)         // an operating system thread
}
```

Blocking a virtual thread - `await`, `thread.sleep`, a lock, a channel -
parks its stack and hands its carrier to the next runnable one, so a hundred
thousand of them cost about as much memory as a hundred operating system
threads. Blocking in a way the runtime cannot see (`os.sleep`, reading a
file, `exec`) blocks the carrier itself, which is what `thread` is for.
Stacks are switched with `ucontext` on unix and with fibers on Windows; where
neither exists a virtual thread falls back to an operating system thread and
nothing about the program's meaning changes.

Threads share the values they are handed, so anything two of them write needs
a lock. `sync { ... }` takes the program-wide one, `sync (lock) { ... }` one
from `thread.mutex()`. Both are re-entrant and both give the lock back on
every way out of the block, `return`, `break` and `continue` included.

```nv
var total = 0

method count(integer times) {
    var i = 0
    while (i < times) {
        sync {
            total = total + 1
        }
        i = i + 1
    }
}
```

The [thread](std/thread.nv) module has the rest: channels, counters, locks,
groups, `joinAll`, `sleep`, `yield` and the size of the pool. `NOVUS_THREADS`
sets how many carriers virtual threads may use (default: the processors of
the machine), `NOVUS_VSTACK` the stack of one in kilobytes (default 128).
The program ends when `main` returns, whatever is still running - await what
has to finish. [examples/11-concurrency](examples/11-concurrency) works
through all of it.

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
| [thread](std/thread.nv) | tasks (`join`, `joinAll`, `done`), `sleep`/`yield`, locks (`mutex`, `lock`, `tryLock`), channels (`channel`, `send`, `recv`, `close`), counters, groups, `cpus`/`parallelism` |

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
| `compiler/runtime/`    | `runtime.nv`: the runtime headers embedded as one string (generated by `tools/embed.nv`) |
| `compiler/std/`        | `stdlib.nv`: the `std/` modules embedded as strings (generated by `tools/embedstd.nv`) |
| `std/`                 | The standard library, one Novus module per file                              |
| `compiler/project/`    | `project.nv` manifests (`manifest.nv`) and git dependencies (`deps.nv`)      |
| `compiler/driver/`     | The `novusc` command line (`cli.nv`) and build/run steps (`build.nv`)         |
| `compiler/main.nv`     | Entry point                                                                   |
| `runtime/`              | The C runtime every compiled program embeds: `novus_rt.h` includes one part per subsystem (`nv_values.h`, `nv_memory.h` - allocator and garbage collector, `nv_classes.h`, `nv_threads.h`, `nv_json.h`, ...) |
| `scripts/`              | `bootstrap.sh`/`.cmd`/`.ps1`, `snapshot.sh`, `cross.sh`                       |
| `test/`                 | Golden tests (`run_tests.sh`) and the self-hosting ladder (`selfhost.sh`)     |
| `examples/`             | Example programs ([overview](examples/README.md))                             |
| `vscode-novus/`         | VS Code extension: highlighting, language server, run/build commands           |
| `website/`              | Documentation site (React + Vite + MDX + Tailwind, built with bun)            |

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

## Benchmarks

Ten workloads implemented in eight languages, measured on one machine and
rendered on the [benchmarks page](website/src/routes/Benchmarks.tsx) of the
site. Sources, runner and raw results live in [benchmarks/](benchmarks/README.md).

```sh
make bench
```

Novus matches the native compilers on unboxed arithmetic (integer loop,
primes, mandelbrot) and uses the least memory of all of them there. On
sorting, object allocation, dynamic arrays and hash maps it is within a few
percent of C++ and Crystal and ahead of Go. It still trails them where a lot
of small strings are built and hashed (string building, word frequency),
which is where the dynamic value representation costs the most. It is faster
than Java, Node and Python on every one of the ten workloads.

A language whose toolchain is not installed on the measuring machine is left
out of `results.json` and of the site rather than shown as an empty column.

## Documentation site

[website/](website/README.md) is the documentation site: React, React Router,
Vite, MDX and Tailwind CSS 4, built with bun. It generates its examples
browser, standard library reference and syntax highlighting from this
repository, so it cannot drift from the language.

```sh
cd website && bun install && bun run dev
```

## Examples

[examples/](examples/README.md) holds 258 programs from `hello world` to a
small virtual machine, grouped from easy to complex: basics, control flow,
methods, strings, arrays, maps, classes, the standard library, algorithms,
complete projects and concurrency. Each one runs on its own and is verified
against a golden file.

```sh
novusc run examples/01-basics/001-hello-world.nv
make examples                    # run all 258
```

## Tests

```sh
make test              # golden tests, examples and the self-hosting ladder
test/run_tests.sh      # only the golden tests (filter: test/run_tests.sh classes)
test/run_examples.sh   # only the examples (filter: test/run_examples.sh maps)
```

## Editor support

A VS Code extension with syntax highlighting, a language server and run/build
commands lives in [vscode-novus/](vscode-novus/README.md).
