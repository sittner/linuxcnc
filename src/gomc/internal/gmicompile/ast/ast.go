// Package ast defines the AST types for GMI (GOMC Interface Definition) files.
//
// A GMI file describes an API with types, enums, and functions that can be
// used for inter-module communication in LinuxCNC. The AST is designed to
// support code generation for:
//   - C server callbacks and types
//   - C REST client (cJSON/libcurl)
//   - Go server handlers and HTTP routing
//   - Go REST client
//   - Python REST client
package ast

import "fmt"

// ---------------------------------------------------------------------------
// Source positions
// ---------------------------------------------------------------------------

// Pos tracks a location in the source for error reporting.
type Pos struct {
	File string
	Line int
	Col  int
}

func (p Pos) String() string {
	if p.File == "" {
		return fmt.Sprintf("%d:%d", p.Line, p.Col)
	}
	return fmt.Sprintf("%s:%d:%d", p.File, p.Line, p.Col)
}

// ---------------------------------------------------------------------------
// API — the top-level compilation unit
// ---------------------------------------------------------------------------

// API represents a complete GMI interface definition.
type API struct {
	Name       string // API name from @api directive
	Version    int    // Version from @version directive
	Prefix     string // REST path prefix from @prefix directive
	RestExport bool   // Whether to expose via REST from @rest_export directive
	Pos        Pos    // Position of @api directive

	Consts []Const
	Enums  []Enum
	Types  []Type
	Funcs  []Func
}

// ---------------------------------------------------------------------------
// Const — named integer constant
// ---------------------------------------------------------------------------

// Const represents a named integer constant (e.g. const MAX_JOINTS = 16).
type Const struct {
	Name  string
	Value int
	Pos   Pos
}

// ---------------------------------------------------------------------------
// Enum — enumeration type
// ---------------------------------------------------------------------------

// Enum represents an enumeration definition.
type Enum struct {
	Name   string
	Pos    Pos
	Values []EnumValue
}

// EnumValue represents a single enum variant.
type EnumValue struct {
	Name  string
	Value int
	Pos   Pos
}

// ---------------------------------------------------------------------------
// Type — struct type
// ---------------------------------------------------------------------------

// Type represents a struct/record type definition.
type Type struct {
	Name   string
	Pos    Pos
	Fields []Field
}

// Field represents a single field in a type.
type Field struct {
	Name string
	Type TypeRef
	Pos  Pos
}

// ---------------------------------------------------------------------------
// TypeRef — type reference (primitive, named, array, slice)
// ---------------------------------------------------------------------------

// TypeKind distinguishes different kinds of type references.
type TypeKind int

const (
	TypePrimitive TypeKind = iota // bool, i32, u32, i64, u64, f64, string
	TypeNamed                     // user-defined type or enum
	TypeArray                     // [N]T fixed-size array (N can be const name or integer)
	TypeSlice                     // []T dynamic slice
)

// TypeRef represents a reference to a type.
type TypeRef struct {
	Kind         TypeKind
	Name         string   // for Primitive: "bool", "i32", etc.; for Named: type name
	Elem         *TypeRef // for Array/Slice: element type
	ArrayLen     int      // for Array: resolved integer length
	ArrayLenName string   // for Array: const name if used (e.g. "MAX_JOINTS")
	Nullable     bool     // T? syntax
}

func (t TypeRef) String() string {
	base := ""
	switch t.Kind {
	case TypePrimitive, TypeNamed:
		base = t.Name
	case TypeArray:
		if t.ArrayLenName != "" {
			base = fmt.Sprintf("[%s]%s", t.ArrayLenName, t.Elem.String())
		} else {
			base = fmt.Sprintf("[%d]%s", t.ArrayLen, t.Elem.String())
		}
	case TypeSlice:
		base = fmt.Sprintf("[]%s", t.Elem.String())
	}
	if t.Nullable {
		return base + "?"
	}
	return base
}

// IsPrimitive returns true if this is a primitive type.
func (t TypeRef) IsPrimitive() bool {
	return t.Kind == TypePrimitive
}

// Primitive type names.
const (
	PrimBool   = "bool"
	PrimI8     = "i8"
	PrimU8     = "u8"
	PrimI32    = "i32"
	PrimU32    = "u32"
	PrimI64    = "i64"
	PrimU64    = "u64"
	PrimF32    = "f32"
	PrimF64    = "f64"
	PrimString = "string"
)

// Primitives is the set of valid primitive type names.
var Primitives = map[string]bool{
	PrimBool:   true,
	PrimI8:     true,
	PrimU8:     true,
	PrimI32:    true,
	PrimU32:    true,
	PrimI64:    true,
	PrimU64:    true,
	PrimF32:    true,
	PrimF64:    true,
	PrimString: true,
}

// ---------------------------------------------------------------------------
// Func — function definition
// ---------------------------------------------------------------------------

// Func represents a function/endpoint definition.
type Func struct {
	Name   string
	Pos    Pos
	Params []Param
	Return *TypeRef // nil if no return type

	// Metadata from annotations.
	Method           string // GET, POST, PUT, DELETE (empty if not REST)
	Path             string // REST endpoint path
	RTSafe           bool   // true if callable from RT context
	Doc              string // documentation string
	Watch            bool   // true if this function supports WebSocket watch subscriptions
	WatchDefaultRate string // default push rate (e.g. "50ms", "1s")
	WatchFactory     bool   // true if watch uses per-connection factory (params sent as subscribe args)
}

// Param represents a function parameter.
type Param struct {
	Name  string
	Type  TypeRef
	ByRef bool // passed as mutable pointer (byref keyword)
	IsPtr bool // passed as opaque typed pointer (ptr keyword) — no marshaling
	Pos   Pos
}
