# Top-level Makefile for Isolated TP Library
#
# This builds the isolated trajectory planner library and tests
# without any dependencies on the LinuxCNC source directory.

.PHONY: all clean test help

all: tests

tests:
	$(MAKE) -C tests

clean:
	$(MAKE) -C tests clean

test: tests
	@echo ""
	@echo "=========================================="
	@echo "Running Isolated TP Tests"
	@echo "=========================================="
	@echo ""
	$(MAKE) -C tests test

help:
	@echo "Isolated TP Library Build"
	@echo "========================="
	@echo ""
	@echo "This directory contains a self-contained, isolated version of"
	@echo "the LinuxCNC trajectory planner (TP) module."
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build everything (default)"
	@echo "  tests  - Build the test executable"
	@echo "  test   - Build and run tests"
	@echo "  clean  - Remove all build artifacts"
	@echo "  help   - Show this help message"
	@echo ""
	@echo "Quick start:"
	@echo "  make         # Build"
	@echo "  make test    # Build and run tests"
	@echo "  make clean   # Clean up"
	@echo ""
	@echo "For more information, see README.md"
