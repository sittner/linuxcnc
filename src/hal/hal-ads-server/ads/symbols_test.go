package ads

import (
	"encoding/binary"
	"testing"
)

// mockPin is a simple in-memory PinAccessor for testing.
type mockPin struct {
	typeName string
	typeID   uint32
	size     uint32
	data     []byte
}

func (m *mockPin) ReadBytes() ([]byte, error) {
	out := make([]byte, len(m.data))
	copy(out, m.data)
	return out, nil
}

func (m *mockPin) WriteBytes(data []byte) error {
	m.data = make([]byte, len(data))
	copy(m.data, data)
	return nil
}

func (m *mockPin) Size() uint32     { return m.size }
func (m *mockPin) TypeName() string { return m.typeName }
func (m *mockPin) TypeID() uint32   { return m.typeID }

func newBoolPin(val bool) *mockPin {
	b := byte(0)
	if val {
		b = 1
	}
	return &mockPin{typeName: "BOOL", typeID: ADSTBool, size: 1, data: []byte{b}}
}

func newDintPin(val int32) *mockPin {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, uint32(val))
	return &mockPin{typeName: "DINT", typeID: ADSTInt32, size: 4, data: b}
}

func TestSymbolTableRegisterAndGetByName(t *testing.T) {
	st := NewSymbolTable()
	p := newBoolPin(true)
	sym := st.Register("stFoo.bReady", p)

	if sym.Name != "stFoo.bReady" {
		t.Errorf("Name = %q", sym.Name)
	}
	if sym.IndexGroup != IdxGrpProcessImageRW {
		t.Errorf("IndexGroup = 0x%X", sym.IndexGroup)
	}
	if sym.IndexOffset != 0 {
		t.Errorf("first symbol offset should be 0, got %d", sym.IndexOffset)
	}

	got := st.GetByName("stFoo.bReady")
	if got != sym {
		t.Error("GetByName returned wrong symbol")
	}

	// Second symbol offset should advance by size of first.
	p2 := newDintPin(42)
	sym2 := st.Register("stFoo.nVal", p2)
	if sym2.IndexOffset != 1 { // 1 byte for the bool
		t.Errorf("second symbol offset = %d, want 1", sym2.IndexOffset)
	}
}

func TestSymbolTableHandleLifecycle(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stX.bFlag", newBoolPin(false))

	handle, errCode := st.CreateHandle("stX.bFlag")
	if errCode != ErrNoError {
		t.Fatalf("CreateHandle error: 0x%X", errCode)
	}
	if handle == 0 {
		t.Error("handle should not be 0")
	}

	sym := st.GetByHandle(handle)
	if sym == nil || sym.Name != "stX.bFlag" {
		t.Errorf("GetByHandle returned %v", sym)
	}

	// Release and verify.
	if errCode := st.ReleaseHandle(handle); errCode != ErrNoError {
		t.Errorf("ReleaseHandle error: 0x%X", errCode)
	}
	if st.GetByHandle(handle) != nil {
		t.Error("handle should be nil after release")
	}
}

func TestSymbolTableReadDataByHandle(t *testing.T) {
	st := NewSymbolTable()
	p := newDintPin(12345)
	st.Register("stA.nVal", p)

	handle, _ := st.CreateHandle("stA.nVal")

	data, errCode := st.ReadData(IdxGrpSymbolValueByHandle, handle, 4)
	if errCode != ErrNoError {
		t.Fatalf("ReadData error: 0x%X", errCode)
	}
	if len(data) != 4 {
		t.Fatalf("ReadData length = %d, want 4", len(data))
	}
	val := int32(binary.LittleEndian.Uint32(data))
	if val != 12345 {
		t.Errorf("ReadData value = %d, want 12345", val)
	}
}

