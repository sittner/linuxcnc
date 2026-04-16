// Package apiserver implements the dynamic API registry and HTTP server
// for LinuxCNC's inter-module communication system.
package apiserver

import "unsafe"

// DispatchFunc is the uniform signature for all generated dispatch wrappers.
// Both cmod and gomod generate functions with this signature.
// The HTTP server calls these — it never touches callbacks directly.
type DispatchFunc func(callbacks unsafe.Pointer, req []byte) ([]byte, error)

// FuncMeta holds static metadata + dispatch for one API function (generated).
// Routing info and dispatch wrapper live together — no parallel arrays.
type FuncMeta struct {
	Name     string // "pin_read"
	Method   string // "GET", "POST", etc. (empty if not REST-exported)
	Path     string // "/pin/{name}" (empty if not REST-exported)
	RTSafe   bool
	Dispatch DispatchFunc // generated wrapper (nil if not REST-exported)
}

// APIMeta holds static metadata for an entire API (generated, read-only).
type APIMeta struct {
	Name       string // "hal"
	Version    int
	RESTExport bool
	Prefix     string     // REST path prefix
	Funcs      []FuncMeta // routing + dispatch in one place
}

// RegisteredAPI is one registered API instance in the registry.
type RegisteredAPI struct {
	Meta      *APIMeta       // generated — routing, dispatch, metadata
	Instance  string         // "hal0" — unique instance name
	Callbacks unsafe.Pointer // opaque — *hal_callbacks_t (cmod) or Go interface
}
