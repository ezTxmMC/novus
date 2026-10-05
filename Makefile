# Novus - self-hosting compiler. Only a C compiler is required.
#
#   make            build build/novusc from the bootstrap snapshot
#   make test       golden tests, 258 examples and the self-hosting check
#   make examples   run every example under examples/NN-*/
#   make snapshot   regenerate bootstrap/novusc.c after compiler changes
#   make cross      cross compile for all platforms (needs zig)
#   make stats      language statistics of the repository (like GitHub's bar)
#   make bench      run the cross language benchmarks (writes benchmarks/results.json)
#   make lsp        build build/novus-lsp, the language server (written in Novus)
#   make lsp-test   lint, golden cases, protocol scenarios, corpus and snippet verification of novus-lsp
#   make snippets   regenerate vscode-novus/snippets/novus.json from the snippet catalogue of novus-lsp
#   make install    copy build/novusc and build/novus-lsp to $(PREFIX)/bin
PREFIX ?= /usr/local

# Every source the language server is built from: its own packages and the embedded std (the server reads it).
LSP_SOURCES := $(wildcard lsp/*.nv lsp/*/*.nv lsp/*/*/*.nv compiler/std/*.nv)

.PHONY: all test examples snapshot cross stats bench lsp lsp-test snippets install clean

all: build/novusc

build/novusc: bootstrap/novusc.c $(wildcard compiler/*.nv compiler/*/*.nv std/*.nv)
	scripts/bootstrap.sh

test: build/novusc
	test/run_tests.sh
	test/run_examples.sh
	test/selfhost.sh

examples: build/novusc
	test/run_examples.sh

build/novus-lsp: build/novusc $(LSP_SOURCES)
	build/novusc build lsp/main.nv -o build/novus-lsp

lsp: build/novus-lsp

lsp-test: build/novus-lsp
	scripts/lsp_lint.sh
	test/run_tests.sh lsp_
	test/run_lsp.sh
	test/run_lsp_corpus.sh
	scripts/lsp_snippets.sh

# the catalogue in lsp/services/snippets is the single source of truth: this rewrites the VS Code export
# (and compiles every snippet); review the diff of vscode-novus/snippets/novus.json before committing it
snippets: build/novusc
	scripts/lsp_snippets.sh --update

# no build/novusc prerequisite: after runtime/codegen changes the old
# build/novusc must run first (two-step rule, see BOOTSTRAP.md)
snapshot:
	scripts/snapshot.sh

cross: build/novusc
	scripts/cross.sh

stats: build/novusc
	build/novusc run tools/langstats.nv

bench: build/novusc
	cd benchmarks && python3 run.py

install: build/novusc build/novus-lsp
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/novusc $(DESTDIR)$(PREFIX)/bin/novusc
	install -m 755 build/novus-lsp $(DESTDIR)$(PREFIX)/bin/novus-lsp

clean:
	rm -rf build dist
