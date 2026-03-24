# Makefile for the hal-go-plugin-template Go HAL plugin
#
# Usage:
#   make            - build mygomodule.so in the current directory
#   make install    - install to go-modules directory (auto-detects RIP vs system)
#   make clean      - remove build artifacts

PLUGIN_NAME ?= mygomodule
PLUGIN_SO   := $(PLUGIN_NAME).so
DESTDIR     ?=

# Detect RIP (run-in-place) environment via EMC2_HOME set by rip-environment.
# In RIP mode, install alongside the source tree; otherwise install system-wide.
ifdef EMC2_HOME
INSTALL_DIR := $(EMC2_HOME)/lib/linuxcnc/go-modules
LIB_DIR     := $(EMC2_HOME)/lib
else
INSTALL_DIR := $(DESTDIR)/usr/lib/linuxcnc/go-modules
LIB_DIR     := /usr/lib
endif

# CGO_LDFLAGS must match the launcher binary — Go's plugin loader rejects
# plugins when any shared CGO dependency (linuxcnc.org/hal) was compiled
# with different flags.
CGO_LDFLAGS ?= -Wl,-rpath,$(LIB_DIR)

.PHONY: all install clean

all: $(PLUGIN_SO)

$(PLUGIN_SO): main.go go.mod
	CGO_ENABLED=1 CGO_LDFLAGS="$(CGO_LDFLAGS)" go build -buildmode=plugin -o $(PLUGIN_SO) .

install: $(PLUGIN_SO)
	install -d $(INSTALL_DIR)
	install -m 644 $(PLUGIN_SO) $(INSTALL_DIR)/$(PLUGIN_SO)

clean:
	rm -f $(PLUGIN_SO)
