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
`scripts/bootstrap.ps1` (Windows) run stages 0-2 and leave `build/novusc`
(on Unix stages 0 and 1 are built with `-O1`: they only have to emit the same C,
and it saves about a third of the time).
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
   The natives of `ast/` and `lexer/` went in this way: the first snapshot
   only added `nv_sexp.h` (the compiler sources did not use it yet), the second
   changed `ast/sexp.nv`, `ast/text.nv` and `lexer/tokens.nv` to declare them
   with `native "nv_..."`.
3. `compiler/runtime/runtime.nv` is generated from `runtime/novus_rt.h` by
   `tools/embed.nv` - which inlines the `#include "nv_*.h"` parts the header
   is split into, each once, in include order - and `compiler/std/stdlib.nv`
   from `std/*.nv` by `tools/embedstd.nv` (`snapshot.sh` does both). Edit
   the headers and the std sources, never the embeddings.
4. Keep the fixpoint: `test/selfhost.sh` must pass. Non-determinism in the
   generator (e.g. depending on memory addresses or unordered iteration)
   would break it - maps iterate in key order, which is deterministic.

## Pipeline

Each package lives in its own directory under `compiler/` (the root of the
compiler's packages, since it has no project.nv); files import the packages
they use by name (`import parser`, `import codegen`).

- `lexer/` produces tokens as strings `KIND file:line value`.
- `parser/` builds an AST of nested s-expressions (also strings); `ast/`
  holds the navigation helpers (`nodeChild`, `nodeCount`, ...). They, the
  string escaping of `ast/` and the token accessors of `lexer/tokens.nv` are
  natives of `runtime/nv_sexp.h`: a node is scanned once, the start and end of
  its children are kept in a per-thread cache (dropped when a garbage
  collection has run since it was filled, see `nv_gc_epoch`; short nodes in a
  direct mapped table, long nodes until then), and asking for the next child
  is an array read. That is what makes the compiler linear in the number of
  statements of a method; the emitted C is the same as with the plain Novus
  versions.
- `loader/` finds the project root, loads packages (`import geo`: every file
  of the folder `geo/`, `import @geo/circle`: one file), standard and
  dependency modules, and flattens the declarations of all files into one
  list. Functions and globals of a package are prefixed with its name like
  those of a std module (`(method geo.area ...)`), recorded with
  `(userpkg geo)`; `codegen/index.nv` indexes them as `unq:area/1` so that an
  unqualified call finds the one package that has the name (or reports that
  several do).
- `codegen/` indexes the declarations (`index.nv`), runs the semantic checks
  (`checks.nv`) and emits C against `runtime/novus_rt.h` - expressions,
  statements, methods, whole program; `modules.nv` maps `json`/`path`/`os`/
  `http` calls to runtime functions.
  `ordering.nv`, `operands.nv` and `temps.nv` make the evaluation order of
  operands left to right (see below). `kinds.nv`, `inference.nv`, `typed.nv`
  and `workers.nv` find the expressions that are plain integers or floats and
  generate them as C numbers (see below).
- `std/` (repository root) holds the standard library; `import os` makes the
  loader parse the embedded copy of `std/os.nv` and prefix its methods and
  globals with the module name (`(method os.mkdir ...)`), which is why they
  are called as `os.mkdir(...)`. A method body `(native "nv_os_mkdir")`
  makes the code generator call that C function directly (`variadic`
  natives receive the argument count first).
- `nvh/` compiles `.nvh` components (HTML with Novus) into Novus source -
  a class based `NvhComponent` whose template became `nvRender(out)` - with
  the line numbers of the `.nvh` file; the loader runs it for every `.nvh`
  it loads, and appends a `main` serving the page when the program itself
  is a `.nvh` file. Classes of a std module (`NvhComponent` in `std/web.nv`)
  are recorded as `(classpkg Class module)` so that their methods see the
  module's functions and globals unqualified.
- `project/` parses `project.nv` manifests and fetches `require`d modules
  with git into the cache; the loader resolves module imports through the
  resulting module table.
- `driver/` implements the command line and drives the C compiler
  (`build.nv`); `cache.nv` is the content-addressed cache of compiled programs
  behind `novusc run` (key = hash of the C, compiler, flags and version; see the
  README); `runtime/runtime.nv` is the embedded copy of the C runtime.

### AST node reference

```
(method name ret (params (p type name)...) (annos (anno Name (a key "v")...)...) body)
    body: (block (at file:line stmt)...), (abstract) or (native "c_function" [variadic])
(class Name Base|- true|false (fields (f type name)...) ctor (methods method...))
    ctor: (ctor (params ...) (block ...)) or (noctor)
(enum Name (consts (c NAME arg...)...) (fields ...) ctor (methods ...))
(iface Name (names m1 m2 ...))        (annodef Name)        (global name expr)
(import "module/path")  (importmod pkg/path)  (importfile "pkg/file" "pos")
(package name "pos")    (userpkg name)        (classpkg Class pkg)

statements:  (var name [expr]) (tvar type name [expr]) (assign name expr)
             (setexpr target expr) (return [expr]) (println e) (print e) (eprintln e)
             (if cond block [else-block | (if ...)]) (while cond block)
             (forin var expr block) (break) (continue) (nop) expr
expressions: atoms 123 1.5 true false name, (str "escaped"), (neg e), (! e),
             (arr e...), (mapl k v ...), (obj Class (f name e)...), (idx t k),
             (mget t name), (mcall t name args...), (call name args...),
             (op l r) for + - * / % == != < > <= >= && ||
```

### Evaluation order

The language defines operands, call arguments (receiver first), array, map and
object literal elements, `a[i]` and `a[i] = v` to be evaluated left to right
(README, Language). C leaves that open - gcc evaluates the arguments of a call
right to left, clang left to right - so `codegen/ordering.nv` enforces it:
when an operand list contains an operand with effects (a call, method call,
`await`, `thread`/`virtual`, a module function used as `io.readLine`) and another operand that is not a literal or a
local, every such operand but the last is evaluated into a temporary by a comma
expression, in order:

```c
(nvt0 = f_a_0(), nv_add_fast(nvt0, f_b_0()))      /* a() + b() */
```

Literals, locals and arithmetic on them stay in place, so the loops over
unboxed integers get no temporaries; reads of numeric fields of `this` and
the numeric natives of `math` only read, so an expression made of those needs
none either (the field of another object is not among them: the class guessed
for it is a guess, and the fallback runs the method of the class it is, so such
a read is sequenced like a call). The temporaries are `nvt0`, `nvt1`, ...,
declared once at the top of the C function (`codegen/temps.nv`; `ctx["temps"]`
counts them). A global's initialiser and an enum constant are not in a
function: each declares its own in a block of `nv_init_globals`. Plain C comma
expressions are used on purpose: a GNU statement expression would tie the
generated code to gcc and clang.

This also applies to the compiler's own sources, which the compiler compiles
with itself: `a() + b()` in `compiler/` runs `a` first whichever C compiler
built `novusc`. A compiler built by clang therefore emits the same C as one
built by gcc - `test/selfhost.sh` checks it (stage 4: the C of stage 3 built
with a compiler of the other family, clang or gcc, emits the same C). Do not
rely on the old accident of gcc's right-to-left order, and keep code
generation that has to happen in a given order (literal numbering, temporaries
counters) in explicit statements rather than in one expression.

The code is split in three: `operands.nv` says what the operands of a node are
(as paths, which serve both for reading an operand and for putting a
temporary in its place - a node kind that is not listed there is not ordered),
`ordering.nv` classifies them and decides what is hoisted, `temps.nv` owns the
temporaries. Which operands have effects is decided on the text of the node
("(call ", "(mcall ", "(await ", "(spawn ") plus a walk for module members
that are calls.

### Generated C

Every value is an `nv` (`NvVal*`, 16 bytes). Small integers are tagged
pointers with the lowest bit set, and a float whose exponent is in
2^-126..2^127 lives in the word with the low bits `010` (the double's bits
rotated, the exponent rebased, three bits dropped, `nv_float()`/`nv_fval()`);
the other floats (NaN, infinity, `-0.0`, extreme magnitudes) are heap cells.
So `nv_type_of()`/`nv_ival()`/`nv_fval()` must be used instead of dereferencing,
and code that must not follow the pointer asks `nv_is_ptr()`, never "is it not
an integer". Objects are one heap block: value, header and one
slot per field, addressed by index (`nv_field_index`) - the names live in the
class. The heap is garbage collected (`runtime/nv_memory.h`); the only thing
the generator has to do for it is register each global as a root
(`nv_gc_root(&g_name)`) before initializing it, since the collector finds
its roots on the stacks, in the runtime's own tables and nowhere else. Maps keep entries in insertion order with a hash index and sort on
demand, so iteration stays in key order (the fixpoint depends on it). A big
map that is sorted and then gets a key out of order keeps a list of item
positions in key order (`ord`, whose first int is the count, so a map header
stays 40 bytes; `test/runtime/map_order.c` guards both) and places new keys into it one by one, so
"insert, iterate, insert, iterate" does not sort the whole map each time;
`nv_map_nth` reads an entry in key order after `nv_map_order`, `remove`
swaps the last entry into the gap (O(1)) and drops the list.
The compiler's own constant tables (keywords, symbol characters, operator
precedences, operator function names) are `private final` globals: a map
literal inside a method is rebuilt on every call (about 200 ns for 20
entries) and the lexer and parser ask for every character and token.
Arithmetic and comparisons go through the inline `*_fast` / `*_bool` wrappers,
conditions never box a bool.

