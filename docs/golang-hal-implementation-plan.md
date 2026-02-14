# Implementation Plan: Golang Userspace HAL Components for LinuxCNC

**Author:** sittner  
**Date:** 2026-02-14  
**Status:** Draft  
**Repository:** sittner/linuxcnc

---

## 1. Executive Summary

This document outlines the implementation plan for enabling userspace LinuxCNC HAL (Hardware Abstraction Layer) components to be written in Go (Golang). The recommended approach uses CGO bindings to wrap the existing C HAL library, following the proven pattern established by the Python HAL bindings.

**Estimated Timeline:** 7 weeks  
**Feasibility:** High  
**Risk Level:** Low-Medium

---

## 2. Objective

Enable the creation of userspace LinuxCNC HAL components in Go, allowing Go programs to:
- Register as HAL components
- Export and manage HAL pins and parameters
- Communicate with other HAL components via shared memory
- Integrate fully into the LinuxCNC ecosystem

---

## 3. Background & Rationale

### 3.1 Current State

LinuxCNC currently supports userspace HAL components in:
- **C/C++** - Native implementation
- **Python** - Via `halmodule.cc` C extension module

### 3.2 Why Golang?

| Benefit | Description |
|---------|-------------|
| **Type Safety** | Strong static typing catches errors at compile time |
| **Concurrency** | Goroutines and channels simplify concurrent I/O operations |
| **Standard Library** | Rich networking, file I/O, and protocol support |
| **Single Binary** | Simplified deployment without runtime dependencies |
| **Growing Ecosystem** | Large community and industrial adoption |

### 3.3 Use Cases

- Network protocol bridges (Modbus TCP, OPC-UA, MQTT)
- Custom UI backends
- Data logging and analytics components
- Integration with cloud services
- Complex state machine implementations

---

## 4. Technical Architecture

### 4.1 HAL Core Concepts

```
┌─────────────────────────────────────────────────────────────┐
│                    HAL Shared Memory                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │Component │  │Component │  │Component │  │Component │    │
│  │  (RT)    │  │  (User)  │  │ (Python) │  │  (Go)    │    │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘    │
│       │             │             │             │           │
│       └─────────────┴─────────────┴─────────────┘           │
│                         │                                    │
│                    HAL Signals                               │
│                    (Pins connected)                          │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Key HAL API Functions

| Function | Purpose |
|----------|---------|
| `hal_init(name)` | Initialize component, returns component ID |
| `hal_exit(comp_id)` | Clean up and unregister component |
| `hal_ready(comp_id)` | Mark component as ready for operation |
| `hal_malloc(size)` | Allocate memory in HAL shared memory |
| `hal_pin_*_new()` | Create pins of various types |
| `hal_param_*_new()` | Create parameters of various types |

### 4.3 Supported Data Types

| HAL Type | C Type | Go Type |
|----------|--------|---------|
| `HAL_BIT` | `hal_bit_t` | `bool` |
| `HAL_FLOAT` | `hal_float_t` (double) | `float64` |
| `HAL_S32` | `hal_s32_t` | `int32` |
| `HAL_U32` | `hal_u32_t` | `uint32` |
| `HAL_S64` | `hal_s64_t` | `int64` |
| `HAL_U64` | `hal_u64_t` | `uint64` |

### 4.4 Pin Directions

| Direction | Constant | Description |
|-----------|----------|-------------|
| Input | `HAL_IN` | Component reads value |
| Output | `HAL_OUT` | Component writes value |
| I/O | `HAL_IO` | Bidirectional |

---

## 5. Implementation Approach

### 5.1 Chosen Strategy: CGO Bindings

After evaluating multiple approaches, **CGO bindings** are recommended:

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| **CGO Bindings** | Direct API access, proven pattern, minimal overhead | Requires CGO, build complexity | ✅ **Selected** |
| Pure Go + mmap | No CGO, easier cross-compile | Complex, fragile, maintenance burden | ❌ Rejected |
| IPC Bridge | Clean separation | Latency, extra process | ❌ Rejected |

### 5.2 Package Structure

```
hal-go/
├── hal.go              # Main package, idiomatic Go API
├── hal_cgo.go          # CGO bindings to C library
├── component.go        # Component type and lifecycle
├── pin.go              # Pin types and operations
├── param.go            # Parameter types and operations
├── types.go            # Type definitions and constants
├── errors.go           # Error handling
├── signal.go           # Signal handling for shutdown
├── examples/
│   ├── simple/         # Basic example
│   ├── modbus/         # Network protocol example
│   └── counter/        # Stateful component example
├── doc.go              # Package documentation
├── go.mod
├── go.sum
├── Makefile
└── README.md
```

---

## 6. API Design

### 6.1 Core Types

```go
package hal

