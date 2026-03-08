package ads

import (
	"encoding/binary"
	"testing"
)

// mockBuffer implements BufferIO using a plain byte slice.
// It provides full control over buffer contents for tests.
type mockBuffer struct {
	buf []byte
}

func newMockBuffer(size int) *mockBuffer {
	return &mockBuffer{buf: make([]byte, size)}
}

func (m *mockBuffer) ReadBuffer(offset, length uint32) ([]byte, error) {
	return m.buf[offset : offset+length], nil
}

func (m *mockBuffer) WriteBuffer(offset uint32, data []byte) error {
	copy(m.buf[offset:], data)
	return nil
}

// seedDint seeds a DINT value at the given offset in the mock buffer.
func (m *mockBuffer) seedDint(offset uint32, val int32) {
	binary.LittleEndian.PutUint32(m.buf[offset:], uint32(val))
}

// seedBool seeds a BOOL value at the given offset in the mock buffer.
func (m *mockBuffer) seedBool(offset uint32, val bool) {
	if val {
		m.buf[offset] = 1
	} else {
		m.buf[offset] = 0
	}
}

func TestSymbolTableRegisterAndGetByName(t *testing.T) {
	mb := newMockBuffer(5)
	st := NewSymbolTable()
	st.SetBufferIO(mb)

	sym := st.Register("stFoo.bReady", 1, "BOOL", ADSTBool)

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
	sym2 := st.Register("stFoo.nVal", 4, "DINT", ADSTInt32)
	if sym2.IndexOffset != 1 { // 1 byte for the bool
		t.Errorf("second symbol offset = %d, want 1", sym2.IndexOffset)
	}
}

func TestSymbolTableHandleLifecycle(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stX.bFlag", 1, "BOOL", ADSTBool)

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
	mb := newMockBuffer(4)
	mb.seedDint(0, 12345)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stA.nVal", 4, "DINT", ADSTInt32)

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
	mb := newMockBuffer(1)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stA.bFlag", 1, "BOOL", ADSTBool)

	handle, _ := st.CreateHandle("stA.bFlag")

	errCode := st.WriteData(IdxGrpSymbolValueByHandle, handle, []byte{1})
	if errCode != ErrNoError {
		t.Fatalf("WriteData error: 0x%X", errCode)
	}
	if mb.buf[0] != 1 {
		t.Errorf("buffer value after write = %d, want 1", mb.buf[0])
	}
}

func TestSymbolTableHandleByName(t *testing.T) {
	mb := newMockBuffer(4)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stB.nCount", 4, "DINT", ADSTInt32)

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
	st.Register("stC.bX", 1, "BOOL", ADSTBool)

	handle, _ := st.CreateHandle("stC.bX")

	// Release via WriteData with IdxGrpReleaseHandle: handle is in the 4-byte write data payload.
	handleBytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(handleBytes, handle)
	errCode := st.WriteData(IdxGrpReleaseHandle, 0, handleBytes)
	if errCode != ErrNoError {
		t.Fatalf("release handle via WriteData error: 0x%X", errCode)
	}
	if st.GetByHandle(handle) != nil {
		t.Error("handle should be nil after release")
	}

	// Short data payload should return an error.
	if errCode := st.WriteData(IdxGrpReleaseHandle, 0, []byte{0x01, 0x00}); errCode == ErrNoError {
		t.Error("expected error for short data payload, got ErrNoError")
	}
}

func TestSymbolTableSymbolCount(t *testing.T) {
	st := NewSymbolTable()
	st.Register("s1", 1, "BOOL", ADSTBool)
	st.Register("s2", 4, "DINT", ADSTInt32)

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
	mb := newMockBuffer(5)
	mb.seedBool(0, true)
	mb.seedDint(1, 99)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	sym1 := st.Register("s1", 1, "BOOL", ADSTBool)  // offset 0, size 1
	sym2 := st.Register("s2", 4, "DINT", ADSTInt32) // offset 1, size 4

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
	if int32(binary.LittleEndian.Uint32(mb.buf[1:5])) != -1 {
		t.Errorf("s2 value = %d, want -1", int32(binary.LittleEndian.Uint32(mb.buf[1:5])))
	}
}

