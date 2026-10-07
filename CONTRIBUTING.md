# Contributing to Novus

Thanks for taking the time to contribute. This document gets you from clone
to a passing test suite, and explains what makes a good pull request.

## Before you start

- For anything bigger than a small fix (a new standard library function, a
  language feature, a change to code generation), open an issue first to
  discuss the approach. It saves you from writing a PR that goes in a
  different direction than the project wants.
- For a typo, a doc fix, a small bug fix or a new example, just open the PR.

## Getting set up

You only need a C compiler (gcc, clang or `zig cc`) — Novus bootstraps itself
from a checked-in C snapshot, so no existing Novus install is required.

```sh
git clone https://github.com/ezTxmMC/novus
cd novus
make              # or: scripts/bootstrap.sh
make test         # golden tests, all 258 examples, self-hosting ladder
```

`make test` is what CI runs; if it passes locally it will pass there too.
See the [Quick start](README.md#quick-start) section of the README for
Windows setup, and [ARCHITECTURE.md](ARCHITECTURE.md) for how the pieces fit
together.

## Where to make your change

| You want to... | Look at |
| --- | --- |
| Fix or extend the standard library | `std/*.nv` (one file per module) |
| Change the language (syntax, semantics) | `compiler/lexer/`, `compiler/parser/`, `compiler/codegen/` |
| Fix a runtime/GC/threading bug | `runtime/*.h` |
| Add an example | `examples/NN-topic/` (see `examples/README.md`) |
| Improve editor support | `lsp/` (the language server) or `vscode-novus/` |
| Fix the docs site | `website/` |

[ARCHITECTURE.md](ARCHITECTURE.md) has the full directory map and the compiler
pipeline (lexer → parser → loader → codegen → generated C).

## The one rule that differs from most projects: the snapshot

`bootstrap/novusc.c` is generated, never hand-edited. If your change touches
anything under `compiler/`, `runtime/` or `std/`, you must regenerate it
before committing:

```sh
make snapshot     # scripts/snapshot.sh
```

This re-embeds the runtime and standard library, rebuilds through the
self-hosting ladder, checks that the compiler reaches a byte-identical
fixpoint, and writes the new `bootstrap/novusc.c`. Commit the regenerated
snapshot together with your source change, in the same commit or PR.

If you're adding a builtin or syntax form that the compiler's own sources
then start using, do it in two steps (feature, snapshot, then use the
feature, snapshot again) — see [BOOTSTRAP.md](BOOTSTRAP.md) for why and a
worked example.

## Tests

```sh
make test              # golden tests, examples and the self-hosting ladder
test/run_tests.sh       # only the golden tests (filter: test/run_tests.sh classes)
test/run_examples.sh    # only the examples (filter: test/run_examples.sh maps)
make lsp-test           # novus-lsp: lint, golden cases, protocol scenarios, corpus, snippets
```

- A language or stdlib change needs a golden test case (`test/cases/`) or an
  example (`examples/`) that exercises it.
- A bug fix should add the case that used to fail.
- Golden tests compare output against a checked-in `.golden` file; if your
  change intentionally changes output, update the `.golden` file in the same
  commit and say so in the PR description.

## Style

- Match the surrounding code — Novus source (`.nv`) follows the conventions
  already in `std/` and `compiler/`; C headers follow `runtime/`.
- Keep commits focused: one logical change per commit, with a message that
  says *why*, not just what changed (the diff already shows what).
- No unrelated reformatting in a functional PR.

## Submitting

1. Fork, branch, commit.
2. `make test` (and `make lsp-test` if you touched `lsp/`) passes locally.
3. Open a PR describing the change and the reasoning behind it. Link the
   issue it resolves, if any.
4. CI runs the same checks on Linux, macOS and Windows; fix what's red.

Security vulnerabilities are **not** reported through issues or PRs — see
[SECURITY.md](SECURITY.md).

## License

Novus is licensed under AGPL-3.0 (see [LICENSE](LICENSE)). By contributing,
you agree that your contribution is licensed under the same terms.
