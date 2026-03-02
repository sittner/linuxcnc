package main

import (
	"strings"
	"testing"
)

func TestParseConfigSimple(t *testing.T) {
	cfg := `
stDISPLAY_DATA
  in bErrRest bool
  out nState dint
`
	pins, err := ParseConfig(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfig error: %v", err)
	}
	if len(pins) != 2 {
		t.Fatalf("expected 2 pins, got %d", len(pins))
	}

	// First pin
	if pins[0].Dir != DirIn {
		t.Errorf("pins[0].Dir = %q, want %q", pins[0].Dir, DirIn)
	}
	if pins[0].HALPath != "stDISPLAY_DATA.bErrRest" {
		t.Errorf("pins[0].HALPath = %q", pins[0].HALPath)
	}
	if pins[0].ADSName != "stDISPLAY_DATA.bErrRest" {
		t.Errorf("pins[0].ADSName = %q", pins[0].ADSName)
	}
	if pins[0].TypeName != "BOOL" {
		t.Errorf("pins[0].TypeName = %q, want BOOL", pins[0].TypeName)
	}

	// Second pin
	if pins[1].Dir != DirOut {
		t.Errorf("pins[1].Dir = %q, want %q", pins[1].Dir, DirOut)
	}
	if pins[1].HALPath != "stDISPLAY_DATA.nState" {
		t.Errorf("pins[1].HALPath = %q", pins[1].HALPath)
	}
	if pins[1].TypeName != "DINT" {
		t.Errorf("pins[1].TypeName = %q, want DINT", pins[1].TypeName)
	}
}

func TestParseConfigArray(t *testing.T) {
	cfg := `
stROOT
  stPOOL[1..3]
    in bReady bool
    out nCount dint
`
	pins, err := ParseConfig(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfig error: %v", err)
	}
	// 3 instances × 2 pins = 6
	if len(pins) != 6 {
		t.Fatalf("expected 6 pins, got %d: %+v", len(pins), pins)
	}

	// Check HAL path uses dot notation for index.
	if pins[0].HALPath != "stROOT.stPOOL.1.bReady" {
		t.Errorf("pins[0].HALPath = %q", pins[0].HALPath)
	}
	// Check ADS name uses bracket notation.
	if pins[0].ADSName != "stROOT.stPOOL[1].bReady" {
		t.Errorf("pins[0].ADSName = %q", pins[0].ADSName)
	}

	// Third instance.
	if pins[4].HALPath != "stROOT.stPOOL.3.bReady" {
		t.Errorf("pins[4].HALPath = %q", pins[4].HALPath)
	}
	if pins[4].ADSName != "stROOT.stPOOL[3].bReady" {
		t.Errorf("pins[4].ADSName = %q", pins[4].ADSName)
	}
}

func TestParseConfigStringType(t *testing.T) {
	cfg := `
stBlock
  in sName string(32)
`
	pins, err := ParseConfig(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfig error: %v", err)
	}
	if len(pins) != 1 {
		t.Fatalf("expected 1 pin, got %d", len(pins))
	}
	if pins[0].TypeName != "STRING(32)" {
		t.Errorf("TypeName = %q, want STRING(32)", pins[0].TypeName)
	}
}

func TestParseConfigComments(t *testing.T) {
	cfg := `
# This is a comment
stBlock
  # nested comment
  in bFlag bool
`
	pins, err := ParseConfig(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfig error: %v", err)
	}
	if len(pins) != 1 {
		t.Fatalf("expected 1 pin, got %d", len(pins))
	}
}

func TestParseConfigArrayFollowedBySibling(t *testing.T) {
	// Verify that a sibling line after an array sub-block is NOT dropped.
	cfg := `
stROOT
  stPOOL[1..2]
    in bReady bool
  in bGlobal bool
`
	pins, err := ParseConfig(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfig error: %v", err)
	}
	// 2 array instances × 1 pin + 1 global pin = 3
	if len(pins) != 3 {
		t.Fatalf("expected 3 pins, got %d: %+v", len(pins), pins)
	}
	// Last pin should be the global one.
	last := pins[2]
	if last.HALPath != "stROOT.bGlobal" {
		t.Errorf("last pin HALPath = %q, want %q", last.HALPath, "stROOT.bGlobal")
	}
	if last.ADSName != "stROOT.bGlobal" {
		t.Errorf("last pin ADSName = %q", last.ADSName)
	}
}