func TestSymbolTableNullTerminatedName(t *testing.T) {
	mb := newMockBuffer(1)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stFoo.bFlag", 1, "BOOL", ADSTBool)

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
	mb := newMockBuffer(4)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	sym := st.Register("stFoo.nVal", 4, "DINT", ADSTInt32)

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
	mb := newMockBuffer(5)
	mb.seedBool(0, true)
	mb.seedDint(1, 42)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	sym1 := st.Register("s1", 1, "BOOL", ADSTBool)  // offset 0, size 1
	sym2 := st.Register("s2", 4, "DINT", ADSTInt32) // offset 1, size 4

	// Build a SumRead request for both symbols.
	writeData := make([]byte, 24) // 2 × 12 bytes
	binary.LittleEndian.PutUint32(writeData[0:], sym1.IndexGroup)
	binary.LittleEndian.PutUint32(writeData[4:], sym1.IndexOffset)
	binary.LittleEndian.PutUint32(writeData[8:], sym1.Size)
	binary.LittleEndian.PutUint32(writeData[12:], sym2.IndexGroup)
	binary.LittleEndian.PutUint32(writeData[16:], sym2.IndexOffset)
	binary.LittleEndian.PutUint32(writeData[20:], sym2.Size)

	resp, errCode := st.ReadWriteData(IdxGrpSumRead, 2, 0, writeData)
	if errCode != ErrNoError {
		t.Fatalf("SumRead error: 0x%X", errCode)
	}
	// Response: 2×4 error codes + 1 byte (bool) + 4 bytes (dint) = 13 bytes.
	if len(resp) != 13 {
		t.Fatalf("SumRead response length = %d, want 13", len(resp))
	}
	if binary.LittleEndian.Uint32(resp[0:4]) != ErrNoError {
		t.Errorf("SumRead s1 errCode = 0x%X", binary.LittleEndian.Uint32(resp[0:4]))
	}
	if binary.LittleEndian.Uint32(resp[4:8]) != ErrNoError {
		t.Errorf("SumRead s2 errCode = 0x%X", binary.LittleEndian.Uint32(resp[4:8]))
	}
	// s1 value: byte 8.
	if resp[8] != 1 {
		t.Errorf("SumRead s1 value = %d, want 1", resp[8])
	}
	// s2 value: bytes 9–12.
	if int32(binary.LittleEndian.Uint32(resp[9:13])) != 42 {
		t.Errorf("SumRead s2 value = %d, want 42", int32(binary.LittleEndian.Uint32(resp[9:13])))
	}
}

func TestSymbolTableFallbackMatching(t *testing.T) {
	mb := newMockBuffer(1)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stFoo.bFlag", 1, "BOOL", ADSTBool)

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

// TestGroupSymbolAutoCreate verifies that registering a leaf creates group
// symbols for every ancestor path prefix.
func TestGroupSymbolAutoCreate(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32)
	st.Register("stFoo.stBar.bFlag", 1, "BOOL", ADSTBool)

	for _, prefix := range []string{"stFoo", "stFoo.stBar"} {
		sym := st.GetByName(prefix)
		if sym == nil {
			t.Errorf("group symbol %q not created", prefix)
			continue
		}
		if sym.IndexGroup != IdxGrpProcessImageRW {
			t.Errorf("%q IndexGroup = 0x%X, want 0x%X", prefix, sym.IndexGroup, IdxGrpProcessImageRW)
		}
		if !sym.IsGroup() {
			t.Errorf("%q IsGroup() = false, want true", prefix)
		}
	}
}

