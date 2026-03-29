package threadcfg

import (
	"testing"

	"github.com/sittner/linuxcnc/src/launcher/pkg/inifile"
)

func TestParseCPUList(t *testing.T) {
	tests := []struct {
		input string
		want  []int
	}{
		{"", nil},
		{"0", []int{0}},
		{"0,2,4", []int{0, 2, 4}},
		{"0-3", []int{0, 1, 2, 3}},
		{"0-3,8-9", []int{0, 1, 2, 3, 8, 9}},
		{"1,3-5,7", []int{1, 3, 4, 5, 7}},
		{" 2 , 4 ", []int{2, 4}},
	}
	for _, tt := range tests {
		got := parseCPUList(tt.input)
		if len(got) != len(tt.want) {
			t.Errorf("parseCPUList(%q) = %v, want %v", tt.input, got, tt.want)
			continue
		}
		for i := range got {
			if got[i] != tt.want[i] {
				t.Errorf("parseCPUList(%q)[%d] = %d, want %d", tt.input, i, got[i], tt.want[i])
			}
		}
	}
}

func makeINI(content string) *inifile.IniFile {
	ini, err := inifile.ParseString(content)
	if err != nil {
		panic(err)
	}
	return ini
}

func TestParseThreads_Basic(t *testing.T) {
	ini := makeINI(`
[THREAD-BASE]
PERIOD = 20000

[THREAD-SERVO]
PERIOD = 1000000
`)

	threads, err := ParseThreads(ini)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(threads) != 2 {
		t.Fatalf("got %d threads, want 2", len(threads))
	}

	// First thread: BASE
	if threads[0].Name != "base-thread" {
		t.Errorf("threads[0].Name = %q, want %q", threads[0].Name, "base-thread")
	}
	if threads[0].Period != 20000 {
		t.Errorf("threads[0].Period = %d, want 20000", threads[0].Period)
	}
	if threads[0].FP != 1 {
		t.Errorf("threads[0].FP = %d, want 1 (default)", threads[0].FP)
	}
	if threads[0].CPU != -1 {
		t.Errorf("threads[0].CPU = %d, want -1 (auto)", threads[0].CPU)
	}

	// Second thread: SERVO
	if threads[1].Name != "servo-thread" {
		t.Errorf("threads[1].Name = %q, want %q", threads[1].Name, "servo-thread")
	}
	if threads[1].Period != 1000000 {
		t.Errorf("threads[1].Period = %d, want 1000000", threads[1].Period)
	}
}

func TestParseThreads_CustomNameAndOptions(t *testing.T) {
	ini := makeINI(`
[THREAD-CUSTOM]
PERIOD = 50000
NAME = my-fast-loop
FP = 0
CPU = 4
`)

	threads, err := ParseThreads(ini)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(threads) != 1 {
		t.Fatalf("got %d threads, want 1", len(threads))
	}
	th := threads[0]
	if th.Name != "my-fast-loop" {
		t.Errorf("Name = %q, want %q", th.Name, "my-fast-loop")
	}
	if th.FP != 0 {
		t.Errorf("FP = %d, want 0", th.FP)
	}
	if th.CPU != 4 {
		t.Errorf("CPU = %d, want 4", th.CPU)
	}
}

func TestParseThreads_NoPeriodError(t *testing.T) {
	ini := makeINI(`
[THREAD-BAD]
FP = 1
`)
	_, err := ParseThreads(ini)
	if err == nil {
		t.Fatal("expected error for missing PERIOD")
	}
}

func TestParseThreads_NoSectionsError(t *testing.T) {
	ini := makeINI(`
[HAL]
HALFILE = test.hal
`)
	_, err := ParseThreads(ini)
	if err == nil {
		t.Fatal("expected error for no THREAD sections")
	}
}

func TestValidateOrder_OK(t *testing.T) {
	threads := []ThreadConfig{
		{section: "BASE", Period: 20000},
		{section: "SERVO", Period: 1000000},
	}
	if err := ValidateOrder(threads); err != nil {
		t.Errorf("unexpected error: %v", err)
	}
}

func TestValidateOrder_WrongOrder(t *testing.T) {
	threads := []ThreadConfig{
		{section: "SERVO", Period: 1000000},
		{section: "BASE", Period: 20000},
	}
	err := ValidateOrder(threads)
	if err == nil {
		t.Fatal("expected error for wrong thread order")
	}
}