// Direction represents pin direction
type Direction int

const (
    In  Direction = iota // HAL_IN
    Out                  // HAL_OUT
    IO                   // HAL_IO
)

// PinType represents the data type of a pin
type PinType int

const (
    TypeBit   PinType = iota // HAL_BIT
    TypeFloat                // HAL_FLOAT
    TypeS32                  // HAL_S32
    TypeU32                  // HAL_U32
    TypeS64                  // HAL_S64
    TypeU64                  // HAL_U64
)

// Component represents a HAL component
type Component struct {
    id      int
    name    string
    ready   bool
    running bool
}

// Pin represents a generic HAL pin
type Pin[T any] struct {
    name      string
    direction Direction
    ptr       unsafe.Pointer
}
```

### 6.2 Component Lifecycle

```go
// NewComponent creates and initializes a new HAL component
func NewComponent(name string) (*Component, error)

// Ready marks the component as ready for operation
func (c *Component) Ready() error

// Running returns true while the component should continue running
func (c *Component) Running() bool

// Exit cleans up the component
func (c *Component) Exit() error
```

### 6.3 Pin Creation (Generic API)

```go
// NewPin creates a new pin with the specified type
func NewPin[T PinValue](c *Component, name string, dir Direction) (*Pin[T], error)

// PinValue constraint for supported pin types
type PinValue interface {
    bool | float64 | int32 | uint32 | int64 | uint64
}

// Get reads the current pin value
func (p *Pin[T]) Get() T

// Set writes a value to the pin
func (p *Pin[T]) Set(value T)
```

### 6.4 Example Usage

```go
package main

import (
    "log"
    "time"

    "github.com/linuxcnc/hal-go"
)

func main() {
    // Initialize component
    comp, err := hal.NewComponent("go-example")
    if err != nil {
        log.Fatal(err)
    }
    defer comp.Exit()

    // Create pins
    input, _ := hal.NewPin[float64](comp, "input", hal.In)
    output, _ := hal.NewPin[float64](comp, "output", hal.Out)
    enable, _ := hal.NewPin[bool](comp, "enable", hal.In)
    counter, _ := hal.NewPin[int32](comp, "counter", hal.Out)

    // Mark component ready
    if err := comp.Ready(); err != nil {
        log.Fatal(err)
    }

    log.Println("Component ready, entering main loop")

    // Main loop
    var count int32
    for comp.Running() {
        if enable.Get() {
            output.Set(input.Get() * 2.0)
            count++
            counter.Set(count)
        }
        time.Sleep(10 * time.Millisecond)
    }

    log.Println("Component shutting down")
}
```

---

## 7. CGO Implementation Details

### 7.1 CGO Header

```go
package hal

/*
#cgo CFLAGS: -I/usr/include/linuxcnc
#cgo LDFLAGS: -llinuxcnchal -lrtapi

#include <stdlib.h>
#include <signal.h>
#include "hal.h"
#include "rtapi.h"
*/
import "C"

import (
    "fmt"
    "unsafe"
)
```

### 7.2 Key Function Bindings

```go
// hal_init wrapper
func halInit(name string) (int, error) {
    cname := C.CString(name)
    defer C.free(unsafe.Pointer(cname))
    
    id := int(C.hal_init(cname))
    if id < 0 {
        return 0, fmt.Errorf("hal_init failed: %d", id)
    }
    return id, nil
}

// hal_pin_float_new wrapper
func halPinFloatNew(name string, dir Direction, compID int) (*float64, error) {
    cname := C.CString(name)
    defer C.free(unsafe.Pointer(cname))
    
    var ptr *C.hal_float_t
    result := C.hal_pin_float_new(cname, C.hal_pin_dir_t(dir), &ptr, C.int(compID))
    if result != 0 {
        return nil, fmt.Errorf("hal_pin_float_new failed: %d", result)
    }
    return (*float64)(unsafe.Pointer(ptr)), nil
}
```

### 7.3 Signal Handling

```go
package hal

import (
    "os"
    "os/signal"
    "syscall"
)

func (c *Component) setupSignalHandler() {
    sigChan := make(chan os.Signal, 1)
    signal.Notify(sigChan, syscall.SIGTERM, syscall.SIGINT)
    
    go func() {
        <-sigChan
        c.running = false
    }()
}
```

---

## 8. Build System Integration

### 8.1 Makefile

```makefile
# hal-go Makefile

LINUXCNC_DIR ?= /usr/include/linuxcnc
LINUXCNC_LIB ?= /usr/lib/linuxcnc

.PHONY: all build test clean install

all: build

