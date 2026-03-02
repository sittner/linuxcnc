package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"strings"
	"sync"

	"linuxcnc.org/hal"

	"linuxcnc.org/hal-ads-server/ads"
)

// typeInfo holds the ADS/TwinCAT type metadata for a single symbol.
type typeInfo struct {
	adsTypeName string // normalised ADS type name, e.g. "BOOL", "DINT", "STRING(32)"
	adstID      uint32 // ADST constant (see ads.ADST*)
	byteSize    uint32 // wire size in bytes
	strLen      int    // for STRING(n): n (chars); 0 for non-string types
	alignment   uint32 // natural alignment in bytes (1, 2, 4, or 8)
}

// parseTypeInfo converts a config type token (already upper-cased) to typeInfo.
func parseTypeInfo(typeName string) (typeInfo, error) {
	// Handle STRING(n) specially.
	if strings.HasPrefix(typeName, "STRING(") {
		var n int
		if _, err := fmt.Sscanf(typeName, "STRING(%d)", &n); err != nil || n <= 0 {
			return typeInfo{}, fmt.Errorf("invalid string type %q", typeName)
		}
		return typeInfo{
			adsTypeName: typeName,
			adstID:      ads.ADSTString,
			byteSize:    uint32(n + 1), // null terminator
			strLen:      n,
			alignment:   1,
		}, nil
	}

	switch typeName {
	case "BOOL":
		return typeInfo{adsTypeName: "BOOL", adstID: ads.ADSTBool, byteSize: 1, alignment: 1}, nil
	case "BYTE", "USINT":
		return typeInfo{adsTypeName: typeName, adstID: ads.ADSTUInt8, byteSize: 1, alignment: 1}, nil
	case "WORD", "UINT":
		return typeInfo{adsTypeName: typeName, adstID: ads.ADSTUInt16, byteSize: 2, alignment: 2}, nil
	case "DWORD", "UDINT", "TIME", "TOD", "DATE", "DT":
		return typeInfo{adsTypeName: typeName, adstID: ads.ADSTUInt32, byteSize: 4, alignment: 4}, nil
	case "SINT":
		return typeInfo{adsTypeName: "SINT", adstID: ads.ADSTInt8, byteSize: 1, alignment: 1}, nil
	case "INT":
		return typeInfo{adsTypeName: "INT", adstID: ads.ADSTInt16, byteSize: 2, alignment: 2}, nil
	case "DINT":
		return typeInfo{adsTypeName: "DINT", adstID: ads.ADSTInt32, byteSize: 4, alignment: 4}, nil
	case "REAL":
		return typeInfo{adsTypeName: "REAL", adstID: ads.ADSTReal32, byteSize: 4, alignment: 4}, nil
	case "LREAL":
		return typeInfo{adsTypeName: "LREAL", adstID: ads.ADSTReal64, byteSize: 8, alignment: 8}, nil
	default:
		return typeInfo{}, fmt.Errorf("unsupported ADS type %q", typeName)
	}
}

// halPinAccessor is the ads.PinAccessor implementation backed by a HAL pin.
// Since hal.Pin[T] is generic and Go does not allow interface variables to hold
// generic types directly, we use a closure-based approach: each accessor stores
// read and write functions that capture the typed pin.
type halPinAccessor struct {
	ti      typeInfo
	readFn  func() ([]byte, error)
	writeFn func([]byte) error
}

func (a *halPinAccessor) ReadBytes() ([]byte, error) { return a.readFn() }
func (a *halPinAccessor) WriteBytes(d []byte) error  { return a.writeFn(d) }
func (a *halPinAccessor) Size() uint32               { return a.ti.byteSize }
func (a *halPinAccessor) TypeName() string           { return a.ti.adsTypeName }
func (a *halPinAccessor) TypeID() uint32             { return a.ti.adstID }