// TestGroupSymbolInfoByName verifies SymbolInfoByName (0xF007) for a group symbol.
func TestGroupSymbolInfoByName(t *testing.T) {
	mb := newMockBuffer(5)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	nVal := st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32) // offset 0, size 4
	st.Register("stFoo.stBar.bFlag", 1, "BOOL", ADSTBool)         // offset 4, size 1

	data, errCode := st.ReadWriteData(IdxGrpSymbolInfoByName, 0, 12, []byte("stFoo.stBar"))
	if errCode != ErrNoError {
		t.Fatalf("SymbolInfoByName for group error: 0x%X", errCode)
	}
	if len(data) != 12 {
		t.Fatalf("expected 12 bytes, got %d", len(data))
	}
	ig := binary.LittleEndian.Uint32(data[0:4])
	io := binary.LittleEndian.Uint32(data[4:8])
	size := binary.LittleEndian.Uint32(data[8:12])
	if ig != IdxGrpProcessImageRW {
		t.Errorf("IndexGroup = 0x%X, want 0x%X", ig, IdxGrpProcessImageRW)
	}
	if io != nVal.IndexOffset {
		t.Errorf("IndexOffset = %d, want %d", io, nVal.IndexOffset)
	}
	if size != 5 { // 4 (DINT) + 1 (BOOL)
		t.Errorf("Size = %d, want 5", size)
	}
}

// TestGroupSymbolCreateHandle verifies CreateHandle succeeds for a group symbol.
func TestGroupSymbolCreateHandle(t *testing.T) {
	mb := newMockBuffer(5)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	mb.seedDint(0, 7)
	mb.seedBool(4, true)
	st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32)
	st.Register("stFoo.stBar.bFlag", 1, "BOOL", ADSTBool)

	handle, errCode := st.CreateHandle("stFoo.stBar")
	if errCode != ErrNoError {
		t.Fatalf("CreateHandle for group error: 0x%X", errCode)
	}
	if handle == 0 {
		t.Error("group handle should not be 0")
	}
	sym := st.GetByHandle(handle)
	if sym == nil || sym.Name != "stFoo.stBar" {
		t.Errorf("GetByHandle returned %v", sym)
	}
}

// TestGroupSymbolReadByHandle verifies that reading a group handle returns
// the concatenated child bytes in offset order.
func TestGroupSymbolReadByHandle(t *testing.T) {
	mb := newMockBuffer(5)
	mb.seedDint(0, 42)
	mb.seedBool(4, true)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32) // offset 0, size 4
	st.Register("stFoo.stBar.bFlag", 1, "BOOL", ADSTBool) // offset 4, size 1

	handle, _ := st.CreateHandle("stFoo.stBar")

	data, errCode := st.ReadData(IdxGrpSymbolValueByHandle, handle, 5)
	if errCode != ErrNoError {
		t.Fatalf("ReadData group handle error: 0x%X", errCode)
	}
	if len(data) != 5 {
		t.Fatalf("group read length = %d, want 5", len(data))
	}
	if int32(binary.LittleEndian.Uint32(data[0:4])) != 42 {
		t.Errorf("nVal in group = %d, want 42", int32(binary.LittleEndian.Uint32(data[0:4])))
	}
	if data[4] != 1 {
		t.Errorf("bFlag in group = %d, want 1", data[4])
	}
}

// TestGroupSymbolWriteByHandle verifies that writing a group handle distributes
// the bytes to the correct child leaf symbols.
func TestGroupSymbolWriteByHandle(t *testing.T) {
	mb := newMockBuffer(5)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32) // offset 0, size 4
	st.Register("stFoo.stBar.bFlag", 1, "BOOL", ADSTBool) // offset 4, size 1

	handle, _ := st.CreateHandle("stFoo.stBar")

	// Write [99, 0, 0, 0, 1] → nVal=99, bFlag=true.
	payload := make([]byte, 5)
	binary.LittleEndian.PutUint32(payload[0:], 99)
	payload[4] = 1

	errCode := st.WriteData(IdxGrpSymbolValueByHandle, handle, payload)
	if errCode != ErrNoError {
		t.Fatalf("WriteData group handle error: 0x%X", errCode)
	}
	if int32(binary.LittleEndian.Uint32(mb.buf[0:4])) != 99 {
		t.Errorf("nVal after group write = %d, want 99", int32(binary.LittleEndian.Uint32(mb.buf[0:4])))
	}
	if mb.buf[4] != 1 {
		t.Errorf("bFlag after group write = %d, want 1", mb.buf[4])
	}
}