func TestSymbolTableWriteDataByHandle(t *testing.T) {
	st := NewSymbolTable()
	p := newBoolPin(false)
	st.Register("stA.bFlag", p)

	handle, _ := st.CreateHandle("stA.bFlag")

	errCode := st.WriteData(IdxGrpSymbolValueByHandle, handle, []byte{1})
	if errCode != ErrNoError {
		t.Fatalf("WriteData error: 0x%X", errCode)
	}
	if p.data[0] != 1 {
		t.Errorf("pin value after write = %d, want 1", p.data[0])
	}
}

func TestSymbolTableHandleByName(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stB.nCount", newDintPin(0))

	// ReadWriteData with IdxGrpSymbolHandleByName should return a 4-byte handle.
	data, errCode := st.ReadWriteData(IdxGrpSymbolHandleByName, 0, 4, []byte("stB.nCount"))
	if errCode != ErrNoError {
		t.Fatalf("ReadWriteData error: 0x%X", errCode)
	}
	if len(data) != 4 {
		t.Fatalf("expected 4-byte handle, got %d bytes", len(data))
	}
	handle := binary.LittleEndian.Uint32(data)
	if handle == 0 {
		t.Error("handle should not be 0")
	}
	// Verify the handle is valid.
	sym := st.GetByHandle(handle)
	if sym == nil || sym.Name != "stB.nCount" {
		t.Errorf("GetByHandle(%d) = %v", handle, sym)
	}
}

func TestSymbolTableReleaseHandleViaWrite(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stC.bX", newBoolPin(true))

	handle, _ := st.CreateHandle("stC.bX")

	// Release via WriteData with IdxGrpReleaseHandle: handle is passed as indexOffset.
	errCode := st.WriteData(IdxGrpReleaseHandle, handle, nil)
	if errCode != ErrNoError {
		t.Fatalf("release handle via WriteData error: 0x%X", errCode)
	}
	if st.GetByHandle(handle) != nil {
		t.Error("handle should be nil after release")
	}
}

func TestSymbolTableSymbolCount(t *testing.T) {
	st := NewSymbolTable()
	st.Register("s1", newBoolPin(false))
	st.Register("s2", newDintPin(0))

	data, errCode := st.ReadData(IdxGrpSymbolCount, 0, 4)
	if errCode != ErrNoError {
		t.Fatalf("ReadData SymbolCount error: 0x%X", errCode)
	}
	count := binary.LittleEndian.Uint32(data)
	if count != 2 {
		t.Errorf("SymbolCount = %d, want 2", count)
	}
}

func TestSymbolTableProcessImage(t *testing.T) {
	st := NewSymbolTable()
	p1 := newBoolPin(true)
	p2 := newDintPin(99)
	sym1 := st.Register("s1", p1) // offset 0, size 1
	sym2 := st.Register("s2", p2) // offset 1, size 4

	// Read s1 by process image offset.
	data, errCode := st.ReadData(IdxGrpProcessImageRW, sym1.IndexOffset, 1)
	if errCode != ErrNoError {
		t.Fatalf("read s1 error: 0x%X", errCode)
	}
	if data[0] != 1 {
		t.Errorf("s1 value = %d, want 1", data[0])
	}

	// Write s2 by process image offset.
	newVal := make([]byte, 4)
	negOne := int32(-1)
	binary.LittleEndian.PutUint32(newVal, uint32(negOne))
	errCode = st.WriteData(IdxGrpProcessImageRW, sym2.IndexOffset, newVal)
	if errCode != ErrNoError {
		t.Fatalf("write s2 error: 0x%X", errCode)
	}
	if int32(binary.LittleEndian.Uint32(p2.data)) != -1 {
		t.Errorf("s2 value = %d, want -1", int32(binary.LittleEndian.Uint32(p2.data)))
	}
}

func TestSymbolTableNullTerminatedName(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stFoo.bFlag", newBoolPin(true))

	// TwinCAT sends null-terminated symbol names; the server must strip them.
	nameWithNull := []byte("stFoo.bFlag\x00")
	data, errCode := st.ReadWriteData(IdxGrpSymbolHandleByName, 0, 4, nameWithNull)
	if errCode != ErrNoError {
		t.Fatalf("CreateHandle with null-terminated name error: 0x%X", errCode)
	}
	handle := binary.LittleEndian.Uint32(data)
	if st.GetByHandle(handle) == nil {
		t.Error("handle should resolve to a symbol")
	}
}

