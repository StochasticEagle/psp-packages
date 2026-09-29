MODULES := $(sort $(patsubst %/Makefile.test,%,$(wildcard */Makefile.test)))

.PHONY: list

list:
	@printf '%s\n' $(MODULES)