// TestGroupSymbolArrayBracketNotation verifies that bracket notation in path
// segments (e.g. "aPools[1]") creates the correct group symbols.
func TestGroupSymbolArrayBracketNotation(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stData.aPools[1].stMsg.eType", 4, "DINT", ADSTInt32)

	for _, prefix := range []string{"stData", "stData.aPools[1]", "stData.aPools[1].stMsg"} {
		if sym := st.GetByName(prefix); sym == nil {
			t.Errorf("expected group symbol %q to exist", prefix)
		}
	}
}

// TestGroupSymbolNotInSymbolCount verifies that group symbols are not counted
// in the SymbolCount response.
func TestGroupSymbolNotInSymbolCount(t *testing.T) {
	st := NewSymbolTable()
	st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32)
	st.Register("stFoo.stBar.bFlag", 1, "BOOL", ADSTBool)

	data, errCode := st.ReadData(IdxGrpSymbolCount, 0, 4)
	if errCode != ErrNoError {
		t.Fatalf("SymbolCount error: 0x%X", errCode)
	}
	count := binary.LittleEndian.Uint32(data)
	if count != 2 {
		t.Errorf("SymbolCount = %d, want 2 (group symbols must not be counted)", count)
	}
}

// TestProcessImageRangeRead verifies that a ProcessImageRW bulk read returns
// all symbols whose offsets fall within the requested range.
func TestProcessImageRangeRead(t *testing.T) {
	mb := newMockBuffer(9)
	mb.seedBool(0, true)
	binary.LittleEndian.PutUint32(mb.buf[1:], 0x12345678)
	binary.LittleEndian.PutUint32(mb.buf[5:], 0xFFFFFFFF)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("s1", 1, "BOOL", ADSTBool)  // offset 0, size 1
	st.Register("s2", 4, "DINT", ADSTInt32) // offset 1, size 4
	st.Register("s3", 4, "DINT", ADSTInt32) // offset 5, size 4

	// Range read covering all 3 symbols (offset=0, length=9).
	data, errCode := st.ReadData(IdxGrpProcessImageRW, 0, 9)
	if errCode != ErrNoError {
		t.Fatalf("range read error: 0x%X", errCode)
	}
	if len(data) != 9 {
		t.Fatalf("range read length = %d, want 9", len(data))
	}
	if data[0] != 1 {
		t.Errorf("s1 = %d, want 1", data[0])
	}
	if binary.LittleEndian.Uint32(data[1:5]) != 0x12345678 {
		t.Errorf("s2 = 0x%X, want 0x12345678", binary.LittleEndian.Uint32(data[1:5]))
	}
	if int32(binary.LittleEndian.Uint32(data[5:9])) != -1 {
		t.Errorf("s3 = %d, want -1", int32(binary.LittleEndian.Uint32(data[5:9])))
	}
}

// TestProcessImageRangeWrite verifies that a ProcessImageRW bulk write
// distributes bytes to all symbols whose offsets fall within the range.
func TestProcessImageRangeWrite(t *testing.T) {
	mb := newMockBuffer(9)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("s1", 1, "BOOL", ADSTBool)  // offset 0, size 1
	st.Register("s2", 4, "DINT", ADSTInt32) // offset 1, size 4
	st.Register("s3", 4, "DINT", ADSTInt32) // offset 5, size 4

	// Range write: 9-byte payload covering all 3 symbols.
	payload := make([]byte, 9)
	payload[0] = 1
	binary.LittleEndian.PutUint32(payload[1:], 0xABCD1234)
	binary.LittleEndian.PutUint32(payload[5:], 0xFFEE0099)
	errCode := st.WriteData(IdxGrpProcessImageRW, 0, payload)
	if errCode != ErrNoError {
		t.Fatalf("range write error: 0x%X", errCode)
	}
	if mb.buf[0] != 1 {
		t.Errorf("s1 after range write = %d, want 1", mb.buf[0])
	}
	if binary.LittleEndian.Uint32(mb.buf[1:5]) != 0xABCD1234 {
		t.Errorf("s2 after range write = 0x%X, want 0xABCD1234", binary.LittleEndian.Uint32(mb.buf[1:5]))
	}
	if binary.LittleEndian.Uint32(mb.buf[5:9]) != 0xFFEE0099 {
		t.Errorf("s3 after range write = 0x%X, want 0xFFEE0099", binary.LittleEndian.Uint32(mb.buf[5:9]))
	}
}

