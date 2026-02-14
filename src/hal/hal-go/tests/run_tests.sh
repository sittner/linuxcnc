#!/bin/bash
# Integration tests for hal-go package
# Must be run with LinuxCNC environment (halrun available)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HAL_GO_DIR="$(dirname "$SCRIPT_DIR")"
TOP_DIR="$(cd "$HAL_GO_DIR/../../.." && pwd)"
PASSTHROUGH="$TOP_DIR/bin/hal-go-passthrough"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() {
    echo -e "${GREEN}PASS${NC}: $1"
    : $((TESTS_PASSED++))
}

fail() {
    echo -e "${RED}FAIL${NC}: $1"
    : $((TESTS_FAILED++))
}

run_test() {
    local name="$1"
    local script="$2"
    : $((TESTS_RUN++))
    echo "Running: $name"
    if bash -c "$script"; then
        pass "$name"
    else
        fail "$name"
    fi
}

# Check prerequisites
if ! command -v halrun &> /dev/null; then
    echo "ERROR: halrun not found. Please source scripts/rip-environment first."
    exit 1
fi

if [[ ! -x "$PASSTHROUGH" ]]; then
    echo "ERROR: hal-go-passthrough not found at $PASSTHROUGH"
    echo "Please build with 'make' first."
    exit 1
fi

echo "============================================"
echo "hal-go Integration Tests"
echo "============================================"
echo ""

# Test 1: Component loads successfully
run_test "Component loads" "
    halrun -f - <<EOF
loadusr -W $PASSTHROUGH
show comp passthrough
unload passthrough
EOF
"

# Test 2: All pins are created
run_test "Pin creation (8 pins)" "
    OUTPUT=\$(halrun -f - <<EOF
loadusr -W $PASSTHROUGH
show pin passthrough.*
EOF
)
    echo \"\$OUTPUT\" | grep -c 'passthrough\.' | grep -q '^8$'
"

# Test 3: Float passthrough works
run_test "Float passthrough" "
    OUTPUT=\$(halrun -f - <<EOF
loadusr -W $PASSTHROUGH
setp passthrough.in-float 123.456
# Give component time to process
loadusr -w sleep 0.1
show pin passthrough.out-float
unload passthrough
EOF
)
    echo \"\$OUTPUT\" | grep -q '123.456'
"

# Test 4: Bit passthrough works
run_test "Bit passthrough" "
    OUTPUT=\$(halrun -f - <<EOF
loadusr -W $PASSTHROUGH
setp passthrough.in-bit true
loadusr -w sleep 0.1
show pin passthrough.out-bit
unload passthrough
EOF
)
    echo \"\$OUTPUT\" | grep -q 'TRUE'
"

# Test 5: S32 passthrough works
run_test "S32 passthrough" "
    OUTPUT=\$(halrun -f - <<EOF
loadusr -W $PASSTHROUGH
setp passthrough.in-s32 -42
loadusr -w sleep 0.1
show pin passthrough.out-s32
unload passthrough
EOF
)
    echo \"\$OUTPUT\" | grep -q '\-42'
"

# Test 6: U32 passthrough works
run_test "U32 passthrough" "
    OUTPUT=\$(halrun -f - <<EOF
loadusr -W $PASSTHROUGH
setp passthrough.in-u32 0xDEADBEEF
loadusr -w sleep 0.1
show pin passthrough.out-u32
unload passthrough
EOF
)
    echo \"\$OUTPUT\" | grep -q 'DEADBEEF'
"

# Test 7: Clean unload (SIGTERM)
run_test "Clean unload (SIGTERM)" "
    halrun -f - <<EOF
loadusr -W $PASSTHROUGH
unload passthrough
EOF
"

# Test 8: No zombie processes after unload
run_test "No zombie processes" "
    halrun -f - <<EOF
loadusr -W $PASSTHROUGH
unload passthrough
EOF
    sleep 0.5
    ! pgrep -f 'hal-go-passthrough' > /dev/null
"

echo ""
echo "============================================"
echo "Results: $TESTS_PASSED/$TESTS_RUN passed"
echo "============================================"

if [[ $TESTS_FAILED -gt 0 ]]; then
    exit 1
fi
exit 0