func TestSymbolTableCompactSymbolInfo(t *testing.T) {
	st := NewSymbolTable()
	sym := st.Register("stFoo.nVal", newDintPin(0))

	// Request compact form (readLen == 12).
	data, errCode := st.ReadWriteData(IdxGrpSymbolInfoByName, 0, 12, []byte("stFoo.nVal"))
	if errCode != ErrNoError {
		t.Fatalf("compact symbol info error: 0x%X", errCode)
	}
	if len(data) != 12 {
		t.Fatalf("expected 12 bytes, got %d", len(data))
	}
	ig := binary.LittleEndian.Uint32(data[0:4])
	io := binary.LittleEndian.Uint32(data[4:8])
	size := binary.LittleEndian.Uint32(data[8:12])
	if ig != sym.IndexGroup {
		t.Errorf("IndexGroup = 0x%X, want 0x%X", ig, sym.IndexGroup)
	}
	if io != sym.IndexOffset {
		t.Errorf("IndexOffset = %d, want %d", io, sym.IndexOffset)
	}
	if size != 4 {
		t.Errorf("Size = %d, want 4", size)
	}
}

func TestSymbolTableSumRead(t *testing.T) {
	st := NewSymbolTable()
	p1 := newBoolPin(true)
	p2 := newDintPin(42)
	sym1 := st.Register("s1", p1) // offset 0, size 1
	sym2 := st.Register("s2", p2) // offset 1, size 4

	// Build a SumRead request for both symbols.
	writeData := make([]byte, 24) // 2 × 12 bytes
	binary.LittleEndian.PutUint32(writeData[0:], sym1.IndexGroup)
	binary.LittleEndian.PutUint32(writeData[4:], sym1.IndexOffset)
	binary.LittleEndian.PutUint32(writeData[8:], sym1.Accessor.Size())
	binary.LittleEndian.PutUint32(writeData[12:], sym2.IndexGroup)
	binary.LittleEndian.PutUint32(writeData[16:], sym2.IndexOffset)
	binary.LittleEndian.PutUint32(writeData[20:], sym2.Accessor.Size())

	resp, errCode := st.ReadWriteData(IdxGrpSumRead, 2, 0, writeData)
	if errCode != ErrNoError {
		t.Fatalf("SumRead error: 0x%X", errCode)
	}
	// Response: 2×8 (errCode+length) + 1 byte (bool) + 4 bytes (dint) = 21 bytes.
	if len(resp) != 21 {
		t.Fatalf("SumRead response length = %d, want 21", len(resp))
	}
	if binary.LittleEndian.Uint32(resp[0:4]) != ErrNoError {
		t.Errorf("SumRead s1 errCode = 0x%X", binary.LittleEndian.Uint32(resp[0:4]))
	}
	if binary.LittleEndian.Uint32(resp[4:8]) != 1 { // length of s1 (bool = 1 byte)
		t.Errorf("SumRead s1 length = %d, want 1", binary.LittleEndian.Uint32(resp[4:8]))
	}
	if binary.LittleEndian.Uint32(resp[8:12]) != ErrNoError {
		t.Errorf("SumRead s2 errCode = 0x%X", binary.LittleEndian.Uint32(resp[8:12]))
	}
	if binary.LittleEndian.Uint32(resp[12:16]) != 4 { // length of s2 (dint = 4 bytes)
		t.Errorf("SumRead s2 length = %d, want 4", binary.LittleEndian.Uint32(resp[12:16]))
	}
	// s1 value: byte 16.
	if resp[16] != 1 {
		t.Errorf("SumRead s1 value = %d, want 1", resp[16])
	}
	// s2 value: bytes 17–20.
	if int32(binary.LittleEndian.Uint32(resp[17:21])) != 42 {
		t.Errorf("SumRead s2 value = %d, want 42", int32(binary.LittleEndian.Uint32(resp[17:21])))
	}
}

