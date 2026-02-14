# hal-go - Go Bindings for LinuxCNC HAL

[![Go Version](https://img.shields.io/badge/go-1.21+-blue.svg)](https://golang.org/dl/)
[![Phase](https://img.shields.io/badge/phase-1%20(design)-yellow.svg)](../../docs/golang-hal-implementation-tracking.md)

Go bindings for LinuxCNC's Hardware Abstraction Layer (HAL), enabling userspace HAL components to be written in Go.

## Overview

HAL (Hardware Abstraction Layer) is the core communication mechanism in LinuxCNC. This package allows Go programs to:

- Register as HAL components
- Export pins that can be connected to signals
- Communicate with other HAL components via shared memory
- Integrate seamlessly into the LinuxCNC ecosystem

## Project Status

**Phase 1: Survey & Design** ✅ (Current)

This phase provides the complete API structure with stub implementations. The package compiles and documents the intended API, but does not yet interface with the HAL C library.

Future phases will add:
- **Phase 2**: CGO bindings to HAL C library
- **Phase 3**: Idiomatic Go API refinements  
- **Phase 4**: Signal handling and graceful shutdown
- **Phase 5**: Testing and validation
- **Phase 6**: Documentation and examples

See [golang-hal-implementation-tracking.md](../../docs/golang-hal-implementation-tracking.md) for details.

## Installation

### Prerequisites

- Go 1.21 or later
- LinuxCNC development headers (for Phase 2+)

### Install Package

```bash
cd src/hal/hal-go
go mod download
```

## Quick Start

Here's a simple example of a HAL component that doubles an input value:

```go
package main

import (
    "log"
    "time"

    "github.com/linuxcnc/hal-go"
)

func main() {
    // Create component
    comp, err := hal.NewComponent("doubler")
    if err != nil {
        log.Fatal(err)
    }
    defer comp.Exit()

    // Create pins
    input, _ := hal.NewPin[float64](comp, "input", hal.In)
    output, _ := hal.NewPin[float64](comp, "output", hal.Out)

    // Mark component ready
    if err := comp.Ready(); err != nil {
        log.Fatal(err)
    }

    log.Println("Doubler component ready")

    // Main loop
    for comp.Running() {
        output.Set(input.Get() * 2.0)
        time.Sleep(10 * time.Millisecond)
    }

    log.Println("Component shutting down")
}
```

## API Overview

### Component Lifecycle

```go
// Create component
comp, err := hal.NewComponent("mycomp")
defer comp.Exit()

// Create pins (must be done before Ready())
pin1, _ := hal.NewPin[float64](comp, "pin1", hal.In)
pin2, _ := hal.NewPin[bool](comp, "pin2", hal.Out)

// Mark component ready
comp.Ready()

// Main loop
for comp.Running() {
    // ... do work ...
}
```

### Pin Types

The package supports all HAL data types through Go's generics:

| Go Type   | HAL Type   | Description              |
|-----------|------------|--------------------------|
| `bool`    | `HAL_BIT`  | Boolean value            |
| `float64` | `HAL_FLOAT`| 64-bit floating point    |
| `int32`   | `HAL_S32`  | Signed 32-bit integer    |
| `uint32`  | `HAL_U32`  | Unsigned 32-bit integer  |

### Pin Directions

| Direction | Description                          |
|-----------|--------------------------------------|
| `hal.In`  | Input pin (component reads)          |
| `hal.Out` | Output pin (component writes)        |
| `hal.IO`  | Bidirectional pin (read and write)   |

### Creating Pins

```go
// Type-safe pin creation with generics
boolPin, _   := hal.NewPin[bool](comp, "enable", hal.In)
floatPin, _  := hal.NewPin[float64](comp, "speed", hal.Out)
int32Pin, _  := hal.NewPin[int32](comp, "count", hal.IO)
uint32Pin, _ := hal.NewPin[uint32](comp, "state", hal.Out)
```

### Reading and Writing Pins

```go
// Read from input pin
value := inputPin.Get()

// Write to output pin
outputPin.Set(42.0)

// Bidirectional pin
current := ioPin.Get()
ioPin.Set(current + 1)
```

## Integration with LinuxCNC

Once Phase 2+ is complete, HAL components written in Go will integrate fully with LinuxCNC:

```bash
# Load the component
halcmd loadusr -W go-doubler

# View component info
halcmd show comp doubler

# View pins
halcmd show pin doubler

# Connect pins to signals
halcmd net speed-in doubler.input <= some-other.output
halcmd net speed-out doubler.output => another-comp.input

# Start HAL
halcmd start
```

## Build Requirements

### Phase 1 (Current)
- Go 1.21+
- No CGO required
- No LinuxCNC installation needed

### Phase 2+ (Future)
- Go 1.21+
- CGO enabled (`CGO_ENABLED=1`)
- LinuxCNC development headers
- LinuxCNC HAL library (`liblinuxcnchal`)

## Documentation

- [Package Documentation](https://pkg.go.dev/github.com/linuxcnc/hal-go) (Phase 2+)
- [Implementation Plan](../../docs/golang-hal-implementation-plan.md)
- [Progress Tracking](../../docs/golang-hal-implementation-tracking.md)
- [LinuxCNC HAL Docs](https://linuxcnc.org/docs/html/hal/intro.html)

## Examples

Examples will be added in Phase 6. Planned examples include:

- `simple/` - Basic input/output component
- `counter/` - Stateful component with internal state
- `modbus/` - Network protocol bridge (stretch goal)

## Contributing

This is an active development project. See the [Implementation Plan](../../docs/golang-hal-implementation-plan.md) for the roadmap and [Progress Tracking](../../docs/golang-hal-implementation-tracking.md) for current status.

## License

This code is part of LinuxCNC and is licensed under the GNU Lesser General Public License (LGPL) version 2 or later.

## References

- [LinuxCNC](https://linuxcnc.org/)
- [HAL Introduction](https://linuxcnc.org/docs/html/hal/intro.html)
- [HAL C API](../hal.h)
- [Python HAL Bindings](../../../lib/python/hal.py)

## Acknowledgments

This implementation follows the design patterns established by the Python HAL bindings (`halmodule.cc`) and is inspired by the successful integration of multiple languages in the LinuxCNC ecosystem.

---

**Status**: Phase 1 (Survey & Design) - API defined with stub implementations  
**Next**: Phase 2 - CGO wrapper development  
**Last Updated**: 2026-02-14