func TestValidateOrder_EqualPeriods(t *testing.T) {
	threads := []ThreadConfig{
		{section: "A", Period: 1000000},
		{section: "B", Period: 1000000},
	}
	if err := ValidateOrder(threads); err != nil {
		t.Errorf("unexpected error for equal periods: %v", err)
	}
}

func TestAssignCPUs_MockTopology(t *testing.T) {
	// Build a mock topology: CPUs 0-7, isolated 4-7, HT pairs: (0,4), (1,5), (2,6), (3,7)
	// Physical cores: 0,1,2,3 (lower of each pair).
	// Isolated physical cores: none (4,5,6,7 are all HT shadows).
	// This means auto-assign gets no pool → no affinity.
	topo := &cpuTopology{
		online:        []int{0, 1, 2, 3, 4, 5, 6, 7},
		isolated:      []int{4, 5, 6, 7},
		physicalCores: map[int]bool{0: true, 1: true, 2: true, 3: true},
		siblingOf: map[int]int{
			0: 0, 1: 1, 2: 2, 3: 3,
			4: 0, 5: 1, 6: 2, 7: 3,
		},
	}

	threads := []ThreadConfig{
		{section: "SERVO", CPU: -1},
	}

	result, err := topo.assignCPUs(threads)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// Pool is empty (isolated CPUs are all HT shadows), so CPU stays -1.
	if result[0].CPU != -1 {
		t.Errorf("CPU = %d, want -1 (no isolated physical cores)", result[0].CPU)
	}
}

func TestAssignCPUs_IsolatedPhysical(t *testing.T) {
	// CPUs 0-3, isolated: 2,3, no HT (each CPU is its own sibling).
	topo := &cpuTopology{
		online:        []int{0, 1, 2, 3},
		isolated:      []int{2, 3},
		physicalCores: map[int]bool{0: true, 1: true, 2: true, 3: true},
		siblingOf:     map[int]int{0: 0, 1: 1, 2: 2, 3: 3},
	}

	threads := []ThreadConfig{
		{section: "BASE", CPU: -1},
		{section: "SERVO", CPU: -1},
	}

	result, err := topo.assignCPUs(threads)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// Highest isolated first → 3, then 2.
	if result[0].CPU != 3 {
		t.Errorf("threads[0].CPU = %d, want 3", result[0].CPU)
	}
	if result[1].CPU != 2 {
		t.Errorf("threads[1].CPU = %d, want 2", result[1].CPU)
	}
}

func TestAssignCPUs_UserPinnedNonExistent(t *testing.T) {
	topo := &cpuTopology{
		online:        []int{0, 1},
		isolated:      nil,
		physicalCores: map[int]bool{0: true, 1: true},
		siblingOf:     map[int]int{0: 0, 1: 1},
	}

	threads := []ThreadConfig{
		{section: "SERVO", CPU: 99},
	}

	_, err := topo.assignCPUs(threads)
	if err == nil {
		t.Fatal("expected error for non-existent CPU")
	}
}

func TestAssignCPUs_UserPinnedHTSibling(t *testing.T) {
	topo := &cpuTopology{
		online:        []int{0, 1, 4, 5},
		isolated:      []int{4, 5},
		physicalCores: map[int]bool{0: true, 1: true},
		siblingOf:     map[int]int{0: 0, 4: 0, 1: 1, 5: 1},
	}

	threads := []ThreadConfig{
		{section: "SERVO", CPU: 4}, // HT shadow of core 0
	}

	_, err := topo.assignCPUs(threads)
	if err == nil {
		t.Fatal("expected error for HT sibling CPU")
	}
}

func TestAssignCPUs_PoolExhausted(t *testing.T) {
	topo := &cpuTopology{
		online:        []int{0, 1, 2},
		isolated:      []int{2},
		physicalCores: map[int]bool{0: true, 1: true, 2: true},
		siblingOf:     map[int]int{0: 0, 1: 1, 2: 2},
	}

	threads := []ThreadConfig{
		{section: "BASE", CPU: -1},
		{section: "SERVO", CPU: -1},
		{section: "EXTRA", CPU: -1},
	}

	result, err := topo.assignCPUs(threads)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// Only 1 isolated core (2), so first thread gets it, rest get -1.
	if result[0].CPU != 2 {
		t.Errorf("threads[0].CPU = %d, want 2", result[0].CPU)
	}
	if result[1].CPU != -1 {
		t.Errorf("threads[1].CPU = %d, want -1", result[1].CPU)
	}
	if result[2].CPU != -1 {
		t.Errorf("threads[2].CPU = %d, want -1", result[2].CPU)
	}
}
