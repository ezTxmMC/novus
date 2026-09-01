# Bootstrapping novusc

Novus has exactly one implementation: `novusc`, written in Novus. To build it
without an existing Novus compiler, the repository carries the C that the
compiler generates for itself - `bootstrap/novusc.c`. Any C99 compiler turns
that file into a working `novusc`, which then rebuilds itself from the
sources in `compiler/`.

## The ladder

```
bootstrap/novusc.c ──cc──▶ novusc0 ──build compiler/main.nv──▶ novusc1 ──▶ novusc2
      (snapshot)           (stage 0)                          (stage 1)     (stage 2)

fixpoint:  novusc1 emit main.nv  ==  novusc2 emit main.nv  ==  bootstrap/novusc.c
```

`scripts/bootstrap.sh` (Linux/macOS), `scripts/bootstrap.cmd` and
`scripts/bootstrap.ps1` (Windows) run stages 0-2 and leave `build/novusc`.
`test/selfhost.sh` additionally checks the fixpoint and that the snapshot is
up to date. `scripts/snapshot.sh` regenerates `compiler/runtime/runtime.nv` and
`bootstrap/novusc.c` from the current sources (with the fixpoint check).

## History

- v0.1: a C++ tree-walking interpreter (`novus run`).
- v0.2: a first Novus-written compiler (`tools/*.nv`) for a small statically
  typed subset, executed by the interpreter, reached the fixpoint.
- v0.3 (now): the compiler was rewritten for the whole language with a
  dynamically typed C runtime and a driver; its C was captured as the
  snapshot, and the C++ interpreter and the old tools were removed. The
  interpreter was used exactly once more - to build the old native compiler
  that compiled the new one.

## Rules for changing the compiler

1. `bootstrap/novusc.c` is generated. Never edit it; run `make snapshot`.
2. The snapshot must be able to compile `compiler/*.nv`. When you add a
   feature the compiler itself wants to use (a builtin, a syntax form),
   do it in two steps: add the feature, regenerate the snapshot, *then* use
   the feature in the compiler sources and regenerate again.
3. `compiler/runtime/runtime.nv` is generated from `runtime/novus_rt.h` by
   `tools/embed.nv` (`snapshot.sh` does this). Edit the header, not the
   embedding.
4. Keep the fixpoint: `test/selfhost.sh` must pass. Non-determinism in the
   generator (e.g. depending on memory addresses or unordered iteration)
   would break it - maps iterate in key order, which is deterministic.

## Pipeline

Each package lives in its own directory under `compiler/`; files import
what they use with `import "file.nv"` (relative to the importing file).

- `lexer/` produces tokens as strings `KIND file:line value`.
- `parser/` builds an AST of nested s-expressions (also strings); `ast/`
  holds the navigation helpers (`nodeChild`, `nodeCount`, ...).
- `loader/` resolves `import "file.nv"` relative to the importing file and
  flattens the declarations of all files into one list.
- `codegen/` indexes the declarations (`index.nv`), runs the semantic checks
  (`checks.nv`) and emits C against `runtime/novus_rt.h` - expressions,
  statements, methods, whole program; `modules.nv` maps `json`/`path`/`os`/
  `http` calls to runtime functions.
- `driver/` implements the command line and drives the C compiler;
  `runtime/runtime.nv` is the embedded copy of the C runtime.

### AST node reference

```
(method name ret (params (p type name)...) (annos (anno Name (a key "v")...)...) body)
    body: (block (at file:line stmt)...) or (abstract)
(class Name Base|- true|false (fields (f type name)...) ctor (methods method...))
    ctor: (ctor (params ...) (block ...)) or (noctor)
(enum Name (consts (c NAME arg...)...) (fields ...) ctor (methods ...))
(iface Name (names m1 m2 ...))        (annodef Name)        (global name expr)
(import "path")

statements:  (var name [expr]) (tvar type name [expr]) (assign name expr)
             (setexpr target expr) (return [expr]) (println e) (print e) (eprintln e)
             (if cond block [else-block | (if ...)]) (while cond block)
             (forin var expr block) (break) (continue) (nop) expr
expressions: atoms 123 1.5 true false name, (str "escaped"), (neg e), (! e),
             (arr e...), (mapl k v ...), (obj Class (f name e)...), (idx t k),
             (mget t name), (mcall t name args...), (call name args...),
             (op l r) for + - * / % == != < > <= >= && ||
```

### Generated C

Every value is an `nv` (`NvVal*`). Locals are `l_name`, top-level constants
`g_NAME`, free methods `f_name_N` (N = arity; same-arity overloads become
`f_name_N_vK` plus a dispatcher that tests parameter types), class methods
`m_Class_name_N(nv self, nv *args, int n)`, constructors `c_Class`. Classes
are registered at start-up (`nv_register_classes`), then top-level constants
are initialized, then enum constants are constructed, then `main` runs. Its
integer return value becomes the process exit code.