func TestGroupSymbolFallbackMatching(t *testing.T) {
	mb := newMockBuffer(4)
	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stFoo.stBar.nVal", 4, "DINT", ADSTInt32)

	// Case-insensitive lookup for the group.
	handle, errCode := st.CreateHandle("STFOO.STBAR")
	if errCode != ErrNoError {
		t.Fatalf("case-insensitive group CreateHandle error: 0x%X", errCode)
	}
	sym := st.GetByHandle(handle)
	if sym == nil || sym.Name != "stFoo.stBar" {
		t.Errorf("expected stFoo.stBar, got %v", sym)
	}
}

// TestPadOffsetAdvance verifies that registering two sequential symbols
// correctly assigns sequential offsets.
func TestPadOffsetAdvance(t *testing.T) {
	st := NewSymbolTable()
	sym1 := st.Register("stMsg._reserved1", 1, "BYTE", ADSTUInt8) // offset 0, size 1
	sym2 := st.Register("stMsg.eType", 4, "DINT", ADSTInt32)      // offset 1, size 4

	if sym1.IndexOffset != 0 {
		t.Errorf("sym1 offset = %d, want 0", sym1.IndexOffset)
	}
	if sym2.IndexOffset != 1 {
		t.Errorf("eType offset = %d, want 1", sym2.IndexOffset)
	}
}

// TestGroupWithPadding verifies that a group symbol containing pad-adjacent
// fields covers the correct byte range, and that zero bytes in the buffer
// appear as zeros in group reads.
func TestGroupWithPadding(t *testing.T) {
	// Buffer: [0, 0x04, 0x03, 0x02, 0x01, 0, 0]
	// offset 0: pad (1 byte) → 0
	// offset 1: nVal DINT (4 bytes) → 0x01020304
	// offset 5-6: pad (2 bytes) → 0
	mb := newMockBuffer(7)
	mb.seedDint(1, 0x01020304)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	st.Register("stMsg._reserved1", 1, "BYTE", ADSTUInt8) // offset 0, size 1
	st.Register("stMsg.nVal", 4, "DINT", ADSTInt32)       // offset 1, size 4
	st.Register("stMsg._align", 2, "WORD", ADSTUInt16)    // offset 5, size 2

	// The group symbol stMsg should cover offsets 0–6 (7 bytes total).
	groupSym := st.GetByName("stMsg")
	if groupSym == nil {
		t.Fatal("group symbol stMsg not created")
	}
	if groupSym.Size != 7 {
		t.Errorf("group Size = %d, want 7", groupSym.Size)
	}

	// Read the group: padding bytes should be zero, nVal bytes should be present.
	handle, errCode := st.CreateHandle("stMsg")
	if errCode != ErrNoError {
		t.Fatalf("CreateHandle error: 0x%X", errCode)
	}
	data, errCode := st.ReadData(IdxGrpSymbolValueByHandle, handle, 7)
	if errCode != ErrNoError {
		t.Fatalf("ReadData error: 0x%X", errCode)
	}
	if len(data) != 7 {
		t.Fatalf("ReadData length = %d, want 7", len(data))
	}
	// Byte 0: pad (_reserved1) → 0.
	if data[0] != 0 {
		t.Errorf("data[0] (pad) = %d, want 0", data[0])
	}
	// Bytes 1-4: nVal = 0x01020304 little-endian.
	gotVal := int32(binary.LittleEndian.Uint32(data[1:5]))
	if gotVal != 0x01020304 {
		t.Errorf("data[1:5] (nVal) = 0x%X, want 0x01020304", gotVal)
	}
	// Bytes 5-6: pad (_align) → 0.
	if data[5] != 0 || data[6] != 0 {
		t.Errorf("data[5:7] (pad) = %v, want [0 0]", data[5:7])
	}
}