func TestParseConfigInvalidArray(t *testing.T) {
	cfg := `
stBad[1..0]
  in bFlag bool
`
	_, err := ParseConfig(strings.NewReader(cfg))
	if err == nil {
		t.Error("expected error for invalid array range, got nil")
	}
}

func TestExpandContainer(t *testing.T) {
	// Simple name.
	inst, err := expandContainer("stFoo")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(inst) != 1 || inst[0].halSeg != "stFoo" {
		t.Errorf("expected [{stFoo stFoo}], got %+v", inst)
	}

	// Array.
	inst, err = expandContainer("arr[2..4]")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(inst) != 3 {
		t.Fatalf("expected 3 instances, got %d", len(inst))
	}
	if inst[0].adsSeg != "arr[2]" {
		t.Errorf("inst[0].adsSeg = %q", inst[0].adsSeg)
	}
	if inst[2].adsSeg != "arr[4]" {
		t.Errorf("inst[2].adsSeg = %q", inst[2].adsSeg)
	}
}

func TestParseTypeInfo(t *testing.T) {
	tests := []struct {
		typeName string
		wantSize uint32
		wantErr  bool
	}{
		{"BOOL", 1, false},
		{"DINT", 4, false},
		{"REAL", 4, false},
		{"LREAL", 8, false},
		{"STRING(32)", 33, false},
		{"STRING(0)", 0, true},
		{"UNKNOWN_TYPE", 0, true},
	}
	for _, tc := range tests {
		ti, err := parseTypeInfo(tc.typeName)
		if tc.wantErr {
			if err == nil {
				t.Errorf("parseTypeInfo(%q) want error, got nil", tc.typeName)
			}
			continue
		}
		if err != nil {
			t.Errorf("parseTypeInfo(%q) unexpected error: %v", tc.typeName, err)
			continue
		}
		if ti.byteSize != tc.wantSize {
			t.Errorf("parseTypeInfo(%q).byteSize = %d, want %d", tc.typeName, ti.byteSize, tc.wantSize)
		}
	}
}

func TestStringMemAccessorReadEmpty(t *testing.T) {
	ti := typeInfo{adsTypeName: "STRING(31)", adstID: 30, byteSize: 32, strLen: 31}
	acc := newStringMemAccessor(ti)

	data, err := acc.ReadBytes()
	if err != nil {
		t.Fatalf("ReadBytes error: %v", err)
	}
	if len(data) != 32 {
		t.Fatalf("ReadBytes length = %d, want 32", len(data))
	}
	for i, b := range data {
		if b != 0 {
			t.Errorf("byte[%d] = %d, want 0", i, b)
		}
	}
}

func TestStringMemAccessorWriteAndRead(t *testing.T) {
	ti := typeInfo{adsTypeName: "STRING(31)", adstID: 30, byteSize: 32, strLen: 31}
	acc := newStringMemAccessor(ti)

	// Write a short string (shorter than n).
	if err := acc.WriteBytes([]byte("hello")); err != nil {
		t.Fatalf("WriteBytes error: %v", err)
	}
	data, _ := acc.ReadBytes()
	if len(data) != 32 {
		t.Fatalf("ReadBytes length = %d, want 32", len(data))
	}
	if string(data[:5]) != "hello" {
		t.Errorf("first 5 bytes = %q, want %q", string(data[:5]), "hello")
	}
	// Remainder must be zero.
	for i := 5; i < 32; i++ {
		if data[i] != 0 {
			t.Errorf("byte[%d] = %d, want 0 (null-padding)", i, data[i])
		}
	}
}

func TestStringMemAccessorWriteExactN(t *testing.T) {
	// STRING(4) → buffer of 5 bytes; write exactly 4 chars.
	ti := typeInfo{adsTypeName: "STRING(4)", adstID: 30, byteSize: 5, strLen: 4}
	acc := newStringMemAccessor(ti)

	if err := acc.WriteBytes([]byte("abcd")); err != nil {
		t.Fatalf("WriteBytes error: %v", err)
	}
	data, _ := acc.ReadBytes()
	if string(data[:4]) != "abcd" {
		t.Errorf("data[:4] = %q, want %q", string(data[:4]), "abcd")
	}
	if data[4] != 0 {
		t.Errorf("null terminator byte[4] = %d, want 0", data[4])
	}
}