// newBitAccessor creates a PinAccessor for a bool HAL pin.
func newBitAccessor(pin *hal.Pin[bool], ti typeInfo) *halPinAccessor {
	return &halPinAccessor{
		ti: ti,
		readFn: func() ([]byte, error) {
			v := pin.Get()
			b := byte(0)
			if v {
				b = 1
			}
			return []byte{b}, nil
		},
		writeFn: func(data []byte) error {
			if len(data) < 1 {
				return fmt.Errorf("bool write: need 1 byte, got %d", len(data))
			}
			pin.Set(data[0] != 0)
			return nil
		},
	}
}

// newU32Accessor creates a PinAccessor for a uint32 HAL pin with a given ADS wire size.
func newU32Accessor(pin *hal.Pin[uint32], ti typeInfo) *halPinAccessor {
	return &halPinAccessor{
		ti: ti,
		readFn: func() ([]byte, error) {
			v := pin.Get()
			switch ti.byteSize {
			case 1:
				return []byte{byte(v)}, nil
			case 2:
				b := make([]byte, 2)
				binary.LittleEndian.PutUint16(b, uint16(v))
				return b, nil
			default: // 4
				b := make([]byte, 4)
				binary.LittleEndian.PutUint32(b, v)
				return b, nil
			}
		},
		writeFn: func(data []byte) error {
			if uint32(len(data)) < ti.byteSize {
				return fmt.Errorf("u32 write: need %d byte(s), got %d", ti.byteSize, len(data))
			}
			var v uint32
			switch ti.byteSize {
			case 1:
				v = uint32(data[0])
			case 2:
				v = uint32(binary.LittleEndian.Uint16(data[:2]))
			default: // 4
				v = binary.LittleEndian.Uint32(data[:4])
			}
			pin.Set(v)
			return nil
		},
	}
}

// newS32Accessor creates a PinAccessor for an int32 HAL pin with a given ADS wire size.
func newS32Accessor(pin *hal.Pin[int32], ti typeInfo) *halPinAccessor {
	return &halPinAccessor{
		ti: ti,
		readFn: func() ([]byte, error) {
			v := pin.Get()
			switch ti.byteSize {
			case 1:
				return []byte{byte(int8(v))}, nil
			case 2:
				b := make([]byte, 2)
				binary.LittleEndian.PutUint16(b, uint16(int16(v)))
				return b, nil
			default: // 4
				b := make([]byte, 4)
				binary.LittleEndian.PutUint32(b, uint32(v))
				return b, nil
			}
		},
		writeFn: func(data []byte) error {
			if uint32(len(data)) < ti.byteSize {
				return fmt.Errorf("s32 write: need %d byte(s), got %d", ti.byteSize, len(data))
			}
			var v int32
			switch ti.byteSize {
			case 1:
				v = int32(int8(data[0]))
			case 2:
				v = int32(int16(binary.LittleEndian.Uint16(data[:2])))
			default: // 4
				v = int32(binary.LittleEndian.Uint32(data[:4]))
			}
			pin.Set(v)
			return nil
		},
	}
}

// newFloatAccessor creates a PinAccessor for a float64 HAL pin.
// REAL uses 4-byte IEEE 754 float32 on the wire; LREAL uses 8-byte float64.
func newFloatAccessor(pin *hal.Pin[float64], ti typeInfo) *halPinAccessor {
	return &halPinAccessor{
		ti: ti,
		readFn: func() ([]byte, error) {
			v := pin.Get()
			if ti.byteSize == 4 {
				b := make([]byte, 4)
				binary.LittleEndian.PutUint32(b, math.Float32bits(float32(v)))
				return b, nil
			}
			b := make([]byte, 8)
			binary.LittleEndian.PutUint64(b, math.Float64bits(v))
			return b, nil
		},
		writeFn: func(data []byte) error {
			if uint32(len(data)) < ti.byteSize {
				return fmt.Errorf("float write: need %d byte(s), got %d", ti.byteSize, len(data))
			}
			var v float64
			if ti.byteSize == 4 {
				v = float64(math.Float32frombits(binary.LittleEndian.Uint32(data[:4])))
			} else {
				v = math.Float64frombits(binary.LittleEndian.Uint64(data[:8]))
			}
			pin.Set(v)
			return nil
		},
	}
}

