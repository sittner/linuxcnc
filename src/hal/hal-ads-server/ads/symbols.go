package ads

import (
	"encoding/binary"
	"strings"
	"sync"
)

// BufferIO is implemented by the Bridge. The SymbolTable calls it for actual
// data reads and writes to/from the process image buffer.
type BufferIO interface {
	// ReadBuffer syncs HAL pins in the requested range to the buffer and
	// returns the buffer slice [offset, offset+length). The returned slice
	// is valid until the next call to ReadBuffer or WriteBuffer.
	ReadBuffer(offset, length uint32) ([]byte, error)
	// WriteBuffer copies data into the buffer starting at offset, then syncs
	// all HAL pins whose byte range intersects [offset, offset+len(data)).
	WriteBuffer(offset uint32, data []byte) error
}

// Symbol represents an ADS symbol mapped to a region of the process image.
type Symbol struct {
	// Name is the full ADS symbol name, e.g. "stDISPLAY_DATA.stPOOL[1].bReady".
	Name string
	// IndexGroup is the IndexGroup used for direct process-image access (IdxGrpProcessImageRW).
	IndexGroup uint32
	// IndexOffset is the byte offset within the process image for direct access.
	IndexOffset uint32
	// Size is the ADS wire size in bytes.
	Size uint32
	// TypeName is the ADS/TwinCAT type name, e.g. "BOOL", "DINT"; last path
	// segment for group symbols.
	TypeName string
	// TypeID is the ADST constant; 0 for group symbols.
	TypeID uint32
	// isGroup is true for auto-created ancestor path symbols.
	isGroup bool
}

// IsGroup returns true if this symbol was auto-created as a container/group
// symbol (i.e. it represents a struct path prefix, not a leaf HAL pin).
func (s *Symbol) IsGroup() bool { return s.isGroup }

// SymbolTable manages ADS symbols and their handle assignments.
type SymbolTable struct {
	mu          sync.RWMutex
	byName      map[string]*Symbol
	handles     map[uint32]*Symbol // handle → symbol
	nextHandle  uint32
	nextOffset  uint32 // next available byte offset in process image
	symbolOrder []*Symbol
	bufIO       BufferIO // set by Bridge via SetBufferIO
}

// NewSymbolTable creates an empty SymbolTable.
func NewSymbolTable() *SymbolTable {
	return &SymbolTable{
		byName:     make(map[string]*Symbol),
		handles:    make(map[uint32]*Symbol),
		nextHandle: 1,
		nextOffset: 0,
	}
}

// SetBufferIO registers the BufferIO implementation (provided by Bridge) that
// the SymbolTable will use for all process-image reads and writes.
func (st *SymbolTable) SetBufferIO(b BufferIO) {
	st.mu.Lock()
	defer st.mu.Unlock()
	st.bufIO = b
}

// parentPrefixes returns all non-leaf path prefixes for a dotted name.
// E.g. "A.B.C" → ["A", "A.B"]. Single-segment names return nil.
func parentPrefixes(name string) []string {
	segs := strings.Split(name, ".")
	if len(segs) <= 1 {
		return nil
	}
	prefixes := make([]string, len(segs)-1)
	for i := 1; i < len(segs); i++ {
		prefixes[i-1] = strings.Join(segs[:i], ".")
	}
	return prefixes
}

// registerSymbolLocked adds sym to byName and symbolOrder, and auto-creates or
// updates group symbols for every ancestor path prefix.
// Must be called with st.mu held.
func (st *SymbolTable) registerSymbolLocked(sym *Symbol) {
	st.byName[sym.Name] = sym
	st.symbolOrder = append(st.symbolOrder, sym)

	// Auto-create or update group symbols for every ancestor prefix.
	symEnd := sym.IndexOffset + sym.Size
	for _, prefix := range parentPrefixes(sym.Name) {
		if existing, ok := st.byName[prefix]; ok && existing.isGroup {
			// Update span: grow Size if this child extends beyond current end.
			curEnd := existing.IndexOffset + existing.Size
			if symEnd > curEnd {
				existing.Size = symEnd - existing.IndexOffset
			}
		} else if !ok {
			segs := strings.Split(prefix, ".")
			st.byName[prefix] = &Symbol{
				Name:        prefix,
				IndexGroup:  IdxGrpProcessImageRW,
				IndexOffset: sym.IndexOffset,
				Size:        sym.Size,
				TypeName:    segs[len(segs)-1],
				TypeID:      0,
				isGroup:     true,
			}
		}
	}
}

