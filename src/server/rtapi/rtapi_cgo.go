// src/server/rtapi/rtapi_cgo.go
//
// RTAPI integration package.
// In production this uses cgo to call into the RTAPI shared library.
// The build tags below select between the real cgo implementation and a
// stub for environments where the LinuxCNC C libraries are not available.

//go:build ignore
// +build ignore

package rtapi

// This file is intentionally excluded from the build (build tag "ignore").
// It documents the cgo-based production implementation.
// See rtapi_stub.go for the stub used when C libraries are unavailable.