// stringMemAccessor is a memory-backed PinAccessor for ADS STRING(n) symbols.
// It does NOT use a HAL pin; instead it holds an internal []byte buffer of size
// n+1 (null-terminated), served directly to ADS reads/writes as a flat byte array.
type stringMemAccessor struct {
	mu  sync.RWMutex
	buf []byte
	ti  typeInfo
}

func newStringMemAccessor(ti typeInfo) *stringMemAccessor {
	return &stringMemAccessor{
		buf: make([]byte, ti.byteSize),
		ti:  ti,
	}
}

func (a *stringMemAccessor) ReadBytes() ([]byte, error) {
	a.mu.RLock()
	defer a.mu.RUnlock()
	out := make([]byte, len(a.buf))
	copy(out, a.buf)
	return out, nil
}

func (a *stringMemAccessor) WriteBytes(data []byte) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	n := copy(a.buf, data)
	// Zero-fill remainder and ensure null termination.
	for i := n; i < len(a.buf); i++ {
		a.buf[i] = 0
	}
	// If data was longer than the buffer, truncate and null-terminate.
	if len(data) >= len(a.buf) {
		a.buf[len(a.buf)-1] = 0
	}
	return nil
}

func (a *stringMemAccessor) Size() uint32     { return a.ti.byteSize }
func (a *stringMemAccessor) TypeName() string { return a.ti.adsTypeName }
func (a *stringMemAccessor) TypeID() uint32   { return a.ti.adstID }

// containerChild is one child PinAccessor within a ContainerAccessor, together
// with its byte offset relative to the start of the container.
type containerChild struct {
	relativeOffset uint32
	accessor       ads.PinAccessor
}

// ContainerAccessor implements ads.PinAccessor for a struct or array-element
// container symbol.  ReadBytes concatenates all children at their correct
// relative offsets (zero-padding gaps); WriteBytes distributes incoming bytes
// to each child.
type ContainerAccessor struct {
	children []containerChild
	size     uint32
	typeName string
}

func (c *ContainerAccessor) ReadBytes() ([]byte, error) {
	buf := make([]byte, c.size)
	for _, ch := range c.children {
		data, err := ch.accessor.ReadBytes()
		if err != nil {
			return nil, err
		}
		end := ch.relativeOffset + uint32(len(data))
		if end > c.size {
			end = c.size
		}
		copy(buf[ch.relativeOffset:end], data)
	}
	return buf, nil
}

func (c *ContainerAccessor) WriteBytes(data []byte) error {
	if uint32(len(data)) < c.size {
		return fmt.Errorf("container write: need %d bytes, got %d", c.size, len(data))
	}
	for _, ch := range c.children {
		end := ch.relativeOffset + ch.accessor.Size()
		if err := ch.accessor.WriteBytes(data[ch.relativeOffset:end]); err != nil {
			return err
		}
	}
	return nil
}

func (c *ContainerAccessor) Size() uint32     { return c.size }
func (c *ContainerAccessor) TypeName() string { return c.typeName }
func (c *ContainerAccessor) TypeID() uint32   { return 0 }

// containerTypeName extracts a short type name from a full container ADS name.
// E.g. "DISPLAY_DATA.stData.aPools[1]" → "aPools", "stMsg" → "stMsg".
func containerTypeName(adsName string) string {
	name := adsName
	if idx := strings.LastIndex(adsName, "."); idx >= 0 {
		name = adsName[idx+1:]
	}
	if idx := strings.Index(name, "["); idx >= 0 {
		name = name[:idx]
	}
	return name
}

// Bridge holds all HAL pins and their corresponding ADS symbol registrations.
type Bridge struct {
	// pins retains references so the GC does not collect them.
	pins []interface{}
}

// containerFrame tracks one in-progress container during NewBridge construction.
type containerFrame struct {
	adsName     string
	startOffset uint32
	children    []containerChild
}

// addChild appends acc as a direct child of the frame at the given absolute
// process-image offset.
func (f *containerFrame) addChild(absoluteOffset uint32, acc ads.PinAccessor) {
	f.children = append(f.children, containerChild{
		relativeOffset: absoluteOffset - f.startOffset,
		accessor:       acc,
	})
}

