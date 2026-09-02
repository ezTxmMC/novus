# Benchmarks

Twelve workloads, implemented once per language, measured on one machine.

```sh
python3 run.py                 # everything, writes results.json
python3 run.py --only primes   # a single workload
python3 run.py --langs novus,rust,cpp
python3 run.py --previous alpha5=/path/to/old/novusc
                               # an older novusc as one more column
```

`--previous` measures an older Novus release in the same run, with the same
sources, so two releases can be compared on equal terms (the results carry it
as `novus@<name>`, the site shows it next to the current one and keeps it out
of the wins). An older `novusc` is one `cc bootstrap/novusc.c` of the
corresponding commit away.

`make bench` from the repository root does the same. The results are rendered
on [the website](../website/src/routes/Benchmarks.tsx).

## Ground rules

- **Same program, idiomatic in each language.** The Rust version uses `Vec`
  and `HashMap`, the Go version slices and maps, Novus arrays and maps - no
  language is asked to imitate another.
- **Identical output enforced.** Every implementation prints the same lines;
  the runner marks a result as disagreeing and refuses to compare it
  otherwise. That rules out accidentally measuring different work.
- **Best of five runs**, wall clock including process start, because that is
  what a user waits for. This is why the JVM and Node look worse than they do
  on long running servers, where their JITs pay off.
- **Peak RSS through `wait4`**, measured by a tiny C helper (`measure.c`) so
  the measuring process does not inflate the child's memory.
- **Release settings**: `g++ -O2`, `rustc -O`, `go build`,
  `crystal build --release`, `javac`/`java`, `node`, `python3`,
  `novusc build` (which uses `cc -O2`).

## Reading them honestly

These are microbenchmarks. They say something about the code a compiler
generates for tight loops, allocation and standard library calls - and
nothing about how a language feels in a large program, how good its tooling
is, or how it behaves under a real workload. The numbers move with the
machine, the CPU governor and the library versions.

Where Novus does well: unboxed integer and float arithmetic (the compiler
proves which locals are numbers and generates plain C), and memory, because
values are tagged pointers, objects are one flat block and the garbage
collector keeps the heap at about twice the live data - `nbody` and
`spectral` allocate a boxed float per operation and stay within a few
megabytes.

Where it does not: sorting and allocation-heavy code, where every element is
still a boxed value behind a pointer, and hash maps, which store more per
entry than a C++ `unordered_map`.
