package parser

import (
	"testing"

	"github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/ast"
)

func TestParseSimpleAPI(t *testing.T) {
	src := `@api test
@version 1
@prefix test
@rest_export true

enum Status {
    OK = 0
    ERROR = 1
}

type Result {
    success: bool
    message: string?
}

@method "GET"
@path "/status"
@rt_safe "true"
func get_status() -> Status
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	if api.Name != "test" {
		t.Errorf("Name = %q, want %q", api.Name, "test")
	}
	if api.Version != 1 {
		t.Errorf("Version = %d, want 1", api.Version)
	}
	if api.Prefix != "test" {
		t.Errorf("Prefix = %q, want %q", api.Prefix, "test")
	}
	if !api.RestExport {
		t.Error("RestExport = false, want true")
	}

	// Enum
	if len(api.Enums) != 1 {
		t.Fatalf("len(Enums) = %d, want 1", len(api.Enums))
	}
	if api.Enums[0].Name != "Status" {
		t.Errorf("Enum[0].Name = %q, want %q", api.Enums[0].Name, "Status")
	}
	if len(api.Enums[0].Values) != 2 {
		t.Fatalf("len(Enum[0].Values) = %d, want 2", len(api.Enums[0].Values))
	}

	// Type
	if len(api.Types) != 1 {
		t.Fatalf("len(Types) = %d, want 1", len(api.Types))
	}
	if api.Types[0].Name != "Result" {
		t.Errorf("Type[0].Name = %q, want %q", api.Types[0].Name, "Result")
	}
	if len(api.Types[0].Fields) != 2 {
		t.Fatalf("len(Type[0].Fields) = %d, want 2", len(api.Types[0].Fields))
	}
	if api.Types[0].Fields[1].Type.Nullable != true {
		t.Error("Type[0].Fields[1].Nullable = false, want true")
	}

	// Func
	if len(api.Funcs) != 1 {
		t.Fatalf("len(Funcs) = %d, want 1", len(api.Funcs))
	}
	fn := api.Funcs[0]
	if fn.Name != "get_status" {
		t.Errorf("Func[0].Name = %q, want %q", fn.Name, "get_status")
	}
	if fn.Method != "GET" {
		t.Errorf("Func[0].Method = %q, want %q", fn.Method, "GET")
	}
	if fn.Path != "/status" {
		t.Errorf("Func[0].Path = %q, want %q", fn.Path, "/status")
	}
	if fn.RTSafe != true {
		t.Error("Func[0].RTSafe = false, want true")
	}
}

func TestParseSliceTypes(t *testing.T) {
	src := `@api test
@version 1

type Item {
    name: string
}

type List {
    items: []Item
}

@method "GET"
@path "/items"
@rt_safe "false"
func get_items() -> []Item
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	if len(api.Types) != 2 {
		t.Fatalf("len(Types) = %d, want 2", len(api.Types))
	}

	list := api.Types[1]
	if list.Fields[0].Type.Kind != ast.TypeSlice {
		t.Errorf("Field[0].Type.Kind = %v, want TypeSlice", list.Fields[0].Type.Kind)
	}

	fn := api.Funcs[0]
	if fn.Return.Kind != ast.TypeSlice {
		t.Errorf("Return.Kind = %v, want TypeSlice", fn.Return.Kind)
	}
}

func TestParseArrayTypes(t *testing.T) {
	src := `@api test
@version 1

type Position {
    coords: [3]f64
}
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	if len(api.Types) != 1 {
		t.Fatalf("len(Types) = %d, want 1", len(api.Types))
	}

	field := api.Types[0].Fields[0]
	if field.Type.Kind != ast.TypeArray {
		t.Errorf("Field.Type.Kind = %v, want TypeArray", field.Type.Kind)
	}
	if field.Type.ArrayLen != 3 {
		t.Errorf("Field.Type.ArrayLen = %d, want 3", field.Type.ArrayLen)
	}
}

func TestParseNegativeEnumValue(t *testing.T) {
	src := `@api test
@version 1

enum Type {
    UNKNOWN = -1
    DEFAULT = 0
}
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	if api.Enums[0].Values[0].Value != -1 {
		t.Errorf("Values[0].Value = %d, want -1", api.Enums[0].Values[0].Value)
	}
}

