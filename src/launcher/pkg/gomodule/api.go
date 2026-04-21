package gomodule

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

// Host is the interface that the launcher provides to Go plugins for
// interacting with the API registry. Plugins receive a Host via the
// Factory function and use it to register their API metadata and
// instances — without importing the apiserver package (which drags
// in net/http, crypto/tls, etc.).
type Host interface {
	// RegisterMeta registers API metadata (types, routes, dispatch functions)
	// for later pairing with concrete instances. Typically called once per API.
	RegisterMeta(meta *APIMeta)

	// Register registers a concrete API instance with callback pointer.
	// Returns error if the api:instance pair already exists.
	Register(apiName string, version int, instance string, callbacks unsafe.Pointer) error
}