func TestSymbolTableFallbackMatching(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stFoo.bFlag", newBoolPin(true))

	// Prefix stripping: "GVL.stFoo.bFlag" should resolve to "stFoo.bFlag".
	handle, errCode := st.CreateHandle("GVL.stFoo.bFlag")
	if errCode != ErrNoError {
		t.Fatalf("fallback (prefix) CreateHandle error: 0x%X", errCode)
	}
	if st.GetByHandle(handle) == nil {
		t.Error("fallback prefix: handle should resolve")
	}

	// Case-insensitive: "STFOO.BFLAG" should resolve.
	handle2, errCode2 := st.CreateHandle("STFOO.BFLAG")
	if errCode2 != ErrNoError {
		t.Fatalf("fallback (case) CreateHandle error: 0x%X", errCode2)
	}
	if st.GetByHandle(handle2) == nil {
		t.Error("fallback case: handle should resolve")
	}
}

// ---------------------------------------------------------------------------
// Alignment helper tests
// ---------------------------------------------------------------------------

func TestAlignUp(t *testing.T) {
	tests := []struct {
		offset    uint32
		alignment uint32
		want      uint32
	}{
		{0, 1, 0},
		{1, 1, 1},
		{3, 1, 3},
		{0, 4, 0},
		{1, 4, 4},
		{4, 4, 4},
		{5, 4, 8},
		{6, 4, 8},
		{7, 4, 8},
		{8, 4, 8},
		{0, 8, 0},
		{1, 8, 8},
		{8, 8, 8},
		{9, 8, 16},
		{6, 2, 6},
		{7, 2, 8},
		// alignment 0 treated same as 1 (no-op)
		{5, 0, 5},
	}
	for _, tc := range tests {
		got := alignUp(tc.offset, tc.alignment)
		if got != tc.want {
			t.Errorf("alignUp(%d, %d) = %d, want %d", tc.offset, tc.alignment, got, tc.want)
		}
	}
}

func TestRegisterAligned(t *testing.T) {
	st := NewSymbolTable()

	// BOOL at offset 0 (align 1) → offset 0
	p1 := newBoolPin(false)
	sym1 := st.RegisterAligned("s1", p1, 1)
	if sym1.IndexOffset != 0 {
		t.Errorf("s1 offset = %d, want 0", sym1.IndexOffset)
	}

	// DINT at offset 1 (align 4) → padded to 4
	p2 := newDintPin(0)
	sym2 := st.RegisterAligned("s2", p2, 4)
	if sym2.IndexOffset != 4 {
		t.Errorf("s2 offset = %d, want 4 (3-byte padding after bool)", sym2.IndexOffset)
	}

	// BOOL at offset 8 (align 1) → offset 8
	p3 := newBoolPin(false)
	sym3 := st.RegisterAligned("s3", p3, 1)
	if sym3.IndexOffset != 8 {
		t.Errorf("s3 offset = %d, want 8", sym3.IndexOffset)
	}

	// WORD at offset 9 (align 2) → padded to 10
	p4 := &mockPin{typeName: "WORD", typeID: ADSTUInt16, size: 2, data: make([]byte, 2)}
	sym4 := st.RegisterAligned("s4", p4, 2)
	if sym4.IndexOffset != 10 {
		t.Errorf("s4 offset = %d, want 10 (1-byte padding)", sym4.IndexOffset)
	}
}