// Register adds a symbol to the table. The symbol's IndexGroup is set to
// IdxGrpProcessImageRW and IndexOffset is assigned automatically.
// For each parent path prefix (e.g. "A.B" for leaf "A.B.C"), a group symbol
// is automatically created or updated so that SymbolInfoByName and
// CreateHandle work for intermediate struct paths.
func (st *SymbolTable) Register(name string, size uint32, typeName string, typeID uint32) *Symbol {
	st.mu.Lock()
	defer st.mu.Unlock()

	sym := &Symbol{
		Name:        name,
		IndexGroup:  IdxGrpProcessImageRW,
		IndexOffset: st.nextOffset,
		Size:        size,
		TypeName:    typeName,
		TypeID:      typeID,
	}
	st.nextOffset += size
	st.registerSymbolLocked(sym)
	return sym
}

// RegisterAt adds a symbol at the given explicit byte offset. The symbol's
// IndexGroup is set to IdxGrpProcessImageRW. nextOffset is updated if
// offset+size exceeds the current maximum. Group symbols for ancestor path
// prefixes are created or updated just like Register.
func (st *SymbolTable) RegisterAt(name string, offset, size uint32, typeName string, typeID uint32) *Symbol {
	st.mu.Lock()
	defer st.mu.Unlock()

	sym := &Symbol{
		Name:        name,
		IndexGroup:  IdxGrpProcessImageRW,
		IndexOffset: offset,
		Size:        size,
		TypeName:    typeName,
		TypeID:      typeID,
	}
	if end := offset + size; end > st.nextOffset {
		st.nextOffset = end
	}
	st.registerSymbolLocked(sym)
	return sym
}

// RegisterPadAt reserves space in the process image at the given offset
// without creating any named symbol. The reserved bytes appear as zeros in
// process-image range reads (handled by the zero-initialised buffer).
// It does NOT appear in byName or symbolOrder, so it is invisible to
// ADS symbol-list and symbol-info responses.
func (st *SymbolTable) RegisterPadAt(offset uint32, size uint32) {
	st.mu.Lock()
	defer st.mu.Unlock()

	if end := offset + size; end > st.nextOffset {
		st.nextOffset = end
	}
}

// SetGroupSize sets an explicit (tail-padding-inclusive) size on the named
// group symbol. This overrides the default computed span so that TwinCAT
// clients receive the full aligned struct size when reading the group.
func (st *SymbolTable) SetGroupSize(name string, size uint32) {
	st.mu.Lock()
	defer st.mu.Unlock()
	if sym := st.byName[name]; sym != nil && sym.isGroup {
		sym.Size = size
	}
}

// GetByName returns the symbol with the given name, or nil if not found.
func (st *SymbolTable) GetByName(name string) *Symbol {
	st.mu.RLock()
	defer st.mu.RUnlock()
	return st.byName[name]
}

// GetByHandle returns the symbol associated with a handle, or nil.
func (st *SymbolTable) GetByHandle(handle uint32) *Symbol {
	st.mu.RLock()
	defer st.mu.RUnlock()
	return st.handles[handle]
}

// CreateHandle allocates a new handle for the named symbol.
// Returns the handle and ErrNoSymbol if the name is not found.
// Name lookup uses findSymbolWithFallback for prefix/case-insensitive matching.
func (st *SymbolTable) CreateHandle(name string) (uint32, uint32) {
	st.mu.Lock()
	defer st.mu.Unlock()

	sym := st.findSymbolWithFallback(name)
	if sym == nil {
		return 0, ErrNoSymbol
	}
	h := st.nextHandle
	st.nextHandle++
	st.handles[h] = sym
	return h, ErrNoError
}

