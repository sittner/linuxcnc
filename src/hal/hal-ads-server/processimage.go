package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"strings"

	"linuxcnc.org/hal"
	"linuxcnc.org/hal-ads-server/ads"
)

// PinDescriptor describes a single HAL pin in the global process-image buffer.
type PinDescriptor struct {
	Type      *TypeEntry // resolved type entry
	Dir       PinDir
	BufOffset uint32 // byte offset in the global buffer
	HALPin    any    // *hal.Pin[bool] | *hal.Pin[uint32] | *hal.Pin[int32] | *hal.Pin[float64] | *hal.Pin[string]
	StrLen    int    // > 0 for STRING(n) types only
	HALPath   string
	ADSName   string
	syncGen   uint64 // last generation this pin was synced (used by ReadBuffer/WriteBuffer)
}

// ProcessImage owns the single global process-image buffer, the byteMap, and all
// HAL pins. It implements ads.BufferIO so that the SymbolTable can call back
// for buffer reads/writes without knowing about HAL.
type ProcessImage struct {
	buf         []byte
	byteMap     []*PinDescriptor // indexed by byte offset; nil = padding
	descriptors []*PinDescriptor // all leaf pin descriptors
	gen         uint64           // incremented on each ReadBuffer/WriteBuffer call
}

// ReadBuffer implements ads.BufferIO.
// It syncs all HAL pins in [offset, offset+length) to the buffer, then
// returns the buffer slice. Callers must copy the slice if they need it beyond
// the next ReadBuffer/WriteBuffer call.
func (b *ProcessImage) ReadBuffer(offset, length uint32) ([]byte, error) {
	if offset+length > uint32(len(b.buf)) {
		return nil, fmt.Errorf("ReadBuffer: range [%d,%d) exceeds buffer size %d",
			offset, offset+length, len(b.buf))
	}
	b.gen++
	for i := offset; i < offset+length; i++ {
		pd := b.byteMap[i]
		if pd == nil || pd.syncGen == b.gen {
			continue
		}
		pd.syncGen = b.gen
		writeToBuffer(pd, b.buf)
	}
	return b.buf[offset : offset+length], nil
}

// WriteBuffer implements ads.BufferIO.
// It copies data into the buffer at offset, then syncs all affected HAL pins
// from the buffer.
func (b *ProcessImage) WriteBuffer(offset uint32, data []byte) error {
	if offset+uint32(len(data)) > uint32(len(b.buf)) {
		return fmt.Errorf("WriteBuffer: range [%d,%d) exceeds buffer size %d",
			offset, offset+uint32(len(data)), len(b.buf))
	}
	copy(b.buf[offset:], data)
	b.gen++
	for i := offset; i < offset+uint32(len(data)); i++ {
		pd := b.byteMap[i]
		if pd == nil || pd.syncGen == b.gen {
			continue
		}
		pd.syncGen = b.gen
		readFromBuffer(pd, b.buf)
	}
	return nil
}

// writeToBuffer reads the HAL pin value and writes it into buf at pd.BufOffset.
func writeToBuffer(pd *PinDescriptor, buf []byte) {
	off := pd.BufOffset
	switch p := pd.HALPin.(type) {
	case *hal.Pin[bool]:
		b := byte(0)
		if p.Get() {
			b = 1
		}
		buf[off] = b
	case *hal.Pin[uint32]:
		v := p.Get()
		switch pd.Type.ByteSize {
		case 1:
			buf[off] = byte(v)
		case 2:
			binary.LittleEndian.PutUint16(buf[off:], uint16(v))
		default: // 4
			binary.LittleEndian.PutUint32(buf[off:], v)
		}
	case *hal.Pin[int32]:
		v := p.Get()
		switch pd.Type.ByteSize {
		case 1:
			buf[off] = byte(int8(v))
		case 2:
			binary.LittleEndian.PutUint16(buf[off:], uint16(int16(v)))
		default: // 4
			binary.LittleEndian.PutUint32(buf[off:], uint32(v))
		}
	case *hal.Pin[float64]:
		v := p.Get()
		if pd.Type.ByteSize == 4 {
			binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(float32(v)))
		} else {
			binary.LittleEndian.PutUint64(buf[off:], math.Float64bits(v))
		}
	case *hal.Pin[string]:
		v := p.Get()
		// Zero the slot, then copy up to StrLen bytes.
		slice := buf[off : off+pd.Type.ByteSize]
		for i := range slice {
			slice[i] = 0
		}
		n := len(v)
		if n > pd.StrLen {
			n = pd.StrLen
		}
		copy(slice[:n], v[:n])
	}
}

// readFromBuffer reads bytes from buf at pd.BufOffset and updates the HAL pin.
func readFromBuffer(pd *PinDescriptor, buf []byte) {
	off := pd.BufOffset
	switch p := pd.HALPin.(type) {
	case *hal.Pin[bool]:
		p.Set(buf[off] != 0)
	case *hal.Pin[uint32]:
		var v uint32
		switch pd.Type.ByteSize {
		case 1:
			v = uint32(buf[off])
		case 2:
			v = uint32(binary.LittleEndian.Uint16(buf[off:]))
		default: // 4
			v = binary.LittleEndian.Uint32(buf[off:])
		}
		p.Set(v)
	case *hal.Pin[int32]:
		var v int32
		switch pd.Type.ByteSize {
		case 1:
			v = int32(int8(buf[off]))
		case 2:
			v = int32(int16(binary.LittleEndian.Uint16(buf[off:])))
		default: // 4
			v = int32(binary.LittleEndian.Uint32(buf[off:]))
		}
		p.Set(v)
	case *hal.Pin[float64]:
		var v float64
		if pd.Type.ByteSize == 4 {
			v = float64(math.Float32frombits(binary.LittleEndian.Uint32(buf[off:])))
		} else {
			v = math.Float64frombits(binary.LittleEndian.Uint64(buf[off:]))
		}
		p.Set(v)
	case *hal.Pin[string]:
		slice := buf[off : off+pd.Type.ByteSize]
		s := string(slice)
		if idx := bytes.IndexByte(slice, 0); idx >= 0 {
			s = s[:idx]
		}
		p.Set(s)
	}
}

