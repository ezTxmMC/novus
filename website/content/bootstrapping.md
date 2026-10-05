# Bootstrapping

Novus has exactly one implementation, written in Novus. To build it without
already having a Novus compiler, the repository carries the C that the
compiler generates for itself:
[bootstrap/novusc.c](https://github.com/ezTxmMC/novus/blob/master/bootstrap/novusc.c).

## The ladder

```
bootstrap/novusc.c ──cc──▶ novusc0 ──build compiler/main.nv──▶ novusc1 ──▶ novusc2
      (snapshot)           (stage 0)                          (stage 1)     (stage 2)

fixpoint:  novusc1 emit main.nv  ==  novusc2 emit main.nv  ==  bootstrap/novusc.c
```

`scripts/bootstrap.sh` runs stages 0-2. `test/selfhost.sh` goes further and
checks the **fixpoint**: the compiler built from the sources emits exactly the
same C as the compiler built from that C. If a change broke determinism, the
two differ and the test fails.

```
stage 0 ok: snapshot builds (novusc 0.1.0-pre.alpha.3)
stage 1 ok: snapshot compiles the current sources
stage 2 ok: the compiler compiles itself
stage 3 ok: FIXPOINT - novusc compiles itself byte-identically
snapshot ok: bootstrap/novusc.c is up to date
```

## Changing the compiler

After editing anything under `compiler/`, `runtime/` or `std/`:

```bash
make snapshot
```

It re-embeds `runtime/novus_rt.h` and `std/*.nv` into the compiler, rebuilds
through the ladder, verifies the fixpoint and writes the new
`bootstrap/novusc.c`. Commit that file together with the sources.

## The two-step rule

The snapshot must be able to compile the current sources. So when you add a
feature that the compiler itself wants to use - a builtin, a new syntax form:

1. implement the feature, run `make snapshot`
2. *then* use the feature in the compiler sources, run `make snapshot` again

Skipping step 1 means the checked-in snapshot cannot parse the sources any
more, and the only way back is an older snapshot.

## Keeping the fixpoint

Anything non-deterministic in code generation breaks it: iteration order that
depends on memory addresses, timestamps in the output, hash-ordered maps.
Novus maps iterate in key order precisely because the compiler depends on it.

## History

- a C++ tree-walking interpreter ran the first Novus programs
- a compiler written in Novus, executed by that interpreter, reached the
  first fixpoint on a small statically typed subset
- the compiler was rewritten for the whole language with a dynamically typed
  runtime; its C became the snapshot, and the C++ host was deleted

Since then, every version of the compiler has been built by the previous one.