// TestRegisterAt verifies that RegisterAt places a symbol at an explicit
// byte offset, updates nextOffset, and creates parent group symbols.
func TestRegisterAt(t *testing.T) {
	mb := newMockBuffer(9)
	mb.seedDint(4, 99)
	mb.seedBool(8, true)

	st := NewSymbolTable()
	st.SetBufferIO(mb)

	// Place a DINT at offset 4 (leaving a 4-byte gap at 0..3).
	sym := st.RegisterAt("stFoo.nVal", 4, 4, "DINT", ADSTInt32)

	if sym.IndexOffset != 4 {
		t.Errorf("IndexOffset = %d, want 4", sym.IndexOffset)
	}
	if sym.IndexGroup != IdxGrpProcessImageRW {
		t.Errorf("IndexGroup = 0x%X", sym.IndexGroup)
	}

	sym2 := st.RegisterAt("stFoo.bFlag", 8, 1, "BOOL", ADSTBool)
	if sym2.IndexOffset != 8 {
		t.Errorf("second symbol offset = %d, want 8", sym2.IndexOffset)
	}

	// Group symbol for "stFoo" should exist.
	grp := st.GetByName("stFoo")
	if grp == nil {
		t.Fatal("group symbol stFoo not created by RegisterAt")
	}
	if !grp.IsGroup() {
		t.Error("stFoo IsGroup() = false")
	}

	// GetByName should return the leaf symbol.
	got := st.GetByName("stFoo.nVal")
	if got == nil || got.IndexOffset != 4 {
		t.Errorf("GetByName(stFoo.nVal) = %v", got)
	}

	// Symbol should appear in symbolOrder (part of symbol list).
	data, errCode := st.ReadData(IdxGrpSymbolCount, 0, 4)
	if errCode != ErrNoError {
		t.Fatalf("SymbolCount error: 0x%X", errCode)
	}
	count := binary.LittleEndian.Uint32(data)
	if count != 2 {
		t.Errorf("SymbolCount = %d, want 2", count)
	}
}

// TestRegisterAtExplicitOffsetRead verifies that reading a symbol registered
// with RegisterAt returns the correct value.
func TestRegisterAtExplicitOffsetRead(t *testing.T) {
	mb := newMockBuffer(12)
	mb.seedDint(8, 42)

	st := NewSymbolTable()
	st.SetBufferIO(mb)
	sym := st.RegisterAt("stA.nVal", 8, 4, "DINT", ADSTInt32)

	handle, errCode := st.CreateHandle("stA.nVal")
	if errCode != ErrNoError {
		t.Fatalf("CreateHandle error: 0x%X", errCode)
	}

	data, errCode := st.ReadData(IdxGrpSymbolValueByHandle, handle, 4)
	if errCode != ErrNoError {
		t.Fatalf("ReadData error: 0x%X", errCode)
	}
	if int32(binary.LittleEndian.Uint32(data)) != 42 {
		t.Errorf("value = %d, want 42", int32(binary.LittleEndian.Uint32(data)))
	}

	// Process-image read at the explicit offset should also work.
	data2, errCode2 := st.ReadData(IdxGrpProcessImageRW, sym.IndexOffset, 4)
	if errCode2 != ErrNoError {
		t.Fatalf("process-image read error: 0x%X", errCode2)
	}
	if int32(binary.LittleEndian.Uint32(data2)) != 42 {
		t.Errorf("process-image value = %d, want 42", int32(binary.LittleEndian.Uint32(data2)))
	}
}

