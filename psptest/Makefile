MODULES := $(sort $(patsubst %/Makefile.test,%,$(wildcard */Makefile.test)))
BUNDLE_DIR ?= build/PSP/GAME/psptest

.PHONY: all launcher modules bundle clean list $(MODULES)

all: launcher modules

launcher:
	$(MAKE) -f Makefile.launcher all

modules: $(MODULES)
	@if [ -z "$(MODULES)" ]; then echo "No package PSPTEST modules implemented yet."; fi

$(MODULES):
	$(MAKE) -C $@ -f Makefile.test all

bundle: launcher
	@rm -rf "$(BUNDLE_DIR)"
	@mkdir -p "$(BUNDLE_DIR)/results"
	@cp EBOOT.PBP "$(BUNDLE_DIR)/EBOOT.PBP"
	@printf 'PSPTEST_MANIFEST\t1\n' > "$(BUNDLE_DIR)/manifest.tsv"
	@set -e; for module in $(MODULES); do \
		$(MAKE) -C "$$module" -f Makefile.test clean all; \
		mkdir -p "$(BUNDLE_DIR)/$$module"; \
		cp "$$module/EBOOT.PBP" "$(BUNDLE_DIR)/$$module/EBOOT.PBP"; \
		printf 'TEST\t%s\t%s/EBOOT.PBP\tauto\n' "$$module" "$$module" >> "$(BUNDLE_DIR)/manifest.tsv"; \
	done
	@echo "PSPTEST bundle: $(BUNDLE_DIR)"

clean:
	$(MAKE) -f Makefile.launcher clean
	@set -e; for module in $(MODULES); do $(MAKE) -C "$$module" -f Makefile.test clean; done
	@rm -rf build

list:
	@printf '%s\n' $(MODULES)