build:
	CGO_CFLAGS="-I$(LINUXCNC_DIR)" \
	CGO_LDFLAGS="-L$(LINUXCNC_LIB) -llinuxcnchal" \
	go build -v ./...

test:
	CGO_CFLAGS="-I$(LINUXCNC_DIR)" \
	CGO_LDFLAGS="-L$(LINUXCNC_LIB) -llinuxcnchal" \
	go test -v ./...

examples: build
	go build -o bin/simple ./examples/simple
	go build -o bin/counter ./examples/counter

clean:
	rm -rf bin/
	go clean

install:
	go install ./...
```

### 8.2 go.mod

```
module github.com/linuxcnc/hal-go

go 1.21

require (
    // No external dependencies for core package
)
```

---

## 9. Testing Strategy

### 9.1 Unit Tests

```go
package hal

import "testing"

func TestComponentInit(t *testing.T) {
    comp, err := NewComponent("test-comp")
    if err != nil {
        t.Fatalf("Failed to create component: %v", err)
    }
    defer comp.Exit()
    
    if comp.name != "test-comp" {
        t.Errorf("Expected name 'test-comp', got '%s'", comp.name)
    }
}

func TestPinCreation(t *testing.T) {
    comp, _ := NewComponent("test-pins")
    defer comp.Exit()
    
    pin, err := NewPin[float64](comp, "test-pin", Out)
    if err != nil {
        t.Fatalf("Failed to create pin: %v", err)
    }
    
    pin.Set(42.0)
    if pin.Get() != 42.0 {
        t.Errorf("Expected 42.0, got %f", pin.Get())
    }
}
```

### 9.2 Integration Tests

- Test with `halrun` and `halcmd`
- Verify pin connectivity with other components
- Test signal handling and graceful shutdown
- Performance benchmarks

---

## 10. Timeline & Milestones

```
Week 1    ┃ Survey & Design
          ┃ ├── Document HAL API requirements
          ┃ ├── Finalize Go API design
          ┃ └── Set up project structure
          ┃
Week 2-3  ┃ CGO Wrapper Development
          ┃ ├── Implement core bindings
          ┃ ├── Component lifecycle
          ┃ ├── Pin creation for all types
          ┃ └── Initial unit tests
          ┃
Week 4    ┃ Idiomatic Go API
          ┃ ├── Generic pin API
          ┃ ├── Error handling
          ┃ └── Minimal working example
          ┃
Week 5    ┃ Signal & Shutdown Support
          ┃ ├── SIGTERM/SIGINT handling
          ┃ ├── Graceful cleanup
          ┃ └── Full component lifecycle
          ┃
Week 6    ┃ Testing & Validation
          ┃ ├── Unit test suite
          ┃ ├── Integration with halcmd
          ┃ ├── Real hardware testing
          ┃ └── Performance benchmarks
          ┃
Week 7    ┃ Documentation & Release
          ┃ ├── GoDoc documentation
          ┃ ├── Usage guide
          ┃ ├── Example components
          ┃ └── Build system integration
```

---

## 11. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| HAL ABI changes break bindings | Low | High | Version-specific builds, CI testing |
| Go GC causes latency issues | Medium | Low | Document as userspace-only, provide tuning guidance |
| Build complexity for end users | Medium | Medium | Provide pre-built packages, detailed docs |
| Memory safety issues in CGO | Low | High | Thorough testing, code review, static analysis |

---

## 12. Future Enhancements

- **Parameter support** - `hal_param_*` functions
- **Component instantiation** - Multiple instances of same component
- **Streaming** - HAL stream support for high-bandwidth data
- **Code generation** - `halcompile` equivalent for Go
- **gRPC/Protobuf** - Modern IPC for complex integrations

---

## 13. References

### LinuxCNC Documentation
- [HAL Introduction](https://github.com/sittner/linuxcnc/blob/master/docs/src/hal/intro.adoc)
- [HAL C API Header](https://github.com/sittner/linuxcnc/blob/master/src/hal/hal.h)
- [Python HAL Module](https://github.com/sittner/linuxcnc/blob/master/lib/python/hal.py)
- [HAL Module C Extension](https://github.com/sittner/linuxcnc/blob/master/src/hal/halmodule.cc)

### Go Documentation
- [CGO Documentation](https://golang.org/cmd/cgo/)
- [Go Generics](https://go.dev/doc/tutorial/generics)
- [unsafe Package](https://pkg.go.dev/unsafe)

---

## 14. Approval & Sign-off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Author | | 2026-02-14 | |
| Technical Review | | | |
| Project Approval | | | |

---

*Document Version: 1.0*  
*Last Updated: 2026-02-14*