# Architecture

How the pieces of this repository fit together, and how a Novus program gets
from source text to a running executable.

## The big picture

```
 your-program.nv
       │
       ▼
┌──────────────┐     ┌───────────────┐     ┌───────────────┐
│    novusc    │────▶│  generated C  │────▶│  C compiler   │──▶ native executable
│ (self-hosted)│     │ (single file) │     │ (gcc/clang/..)│
└──────────────┘     └───────────────┘     └───────────────┘
```

Novus compiles to portable C, then hands that C to a normal C compiler. The
only tool you must have installed is a C compiler (`cc`, `gcc`, `clang` or
`zig cc`) — `novusc` itself is distributed either as a prebuilt binary or
built from a checked-in C snapshot, see [BOOTSTRAP.md](BOOTSTRAP.md).

## The compiler pipeline (`compiler/`)

```
source (.nv/.nvh)
   │
   ▼  compiler/lexer/        tokens.nv, chars.nv, lexer.nv
tokens
   │
   ▼  compiler/parser/       tokens, types, expressions, statements, members, declarations
AST (string-encoded s-expressions, see compiler/ast/)
   │
   ▼  compiler/loader/       packages, @ file imports, modules (paths.nv, loader.nv)
   │  compiler/manifest/     project.nv manifests, git dependencies (deps.nv)
flattened program
   │
   ▼  compiler/nvh/          .nvh template -> Novus class (nvh.nv)
   │
   ▼  compiler/codegen/      index, checks, builtins, calls, expressions, statements,
   │                         methods, program, initorder; typed code: kinds, inference,
   │                         typed, workers
generated C (one file, runtime included)
   │
   ▼  compiler/driver/       cli.nv (command line), build.nv (build/run steps), cache.nv
native executable
```

Everything under `compiler/` is Novus itself (self-hosting): `novusc` compiles
these sources to produce itself. Two parts are *embedded as strings* rather
than imported normally, because the compiler has to carry them inside every
binary it produces:

- `compiler/runtime/runtime.nv` — the C runtime headers (`runtime/*.h`),
  embedded by `tools/embed.nv`.
- `compiler/std/stdlib.nv` — the standard library (`std/*.nv`), embedded by
  `tools/embedstd.nv`.

Both are generated; edit the sources (`runtime/`, `std/`) and run
`make snapshot`, never the embeddings directly (see
[BOOTSTRAP.md](BOOTSTRAP.md)).

## Directory map

| Path | What |
| ---- | ---- |
| `bootstrap/novusc.c` | Generated C of the compiler — the stage 0 snapshot (never edit by hand) |
| `compiler/lexer/` | Lexer |
| `compiler/ast/` | The string-encoded s-expression AST |
| `compiler/parser/` | Parser |
| `compiler/loader/` | Program loading: packages, `@` file imports, modules |
| `compiler/manifest/` | `project.nv` manifests and git dependencies |
| `compiler/nvh/` | `.nvh` components: template -> Novus class |
| `compiler/codegen/` | Novus -> C, including the typed/unboxed code path |
| `compiler/runtime/` | The C runtime, embedded as a string |
| `compiler/std/` | The `std/` modules, embedded as strings |
| `compiler/driver/` | The `novusc` command line, build/run steps, the program cache |
| `compiler/main.nv` | Entry point |
| `runtime/` | The C runtime every compiled program embeds (`novus_rt.h` + one header per subsystem: `nv_values.h`, `nv_memory.h` — allocator and GC, `nv_classes.h`, `nv_typed.h`, `nv_threads.h`, `nv_json.h`, ...) |
| `std/` | The standard library, one Novus module per file |
| `scripts/` | `bootstrap.sh`/`.cmd`/`.ps1`, `snapshot.sh`, `cross.sh` |
| `test/` | Golden tests (`run_tests.sh`) and the self-hosting ladder (`selfhost.sh`) |
| `examples/` | 258 example programs ([overview](examples/README.md)) |
| `lsp/` | `novus-lsp`, the language server, written in Novus ([lsp/DESIGN.md](lsp/DESIGN.md)) |
| `vscode-novus/` | VS Code extension: highlighting, run/build commands, client of `novus-lsp` |
| `website/` | Documentation site (React + Vite + MDX + Tailwind, built with bun) |

## Key design decisions, and why

- **Compile to C, not to machine code directly.** C compilers already solve
  portability and optimization; Novus only has to be good at generating C
  they optimize well (see the "typed code" section of the README for how far
  that goes — unboxed numbers compiled to plain `long long`/`double`).
- **A checked-in C snapshot bootstraps everything** (`bootstrap/novusc.c`), so
  building Novus never requires a Novus compiler to already exist — only a C
  compiler. See [BOOTSTRAP.md](BOOTSTRAP.md) for the exact ladder and the
  fixpoint check that keeps it honest.
- **Dynamically typed values at run time**, with a compiler that proves a
  narrower, typed path (`codegen/kinds`, `inference`, `typed`, `workers`)
  wherever it safely can, instead of requiring static types everywhere.
  Correctness never depends on the typed path firing; it is purely an
  optimization.
- **One generated C file per program**, runtime included — `novusc emit`
  output can be handed to any C compiler on any platform, no separate
  Novus-specific linking step.
- **A mark-sweep GC built into the runtime** (`runtime/nv_memory.h`) rather
  than reference counting, to keep single-threaded code free of refcount
  traffic and to make multi-threaded sharing (see Concurrency in the README)
  straightforward.

## Where to go next

- Building and the bootstrap ladder: [BOOTSTRAP.md](BOOTSTRAP.md)
- Language reference and standard library: [README.md](README.md)
- Contributing a change: [CONTRIBUTING.md](CONTRIBUTING.md)
- The language server: [lsp/DESIGN.md](lsp/DESIGN.md)