// ReleaseHandle releases a previously allocated handle.
func (st *SymbolTable) ReleaseHandle(handle uint32) uint32 {
	st.mu.Lock()
	defer st.mu.Unlock()

	if _, ok := st.handles[handle]; !ok {
		return ErrClientInvalidHdl
	}
	delete(st.handles, handle)
	return ErrNoError
}

// ReadData services an ADS Read request.
// Returns the response data and an ADS error code.
func (st *SymbolTable) ReadData(indexGroup, indexOffset, length uint32) ([]byte, uint32) {
	switch indexGroup {
	case IdxGrpSymbolValueByHandle:
		sym := st.GetByHandle(indexOffset)
		if sym == nil {
			return nil, ErrClientInvalidHdl
		}
		return st.readSymbol(sym, length)

	case IdxGrpProcessImageRW:
		return st.readProcessImageRange(indexOffset, length)

	case IdxGrpSymbolVersion:
		buf := make([]byte, 4)
		binary.LittleEndian.PutUint32(buf, 1)
		return buf, ErrNoError

	case IdxGrpSymbolCount:
		st.mu.RLock()
		count := uint32(len(st.symbolOrder))
		st.mu.RUnlock()
		buf := make([]byte, 4)
		binary.LittleEndian.PutUint32(buf, count)
		return buf, ErrNoError

	case IdxGrpSymbolListInfo:
		return st.buildSymbolListInfo()

	case IdxGrpSymbolListUpload:
		return st.buildSymbolList()

	default:
		return nil, ErrNoSymbol
	}
}

// WriteData services an ADS Write request.
// Returns an ADS error code.
func (st *SymbolTable) WriteData(indexGroup, indexOffset uint32, data []byte) uint32 {
	switch indexGroup {
	case IdxGrpSymbolValueByHandle:
		sym := st.GetByHandle(indexOffset)
		if sym == nil {
			return ErrClientInvalidHdl
		}
		return st.writeSymbol(sym, data)

	case IdxGrpProcessImageRW:
		return st.writeProcessImageRange(indexOffset, data)

	case IdxGrpReleaseHandle:
		if len(data) < 4 {
			return ErrInternal
		}
		handle := binary.LittleEndian.Uint32(data[0:4])
		return st.ReleaseHandle(handle)

	default:
		return ErrNoSymbol
	}
}

// ReadWriteData services an ADS ReadWrite request.
// Returns the response data and an ADS error code.
func (st *SymbolTable) ReadWriteData(indexGroup, indexOffset, readLen uint32, writeData []byte) ([]byte, uint32) {
	switch indexGroup {
	case IdxGrpSymbolHandleByName:
		// writeData contains the symbol name (null-terminated); response is the 4-byte handle.
		name := strings.TrimRight(string(writeData), "\x00")
		handle, errCode := st.CreateHandle(name)
		if errCode != ErrNoError {
			return nil, errCode
		}
		buf := make([]byte, 4)
		binary.LittleEndian.PutUint32(buf, handle)
		return buf, ErrNoError

	case IdxGrpSymbolInfoByName:
		name := strings.TrimRight(string(writeData), "\x00")
		st.mu.RLock()
		sym := st.findSymbolWithFallback(name)
		st.mu.RUnlock()
		if sym == nil {
			return nil, ErrNoSymbol
		}
		// Compact response: client only wants IndexGroup + IndexOffset + Size (12 bytes).
		if readLen == 12 {
			buf := make([]byte, 12)
			binary.LittleEndian.PutUint32(buf[0:4], sym.IndexGroup)
			binary.LittleEndian.PutUint32(buf[4:8], sym.IndexOffset)
			binary.LittleEndian.PutUint32(buf[8:12], sym.Size)
			return buf, ErrNoError
		}
		// Full symbol info response.
		return buildSymbolInfo(sym), ErrNoError

	case IdxGrpSumRead:
		// indexOffset = number of read sub-requests.
		// writeData = N × 12 bytes: IndexGroup(4) + IndexOffset(4) + Length(4).
		// Response = N × 4-byte error codes, then concatenated data for successful reads.
		numReads := indexOffset
		type readResult struct {
			errCode uint32
			data    []byte
		}
		results := make([]readResult, numReads)
		for i := uint32(0); i < numReads; i++ {
			off := i * 12
			if off+12 > uint32(len(writeData)) {
				results[i] = readResult{errCode: ErrInternal}
				continue
			}
			ig := binary.LittleEndian.Uint32(writeData[off:])
			io := binary.LittleEndian.Uint32(writeData[off+4:])
			ln := binary.LittleEndian.Uint32(writeData[off+8:])
			data, ec := st.ReadData(ig, io, ln)
			results[i] = readResult{errCode: ec, data: data}
		}
		// Build response: all error codes first, then all data payloads.
		totalLen := numReads * 4
		for _, r := range results {
			if r.errCode == ErrNoError {
				totalLen += uint32(len(r.data))
			}
		}
		resp := make([]byte, totalLen)
		pos := 0
		for _, r := range results {
			binary.LittleEndian.PutUint32(resp[pos:], r.errCode)
			pos += 4
		}
		for _, r := range results {
			if r.errCode == ErrNoError && len(r.data) > 0 {
				copy(resp[pos:], r.data)
				pos += len(r.data)
			}
		}
		return resp, ErrNoError

	default:
		return nil, ErrNoSymbol
	}
}

