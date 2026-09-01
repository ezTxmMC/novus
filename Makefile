# Novus - self-hosting compiler. Only a C compiler is required.
#
#   make            build build/novusc from the bootstrap snapshot
#   make test       golden tests + self-hosting fixpoint check
#   make snapshot   regenerate bootstrap/novusc.c after compiler changes
#   make cross      cross compile for all platforms (needs zig)
#   make install    copy build/novusc to $(PREFIX)/bin
PREFIX ?= /usr/local

.PHONY: all test snapshot cross install clean

all: build/novusc

build/novusc: bootstrap/novusc.c $(wildcard compiler/*.nv compiler/*/*.nv)
	scripts/bootstrap.sh

test: build/novusc
	test/run_tests.sh
	test/selfhost.sh

snapshot: build/novusc
	scripts/snapshot.sh

cross: build/novusc
	scripts/cross.sh

install: build/novusc
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/novusc $(DESTDIR)$(PREFIX)/bin/novusc

clean:
	rm -rf build dist
