# Novus

[![CI](https://github.com/ezTxmMC/novus/actions/workflows/ci.yml/badge.svg)](https://github.com/ezTxmMC/novus/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](LICENSE)
[![Releases](https://img.shields.io/github/v/release/ezTxmMC/novus?include_prereleases)](https://github.com/ezTxmMC/novus/releases)

**Novus is a self-hosting programming language that compiles to portable C.**
Write Novus, get a native executable, no runtime to install on the target
machine - just a C compiler to build it.

```nv
package main

method main {
    var names = ["Ada", "Grace"]
    for (n in names) {
        println "Hello, ${n}!"
    }
}
```

## Who this is for

- **You want a scripting-language feel with native performance.** Dynamically
  typed, no boilerplate, but the compiler proves a typed path where it can
  and generates plain `long long`/`double` arithmetic for it - see
  [Typed code](#typed-code) and the [benchmarks](#benchmarks): it matches C++
  on unboxed arithmetic and beats Go, Java, Node and Python across the board.
- **You want one binary, no runtime to ship.** `novusc build` produces a
  single native executable; `novusc emit` produces a single portable C file
  that any C compiler on any platform can build - no Novus install needed on
  the machine that runs it.
- **You want built-in web UI without a JS build pipeline.** `.nvh` files are
  server-rendered, live components (state on the server, only the changed
  parts patched into the page) - see [Web components](#web-components-nvh).
- **You're curious how a self-hosting compiler works.** `novusc` is written
  in Novus and compiles itself to a byte-identical fixpoint; the whole
  pipeline (lexer → parser → codegen → C) is readable Novus source, not a
  black box. See [ARCHITECTURE.md](ARCHITECTURE.md).

There is no other implementation of Novus: a checked-in C snapshot of the
compiler bootstraps everything, so the only thing you need to build it
yourself is a C compiler (gcc, clang or `zig cc`). Prebuilt binaries are also
published for every release, so you don't need to build it at all to just
use it.

<!--toc:start-->
- [Novus](#novus)
  - [Who this is for](#who-this-is-for)
  - [Quick start](#quick-start)
  - [Using novusc](#using-novusc)
    - [Build latency: the cache of `run`](#build-latency-the-cache-of-run)
  - [Projects and dependencies](#projects-and-dependencies)
  - [Packages and imports](#packages-and-imports)
  - [Language](#language)
    - [Evaluation order](#evaluation-order)
    - [Cascades](#cascades)
    - [C blocks](#c-blocks)
    - [Project values](#project-values)
    - [Numbers](#numbers)
    - [Typed code](#typed-code)
  - [Concurrency](#concurrency)
  - [Web components (.nvh)](#web-components-nvh)
  - [Standard library](#standard-library)
  - [Architecture](#architecture)
  - [Self-hosting and hacking on the compiler](#self-hosting-and-hacking-on-the-compiler)
  - [Repository statistics](#repository-statistics)
  - [Benchmarks](#benchmarks)
  - [Documentation site](#documentation-site)
  - [Examples](#examples)
  - [Tests](#tests)
  - [Editor support](#editor-support)
  - [Contributing and community](#contributing-and-community)
<!--toc:end-->

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

To install a release instead of building, download the files of your target
from the [releases](https://github.com/ezTxmMC/novus/releases) and move them
onto the `PATH` - on Linux:

```sh
TARGET=x86_64-linux-gnu        # or aarch64-linux-gnu, x86_64-linux-musl
BASE=https://github.com/ezTxmMC/novus/releases/latest/download   # or .../download/<tag>
curl -fLO $BASE/novusc-$TARGET && curl -fLO $BASE/novus-lsp-$TARGET
chmod +x novusc-$TARGET novus-lsp-$TARGET
sudo mv novusc-$TARGET /usr/bin/novusc && sudo mv novus-lsp-$TARGET /usr/bin/novus-lsp
```

A C compiler is still needed to compile programs. The documentation site has
the macOS and Windows steps.

## Using novusc

```
novusc run <file.nv> [args...]     compile (cached) and run it
novusc build <file.nv> [options]   compile to a native executable
novusc emit <file.nv> [-o out.c]   write the generated C (single file, runtime included)
novusc check <file.nv> [--offline] parse and analyze only (starts no C compiler)
novusc nvh <file.nvh>              the Novus code a .nvh component compiles to
novusc cache [dir | clear]         the cache of compiled programs
novusc version
```

`run` and `build` also take a `.nvh` component: it is served as the page at
`/` (see [Web components](#web-components-nvh)).

Build options: `-o <path>`, `--cc <compiler>`, `--cflags <flags>`,
`--target <triple>` (cross compile through `zig cc -target`, e.g.
`--target x86_64-windows-gnu`), `--keep-c`, `--no-runtime`, `--cache` (take the
executable from the cache of `run`, see below) and `--offline`. The C compiler
defaults to `$NOVUS_CC`, then `cc` (`gcc` on Windows); `$NOVUS_CFLAGS` adds
flags to the default `-O2 -ffp-contract=off` (no fused multiply-add: floats
print the same everywhere). Programs are single, self-contained C files: `novusc emit` output can
be handed to any C compiler on any platform.

Which C compiler is used matters: `gcc -O2` optimizes the generated code
noticeably better than clang (8 ms vs 25 ms on a ten million iteration loop),
so `novusc` picks `gcc` when it is installed. Override with `NOVUS_CC`.

### Build latency: the cache of `run`

The C compiler is what takes the time (about 0.2-0.3 s even for a hello
world, the runtime is compiled with every program). `novusc run` therefore

- keeps what it compiled: the executable is stored under a key that is the
  hash of the generated C, the C compiler and its `--version`, the flags and
  the novusc version, and the next `run` of the same program starts it
  directly - a hello world takes 4 ms instead of 190 ms. Any change to the
  program, a standard module, a dependency, the compiler or the flags is a
  different key, so a stale executable is never run;
- optimizes with `-O1` instead of `-O2` (`build` keeps `-O2`). `NOVUS_RUN_OPT`
  (`0`, `1`, `2`, `3` or `s`) changes it.

Measured on the benchmark programs (`benchmarks/novus`, Linux, gcc), the time
of `novusc build` plus one run of the result, best of three:

| `-O` | C compiler time | run time | together |
| ---- | --------------- | -------- | -------- |
| 0    | 4.4 s           | 3.63 s   | 8.0 s    |
| 1    | 3.6 s           | 0.64 s   | 4.3 s    |
| 2    | 7.0 s           | 0.55 s   | 7.6 s    |

(sums over the twelve programs; `-O0` does not even compile faster than `-O1`
here, and nbody alone takes 3 s with it). `-O1` costs the compute-heavy programs at most about 20%
over `-O2` (nbody 459 ms against 387 ms) and halves the compile time, which
is what `run` is waiting for; a program that runs for many seconds should be
`build`t, or run with `NOVUS_RUN_OPT=2`.

The cache lives in `$NOVUS_CACHE`, default `$XDG_CACHE_HOME/novus` or
`~/.cache/novus` (`~/Library/Caches/novus` on macOS,
`%LOCALAPPDATA%\novus\cache` on Windows); `NOVUS_CACHE=off` (in any case)
switches it off (then every run compiles to a temporary file, as before). It
is capped at `$NOVUS_CACHE_LIMIT`, default `256M` (`K`, `M`, `G` or plain
bytes, at most 1 PiB; a bad value is reported before anything is compiled):
when a new program does not fit, the least recently used ones are deleted
first. Executables are written under a temporary name and renamed into
place, so several `novusc` processes can use one cache at the same time (the
same program compiled twice at once is harmless). Only files named like a
cache entry (a 32 digit hex key, `.exe` on Windows, and its `.use` stamp) or
like a leftover of a killed compile (`tmp-<pid>-<key>`, with `.c` or `.exe`,
removed when over an hour old) are ever deleted, and never a folder -
`NOVUS_CACHE` can point at any folder. `novusc cache` shows what is in it
(a folder that does not exist yet is an empty cache), `novusc cache dir` the
folder and `novusc cache clear` empties it. `build --cache` takes the
executable from the same cache and replaces `-o` with a copy of it (not
combinable with `--keep-c` or `--no-runtime`). A cache folder that cannot be
created or written is ignored: the program is compiled to a temporary file as
without a cache, and a program that is cached already still runs from a read
only folder.

The cache runs what it finds: there is no check of who wrote a file there.
Keep it in a folder only you can write (the default is below your home);
pointing `NOVUS_CACHE` at a shared writable folder such as `/tmp` lets
anyone who can compute a program's key plant an executable for it.

`check` only parses and analyzes; it never starts a C compiler (it needs
neither `cc` nor `gcc` to be installed). A tool that checks files of a
project without side effects (a language server) passes `--offline` (`check`
ignores arguments it does not know): a dependency that is not in `$NOVUS_DEPS` yet is then an error
(`novusc: dependency <module> is not fetched ...`, exit code 1) instead of a
`git clone`. The `fetching <module> @ <version>` progress line of a clone goes
to stderr, so the standard output of `check`, `emit` and `deps` stays the
result.

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
import "github.com/user/geo/shapes"           // a package (folder) of the module
import "github.com/user/geo/shapes/circle"    // one file of it
```

## Packages and imports

A folder is a package. The project root (the folder of `project.nv`, else
the one of the main file) holds the main package; every folder below it is a
package whose files declare its name:

```nv
package main

import geo              // every .nv/.nvh file in geo/ (package geo)
import geo/shapes       // the nested package geo/shapes/ (package shapes)
import @geo/circle      // only geo/circle.nv - no extension
import @helpers         // helpers.nv of the root folder
import json             // no such folder: the standard module
```

There are no relative file imports. What packages declare is used
unqualified (`area(c)`) as long as one package alone has the name; when two
have it, the call names the package: `geo.describe()`, `draw.describe()`.
Inside a package its own names come first, and the main package's names
always win.

## Language

The showcase in [test/syntax.nv](test/syntax.nv) and the golden tests in
[test/cases/](test/cases/) cover the language: packages (folders) and imports,
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

Top-level constants and enum constants are initialised before `main`, each
after the ones it uses: a global may use a global declared later (in the same
or another file), enum constants (`final DEFAULT = Mode.FAST`) and functions
that do; an enum constant may be built from a global. What a unit uses is read
off the generated C, so it covers calls through free functions and the
constructors and methods of the classes it mentions, base classes included
(not calls through a value's method name). Units that do not depend on each other keep the order of
the source. Two initialisers that use each other (or a global that uses itself)
are a compile error that names the cycle: `error: constant A: initialisers use
each other: global A -> global B -> global A`. A cycle that only exists through
a function body is ordered by source position; the global it reads early is
`null` until its own initialiser has run.

### Evaluation order

Operands are evaluated **left to right**, whatever C compiler builds the
program (gcc evaluates the arguments of a C call right to left, clang left to
right - the generated code does not leave it to either):

- `a() + b()` and every other binary operator: `a()` before `b()`; in a chain
  `a() + b() + c()` all operands in that order, then the additions.
- Calls: the arguments in order; for `x.m(a(), b())` the receiver `x` first,
  then `a()`, then `b()`. The same holds for constructors, module functions
  and `thread`/`virtual` calls (their arguments are evaluated by the thread
  that starts them).
- Array literals element by element, map literals key then value, entry by
  entry, object literals field by field.
- `a[i]` evaluates `a`, then `i`; `a[i] = v` evaluates `a`, `i`, then `v`;
  `o.f = v` evaluates `o`, then `v`. `return f(g(), h())` is `g`, `h`, `f`.
- `&&` and `||` evaluate the right operand only when the left one asks for it.
- Enum constants: the constructor arguments of `RED(a(), b())` in order.
- A module member written without parentheses (`io.readLine`, `os.args`) is a
  call of a parameterless function, and ordered like one; a module's global is
  a plain read.
- An operand that reads a global, a field or an element is evaluated before a
  later operand that calls code, so it sees the value from before that call:
  after `n = 0`, `n + bump()` adds the old `n`. (Two operands that only read
  have nothing to order: no code runs between them.)

There are no assignment expressions and no compound assignment (`+=`), so a
statement `x = e` has nothing else to order. Operands that cannot have an effect
(literals, locals, arithmetic on them) are not ordered and cost nothing: the
compiler only splits an operand list up when an operand contains a call, a
method call, `await` or `thread`/`virtual` and another operand is not a
literal or a local. When several operands that have no effect fail at run time
(`a[9] + b[9]`, both out of range), which of the errors is reported is not
defined.

Values are dynamically typed at run time. Integers are 64 bit and wrap around
on overflow; a small one (62 bit) is a tagged pointer and never allocated, and
so is a float whose magnitude lies between 2^-126 and 2^127 (it sits in the word
of the value itself; NaN, the infinities, `-0.0` and the extreme magnitudes are
16 byte heap cells, and both forms behave the same). Arrays, maps and objects
are passed by reference. Objects are one block of value, header and field slots
(32 bytes for a one-field class), string literals are static values the
compiler lays down rather than something the runtime boxes, and
maps are hash indexed but always iterate in key order (`remove` is O(1);
adding a key out of order to a map that was already iterated costs a binary
search at the next iteration, not a sort of the map; removing a key from a
map with a pending order sorts again at the next iteration). Missing
interface/abstract implementations, unknown names and unknown fields are
errors. Array `remove(i)`/`insert` shift the tail (O(n), so `remove(0)` in a
loop is quadratic) and `contains`/`indexOf` scan.

### Cascades

`receiver..section..section` runs every section on the receiver and the whole
expression is the receiver itself - not what a section returns (the cascade
of Dart):

```nv
var page = Page{title="Home"}
    ..addSection("intro")        // a method call: its result is dropped
    ..addSection("usage")
    ..footer = "(c) 2026"         // a member assignment
    ..tags["draft"] = true        // an element of a member (`..[i] = v` is an element of the receiver)

var names = []..append("Ada")..append("Grace")    // names is the array
```

A section is a selector chain on the receiver (`..a().b`, `..items[0].name()`),
optionally ending in an assignment; the value of an assignment is parsed
without another cascade, so `..a = 1..b = 2` are two sections. The receiver is
evaluated once, the sections run in order, and a cascade binds to the operand
on its left (`x + y..m()` cascades on `y`); put it in parentheses to cascade on
a larger expression.

### C blocks

`c { ... }` is raw C inside Novus - for a system call, a library or a hot
loop that the language does not offer. The text up to the matching brace is
copied into the generated C unchanged (braces in C strings, character literals
and comments do not count):

```nv
c {
    #include <unistd.h>                    // outside of methods: before all declarations,
    static long long pid(void) {           // after the runtime, in the order of the source
        return (long long)getpid();
    }
}

method processId(): integer {
    c { return nv_int(pid()); }            // the whole body may be C
}

method bump(integer by) {
    var total = 0
    c {                                    // a statement: a C block at this place
        $total = nv_int(nv_ival($total) + nv_ival($by));
    }
    println total
}
```

`$name` is the value (an `nv`) of a variable, parameter, field or global of the
program, written exactly like Novus reads it, so it can be assigned to as well;
`$$` is a dollar sign. An unknown name is a compile error. A method that
contains a C block keeps all its variables boxed, so `$x = ...` always works.
Outside of methods there are no variables: only `$$`. The runtime that every
program embeds is in scope (`runtime/novus_rt.h`): `nv` is the value type,
`nv_int(long long)`/`nv_ival(nv)`, `nv_float(double)`/`nv_fval(nv)`,
`nv_str(const char *)`, `nv_bool(int)`, `nv_nil`, `nv_println(nv)`. The C is not
checked by Novus: its mistakes are the C compiler's, and a program with C
blocks is exactly as safe as the C in it. `novusc check --offline` does not
compile it and `novusc emit` prints it as it is. In a `.nvh` header the same
`c { }` works inside its methods.

### Project values

`import project` gives a program the settings of the `project.nv` it is built
from, fixed at compile time:

```nv
import project

method main {
    println project.name() + " " + project.version()    // "github.com/user/app 0.1.0"
    for (dep in project.requires()) {                    // maps with "module" and "version"
        println dep["module"] + " " + dep["version"]
    }
}
```

`name()`, `version()`, `main()`, `lib()`, `output()`, `novus()` are strings (`""`
when the manifest does not set them; `main` and `output` as the build uses them),
`requires()` and `replaces()` are arrays of maps (`module`/`version` and
`module`/`path`), `get(key)` reads one setting by its `project.nv` name
(`"project"`, `"version"`, ...) and `all()` is the map of them. Without a
`project.nv` the import is a compile error; a dependency that imports `project`
sees the manifest of the program that is built, not its own. `novusc version`
and `novus-lsp --version` are `project.version()` of `compiler/project.nv` and
`lsp/project.nv`.

### Numbers

The same rules hold whether the compiler computes with boxed values or - see
the next section - with plain C numbers:

- `integer` is 64 bit two's complement and wraps around (`max + 1` is the
  lowest integer). Integer `/` truncates toward zero; `x / 0` and `x % 0` are
  `0` (and so are `/ 0.0` and `% 0.0` for floats); dividing the lowest integer
  by `-1` gives the lowest integer and `% -1` gives `0`.
- An integer next to a float is converted to a float first: `7 / 2` is `3`,
  `7 / 2.0` is `3.5`, and `float x = 7 / 2` is `3.0` - the division is the
  integer one, the conversion comes after.
- A float converted to an integer (`integer n = 2.9`, a setter) is truncated
  toward zero, and `math.floor`, `ceil` and `round` give integers; NaN and a
  float that does not fit give the lowest integer, on every platform.
- NaN is neither smaller than, larger than nor equal to anything, itself
  included: every comparison but `!=` is `false`. Two integers are compared as
  integers (exactly, also beyond 2^53), an integer and a float as doubles.
- A field declared `integer` or `float` converts what is stored into it like
  the constructor and the setter do, whichever way it is stored: `this.f = v`,
  a bare `f = v` inside the class, `p.f = v` from outside, `p.f(v)` and an
  object literal `P{f = v}`. A value that is not a number (a string, `null`) is
  kept as it is.

### Typed code

Where the compiler can tell what an expression is, it computes it as a C
`long long` or `double` and boxes the result once, where a boxed value is
needed - no tag tests, no allocation, plain registers. An expression has a
kind (`codegen/kinds.nv`) when it is

- a number literal, or a local that only ever holds integers (every
  declaration and assignment is an integer expression) or only floats - this
  includes parameters declared `integer` and `float` that the body does not
  assign something else;
- `+ - * / % & | ^ << >>` on integers, `+ - * / %` on floats, an integer
  mixed with a float (the integer is converted), and a float with *any* value
  on the other side of `- * / %`: the value is checked when it is read, and a
  string or `null` there is the same runtime error the boxed operator reports;
- a call of a method with an unboxed worker (below), or of the numeric natives
  of `math` (`sqrt pow sin cos tan atan2 log exp floor ceil round toFloat32`);
- a field declared `integer` or `float`, read from `this` or from an object
  whose class the compiler can tell: a parameter declared `Body`, an element of
  a parameter declared `array<Body>`, a local assigned only from those or from
  `Body(...)`. The class of the object is checked when the field is read; an
  object of another class, a subclass included, is asked the ordinary way.

A method with numbers in its signature is compiled twice: an unboxed worker
takes its `integer`/`float` parameters as `long long`/`double` (the ones its
body does not reassign) and returns a number as one when everything it returns
is an expression of known kind, and the method every other call reaches
converts boxed arguments exactly as the body always did, calls the worker and
boxes the result. `fib(n - 1) + fib(n - 2)` in `fib(integer n): integer` is
two C calls and an addition.

The types written in a signature are *trusted for fields only where every store
converts* (see above); a field that holds a value that is no number of its
declared kind (a string, `null`) is reported when typed code *computes with it*
- `error: expected a float but found string` - instead of being treated as a
number. Where the field is only read as it is, nothing is checked and the value
is the one that was stored: passed as an argument, printed, compared with `==`
or `!=`, returned, kept in a local (`var v = n.value()`) or handed to a method
with number parameters (which convert it as they always did) - the same with or
without a neighbouring operand that has effects. Arithmetic and `<` `>` `<=`
`>=` on such a field are the computing: `total + n.value()` with an integer
`total` and a field that holds `"7"` is the type error, not the string `"07"`. Parameters and returns are
not trusted: an argument that is not what the parameter says is converted by
the entry of the method as before, and a method that can return something
else than a number has no unboxed result. `array<float>` says nothing about
the elements: reading one is an ordinary value.

Two more promises hold in typed code. Operands are evaluated left to right
even when they are unboxed temporaries (and the field of an object whose class
is only a guess is read in order too, because an object of another class runs
that class's method). Of two operands that both fail, which reports first is
not specified. And floating point is one IEEE operation at a time: the
generated C is compiled with `-ffp-contract=off`, because a C compiler may
otherwise fuse `a * b + c` into one multiply-add (clang does by default, gcc on
processors that have one) and print other last digits than the boxed
arithmetic and other platforms do.

Floats in `array<float>` loops, vector math over objects and recursive integer
code are where this shows: `benchmarks/novus/nbody.nv` takes 0.38 s instead of
3.4 s, `spectral.nv` 22 ms instead of 141 ms (see the table in
[benchmarks/README.md](benchmarks/README.md)). Call sites cache what a member
name resolved to (a field slot or a method) per class, so method calls on
objects, getters and setters cost a compare instead of a lookup.

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

Strings are byte strings and every string builtin works on their length, not on
a C string: `==`, `<`, `indexOf`, `contains`, `startsWith`, `endsWith`, `split`,
`replace`, `words`, `join`, `sort`, `hash.fnv1a` and `hash.crc32` treat a NUL
byte (`chr(0)`, `io.readBytes`) as an ordinary character - they used to stop at
the first one - and none of them copies a substring view to find a terminator
(`indexOf` on a 1 MB view cost 90 us before it looked at a byte). `replace`
returns the receiver itself when there is nothing to change and a new string
otherwise; `trim` returns the receiver itself when there is nothing to trim and
a copy otherwise - not a view, so a short result does not keep a big string
alive (`substring` does return a view). Map keys and the text printed by
`println` are still C strings and end at a NUL byte: after
`m["x" + chr(0) + "y"] = 1`, `m.has("x")` is true.

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

## Web components (.nvh)

A `.nvh` file is HTML with Novus in it - like a PHP page - and at the same
time a component, like a `.vue` or `.svelte` file. It is rendered on the
server, its state stays there, and the page is live: events go to the
server, handlers run, and only the components whose output changed are
patched into the page. Changes made elsewhere - another visitor, a timer -
are pushed over a server-sent event stream.

```html
<?nv
prop string label = "Count"          // set by the parent or the URL
ref integer count = 0                // state; the page follows it

method add(integer by) {
    count = count + by
    emit("change", count)            // for a parent listening with @change
}
?>
<div class="counter">
    <button @click="add(-1)" disabled={count <= 0}>-</button>
    <span class:high={count > 9}>{label}: {count}</span>
    <button @click="add(1)">+</button>
</div>
<style>
.counter { display: flex; gap: .5rem }
</style>
```

```sh
novusc run Counter.nvh               # serves it on http://localhost:8080
```

Templates have `{expr}`, `{@html expr}`, `{#if}`/`{:else}`/`{/if}`,
`{#for x, i in xs}`/`{/for}`, the PHP forms `<?= expr ?>` and
`<?nv statements ?>`, `attr={expr}`, `class:name={cond}`, events
(`@click="handler(arg, $value)"`, `@keydown.enter.prevent`, `@click={count = 0}`)
and two-way `bind="ref"`. Components are tags starting with an upper-case
letter (`<Counter start={3} key={id} @change="...">slot</Counter>`, found next
to the file or in `components/`), with props, slots, events and keys. An app
maps URLs to page components with the [web](std/web.nv) module:

```nv
import web
import pages                         // pages/Home.nvh, pages/User.nvh, ...

method main {
    web.page("/", Home())
    web.page("/user/:id", User())    // :id and ?query= become props
    web.files("/assets", "public")
    web.tick(1000)                   // tick() on open pages, changes pushed
    web.serve(web.port(8080))
}
```

The compiler turns `Counter.nvh` into a class `Counter based NvhComponent`
(`novusc nvh Counter.nvh` shows the code, line numbers stay those of the
`.nvh` file); the server is a single `poll()` loop, so handlers never race
and state shared through globals needs no lock. `web.view`, `web.render`,
`web.trigger` and `web.input` run components without a server, for tests.
[examples/web](examples/web) is a complete app (counters, a live clock, a
shared todo list and a chat); the [documentation](website/content/components.md)
has the details.

### Markdown components (.nvmd)

Markdown with Novus in it (`.nvmd`, like `.mdx` with JSX) is not part of the
compiler: the dependency
[github.com/ezTxmMC/nvh-markdown](https://github.com/ezTxmMC/nvh-markdown)
generates `.nvh` components from `.nvmd` files (`novusc run nvmd.nv build`),
which the compiler then loads like any other component. Its README has the
format and the setup.

## Standard library

The standard library lives in [std/](std/) - one Novus file per module,
embedded into `novusc`. A module is used through its name after
`import <module>`; its functions are namespaced (`strings.repeat(...)`),
so they never clash with your own. Functions marked `native "..."` are
implemented by the C runtime, everything else is plain Novus you can read.

| Module | What |
| ------ | ---- |
| [os](std/os.nv) | files and directories (`mkdir`, `listDir`, `removeAll`, `copy`, `realpath`, `isSymlink`, `modified`/`modifiedMillis`, ...), processes (`exec`, `output` - they inherit stdin; `run` -> `{code, output}` with stdin closed; `shellQuote`/`shellQuoteFor`), environment, `time`/`clock`/`sleep`, `hasCommand`, `envOr` |
| [path](std/path.nv) | `join`, `absolute`, `normalize`, `relative`, `dirname`/`basename`/`stem`/`extension`, `withExtension`, `segments`, `exists`/`isDir`/`isFile` |
| [json](std/json.nv) | `stringify`, `pretty`, `parse`, `tryParse` (null instead of aborting), `parseOr`, `isValid` (strict), `load`, `save`; surrogate pairs decode to real UTF-8, nesting is limited to 256 levels, the writer always emits valid JSON (ill-formed UTF-8 becomes U+FFFD, NaN/Infinity become `null`) |
| [http](std/http.nv) | `get`/`post`/`put`/`delete`, `request` -> `{status, ok, body, headers, error}`, `download`, `getJson`, `postJson` (driven by `curl`, https included) |
| [strings](std/strings.nv) | `repeat`, `padLeft`/`padRight`, `reverse`, `lines`, `words`, `count`, `lastIndexOf`, `capitalize`, `isDigit`/`isAlpha`/`isSpace`/`isNumeric`, `chars`, `stripPrefix`/`stripSuffix`, `truncate`, `compare` |
| [arrays](std/arrays.nv) | `sort`/`sortDesc`, `reverse`, `unique` (linear for arrays of strings or of numbers - NaN is always kept and costs nothing extra - pairwise like `contains` for other mixes), `range`, `slice`, `concat`, `sum`/`min`/`max`, `first`/`last`, `countOf`, `copy`, `chunk` |
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
| [io](std/io.nv) | `readLine`, `readAll`, `readLines`, `readBytes(n)` (exactly n bytes, NUL safe - protocol framing), `eof` (both block a virtual thread's carrier: read on an OS `thread`), `write`, `writeErr`, `flush`, `prompt` |
| [unicode](std/unicode.nv) | UTF-8 text and UTF-16 positions: `utf16Length`, `byteToUtf16`, `utf16ToByte`, `codePointAt`, `fromCodePoint`, `isValidUtf8`, `charLength` |
| [test](std/test.nv) | `assert`, `assertEqual`, `report` |
| [toml](std/toml.nv) | `.toml` files: `parse`, `parseOr`, `isValid`, `errorOf`, `stringify`, `load`, `save` (tables, arrays of tables, inline tables, all string and number forms) |
| [yaml](std/yaml.nv) | `.yaml`/`.yml` files: the same functions - block and flow collections, quoted and block scalars (`\|`, `>`), comments; no anchors or multiple documents |
| [properties](std/properties.nv) | `.properties`/`.cfg`/`.ini` files: `parse` (flat, `[section]` prefixes keys), `sections`, `stringify`, `stringifySections`, `load`, `loadSections`, `save` |
| [config](std/config.nv) | any of them by extension: `load`/`save` (`.json`, `.toml`, `.yaml`, `.yml`, `.properties`, `.cfg`, `.ini`), `parse`/`stringify` by format, `get(value, "server.port", fallback)` |
| [web](std/web.nv) | `.nvh` pages and the live HTTP server: `page`, `files`, `tick`, `serve`, `port`; `view`/`render`/`trigger`/`input`/`document` for rendering without a server |
| [thread](std/thread.nv) | tasks (`join`, `joinAll`, `done`), `sleep`/`yield`, locks (`mutex`, `lock`, `tryLock`), channels (`channel`, `send`, `recv`, `close`), counters, groups, `cpus`/`parallelism` |

`io`, `json` and `os` carry what a protocol server (a language server speaking
stdio JSON-RPC, say) needs:

```nv
import io
import json
import os
import unicode

method readMessage(): string {
    var length = 0
    var line = io.readLine()
    while (line != "") {                      // headers end at the blank line
        if (line.startsWith("Content-Length: ")) {
            length = parseInt(line.substring(16, line.length()))
        }
        line = io.readLine()
    }
    return io.readBytes(length)               // exactly length bytes, no newline needed
}

method main {
    while (!io.eof()) {                       // true once stdin ended
        var request = json.tryParse(readMessage())    // null instead of aborting
        if (typeOf(request) == "map") {
            var column = unicode.utf16ToByte("a😀b", 3)   // LSP columns are UTF-16 units
        }
    }
    var git = os.run("git status --short 2>&1")           // {code, output}, the command's stdin is closed
    var listing = os.output("ls " + os.shellQuote(os.cwd()))
}
```

- `io.readBytes(n)` blocks until `n` bytes arrived and returns fewer only at end
  of input; it shares the buffer of `readLine`, keeps NUL bytes, and fails
  (catchably) above 1 GiB. `io.eof()` peeks one byte, so it waits while the
  input is open but idle. Both block the carrier thread of a `virtual` thread
  and with it the other virtual threads on it - read on an OS `thread`.
- On Windows stdin is switched to binary mode: `io.readAll()` and `readBytes`
  return the bytes as they are, so `\r\n` stays `\r\n` (it used to become `\n`,
  and a Ctrl-Z byte used to end the input). `readLine` strips the `\r`.
- `json.parse` decodes `\uD83D\uDE00` to one 4 byte UTF-8 character (a lone
  surrogate becomes U+FFFD), and `parse`, `isValid` and `stringify` stop at 256
  nesting levels - with an error you can trap, never a crash. `json.tryParse`
  returns `null` for text `parse` would abort on and leaves an enclosing
  `tryRun`'s error alone. `json.isValid` follows RFC 8259 strictly (no `01`,
  `1.`, raw control characters, only space/tab/LF/CR as whitespace, nothing
  after the value even behind a NUL byte); `parse` stays lenient there, but a
  NUL byte in an object key is an error. `json.parseOr` uses the strict check.
- TOML numbers are strict too (`port = 08080` is an error now; it read as 8080).
- `os.run(command)` gives the real exit code and stdout and closes the
  command's stdin; `os.output` and `os.exec` inherit the program's stdin, so a
  protocol server must use `run`. stderr is not captured (append `2>&1`).
  `os.shellQuote` quotes one argument for the shell (`shellQuoteFor("windows",
  ...)` tests the cmd.exe rules anywhere), `os.realpath` is `""` for a missing
  path, `os.modifiedMillis` has millisecond resolution.
- `exit(code)` and a `main` that returns end the process even while another OS
  thread is blocked reading stdin (`exit()` used to wait for the stdio lock the
  blocked read holds, so such a program never ended): stdout and stderr are
  flushed by hand, then the process ends without exit handlers.
- `os.removeAll` never follows a link: a symlink (on Windows also a directory
  symlink or junction) inside the tree, or the path itself (also when it is
  written with a trailing separator), is removed and what
  it points to stays.
- A concatenation whose result would exceed 2147483646 bytes raises the same
  catchable `text is longer than ... bytes` error as the string builders (it
  used to overflow the length). `tryError()` returns runtime error texts up to
  4095 bytes (it cut them at 511).
- `unicode` maps byte offsets to UTF-16 offsets and back (both clamp; an
  offset inside a character lands on its start) and reads code points; none
  of it aborts on malformed UTF-8.
- The compiler is about 17 times faster on itself (`novusc check
  compiler/main.nv`: 1.1 s to 0.07 s) and its time is linear in the number of
  statements, blocks and locals of a method: its syntax tree is made of
  strings, and finding the n-th child of a node used to rescan the node from
  its start for every child. The navigation (`ast.nodeChild`, `nodeCount`,
  `nodeHead`, `atomEnd`, `isList`, plus `sexpStr`, `sexpUnescape` and the token
  accessors) is native now (`runtime/nv_sexp.h`): a node is scanned once and
  the places where its children start are remembered in a small per-thread
  cache. Short nodes (under 512 bytes) share a direct mapped table where a
  newer node replaces an older one - scanning one again is cheap; long nodes
  (a block of thousands of statements) are kept until the next garbage
  collection, which makes the cache forget everything (the memory a string
  lived in can be handed out again). The cache is freed when an operating
  system thread ends. Code generation no longer copies the whole map of
  locals for every block (a block adds its locals to the map of the enclosing
  blocks and takes them out again at its end), which needed `map.remove` to
  cost one probe run instead of the size of the map. A method of 4000
  statements went from 21 s to 8 ms; the emitted C is byte-identical. One
  thing is still quadratic: the nesting depth of a single expression, because
  finding where a child ends scans that child's whole subtree at every level
  (`a + a + ... + a` with 1000 terms: 8 ms, 4000 terms: 90 ms).
- The Windows branches of `os.run` (`<NUL`), `os.realpath`
  (`GetFinalPathNameByHandle`), `os.modifiedMillis`, the cmd.exe quoting, the
  link handling of `os.removeAll`, `_exit` and the binary stdin were cross compiled (zig cc, x86_64-windows-gnu) and the
  golden tests run under Wine; they have not been run on a real Windows.

Free builtins need no import: `readFile`, `writeFile`, `fileExists`,
`removeFile`, `readLine`, `args`, `parseInt`, `parseFloat`, `chr`, `ord`,
`typeOf`, `exec`, `env`, `exit`, `platform`.

## Architecture

The compiler pipeline (lexer → parser → loader → codegen → generated C), the
full directory map and the key design decisions (why C, why a checked-in
bootstrap snapshot, why dynamically typed values with a typed fast path) now
live in their own document: **[ARCHITECTURE.md](ARCHITECTURE.md)**.

## Self-hosting and hacking on the compiler

`test/selfhost.sh` (part of `make test`) verifies the ladder after every
change:

1. the snapshot `bootstrap/novusc.c` builds with a plain C compiler
2. it compiles the current `compiler/*.nv` sources
3. that compiler compiles itself
4. **fixpoint**: the second-generation compiler emits byte-identical C
   (stage 2 == stage 3), and that C is what is checked in as the snapshot
5. a compiler built from the snapshot by the other of clang/gcc emits the same
   C (nothing the compiler generates depends on the C compiler's choice of
   evaluation order)

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

[website/](website/README.md) is the documentation site, written in Novus: nvh
components, the documentation as `.nvmd` files (Markdown with Novus) and
Tailwind CSS 4.3, through the dependencies
[nvh-markdown](https://github.com/ezTxmMC/nvh-markdown) and
[nvh-tailwindcss](https://github.com/ezTxmMC/nvh-tailwindcss). Examples, the
standard library reference and the benchmarks are read from this repository
when the site is built, so it cannot drift from the language.

```sh
cd website
../build/novusc run main.nv                # development server on :8080
../build/novusc run main.nv export dist    # the static site
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
make lsp-test          # novus-lsp: lint, golden cases, protocol scenarios, corpus, snippets
```

A golden case may bring `<name>.stdin` (the program's stdin), `<name>.stdin.sh`
(a script that writes it) or an empty `<name>.stdin.open`: stdin then stays
open and never delivers anything, for programs that must end while a thread is
blocked reading it. Every case is stopped and fails when it runs longer than
300 s (`CASE_SECONDS`) or the seconds in an optional `<name>.timeout`, so a hang
cannot stall the suite (guard: `timeout`, `gtimeout` or perl; the runner warns
when none exists). A case without `<name>.stdin` reads an empty stdin. An
optional `<name>.needs_mb` skips a case on a machine known to have less free
memory (MiB).

## Editor support

The recommended IDE for Novus is [Lumen IDE](https://github.com/ezTxmMC/lumen-ide).
Any other editor works through the language server as well:

`novus-lsp` is the language server: completion with auto import and snippets,
hover, signature help, definition, references, rename, symbols, folding,
formatting, code actions, semantic highlighting, and three layers of
diagnostics (its own, `novusc check`, and the optional Pureline style rules).
It is written in Novus, speaks LSP 3.17 over stdio and works in every editor
that can start a command.

```sh
make lsp                    # build/novus-lsp
make install                # novusc and novus-lsp into /usr/local/bin
novus-lsp --version         # novus-lsp 0.1.0
```

Releases carry `novus-lsp-<target>` next to `novusc-<target>`.

| Editor | Setup |
| ------ | ----- |
| VS Code | the extension in [vscode-novus/](vscode-novus/README.md); it finds the server through `novus.server.path`, `build/novus-lsp` in the workspace, then `PATH` |
| Neovim | `vim.lsp.config('novus', { cmd = { 'novus-lsp', '--stdio' }, filetypes = { 'novus' }, root_markers = { 'project.nv', '.git' } })` and `vim.lsp.enable('novus')` |
| Helix | a `[language-server.novus-lsp]` with `command = "novus-lsp"` and a `[[language]]` with `file-types = ["nv"]` and `language-servers = ["novus-lsp"]` |
| IntelliJ and the other JetBrains IDEs | the LSP4IJ plugin, command `novus-lsp --stdio`, file name pattern `*.nv` |
| Zed | needs a Zed extension that registers the language and returns `novus-lsp --stdio` (none exists yet) |
| Emacs, Sublime Text, Kate, ... | any LSP client: command `novus-lsp --stdio`, language id `novus` |

The [documentation site](#documentation-site) has the complete
setup of each editor, every setting (`novus.check.mode`, `novus.pureline.*`,
`novus.format.*`, ...) and the list of snippet prefixes; the sources are
[website/content/projects/editor.nvmd](website/content/projects/editor.nvmd) and
[language-server.nvmd](website/content/projects/language-server.nvmd).
`make snippets` regenerates `vscode-novus/snippets/novus.json` from the
catalogue in `lsp/services/snippets/`. The TypeScript server that
`vscode-novus/` still contains stays selectable (`novus.server.implementation`)
until `novus-lsp` has every one of its features.

## Contributing and community

Contributions are welcome - small fixes and examples as much as compiler and
language work.

- **[CONTRIBUTING.md](CONTRIBUTING.md)** - how to build, where the code you
  want to change lives, the one rule about the bootstrap snapshot, and what a
  good pull request looks like.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - how the compiler pipeline and the
  repository fit together, for orienting yourself before a bigger change.
- **[SECURITY.md](SECURITY.md)** - how to report a vulnerability privately.
- **[Issues](https://github.com/ezTxmMC/novus/issues)** - bug reports,
  feature discussion, and issues worth picking up as a first contribution.
