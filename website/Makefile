# The documentation site. The .nvmd pages are translated to .nvh by nvh-markdown first (the compiler only reads .nv and .nvh).
#
#   make content   website/content/**/*.nvmd -> *.nvh next to them
#   make dev       content + the dev server on :8080 (Tailwind watches)
#   make export    content + the static site in dist/ + link check
#   make clean     removes the generated .nvh files and dist/
NOVUSC ?= ../build/novusc
NVMD ?= ../../nvh-markdown/nvmd.nv
DIST ?= dist

.PHONY: content dev export clean

content:
	$(NOVUSC) run $(NVMD) build content

dev: content
	$(NOVUSC) run main.nv

export: content
	$(NOVUSC) run main.nv export $(DIST)
	$(NOVUSC) run tools/linkcheck.nv $(DIST)

clean:
	$(NOVUSC) run $(NVMD) clean content
	rm -rf $(DIST)