func TestStringMemAccessorWriteLongerThanN(t *testing.T) {
	// STRING(4) → buffer of 5 bytes; write 6 chars (longer than n+1).
	ti := typeInfo{adsTypeName: "STRING(4)", adstID: 30, byteSize: 5, strLen: 4}
	acc := newStringMemAccessor(ti)

	if err := acc.WriteBytes([]byte("toolong")); err != nil {
		t.Fatalf("WriteBytes error: %v", err)
	}
	data, _ := acc.ReadBytes()
	if len(data) != 5 {
		t.Fatalf("ReadBytes length = %d, want 5", len(data))
	}
	// Buffer must be null-terminated.
	if data[4] != 0 {
		t.Errorf("null terminator byte[4] = %d, want 0", data[4])
	}
	// First 4 bytes should be first 4 chars of input.
	if string(data[:4]) != "tool" {
		t.Errorf("data[:4] = %q, want %q", string(data[:4]), "tool")
	}
}

func TestStringMemAccessorMetadata(t *testing.T) {
	ti := typeInfo{adsTypeName: "STRING(31)", adstID: 30, byteSize: 32, strLen: 31}
	acc := newStringMemAccessor(ti)

	if acc.Size() != 32 {
		t.Errorf("Size() = %d, want 32", acc.Size())
	}
	if acc.TypeName() != "STRING(31)" {
		t.Errorf("TypeName() = %q, want %q", acc.TypeName(), "STRING(31)")
	}
	if acc.TypeID() != 30 {
		t.Errorf("TypeID() = %d, want 30", acc.TypeID())
	}
}

func TestStringMemAccessorReadReturnsCopy(t *testing.T) {
	ti := typeInfo{adsTypeName: "STRING(4)", adstID: 30, byteSize: 5, strLen: 4}
	acc := newStringMemAccessor(ti)

	_ = acc.WriteBytes([]byte("abcd"))
	data, _ := acc.ReadBytes()

	// Mutate the returned slice.
	data[0] = 'X'

	// Internal buffer must be unchanged.
	data2, _ := acc.ReadBytes()
	if data2[0] != 'a' {
		t.Errorf("internal buffer modified by caller: data2[0] = %q, want 'a'", data2[0])
	}
}

func TestParseConfigActionsSimple(t *testing.T) {
	cfg := `
stDISPLAY_DATA
  out dtTime DT
  in bFlag bool
`
	actions, err := ParseConfigActions(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfigActions error: %v", err)
	}

	// Expected sequence: BeginContainer + 2×Pin + EndContainer
	if len(actions) != 4 {
		t.Fatalf("expected 4 actions, got %d: %+v", len(actions), actions)
	}

	if actions[0].Kind != ConfigActionBeginContainer {
		t.Errorf("actions[0].Kind = %d, want BeginContainer", actions[0].Kind)
	}
	// stDISPLAY_DATA has DT (align 4) and BOOL (align 1) → max = 4
	if actions[0].Alignment != 4 {
		t.Errorf("actions[0].Alignment = %d, want 4", actions[0].Alignment)
	}

	if actions[1].Kind != ConfigActionPin {
		t.Errorf("actions[1].Kind = %d, want Pin", actions[1].Kind)
	}
	if actions[1].Pin.TypeName != "DT" {
		t.Errorf("actions[1].Pin.TypeName = %q, want DT", actions[1].Pin.TypeName)
	}
	if actions[1].Alignment != 4 {
		t.Errorf("actions[1].Alignment = %d, want 4 (DT)", actions[1].Alignment)
	}

	if actions[2].Kind != ConfigActionPin {
		t.Errorf("actions[2].Kind = %d, want Pin", actions[2].Kind)
	}
	if actions[2].Alignment != 1 {
		t.Errorf("actions[2].Alignment = %d, want 1 (BOOL)", actions[2].Alignment)
	}

	if actions[3].Kind != ConfigActionEndContainer {
		t.Errorf("actions[3].Kind = %d, want EndContainer", actions[3].Kind)
	}
	if actions[3].Alignment != 4 {
		t.Errorf("actions[3].Alignment = %d, want 4", actions[3].Alignment)
	}
}