The code generator avoids the dynamic lookups wherever it already knows the
answer. Inside a class method a field access is the slot itself
(`nv_fields(self->o)[2]`), because fields are laid out base class first and a
subclass keeps the indices of its base. Member lookups on other values go
through the inline cache of the call site (below), whose misses use a shared
cache keyed on the class and the *name pointer* (every name is a string
literal, so a hit is two compares). Calls with up to three arguments
skip the runtime's `va_list`, `x.append(v)`, `x.length()` and `x.has(k)` go
straight to the collection, `a + b + c + ...` becomes one `nv_add_chain` that
fills a single buffer, and a `for (x in xs)` loop whose body contains no call
walks the array itself instead of a snapshot copy.

Numbers: `codegen/kinds.nv` says of an expression whether it always produces
an integer (`i`), a float (`d`) or neither. Literals, locals, arithmetic and
comparisons on them, calls of methods that have an unboxed worker, the numeric
natives of `math` (`sqrt` is `sqrt()`, `floor` is `nv_floor_int()`) and fields
declared `integer`/`float` have a kind. `codegen/inference.nv` finds the
locals (and parameters declared `integer`/`float`) that only ever hold one
kind - every declaration and assignment of the name must have it, started from
"all candidates qualify" and dropped until stable - and records them as
`int:x` / `float:x` marks in the locals map, next to `cls:x` / `elem:x` marks
for locals that are guessed to hold an object (or an array of objects) of a
class. A counting loop then compiles to plain C, boxing only where the value
crosses into a dynamic context:

