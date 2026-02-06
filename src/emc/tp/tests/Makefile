# Makefile for standalone TP test build
#
# This Makefile compiles the trajectory planner in standalone mode
# (with TP_STANDALONE defined) to validate abstractions and identify
# remaining dependencies.
#
# Usage: make
#

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -DTP_STANDALONE -DULAPI \
	-I. -I.. \
	-I../../../libnml/posemath \
	-I../../../emc/nml_intf \
	-I../../../rtapi \
	-I../../../emc/motion \
	-I../../../emc/kinematics \
	-I../../../hal
LDFLAGS = -lm

# Source files
TP_DIR = ..
POSEMATH_DIR = ../../../libnml/posemath
NML_INTF_DIR = ../../../emc/nml_intf

# TP source files to compile
TP_SOURCES = \
	$(TP_DIR)/tp.c \
	$(TP_DIR)/tc.c \
	$(TP_DIR)/tcq.c \
	$(TP_DIR)/blendmath.c \
	$(TP_DIR)/spherical_arc.c \
	$(TP_DIR)/sp_scurve.c \
	$(TP_DIR)/tp_motion_interface.c

# Posemath sources (need these for actual math operations)
POSEMATH_SOURCES = \
	$(POSEMATH_DIR)/_posemath.c \
	$(POSEMATH_DIR)/gomath.c \
	$(POSEMATH_DIR)/sincos.c

# NML interface sources (for EmcPose utilities)
NML_INTF_SOURCES = \
	$(NML_INTF_DIR)/emcpose.c

# Stub sources
STUB_SOURCES = \
	tp_standalone_stubs.c

# Test program
TEST_SOURCES = \
	test_tp_standalone.c

# Object files
TP_OBJECTS = $(TP_SOURCES:.c=.o)
POSEMATH_OBJECTS = $(POSEMATH_SOURCES:.c=.o)
NML_INTF_OBJECTS = $(NML_INTF_SOURCES:.c=.o)
STUB_OBJECTS = $(STUB_SOURCES:.c=.o)
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

ALL_OBJECTS = $(TP_OBJECTS) $(POSEMATH_OBJECTS) $(NML_INTF_OBJECTS) $(STUB_OBJECTS) $(TEST_OBJECTS)

# Target executable
TARGET = test_tp_standalone

# Default target
all: $(TARGET)

# Link the test executable
$(TARGET): $(ALL_OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Build successful!"
	@echo ""
	@echo "Run with: ./$(TARGET)"

# Compile TP source files
$(TP_DIR)/%.o: $(TP_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile posemath source files  
$(POSEMATH_DIR)/%.o: $(POSEMATH_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile NML interface source files
$(NML_INTF_DIR)/%.o: $(NML_INTF_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile stub files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGET) $(ALL_OBJECTS)
	@echo "Clean complete."

# Show what would be built
show:
	@echo "TP sources:"
	@echo "$(TP_SOURCES)" | tr ' ' '\n'
	@echo ""
	@echo "Posemath sources:"
	@echo "$(POSEMATH_SOURCES)" | tr ' ' '\n'
	@echo ""
	@echo "NML interface sources:"
	@echo "$(NML_INTF_SOURCES)" | tr ' ' '\n'
	@echo ""
	@echo "Stub sources:"
	@echo "$(STUB_SOURCES)" | tr ' ' '\n'
	@echo ""
	@echo "Test sources:"
	@echo "$(TEST_SOURCES)" | tr ' ' '\n'

# Help target
help:
	@echo "Standalone TP Test Build"
	@echo "========================="
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build the test executable (default)"
	@echo "  test   - Build and run the test"
	@echo "  clean  - Remove build artifacts"
	@echo "  show   - Show source files that will be compiled"
	@echo "  help   - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make          # Build the test"
	@echo "  make test     # Build and run the test"
	@echo "  make clean    # Clean up"
	@echo "  ./$(TARGET)   # Run the test directly"

# Test target - build and run
test: $(TARGET)
	@echo ""
	@echo "==========================================  "
	@echo "Running TP Standalone Tests"
	@echo "=========================================="
	@echo ""
	@./$(TARGET)

.PHONY: all clean show help test
