package main

import (
	"fmt"
	"strings"

	"linuxcnc.org/hal-ads-server/ads"
)

// TypeEntry consolidates all type-centric information for an ADS/TwinCAT type.
type TypeEntry struct {
	ADSTypeName string // normalised ADS type name, e.g. "BOOL", "DINT", "REAL"
	HALTypeName string // HAL pin type: "bit", "s32", "u32", "float", "string"
	ADSTID      uint32 // ADST constant (ads.ADST*)
	ByteSize    uint32 // wire size in bytes
	Alignment   uint32 // natural alignment: 1, 2, 4, or 8
}

// TypeEntries is the global registry of all statically-known ADS types.
// STRING(n) is parametric and is not listed here; use makeStringTypeEntry.
var TypeEntries = []*TypeEntry{
	// 1-byte types
	{ADSTypeName: "BOOL", HALTypeName: "bit", ADSTID: ads.ADSTBool, ByteSize: 1, Alignment: 1},
	{ADSTypeName: "BYTE", HALTypeName: "u32", ADSTID: ads.ADSTUInt8, ByteSize: 1, Alignment: 1},
	{ADSTypeName: "USINT", HALTypeName: "u32", ADSTID: ads.ADSTUInt8, ByteSize: 1, Alignment: 1},
	{ADSTypeName: "SINT", HALTypeName: "s32", ADSTID: ads.ADSTInt8, ByteSize: 1, Alignment: 1},
	// 2-byte types
	{ADSTypeName: "WORD", HALTypeName: "u32", ADSTID: ads.ADSTUInt16, ByteSize: 2, Alignment: 2},
	{ADSTypeName: "UINT", HALTypeName: "u32", ADSTID: ads.ADSTUInt16, ByteSize: 2, Alignment: 2},
	{ADSTypeName: "INT", HALTypeName: "s32", ADSTID: ads.ADSTInt16, ByteSize: 2, Alignment: 2},
	// 4-byte types
	{ADSTypeName: "DWORD", HALTypeName: "u32", ADSTID: ads.ADSTUInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "UDINT", HALTypeName: "u32", ADSTID: ads.ADSTUInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "TIME", HALTypeName: "u32", ADSTID: ads.ADSTUInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "TOD", HALTypeName: "u32", ADSTID: ads.ADSTUInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "DATE", HALTypeName: "u32", ADSTID: ads.ADSTUInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "DT", HALTypeName: "u32", ADSTID: ads.ADSTUInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "DINT", HALTypeName: "s32", ADSTID: ads.ADSTInt32, ByteSize: 4, Alignment: 4},
	{ADSTypeName: "REAL", HALTypeName: "float", ADSTID: ads.ADSTReal32, ByteSize: 4, Alignment: 4},
	// 8-byte types
	{ADSTypeName: "LREAL", HALTypeName: "float", ADSTID: ads.ADSTReal64, ByteSize: 8, Alignment: 8},
}

// TypesByADSName provides O(1) lookup of TypeEntry by ADS type name.
// Populated by init(). STRING(n) types are not in this map; use resolveType.
var TypesByADSName map[string]*TypeEntry

func init() {
	TypesByADSName = make(map[string]*TypeEntry, len(TypeEntries))
	for _, e := range TypeEntries {
		TypesByADSName[e.ADSTypeName] = e
	}
}

// makeStringTypeEntry creates a TypeEntry for a STRING(n) type at parse time.
// n must be > 0. The wire size is n+1 bytes (null terminator included).
func makeStringTypeEntry(n int) *TypeEntry {
	return &TypeEntry{
		ADSTypeName: fmt.Sprintf("STRING(%d)", n),
		HALTypeName: "string",
		ADSTID:      ads.ADSTString,
		ByteSize:    uint32(n + 1),
		Alignment:   1,
	}
}

// resolveType converts an upper-cased config type token to a TypeEntry.
// For STRING(n) types it returns a newly allocated TypeEntry and strLen=n.
// For all other types it returns the global TypeEntry and strLen=0.
func resolveType(typeName string) (*TypeEntry, int, error) {
	if strings.HasPrefix(typeName, "STRING(") {
		var n int
		if _, err := fmt.Sscanf(typeName, "STRING(%d)", &n); err != nil || n <= 0 {
			return nil, 0, fmt.Errorf("invalid string type %q", typeName)
		}
		return makeStringTypeEntry(n), n, nil
	}
	te, ok := TypesByADSName[typeName]
	if !ok {
		return nil, 0, fmt.Errorf("unsupported ADS type %q", typeName)
	}
	return te, 0, nil
}
