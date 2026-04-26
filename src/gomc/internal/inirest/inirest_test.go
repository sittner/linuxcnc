package inirest

import (
	"encoding/json"
	"testing"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func setupTestINI(t *testing.T) {
	t.Helper()
	parsed, err := inifile.ParseString(`
[DISPLAY]
GEOMETRY = XYZABCUVW
MAX_FEED_OVERRIDE = 1.5
LATHE =

[FILTER]
PROGRAM_EXTENSION = .nc
PROGRAM_EXTENSION = .ngc
PROGRAM_EXTENSION = .py

[KINS]
JOINTS = 3

[EMC]
MACHINE = Test Machine
`)
	if err != nil {
		t.Fatal(err)
	}
	// Register with a fresh registry.
	reg := apiserver.NewRegistry()
	if err := Register(reg, parsed); err != nil {
		t.Fatal(err)
	}
}

func TestQuerySingleValue(t *testing.T) {
	setupTestINI(t)

	body, _ := json.Marshal([]queryItem{
		{Section: "DISPLAY", Key: "GEOMETRY"},
	})
	resp, err := dispatchQuery(nil, body)
	if err != nil {
		t.Fatal(err)
	}

	var results []resultItem
	if err := json.Unmarshal(resp, &results); err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 {
		t.Fatalf("expected 1 result, got %d", len(results))
	}
	if results[0].Value == nil || *results[0].Value != "XYZABCUVW" {
		t.Errorf("expected XYZABCUVW, got %v", results[0].Value)
	}
}

func TestQueryMissingKey(t *testing.T) {
	setupTestINI(t)

	body, _ := json.Marshal([]queryItem{
		{Section: "DISPLAY", Key: "NONEXISTENT"},
	})
	resp, err := dispatchQuery(nil, body)
	if err != nil {
		t.Fatal(err)
	}

	var results []resultItem
	if err := json.Unmarshal(resp, &results); err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 {
		t.Fatalf("expected 1 result, got %d", len(results))
	}
	if results[0].Value != nil {
		t.Errorf("expected nil value for missing key, got %v", *results[0].Value)
	}
}

func TestQueryEmptyValue(t *testing.T) {
	setupTestINI(t)

	body, _ := json.Marshal([]queryItem{
		{Section: "DISPLAY", Key: "LATHE"},
	})
	resp, err := dispatchQuery(nil, body)
	if err != nil {
		t.Fatal(err)
	}

	var results []resultItem
	if err := json.Unmarshal(resp, &results); err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 {
		t.Fatalf("expected 1 result, got %d", len(results))
	}
	// Empty value should still return a pointer (key exists).
	if results[0].Value == nil {
		t.Error("expected non-nil value for existing key with empty value")
	} else if *results[0].Value != "" {
		t.Errorf("expected empty string, got %q", *results[0].Value)
	}
}

func TestQueryFindAll(t *testing.T) {
	setupTestINI(t)

	body, _ := json.Marshal([]queryItem{
		{Section: "FILTER", Key: "PROGRAM_EXTENSION", All: true},
	})
	resp, err := dispatchQuery(nil, body)
	if err != nil {
		t.Fatal(err)
	}

	var results []resultItem
	if err := json.Unmarshal(resp, &results); err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 {
		t.Fatalf("expected 1 result, got %d", len(results))
	}
	if len(results[0].Values) != 3 {
		t.Fatalf("expected 3 values, got %d: %v", len(results[0].Values), results[0].Values)
	}
	want := []string{".nc", ".ngc", ".py"}
	for i, w := range want {
		if results[0].Values[i] != w {
			t.Errorf("values[%d] = %q, want %q", i, results[0].Values[i], w)
		}
	}
}

func TestQueryFindAllMissing(t *testing.T) {
	setupTestINI(t)

	body, _ := json.Marshal([]queryItem{
		{Section: "FILTER", Key: "NONEXISTENT", All: true},
	})
	resp, err := dispatchQuery(nil, body)
	if err != nil {
		t.Fatal(err)
	}

	var results []resultItem
	if err := json.Unmarshal(resp, &results); err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 {
		t.Fatalf("expected 1 result, got %d", len(results))
	}
	if results[0].Values == nil {
		t.Error("expected empty slice, got nil")
	} else if len(results[0].Values) != 0 {
		t.Errorf("expected 0 values, got %d", len(results[0].Values))
	}
}

func TestQueryBulk(t *testing.T) {
	setupTestINI(t)

	body, _ := json.Marshal([]queryItem{
		{Section: "DISPLAY", Key: "GEOMETRY"},
		{Section: "DISPLAY", Key: "MAX_FEED_OVERRIDE"},
		{Section: "EMC", Key: "MACHINE"},
		{Section: "KINS", Key: "JOINTS"},
		{Section: "DISPLAY", Key: "NONEXISTENT"},
		{Section: "FILTER", Key: "PROGRAM_EXTENSION", All: true},
	})
	resp, err := dispatchQuery(nil, body)
	if err != nil {
		t.Fatal(err)
	}

	var results []resultItem
	if err := json.Unmarshal(resp, &results); err != nil {
		t.Fatal(err)
	}
	if len(results) != 6 {
		t.Fatalf("expected 6 results, got %d", len(results))
	}

	// Check a few values.
	if results[0].Value == nil || *results[0].Value != "XYZABCUVW" {
		t.Errorf("result[0]: want XYZABCUVW, got %v", results[0].Value)
	}
	if results[1].Value == nil || *results[1].Value != "1.5" {
		t.Errorf("result[1]: want 1.5, got %v", results[1].Value)
	}
	if results[4].Value != nil {
		t.Errorf("result[4]: want nil for missing key, got %v", *results[4].Value)
	}
	if len(results[5].Values) != 3 {
		t.Errorf("result[5]: want 3 values, got %d", len(results[5].Values))
	}
}