```c
long long l_i = 0LL;
while (l_i < 10000000LL) { l_sum = nv_iadd(l_sum, l_i); l_i = nv_iadd(l_i, 1LL); }
```

What is *not* trusted decides how this stays exact. Integer arithmetic goes
through `nv_iadd/isub/imul/ineg` (unsigned, so it wraps instead of being
undefined); a float with any value on the other side of `- * / %` is
`nv_fmul_du(double, nv)` (the value is checked and the error is the boxed
operator's); a float converted to an integer is `nv_d2i()`; a comparison of
unboxed numbers is a C comparison, which is also what the boxed one means for
NaN. A numeric field of a class is read as `nv_get_field(ic, obj, cls, slot,
"name")` followed by `nv_unbox_float()`/`nv_unbox_int()`: the class pointer of
the object is compared to the guessed one and the slot read, anything else is
asked the way an unguessed call is (`nv_ic0`), and a value that is no number
of the declared kind is a runtime error - which is why every store into a
numeric field converts (`coerceFieldCode()`, `nv_coerce_numeric_field()`,
`nv_coerce_kind()`). That error belongs to *computing* with the field only: a
bare read (`isBareRead()` in `kinds.nv`: `p.f()`, `this.f()`, the name of a
field) has no kind where its value is stored or passed on - `valueKind()` is
what the inference of locals and of results asks, `comparesValues()` takes `==`
and `!=` out of the typed comparisons, `genNumberAs()` converts it boxed
(`nv_as_int`) and `sequenceOperands()` holds it in a boxed temporary unless
the node it belongs to computes with numbers (`computesWithNumbers()`).

The generated C is compiled with `-ffp-contract=off` (`FLOAT_FLAGS` in
`driver/build.nv`, `scripts/bootstrap.sh`, `scripts/cross.sh`, the self-host
test): the typed code puts `a * b + c` into one C expression, which a C
compiler would otherwise fuse into a multiply-add that rounds once.

`codegen/workers.nv` gives free methods with numbers in their signature a
second C function, `f_name_N_raw`, that takes `long long`/`double` parameters
and returns a number unboxed; `f_name_N` stays as the boxed entry that
converts its arguments the way the body always did (`nv_as_int`,
`nv_as_double`), calls the worker and boxes the result. A worker is described
by a descriptor `r:ppp` (`i`, `d` or `n` for the result and each parameter)
kept in the program index as `raw:name/arity`. What a body returns decides
whether the result can be unboxed - every `return` must have a kind - and that
depends on the workers of the methods it calls, so `analyseWorkers()` starts
with every candidate declared as typed as its signature, analyses the bodies
again and again with what the previous round left, and stops when no descriptor
changes (a round only takes kinds away). Overloaded, native, abstract and
`async` methods get no worker. The temporaries of the evaluation order of an
unboxed operand are `double nvdN` / `long long nviN` (`temps.nv`), and the
rewritten node is `(rawd nvdN)` / `(rawi nviN)`.

Every `target.name(args)` call site that is not a module call has an inline
cache (`NvIc`, one slot of the per-thread table `nv_ics[]` the program
declares): the class it saw last and what the name resolved to there, a field
slot or a method. `nv_ic0`..`nv_ic3` check the class and go straight to the
slot or the method; a miss resolves like `nv_invoke_args` does and refills.

Locals are `l_name`, top-level constants
`g_NAME`, free methods `f_name_N` (N = arity; same-arity overloads become
`f_name_N_vK` plus a dispatcher that tests parameter types), class methods
`m_Class_name_N(nv self, nv *args, int n)`, constructors `c_Class`. Classes
are registered at start-up (`nv_register_classes`), then `nv_init_globals`
makes every global a collector root (reading `nil` until initialised) and runs
the initialisation of the globals and the enum constants, each after the ones
it uses (`codegen/initorder.nv` reads the dependencies off the generated C: a
global's expression, an enum's constants, and the free functions and classes
they mention), then `main` runs. Its integer return value becomes the process
exit code; the program ends through `nv_terminate`, which flushes stdout and
stderr by hand and calls `_exit` - `exit()` would lock stdin, which another
thread may hold while it is blocked reading.
