package apiserver

import (
	"syscall"
	"testing"
	"unsafe"
)

// fakeCallbacks is a dummy value for tests — we just need a non-nil pointer.
var fakeCallbacks = unsafe.Pointer(&struct{}{})

func testMeta(name string, version int, rest bool) *APIMeta {
	return &APIMeta{
		Name:       name,
		Version:    version,
		RESTExport: rest,
		Prefix:     name,
	}
}

func TestRegisterAndGet(t *testing.T) {
	r := NewRegistry()

	meta := testMeta("hal", 1, true)
	err := r.Register(meta, "hal0", fakeCallbacks)
	if err != nil {
		t.Fatalf("Register: unexpected error: %v", err)
	}

	api := r.Get("hal0")
	if api == nil {
		t.Fatal("Get: returned nil for registered instance")
	}
	if api.Instance != "hal0" {
		t.Errorf("Instance = %q, want %q", api.Instance, "hal0")
	}
	if api.Meta.Name != "hal" {
		t.Errorf("Meta.Name = %q, want %q", api.Meta.Name, "hal")
	}
	if api.Callbacks != fakeCallbacks {
		t.Error("Callbacks pointer mismatch")
	}
}

func TestRegisterDuplicate(t *testing.T) {
	r := NewRegistry()

	meta := testMeta("hal", 1, true)
	if err := r.Register(meta, "hal0", fakeCallbacks); err != nil {
		t.Fatalf("first Register: %v", err)
	}

	err := r.Register(meta, "hal0", fakeCallbacks)
	if err != syscall.EEXIST {
		t.Errorf("duplicate Register: got %v, want EEXIST", err)
	}
}

func TestRegisterInvalidArgs(t *testing.T) {
	r := NewRegistry()

	if err := r.Register(nil, "x", fakeCallbacks); err != syscall.EINVAL {
		t.Errorf("nil meta: got %v, want EINVAL", err)
	}
	if err := r.Register(testMeta("x", 1, false), "", fakeCallbacks); err != syscall.EINVAL {
		t.Errorf("empty instance: got %v, want EINVAL", err)
	}
}

func TestGetAPIVersionMatch(t *testing.T) {
	r := NewRegistry()
	meta := testMeta("hal", 2, true)
	r.Register(meta, "hal0", fakeCallbacks)

	cb, err := r.GetAPI("hal0", 2)
	if err != nil {
		t.Fatalf("GetAPI: %v", err)
	}
	if cb != fakeCallbacks {
		t.Error("callbacks pointer mismatch")
	}
}

func TestGetAPIVersionMismatch(t *testing.T) {
	r := NewRegistry()
	meta := testMeta("hal", 2, true)
	r.Register(meta, "hal0", fakeCallbacks)

	_, err := r.GetAPI("hal0", 1)
	if err != syscall.EINVAL {
		t.Errorf("version mismatch: got %v, want EINVAL", err)
	}
}

func TestGetAPINotFound(t *testing.T) {
	r := NewRegistry()

	_, err := r.GetAPI("nonexistent", 1)
	if err != syscall.ENOENT {
		t.Errorf("not found: got %v, want ENOENT", err)
	}
}

func TestGetNotFound(t *testing.T) {
	r := NewRegistry()

	if api := r.Get("nonexistent"); api != nil {
		t.Errorf("Get: got %v, want nil", api)
	}
}

func TestInstances(t *testing.T) {
	r := NewRegistry()
	r.Register(testMeta("hal", 1, true), "hal0", fakeCallbacks)
	r.Register(testMeta("halcmd", 1, true), "halcmd0", fakeCallbacks)

	names := r.Instances()
	if len(names) != 2 {
		t.Fatalf("Instances: got %d, want 2", len(names))
	}

	found := map[string]bool{}
	for _, n := range names {
		found[n] = true
	}
	if !found["hal0"] || !found["halcmd0"] {
		t.Errorf("Instances = %v, want hal0 + halcmd0", names)
	}
}

func TestMultipleAPIs(t *testing.T) {
	r := NewRegistry()
	r.Register(testMeta("hal", 1, true), "hal0", fakeCallbacks)
	r.Register(testMeta("halcmd", 1, true), "halcmd0", fakeCallbacks)

	if r.Get("hal0") == nil {
		t.Error("hal0 not found")
	}
	if r.Get("halcmd0") == nil {
		t.Error("halcmd0 not found")
	}
}
