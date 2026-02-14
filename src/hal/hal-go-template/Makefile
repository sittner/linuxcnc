# Makefile for standalone Go HAL component
# Works with installed LinuxCNC or RIP (Run-In-Place) build

COMPONENT := mycomponent

# Detect LinuxCNC environment
ifdef EMC2_HOME
    # RIP (Run-In-Place) environment
    HAL_GO_DIR := $(EMC2_HOME)/share/linuxcnc/hal-go
    CGO_CFLAGS := -I$(EMC2_HOME)/include -I$(EMC2_HOME)/src/hal -I$(EMC2_HOME)/src/rtapi -DULAPI
    CGO_LDFLAGS := -L$(EMC2_HOME)/lib -Wl,-rpath,$(EMC2_HOME)/lib -llinuxcnchal
else
    # Installed LinuxCNC - check standard locations
    HAL_GO_DIR := $(shell \
        if [ -d "/usr/share/linuxcnc/hal-go" ]; then \
            echo "/usr/share/linuxcnc/hal-go"; \
        elif [ -d "/usr/local/share/linuxcnc/hal-go" ]; then \
            echo "/usr/local/share/linuxcnc/hal-go"; \
        else \
            echo ""; \
        fi)
    CGO_CFLAGS := -I/usr/include/linuxcnc -DULAPI
    CGO_LDFLAGS := -L/usr/lib/linuxcnc -Wl,-rpath,/usr/lib/linuxcnc -llinuxcnchal
endif

# Go settings
GO := go
GOFLAGS := -v

.PHONY: all clean check-env

all: check-env go.work $(COMPONENT)

check-env:
	@if [ -z "$(HAL_GO_DIR)" ] || [ ! -d "$(HAL_GO_DIR)" ]; then \
		echo "Error: hal-go package not found."; \
		echo ""; \
		echo "For RIP build: source /path/to/linuxcnc/scripts/rip-environment"; \
		echo "For installed: ensure LinuxCNC is installed with Go support"; \
		exit 1; \
	fi
	@echo "Using hal-go from: $(HAL_GO_DIR)"

# Create Go workspace to reference hal-go package
go.work: go.mod
	@echo "Creating Go workspace..."
	@rm -f go.work
	$(GO) work init
	$(GO) work use .
	$(GO) work use $(HAL_GO_DIR)

$(COMPONENT): main.go go.work
	@echo "Building $(COMPONENT)..."
	CGO_ENABLED=1 \
	CGO_CFLAGS="$(CGO_CFLAGS)" \
	CGO_LDFLAGS="$(CGO_LDFLAGS)" \
	$(GO) build $(GOFLAGS) -o $(COMPONENT) .

clean:
	rm -f $(COMPONENT) go.work

# Run component (for testing)
run: $(COMPONENT)
	./$(COMPONENT)