func TestBeginEndContainer(t *testing.T) {
	st := NewSymbolTable()

	// BOOL at 0
	st.RegisterAligned("bA", newBoolPin(false), 1) // offset 0, next=1
	st.RegisterAligned("bB", newBoolPin(false), 1) // offset 1, next=2

	// Enter struct with alignment 4 → pad to 4
	st.BeginContainer(4)
	if st.nextOffset != 4 {
		t.Errorf("after BeginContainer(4): nextOffset = %d, want 4", st.nextOffset)
	}

	st.RegisterAligned("nVal", newDintPin(0), 4)      // offset 4, next=8
	st.RegisterAligned("bFlag", newBoolPin(false), 1) // offset 8, next=9

	// Exit struct with alignment 4 → pad 9 to 12
	st.EndContainer(4)
	if st.nextOffset != 12 {
		t.Errorf("after EndContainer(4): nextOffset = %d, want 12", st.nextOffset)
	}

	// Next symbol starts at 12
	sym := st.RegisterAligned("bNext", newBoolPin(false), 1)
	if sym.IndexOffset != 12 {
		t.Errorf("bNext offset = %d, want 12", sym.IndexOffset)
	}
}

// TestGalvDisplayLayout verifies that the natural-alignment padding for the
// DISPLAY_DATA / ST_DISP_DATA / ST_DISP_POOL hierarchy matches the expected
// process-image offsets.  The sequence of BeginContainer / RegisterAligned /
// EndContainer calls mirrors what NewBridge produces when processing
// configs/galv-display.cfg via ParseConfigActions.
func TestGalvDisplayLayout(t *testing.T) {
	st := NewSymbolTable()
	mk := func(size uint32) *mockPin {
		return &mockPin{size: size, data: make([]byte, size)}
	}

	// DISPLAY_DATA container (align 4 = max of stData:4, stErrors:1)
	st.BeginContainer(4)

	// stData container (align 4 = max of DT:4, BOOLs:1, ST_DISP_POOL:4)
	st.BeginContainer(4)

	assertOffset := func(name string, sym *Symbol, want uint32) {
		t.Helper()
		if sym.IndexOffset != want {
			t.Errorf("%s: IndexOffset = %d, want %d", name, sym.IndexOffset, want)
		}
	}

	s := st.RegisterAligned("dtCurrentTime", mk(4), 4)
	assertOffset("dtCurrentTime", s, 0)

	s = st.RegisterAligned("bGlobalErr", mk(1), 1)
	assertOffset("bGlobalErr", s, 4)

	s = st.RegisterAligned("bAckErr", mk(1), 1)
	assertOffset("bAckErr", s, 5)

	// aPools[1]: ST_DISP_POOL (align 4 – has DWORD/REAL/TIME members)
	// BeginContainer should pad offset 6 → 8
	st.BeginContainer(4)
	if st.nextOffset != 8 {
		t.Errorf("aPools[1] start: nextOffset = %d, want 8 (2-byte padding)", st.nextOffset)
	}

	s = st.RegisterAligned("sPoolName", mk(32), 1) // STRING(31)
	assertOffset("sPoolName", s, 8)

	s = st.RegisterAligned("nFormulaId", mk(4), 4)
	assertOffset("nFormulaId", s, 40)

	s = st.RegisterAligned("sFormulaName", mk(32), 1) // STRING(31)
	assertOffset("sFormulaName", s, 44)

	s = st.RegisterAligned("eState", mk(2), 2) // WORD – already 2-aligned at 76
	assertOffset("eState", s, 76)

	s = st.RegisterAligned("fTemp", mk(4), 4) // REAL – pad 78 → 80
	assertOffset("fTemp", s, 80)

	// Skip to bPumpOnIdle (after fCurrent..fLeakPress + four TIME fields + four WORD fields + fTempSetpoint)
	// fCurrent(4) fVoltage(4) fTiltPos(4) fTiltVelo(4) fSectPos(4) fSectVelo(4) fLeakPress(4) = 7×4=28
	// tProcTimeTotal tProcTimeRem tPhaseTimeTotal tPhaseTimeRem = 4×4=16
	// nShiftCurr nShiftCount nRepeatCurr nRepeatCount = 4×2=8
	// fTempSetpoint = 4
	// All start at 84; total = 28+16+8+4 = 56 bytes → next = 84+56 = 140
	for _, size := range []uint32{4, 4, 4, 4, 4, 4, 4} { // fCurrent..fLeakPress
		st.RegisterAligned("_", mk(size), 4)
	}
	for i := 0; i < 4; i++ { // four TIME fields
		st.RegisterAligned("_", mk(4), 4)
	}
	for i := 0; i < 4; i++ { // four WORD fields
		st.RegisterAligned("_", mk(2), 2)
	}
	st.RegisterAligned("fTempSetpoint", mk(4), 4)

	s = st.RegisterAligned("bPumpOnIdle", mk(1), 1)
	assertOffset("bPumpOnIdle", s, 140)

	// tMixerTimeManual: TIME (align 4) – pad 141 → 144 (3 bytes)
	s = st.RegisterAligned("tMixerTimeManual", mk(4), 4)
	assertOffset("tMixerTimeManual", s, 144)

	s = st.RegisterAligned("tMixerTimeAuto", mk(4), 4)
	assertOffset("tMixerTimeAuto", s, 148)

	// bManuEnable..bSectJogNeg: 7 BOOLs at 152..158
	for i := 0; i < 7; i++ {
		st.RegisterAligned("_", mk(1), 1)
	}
	// next = 159

	// stMsg container (align 2 = max of WORD:2, BOOLs:1)
	// BeginContainer should pad 159 → 160 (1 byte)
	st.BeginContainer(2)
	if st.nextOffset != 160 {
		t.Errorf("stMsg start: nextOffset = %d, want 160 (1-byte padding after bSectJogNeg)", st.nextOffset)
	}

	s = st.RegisterAligned("stMsg.eType", mk(2), 2)
	assertOffset("stMsg.eType", s, 160)

	st.RegisterAligned("stMsg.bEnableOk", mk(1), 1)     // 162
	st.RegisterAligned("stMsg.bEnableCancel", mk(1), 1) // 163
	st.RegisterAligned("stMsg.bOk", mk(1), 1)           // 164
	st.RegisterAligned("stMsg.bCancel", mk(1), 1)       // 165

	// EndContainer(stMsg, align 2): pad 166 → 166 (already even)
	st.EndContainer(2)
	if st.nextOffset != 166 {
		t.Errorf("after stMsg EndContainer: nextOffset = %d, want 166", st.nextOffset)
	}

	// aMixers[1]: ST_DISP_MIXER (align 4 = max of REAL:4, BOOL:1)
	// BeginContainer should pad 166 → 168 (2 bytes)
	st.BeginContainer(4)
	if st.nextOffset != 168 {
		t.Errorf("aMixers[1] start: nextOffset = %d, want 168 (2-byte padding after stMsg)", st.nextOffset)
	}

	s = st.RegisterAligned("aMixers[1].fPower", mk(4), 4)
	assertOffset("aMixers[1].fPower", s, 168)

	st.RegisterAligned("aMixers[1].bManu", mk(1), 1) // 172, next=173

	// EndContainer(aMixers[1], align 4): pad 173 → 176 (3 bytes)
	st.EndContainer(4)
	if st.nextOffset != 176 {
		t.Errorf("after aMixers[1] EndContainer: nextOffset = %d, want 176 (3-byte end padding)", st.nextOffset)
	}

	// aMixers[2..4]: each element is 8 bytes (BeginContainer+fPower+bManu+EndContainer)
	for i := uint32(2); i <= 4; i++ {
		base := 168 + (i-1)*8
		st.BeginContainer(4)
		s = st.RegisterAligned("fPower", mk(4), 4)
		if s.IndexOffset != base {
			t.Errorf("aMixers[%d].fPower offset = %d, want %d", i, s.IndexOffset, base)
		}
		st.RegisterAligned("bManu", mk(1), 1)
		st.EndContainer(4)
	}
	// After aMixers[4]: nextOffset = 168 + 4*8 = 200

	// EndContainer(aPools[1], align 4): 200 already aligned
	st.EndContainer(4)
	if st.nextOffset != 200 {
		t.Errorf("after aPools[1] EndContainer: nextOffset = %d, want 200", st.nextOffset)
	}

	// EndContainer(stData, align 4): still 200
	st.EndContainer(4)

	// stErrors container (align 1 – all BOOL members)
	st.BeginContainer(1)
	if st.nextOffset != 200 {
		t.Errorf("stErrors start: nextOffset = %d, want 200", st.nextOffset)
	}

	// stGlobalErrors (align 1, 4 BOOLs)
	st.BeginContainer(1)
	s = st.RegisterAligned("bEmergStop", mk(1), 1)
	assertOffset("bEmergStop", s, 200)
	st.RegisterAligned("bDriveSupplyErr", mk(1), 1)
	st.RegisterAligned("bTempWarn", mk(1), 1)
	st.RegisterAligned("bTempErr", mk(1), 1)
	st.EndContainer(1)
	if st.nextOffset != 204 {
		t.Errorf("after stGlobalErrors: nextOffset = %d, want 204", st.nextOffset)
	}

	// aPoolErrors[1] (align 1, 14 BOOLs + 4×aMixerErrors)
	st.BeginContainer(1)
	s = st.RegisterAligned("bHeaterTempWarn", mk(1), 1)
	assertOffset("bHeaterTempWarn", s, 204)

	// 13 more pool-error BOOLs
	for i := 0; i < 13; i++ {
		st.RegisterAligned("_", mk(1), 1)
	}
	// nextOffset = 204 + 14 = 218

	// aMixerErrors[1..4]: each 5 BOOLs (align 1, no end-padding)
	s = st.RegisterAligned("_dummy_begin_check", mk(0), 1) // peek at offset
	mixErrBase := s.IndexOffset
	if mixErrBase != 218 {
		t.Errorf("aMixerErrors[1] start: nextOffset = %d, want 218", mixErrBase)
	}
	// undo the dummy registration by just continuing (offset was 218, size 0 → still 218)

	for i := uint32(1); i <= 4; i++ {
		base := 218 + (i-1)*5
		st.BeginContainer(1)
		s = st.RegisterAligned("bDriveWarn", mk(1), 1)
		if s.IndexOffset != base {
			t.Errorf("aMixerErrors[%d].bDriveWarn offset = %d, want %d", i, s.IndexOffset, base)
		}
		st.RegisterAligned("bDriveErr", mk(1), 1)
		st.RegisterAligned("bOverloadErr", mk(1), 1)
		st.RegisterAligned("bUnderloadWarn", mk(1), 1)
		st.RegisterAligned("bVeloErr", mk(1), 1)
		st.EndContainer(1) // no end-padding (align 1)
	}

	st.EndContainer(1) // aPoolErrors[1]
	st.EndContainer(1) // stErrors
	st.EndContainer(4) // DISPLAY_DATA (align 4: pad 238 → 240)

	if st.nextOffset != 240 {
		t.Errorf("final process image size: nextOffset = %d, want 240", st.nextOffset)
	}
}

func TestFindSymbolWithFallbackDisplayData(t *testing.T) {
	st := NewSymbolTable()
	st.Register("DISPLAY_DATA.stData.bGlobalErr", newBoolPin(true))

	// TwinCAT HMI may query with "DISPLAY_DATA." prefix; it should be stripped.
	handle, errCode := st.CreateHandle("DISPLAY_DATA.stData.bGlobalErr")
	if errCode != ErrNoError {
		t.Fatalf("exact match failed: 0x%X", errCode)
	}
	if st.GetByHandle(handle) == nil {
		t.Error("exact match: handle should resolve")
	}

	// display_data. prefix strip (lowercase)
	handle2, errCode2 := st.CreateHandle("display_data.stData.bGlobalErr")
	if errCode2 != ErrNoError {
		t.Fatalf("display_data prefix strip failed: 0x%X", errCode2)
	}
	if st.GetByHandle(handle2) == nil {
		t.Error("display_data prefix strip: handle should resolve")
	}
}
