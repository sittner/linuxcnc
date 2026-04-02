# Makefile for a Go HAL plugin (.so)
#
# Uses modcompile to locate LinuxCNC paths and the exact Go binary used to
# build LinuxCNC. This guarantees the plugin is compiled with the same Go
# toolchain as linuxcnc-launcher (a hard requirement for Go plugins).
#
# Usage:
#   make            - build mygomodule.so in the current directory
#   make install    - install to GOMC_GOMOD_DIR (auto-detected via modcompile)
#   make clean      - remove build artifacts

PLUGIN_NAME ?= mygomodule
PLUGIN_SO   := $(PLUGIN_NAME).so

# ---------------------------------------------------------------------------
# Get paths from modcompile
# ---------------------------------------------------------------------------
MODCOMPILE := $(shell command -v modcompile 2>/dev/null)
ifneq ($(MODCOMPILE),)
$(eval $(shell $(MODCOMPILE) --print-make-inc))
endif

CGO_LDFLAGS ?= -Wl,-rpath,$(GOMC_LIB_DIR)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all install clean check-env

all: check-env go.work $(PLUGIN_SO)

check-env:
	@if [ -z "$(MODCOMPILE)" ]; then \
		echo "Error: modcompile not found in PATH."; \
		echo "Source rip-environment or install LinuxCNC."; \
		exit 1; \
	fi
	@if [ ! -d "$(GOMC_LAUNCHER_DIR)" ]; then \
		echo "Error: launcher not found at $(GOMC_LAUNCHER_DIR)"; \
		exit 1; \
	fi
	@echo "GO:       $(GOMC_GO)"
	@echo "launcher: $(GOMC_LAUNCHER_DIR)"

go.work: go.mod
	@rm -f go.work go.work.sum
	GOTOOLCHAIN=local $(GOMC_GO) work init
	GOTOOLCHAIN=local $(GOMC_GO) work use .
	GOTOOLCHAIN=local $(GOMC_GO) work edit \
		-replace github.com/sittner/linuxcnc/src/launcher=$(GOMC_LAUNCHER_DIR)

$(PLUGIN_SO): main.go go.mod go.work
	CGO_ENABLED=1 \
	CGO_LDFLAGS="$(CGO_LDFLAGS)" \
	GOTOOLCHAIN=local \
	$(GOMC_GO) build -buildmode=plugin -o $(PLUGIN_SO) .

install: $(PLUGIN_SO)
	install -d $(DESTDIR)$(GOMC_GOMOD_DIR)
	install -m 644 $(PLUGIN_SO) $(DESTDIR)$(GOMC_GOMOD_DIR)/$(PLUGIN_SO)

clean:
	rm -f $(PLUGIN_SO) go.work go.work.sum