// readProcessImageRange reads a contiguous range of the process image via bufIO.
func (st *SymbolTable) readProcessImageRange(offset, length uint32) ([]byte, uint32) {
	st.mu.RLock()
	bufIO := st.bufIO
	nextOff := st.nextOffset
	st.mu.RUnlock()

	if offset+length > nextOff {
		return nil, ErrInvalidOffset
	}
	if bufIO == nil {
		// No buffer registered (test-only path): return zero bytes.
		return make([]byte, length), ErrNoError
	}
	data, err := bufIO.ReadBuffer(offset, length)
	if err != nil {
		return nil, ErrInvalidOffset
	}
	// Return a copy so the caller owns the slice.
	out := make([]byte, len(data))
	copy(out, data)
	return out, ErrNoError
}

// writeProcessImageRange writes a contiguous range of the process image via bufIO.
func (st *SymbolTable) writeProcessImageRange(offset uint32, data []byte) uint32 {
	st.mu.RLock()
	bufIO := st.bufIO
	nextOff := st.nextOffset
	st.mu.RUnlock()

	if offset+uint32(len(data)) > nextOff {
		return ErrInvalidOffset
	}
	if bufIO == nil {
		// No buffer registered (test-only path): silently succeed.
		return ErrNoError
	}
	if err := bufIO.WriteBuffer(offset, data); err != nil {
		return ErrInternal
	}
	return ErrNoError
}

// readSymbol reads the value of sym from the buffer and returns ADS wire bytes.
func (st *SymbolTable) readSymbol(sym *Symbol, length uint32) ([]byte, uint32) {
	st.mu.RLock()
	bufIO := st.bufIO
	st.mu.RUnlock()

	if bufIO == nil {
		return make([]byte, sym.Size), ErrNoError
	}
	data, err := bufIO.ReadBuffer(sym.IndexOffset, sym.Size)
	if err != nil {
		return nil, ErrInternal
	}
	out := make([]byte, sym.Size)
	copy(out, data)
	if length > 0 && uint32(len(out)) > length {
		out = out[:length]
	}
	return out, ErrNoError
}

// writeSymbol writes ADS wire bytes to the buffer at sym's offset.
func (st *SymbolTable) writeSymbol(sym *Symbol, data []byte) uint32 {
	st.mu.RLock()
	bufIO := st.bufIO
	st.mu.RUnlock()

	if bufIO == nil {
		return ErrNoError
	}
	// Only write up to sym.Size bytes.
	payload := data
	if uint32(len(payload)) > sym.Size {
		payload = payload[:sym.Size]
	}
	if err := bufIO.WriteBuffer(sym.IndexOffset, payload); err != nil {
		return ErrInternal
	}
	return ErrNoError
}