// TestRegisterPadAt verifies that RegisterPadAt:
//   - does NOT add the pad to byName
//   - does NOT add the pad to symbolOrder (not in symbol list/count)
//   - updates nextOffset
//   - buffer reads over the padded range return zero bytes
func TestRegisterPadAt(t *testing.T) {
	mb := newMockBuffer(3)
	mb.seedBool(0, true) // bFlag at offset 0

	st := NewSymbolTable()
	st.SetBufferIO(mb)

	// Register a real pin at offset 0 and a pad at offset 1.
	st.RegisterAt("stX.bFlag", 0, 1, "BOOL", ADSTBool)
	st.RegisterPadAt(1, 2) // 2-byte pad at offset 1

	// Pad must NOT appear in byName.
	if sym := st.GetByName("stX._pad0"); sym != nil {
		t.Error("pad symbol unexpectedly found in byName")
	}

	// Symbol count must NOT include the pad.
	data, errCode := st.ReadData(IdxGrpSymbolCount, 0, 4)
	if errCode != ErrNoError {
		t.Fatalf("SymbolCount error: 0x%X", errCode)
	}
	count := binary.LittleEndian.Uint32(data)
	if count != 1 {
		t.Errorf("SymbolCount = %d, want 1 (pad must not be counted)", count)
	}

	// CreateHandle for pad name must fail.
	_, hErrCode := st.CreateHandle("stX._pad")
	if hErrCode == ErrNoError {
		t.Error("CreateHandle for pad name should fail, got ErrNoError")
	}

	// Process-image range read covering [0..2] (bFlag + pad):
	// byte 0 = 1 (bFlag=true), bytes 1-2 = 0 (pad, zero in buffer).
	imgData, imgErr := st.ReadData(IdxGrpProcessImageRW, 0, 3)
	if imgErr != ErrNoError {
		t.Fatalf("process-image read error: 0x%X", imgErr)
	}
	if len(imgData) != 3 {
		t.Fatalf("process-image data length = %d, want 3", len(imgData))
	}
	if imgData[0] != 1 {
		t.Errorf("imgData[0] (bFlag) = %d, want 1", imgData[0])
	}
	if imgData[1] != 0 || imgData[2] != 0 {
		t.Errorf("imgData[1:3] (pad) = %v, want [0 0]", imgData[1:3])
	}
}

// TestSetGroupSizeTailPadding verifies that SetGroupSize overrides the computed
// span so that group reads return the full aligned struct size including tail
// padding. This is required so that TwinCAT clients reading a struct-level
// symbol get a correctly-sized buffer.
func TestSetGroupSizeTailPadding(t *testing.T) {
	// Buffer: DINT at 0 (size 4), BOOL at 4 (size 1), tail pad 5-7 (size 3) = 8 bytes.
	mb := newMockBuffer(8)
	mb.seedDint(0, 0x01020304)
	mb.seedBool(4, true)
	// bytes 5-7 are zero (tail padding, already 0 in make([]byte, 8))

	st := NewSymbolTable()
	st.SetBufferIO(mb)

	st.RegisterAt("stFoo.nVal", 0, 4, "DINT", ADSTInt32)
	st.RegisterAt("stFoo.bFlag", 4, 1, "BOOL", ADSTBool)

	grp := st.GetByName("stFoo")
	if grp == nil {
		t.Fatal("group symbol stFoo not found")
	}

	// Before SetGroupSize: Size == 5 (computed span, no tail padding).
	if got := grp.Size; got != 5 {
		t.Errorf("initial Size = %d, want 5", got)
	}

	// Set the aligned size (8 bytes, including 3 bytes of tail padding).
	st.SetGroupSize("stFoo", 8)

	if got := grp.Size; got != 8 {
		t.Errorf("after SetGroupSize(8), Size = %d, want 8", got)
	}

	// ReadBytes should return 8 bytes; tail padding bytes must be zero.
	handle, errCode := st.CreateHandle("stFoo")
	if errCode != ErrNoError {
		t.Fatalf("CreateHandle error: 0x%X", errCode)
	}
	data, errCode := st.ReadData(IdxGrpSymbolValueByHandle, handle, 8)
	if errCode != ErrNoError {
		t.Fatalf("ReadData error: 0x%X", errCode)
	}
	if len(data) != 8 {
		t.Fatalf("ReadData length = %d, want 8", len(data))
	}
	if int32(binary.LittleEndian.Uint32(data[0:4])) != 0x01020304 {
		t.Errorf("nVal = 0x%X, want 0x01020304", binary.LittleEndian.Uint32(data[0:4]))
	}
	if data[4] != 1 {
		t.Errorf("bFlag = %d, want 1", data[4])
	}
	// Tail padding bytes 5-7 should be zero.
	for i := 5; i < 8; i++ {
		if data[i] != 0 {
			t.Errorf("tail padding byte[%d] = %d, want 0", i, data[i])
		}
	}
}
