package hal

// This file serves as the main entry point for the hal package.
// It re-exports the key types and functions for convenience.

// Version is the version of the hal-go package.
const Version = "0.1.0-phase1"

// HAL constant definitions from hal.h
const (
	// NameLen is the maximum length for HAL names (pins, signals, components).
	// This matches HAL_NAME_LEN from hal.h.
	NameLen = 47
)

// Phase1Note contains information about the current implementation phase.
const Phase1Note = `
This is Phase 1 (Survey & Design) of the Golang HAL implementation.
Current features:
  - Complete API structure defined
  - Stub implementations of all functions
  - Type-safe generic Pin[T] API
  - Error handling infrastructure
  
The package compiles but does not yet interface with the HAL C library.
Phase 2+ will add CGO bindings for actual HAL integration.
`