func TestParseConst(t *testing.T) {
	src := `@api test
@version 1

const MAX_JOINTS = 16

type Joints {
    values: [MAX_JOINTS]f64
}
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	if len(api.Consts) != 1 {
		t.Fatalf("len(Consts) = %d, want 1", len(api.Consts))
	}
	if api.Consts[0].Name != "MAX_JOINTS" {
		t.Errorf("Consts[0].Name = %q, want %q", api.Consts[0].Name, "MAX_JOINTS")
	}
	if api.Consts[0].Value != 16 {
		t.Errorf("Consts[0].Value = %d, want 16", api.Consts[0].Value)
	}

	// Array should resolve named size
	field := api.Types[0].Fields[0]
	if field.Type.Kind != ast.TypeArray {
		t.Fatalf("Field.Type.Kind = %v, want TypeArray", field.Type.Kind)
	}
	if field.Type.ArrayLen != 16 {
		t.Errorf("Field.Type.ArrayLen = %d, want 16", field.Type.ArrayLen)
	}
	if field.Type.ArrayLenName != "MAX_JOINTS" {
		t.Errorf("Field.Type.ArrayLenName = %q, want %q", field.Type.ArrayLenName, "MAX_JOINTS")
	}
}

func TestParseByRef(t *testing.T) {
	src := `@api test
@version 1

type Pose {
    x: f64
    y: f64
}

func forward(joints: []f64, world: Pose byref, flags: u64 byref) -> i32
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	fn := api.Funcs[0]
	if len(fn.Params) != 3 {
		t.Fatalf("len(Params) = %d, want 3", len(fn.Params))
	}

	// joints: []f64 — no byref
	if fn.Params[0].ByRef {
		t.Error("Params[0].ByRef = true, want false")
	}
	// world: Pose byref
	if !fn.Params[1].ByRef {
		t.Error("Params[1].ByRef = false, want true")
	}
	if fn.Params[1].Type.Kind != ast.TypeNamed {
		t.Errorf("Params[1].Type.Kind = %v, want TypeNamed", fn.Params[1].Type.Kind)
	}
	// flags: u64 byref
	if !fn.Params[2].ByRef {
		t.Error("Params[2].ByRef = false, want true")
	}
	if fn.Params[2].Type.Kind != ast.TypePrimitive {
		t.Errorf("Params[2].Type.Kind = %v, want TypePrimitive", fn.Params[2].Type.Kind)
	}
}

func TestParsePtrIsUnknownType(t *testing.T) {
	src := `@api test
@version 1

func bad(handle: ptr) -> i32
`
	api, _ := Parse("test.gmi", src)

	// "ptr" is not a primitive — it should be parsed as TypeNamed (unknown type)
	p := api.Funcs[0].Params[0]
	if p.Type.Kind != ast.TypeNamed {
		t.Errorf("ptr should be TypeNamed (unknown), got Kind=%v", p.Type.Kind)
	}
	if p.Type.Name != "ptr" {
		t.Errorf("Name = %q, want %q", p.Type.Name, "ptr")
	}
}

func TestParseConstAndByRefCombined(t *testing.T) {
	src := `@api kins
@version 1

const MAX_JOINTS = 16

type Pose {
    x: f64
    y: f64
    z: f64
}

@rt_safe "true"
func forward(joints: [MAX_JOINTS]f64, world: Pose byref, fflags: u64, iflags: u64 byref) -> i32
`
	api, errors := Parse("test.gmi", src)
	if len(errors) > 0 {
		t.Fatalf("Parse errors: %v", errors)
	}

	fn := api.Funcs[0]
	if !fn.RTSafe {
		t.Error("RTSafe = false, want true")
	}

	// joints: [MAX_JOINTS]f64 — array with named size, no byref
	p0 := fn.Params[0]
	if p0.Type.Kind != ast.TypeArray {
		t.Fatalf("Params[0].Type.Kind = %v, want TypeArray", p0.Type.Kind)
	}
	if p0.Type.ArrayLen != 16 {
		t.Errorf("Params[0].Type.ArrayLen = %d, want 16", p0.Type.ArrayLen)
	}
	if p0.Type.ArrayLenName != "MAX_JOINTS" {
		t.Errorf("Params[0].Type.ArrayLenName = %q, want %q", p0.Type.ArrayLenName, "MAX_JOINTS")
	}
	if p0.ByRef {
		t.Error("Params[0].ByRef = true, want false")
	}

	// world: Pose byref
	if !fn.Params[1].ByRef {
		t.Error("Params[1].ByRef = false, want true")
	}

	// fflags: u64 — value
	if fn.Params[2].ByRef {
		t.Error("Params[2].ByRef = true, want false")
	}

	// iflags: u64 byref
	if !fn.Params[3].ByRef {
		t.Error("Params[3].ByRef = false, want true")
	}
}
