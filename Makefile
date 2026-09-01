# Novus - self-hosting compiler. Only a C compiler is required.
#
#   make            build build/novusc from the bootstrap snapshot
#   make test       golden tests + self-hosting fixpoint check
#   make snapshot   regenerate bootstrap/novusc.c after compiler changes
#   make cross      cross compile for all platforms (needs zig)
#   make stats      language statistics of the repository (like GitHub's bar)
#   make install    copy build/novusc to $(PREFIX)/bin
PREFIX ?= /usr/local

.PHONY: all test snapshot cross stats install clean

all: build/novusc

build/novusc: bootstrap/novusc.c $(wildcard compiler/*.nv compiler/*/*.nv std/*.nv)
	scripts/bootstrap.sh

test: build/novusc
	test/run_tests.sh
	test/selfhost.sh

# no build/novusc prerequisite: after runtime/codegen changes the old
# build/novusc must run first (two-step rule, see BOOTSTRAP.md)
snapshot:
	scripts/snapshot.sh

cross: build/novusc
	scripts/cross.sh

stats: build/novusc
	build/novusc run tools/langstats.nv

install: build/novusc
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/novusc $(DESTDIR)$(PREFIX)/bin/novusc

clean:
	rm -rf build dist