// NewBridge creates HAL pins for all ConfigActionPin actions and registers them
// in the provided SymbolTable with natural-alignment padding.  Container
// boundary actions (ConfigActionBeginContainer / ConfigActionEndContainer)
// trigger the corresponding SymbolTable alignment calls so that struct-start and
// struct-end padding match TwinCAT's C-style natural alignment.  A
// ContainerAccessor is also registered for each container so that the TwinCAT
// HMI can look up the container via SymbolInfoByName and read its bytes in one
// CmdRead request.
func NewBridge(comp *hal.Component, actions []ConfigAction, st *ads.SymbolTable) (*Bridge, error) {
	b := &Bridge{}
	var containerStack []containerFrame
	for _, action := range actions {
		switch action.Kind {
		case ConfigActionBeginContainer:
			st.BeginContainer(action.Alignment)
			containerStack = append(containerStack, containerFrame{
				adsName:     action.ADSName,
				startOffset: st.CurrentOffset(),
			})

		case ConfigActionEndContainer:
			if len(containerStack) > 0 {
				frame := containerStack[len(containerStack)-1]
				containerStack = containerStack[:len(containerStack)-1]

				st.EndContainer(frame.startOffset, action.Alignment)

				size := st.CurrentOffset() - frame.startOffset
				acc := &ContainerAccessor{
					children: frame.children,
					size:     size,
					typeName: containerTypeName(frame.adsName),
				}
				st.RegisterContainer(frame.adsName, acc, frame.startOffset)

				// Add this container as a direct child of the parent frame (if any)
				// so the parent can recursively read its bytes.
				if len(containerStack) > 0 {
					containerStack[len(containerStack)-1].addChild(frame.startOffset, acc)
				}
			}

		case ConfigActionPin:
			cp := action.Pin
			ti, err := parseTypeInfo(cp.TypeName)
			if err != nil {
				return nil, fmt.Errorf("symbol %q: %w", cp.ADSName, err)
			}

			dir := hal.In
			if cp.Dir == DirOut {
				dir = hal.Out
			}

			var acc ads.PinAccessor

			switch {
			case ti.adstID == ads.ADSTBool:
				p, err := hal.NewPin[bool](comp, cp.HALPath, dir)
				if err != nil {
					return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
				}
				acc = newBitAccessor(p, ti)
				b.pins = append(b.pins, p)

			case ti.adstID == ads.ADSTUInt8 || ti.adstID == ads.ADSTUInt16 || ti.adstID == ads.ADSTUInt32:
				p, err := hal.NewPin[uint32](comp, cp.HALPath, dir)
				if err != nil {
					return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
				}
				acc = newU32Accessor(p, ti)
				b.pins = append(b.pins, p)

			case ti.adstID == ads.ADSTInt8 || ti.adstID == ads.ADSTInt16 || ti.adstID == ads.ADSTInt32:
				p, err := hal.NewPin[int32](comp, cp.HALPath, dir)
				if err != nil {
					return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
				}
				acc = newS32Accessor(p, ti)
				b.pins = append(b.pins, p)

			case ti.adstID == ads.ADSTReal32 || ti.adstID == ads.ADSTReal64:
				p, err := hal.NewPin[float64](comp, cp.HALPath, dir)
				if err != nil {
					return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
				}
				acc = newFloatAccessor(p, ti)
				b.pins = append(b.pins, p)

			case ti.adstID == ads.ADSTString:
				acc = newStringMemAccessor(ti)

			default:
				return nil, fmt.Errorf("symbol %q: unsupported type %q", cp.ADSName, cp.TypeName)
			}

			sym := st.RegisterAligned(cp.ADSName, acc, ti.alignment)

			// Add this leaf as a direct child of the innermost container frame.
			if len(containerStack) > 0 {
				containerStack[len(containerStack)-1].addChild(sym.IndexOffset, acc)
			}
		}
	}
	return b, nil
}
