# The documentation site. The .nvmd pages are translated to .nvh by nvh-markdown first (the compiler only reads .nv and .nvh).
#
#   make deps       fetch the dependencies (nvh-markdown, nvh-tailwindcss)
#   make sync       refresh snapshot/ from a Novus checkout (NOVUS=path); only needed to update the numbers and examples
#   make content   website/content/**/*.nvmd -> *.nvh next to them
#   make dev       content + the dev server on :8080 (Tailwind watches)
#   make export    content + the static site in dist/ + link check
#   make clean     removes the generated .nvh files and dist/
NOVUSC ?= novusc
DEPS ?= $(or $(NOVUS_DEPS),$(HOME)/.novus/deps)
NVMD ?= $(DEPS)/github.com/ezTxmMC/nvh-markdown@latest/nvmd.nv
DIST ?= dist

.PHONY: deps sync content dev export clean

# fetches nvh-markdown and nvh-tailwindcss (project.nv) into the dependency cache
deps:
	$(NOVUSC) deps

# refreshes snapshot/ from a Novus checkout: make sync NOVUS=../novus
sync:
	tools/sync.sh $(NOVUS)

content: deps
	$(NOVUSC) run $(NVMD) build content

dev: content
	$(NOVUSC) run main.nv

export: content
	$(NOVUSC) run main.nv export $(DIST)
	$(NOVUSC) run tools/linkcheck.nv $(DIST)

clean:
	$(NOVUSC) run $(NVMD) clean content
	rm -rf $(DIST)
