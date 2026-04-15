package parser

import (
	"testing"

	"github.com/sittner/linuxcnc/src/launcher/internal/gmicompile/ast"
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
    coords: [f64; 3]
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
