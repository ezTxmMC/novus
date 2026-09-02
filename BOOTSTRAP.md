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

- First a C++ tree-walking interpreter (`novus run`).
- Then a first Novus-written compiler (`tools/*.nv`) for a small statically
  typed subset, executed by the interpreter, reached the fixpoint.
- Now (0.1.0-pre.alpha.1): the compiler was rewritten for the whole language with a
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
   `tools/embed.nv` - which inlines the `#include "nv_*.h"` parts the header
   is split into, each once, in include order - and `compiler/std/stdlib.nv`
   from `std/*.nv` by `tools/embedstd.nv` (`snapshot.sh` does both). Edit
   the headers and the std sources, never the embeddings.
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
- `std/` (repository root) holds the standard library; `import os` makes the
  loader parse the embedded copy of `std/os.nv` and prefix its methods and
  globals with the module name (`(method os.mkdir ...)`), which is why they
  are called as `os.mkdir(...)`. A method body `(native "nv_os_mkdir")`
  makes the code generator call that C function directly (`variadic`
  natives receive the argument count first).
- `project/` parses `project.nv` manifests and fetches `require`d modules
  with git into the cache; the loader resolves module imports through the
  resulting module table.
- `driver/` implements the command line and drives the C compiler;
  `runtime/runtime.nv` is the embedded copy of the C runtime.

### AST node reference

```
(method name ret (params (p type name)...) (annos (anno Name (a key "v")...)...) body)
    body: (block (at file:line stmt)...), (abstract) or (native "c_function" [variadic])
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

Every value is an `nv` (`NvVal*`, 24 bytes; small integers are tagged
pointers with the lowest bit set, so `nv_type_of()`/`nv_ival()` must be used
instead of dereferencing). Objects are one heap block: value, header and one
slot per field, addressed by index (`nv_field_index`) - the names live in the
class. The heap is garbage collected (`runtime/nv_memory.h`); the only thing
the generator has to do for it is register each global as a root
(`nv_gc_root(&g_name)`) before initializing it, since the collector finds
its roots on the stacks, in the runtime's own tables and nowhere else. Maps keep entries in insertion order with a hash index and sort on
demand, so iteration stays in key order (the fixpoint depends on it).
Arithmetic and comparisons go through the inline `*_fast` / `*_bool` wrappers,
conditions never box a bool.

The code generator avoids the dynamic lookups wherever it already knows the
answer. Inside a class method a field access is the slot itself
(`nv_fields(self->o)[2]`), because fields are laid out base class first and a
subclass keeps the indices of its base. Member lookups on other values go
through a cache keyed on the class and the *name pointer* (every name is a
string literal, so a hit is two compares). Calls with up to three arguments
skip the runtime's `va_list`, `x.append(v)`, `x.length()` and `x.has(k)` go
straight to the collection, `a + b + c + ...` becomes one `nv_add_chain` that
fills a single buffer, and a `for (x in xs)` loop whose body contains no call
walks the array itself instead of a snapshot copy.

`codegen/inference.nv` finds locals (and parameters declared `integer`) that
can only ever hold integers and generates them as C `long long`, boxing only
where the value crosses into a dynamic context. A counting loop then compiles
to plain C:

```c
long long l_i = 0LL;
while (l_i < 10000000LL) { l_sum = (l_sum + l_i); l_i = (l_i + 1LL); }
```

The analysis is conservative: a name qualifies only if every declaration and
assignment in the method is an integer expression, loop variables and names
that are only assigned (possibly globals) never qualify.

Locals are `l_name`, top-level constants
`g_NAME`, free methods `f_name_N` (N = arity; same-arity overloads become
`f_name_N_vK` plus a dispatcher that tests parameter types), class methods
`m_Class_name_N(nv self, nv *args, int n)`, constructors `c_Class`. Classes
are registered at start-up (`nv_register_classes`), then top-level constants
are initialized, then enum constants are constructed, then `main` runs. Its
integer return value becomes the process exit code.