// buildSymbolInfo encodes the ADS symbol info structure for a single symbol.
// Format matches TwinCAT AdsSymbolEntry:
//
// uint32 entryLength
// uint32 indexGroup
// uint32 indexOffset
// uint32 size
// uint32 dataType (ADST)
// uint32 flags (=0)
// uint16 nameLength (excl. null terminator)
// uint16 typeLength (excl. null terminator)
// uint16 commentLength (=0)
// [nameLength+1] bytes: name (null-terminated)
// [typeLength+1] bytes: type name (null-terminated)
// [1] byte: empty comment (null byte)
func buildSymbolInfo(sym *Symbol) []byte {
	nameBytes := []byte(sym.Name)
	typeBytes := []byte(sym.TypeName)
	// Fixed header: 6×uint32 (24 bytes) + 3×uint16 (6 bytes) = 30 bytes,
	// followed by null-terminated name, null-terminated type, and a null comment byte.
	entryLen := uint32(30 + len(nameBytes) + 1 + len(typeBytes) + 1 + 1)
	buf := make([]byte, entryLen)
	off := 0
	putUint32LE(buf, off, entryLen)
	off += 4
	putUint32LE(buf, off, sym.IndexGroup)
	off += 4
	putUint32LE(buf, off, sym.IndexOffset)
	off += 4
	putUint32LE(buf, off, sym.Size)
	off += 4
	putUint32LE(buf, off, sym.TypeID)
	off += 4
	putUint32LE(buf, off, 0) // flags
	off += 4
	putUint16LE(buf, off, uint16(len(nameBytes)))
	off += 2
	putUint16LE(buf, off, uint16(len(typeBytes)))
	off += 2
	putUint16LE(buf, off, 0) // comment length
	off += 2
	copy(buf[off:], nameBytes)
	off += len(nameBytes) + 1 // +1 for null terminator (already zero)
	copy(buf[off:], typeBytes)
	off += len(typeBytes) + 1
	_ = off // comment null byte is already zero
	return buf
}

// buildSymbolListInfo builds the response for IdxGrpSymbolListInfo.
// Returns: uint32 uploadLength, uint32 symbolCount.
func (st *SymbolTable) buildSymbolListInfo() ([]byte, uint32) {
	st.mu.RLock()
	defer st.mu.RUnlock()

	var uploadLen uint32
	for _, sym := range st.symbolOrder {
		info := buildSymbolInfo(sym)
		uploadLen += uint32(len(info))
	}
	buf := make([]byte, 8)
	binary.LittleEndian.PutUint32(buf[0:], uploadLen)
	binary.LittleEndian.PutUint32(buf[4:], uint32(len(st.symbolOrder)))
	return buf, ErrNoError
}

// buildSymbolList builds the full symbol list upload payload.
func (st *SymbolTable) buildSymbolList() ([]byte, uint32) {
	st.mu.RLock()
	defer st.mu.RUnlock()

	var result []byte
	for _, sym := range st.symbolOrder {
		result = append(result, buildSymbolInfo(sym)...)
	}
	return result, ErrNoError
}

// findSymbolWithFallback looks up a symbol by name, trying exact match first,
// then stripping common PLC namespace prefixes (case-insensitive), and finally
// a full case-insensitive scan of the symbol table.
// Must be called with st.mu held (read or write).
func (st *SymbolTable) findSymbolWithFallback(name string) *Symbol {
	// Exact match.
	if sym := st.byName[name]; sym != nil {
		return sym
	}
	// Strip common PLC namespace prefixes (match case-insensitively so "GVL.",
	// "gvl.", etc. all work).
	nameLower := strings.ToLower(name)
	for _, prefix := range []string{"gvl.", "main.", "plc."} {
		if strings.HasPrefix(nameLower, prefix) {
			stripped := name[len(prefix):]
			if sym := st.byName[stripped]; sym != nil {
				return sym
			}
		}
	}
	// Case-insensitive fallback.
	for symName, sym := range st.byName {
		if strings.ToLower(symName) == nameLower {
			return sym
		}
	}
	return nil
}