// BuildProcessImage creates HAL pins for all LayoutPins and registers them in the
// provided SymbolTable using pre-computed byte offsets from the layout.
// Pad entries (Dir == DirPad) occupy process-image space but do not create
// HAL pins and are not registered in the ADS symbol list.
func BuildProcessImage(comp *hal.Component, pins []LayoutPin, st *ads.SymbolTable) (*ProcessImage, error) {
	// Compute total buffer size from all pins (including pads).
	var totalSize uint32
	for _, cp := range pins {
		if end := cp.Offset + cp.Size; end > totalSize {
			totalSize = end
		}
	}

	b := &ProcessImage{
		buf:     make([]byte, totalSize),
		byteMap: make([]*PinDescriptor, totalSize),
	}

	for _, cp := range pins {
		if cp.Dir == DirPad {
			// Padding: reserve process-image space only (no HAL pin, no ADS name).
			st.RegisterPadAt(cp.Offset, cp.Size)
			continue
		}

		dir := hal.In
		switch cp.Dir {
		case DirOut:
			dir = hal.Out
		case DirInOut:
			dir = hal.IO
		}

		pd := &PinDescriptor{
			Type:      cp.Type,
			Dir:       cp.Dir,
			BufOffset: cp.Offset,
			StrLen:    cp.StrLen,
			HALPath:   cp.HALPath,
			ADSName:   cp.ADSName,
		}

		switch {
		case cp.Type.ADSTID == ads.ADSTBool:
			p, err := hal.NewPin[bool](comp, cp.HALPath, dir)
			if err != nil {
				return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
			}
			pd.HALPin = p

		case cp.Type.ADSTID == ads.ADSTUInt8 || cp.Type.ADSTID == ads.ADSTUInt16 || cp.Type.ADSTID == ads.ADSTUInt32:
			p, err := hal.NewPin[uint32](comp, cp.HALPath, dir)
			if err != nil {
				return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
			}
			pd.HALPin = p

		case cp.Type.ADSTID == ads.ADSTInt8 || cp.Type.ADSTID == ads.ADSTInt16 || cp.Type.ADSTID == ads.ADSTInt32:
			p, err := hal.NewPin[int32](comp, cp.HALPath, dir)
			if err != nil {
				return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
			}
			pd.HALPin = p

		case cp.Type.ADSTID == ads.ADSTReal32 || cp.Type.ADSTID == ads.ADSTReal64:
			p, err := hal.NewPin[float64](comp, cp.HALPath, dir)
			if err != nil {
				return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
			}
			pd.HALPin = p

		case cp.Type.ADSTID == ads.ADSTString:
			p, err := hal.NewPin[string](comp, cp.HALPath, dir)
			if err != nil {
				return nil, fmt.Errorf("create HAL pin %q: %w", cp.HALPath, err)
			}
			pd.HALPin = p

		default:
			return nil, fmt.Errorf("symbol %q: unsupported type %q", cp.ADSName, cp.Type.ADSTypeName)
		}

		b.descriptors = append(b.descriptors, pd)
		// Fill byteMap entries for this pin's byte range.
		for i := cp.Offset; i < cp.Offset+cp.Size; i++ {
			b.byteMap[i] = pd
		}

		st.RegisterAt(cp.ADSName, cp.Offset, cp.Size, cp.Type.ADSTypeName, cp.Type.ADSTID)
	}

	// Compute padded group sizes from layout information, including tail padding.
	// For each unique parent prefix, find the start offset, the end of the last
	// member, and the maximum field alignment. The padded size is then
	// alignUp(lastEnd, maxAlign) - startOffset, matching TwinCAT pack mode 0.
	type groupBounds struct {
		startOffset uint32
		lastEnd     uint32
		maxAlign    uint32
	}
	groups := make(map[string]*groupBounds)

	for _, cp := range pins {
		segs := strings.Split(cp.ADSName, ".")
		for prefixLen := 1; prefixLen < len(segs); prefixLen++ {
			prefix := strings.Join(segs[:prefixLen], ".")
			end := cp.Offset + cp.Size
			if gb, ok := groups[prefix]; !ok {
				groups[prefix] = &groupBounds{
					startOffset: cp.Offset,
					lastEnd:     end,
					maxAlign:    cp.Align,
				}
			} else {
				if end > gb.lastEnd {
					gb.lastEnd = end
				}
				if cp.Align > gb.maxAlign {
					gb.maxAlign = cp.Align
				}
			}
		}
	}
	for name, gb := range groups {
		st.SetGroupSize(name, alignUp(gb.lastEnd, gb.maxAlign)-gb.startOffset)
	}

	// Connect the buffer to the symbol table.
	st.SetBufferIO(b)

	return b, nil
}
