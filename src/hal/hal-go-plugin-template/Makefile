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
else
INSTALL_DIR := $(DESTDIR)/usr/lib/linuxcnc/go-modules
endif

# CGO flags are inherited from the environment; set them to match the LinuxCNC
# build tree if building outside the source tree.
# Example:
#   CGO_CFLAGS="-I/usr/include/linuxcnc" \
#   CGO_LDFLAGS="-L/usr/lib -llinuxcnchal" \
#   make

.PHONY: all install clean

all: $(PLUGIN_SO)

$(PLUGIN_SO): main.go go.mod
	CGO_ENABLED=1 go build -buildmode=plugin -o $(PLUGIN_SO) .

install: $(PLUGIN_SO)
	install -d $(INSTALL_DIR)
	install -m 644 $(PLUGIN_SO) $(INSTALL_DIR)/$(PLUGIN_SO)

clean:
	rm -f $(PLUGIN_SO)