func TestParseConfigActionsNestedStruct(t *testing.T) {
	cfg := `
stRoot
  stInner
    out fVal real
    in bFlag bool
  out nOuter dint
`
	actions, err := ParseConfigActions(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfigActions error: %v", err)
	}

	// stInner: max(REAL:4, BOOL:1) = 4
	// stRoot: max(stInner:4, DINT:4) = 4
	// Expected:
	//
	//	0: BeginContainer(4)  – stRoot
	//	1: BeginContainer(4)  – stInner
	//	2: Pin fVal (REAL, align 4)
	//	3: Pin bFlag (BOOL, align 1)
	//	4: EndContainer(4)    – stInner
	//	5: Pin nOuter (DINT, align 4)
	//	6: EndContainer(4)    – stRoot
	if len(actions) != 7 {
		t.Fatalf("expected 7 actions, got %d: %+v", len(actions), actions)
	}
	if actions[0].Kind != ConfigActionBeginContainer || actions[0].Alignment != 4 {
		t.Errorf("actions[0]: %+v", actions[0])
	}
	if actions[1].Kind != ConfigActionBeginContainer || actions[1].Alignment != 4 {
		t.Errorf("actions[1]: %+v", actions[1])
	}
	if actions[4].Kind != ConfigActionEndContainer || actions[4].Alignment != 4 {
		t.Errorf("actions[4]: %+v", actions[4])
	}
	if actions[5].Kind != ConfigActionPin || actions[5].Pin.TypeName != "DINT" {
		t.Errorf("actions[5]: %+v", actions[5])
	}
	if actions[6].Kind != ConfigActionEndContainer || actions[6].Alignment != 4 {
		t.Errorf("actions[6]: %+v", actions[6])
	}
}

func TestParseConfigActionsArray(t *testing.T) {
	cfg := `
stRoot
  stPool[1..2]
    out fVal real
    in bFlag bool
`
	actions, err := ParseConfigActions(strings.NewReader(cfg))
	if err != nil {
		t.Fatalf("ParseConfigActions error: %v", err)
	}

	// stRoot: max of two pool containers, each with max(REAL:4, BOOL:1)=4 → root align 4
	// Expected (each pool element: BeginContainer(4) + 2 Pins + EndContainer(4)):
	//
	//	0: BeginContainer(4)    – stRoot
	//	1: BeginContainer(4)    – stPool[1]
	//	2: Pin fVal (REAL, align 4)
	//	3: Pin bFlag (BOOL, align 1)
	//	4: EndContainer(4)      – stPool[1]
	//	5: BeginContainer(4)    – stPool[2]
	//	6: Pin fVal
	//	7: Pin bFlag
	//	8: EndContainer(4)      – stPool[2]
	//	9: EndContainer(4)      – stRoot
	if len(actions) != 10 {
		t.Fatalf("expected 10 actions, got %d: %+v", len(actions), actions)
	}

	// Check first pool instance pin name
	if actions[2].Pin.ADSName != "stRoot.stPool[1].fVal" {
		t.Errorf("actions[2] ADSName = %q", actions[2].Pin.ADSName)
	}
	// Check second pool instance pin name
	if actions[6].Pin.ADSName != "stRoot.stPool[2].fVal" {
		t.Errorf("actions[6] ADSName = %q", actions[6].Pin.ADSName)
	}
}

func TestParseConfigActionsAlignmentForType(t *testing.T) {
	tests := []struct {
		typeName  string
		wantAlign uint32
	}{
		{"BOOL", 1},
		{"BYTE", 1},
		{"USINT", 1},
		{"SINT", 1},
		{"WORD", 2},
		{"UINT", 2},
		{"INT", 2},
		{"DWORD", 4},
		{"UDINT", 4},
		{"DINT", 4},
		{"REAL", 4},
		{"TIME", 4},
		{"TOD", 4},
		{"DATE", 4},
		{"DT", 4},
		{"LREAL", 8},
		{"STRING(31)", 1},
		{"STRING(80)", 1},
	}
	for _, tc := range tests {
		got := alignmentForType(tc.typeName)
		if got != tc.wantAlign {
			t.Errorf("alignmentForType(%q) = %d, want %d", tc.typeName, got, tc.wantAlign)
		}
	}
}
