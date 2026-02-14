package hal

import (
	"fmt"
	"sync"
)

// Component represents a HAL component.
// A component is the basic unit of HAL functionality. It can export pins
// and parameters that other components can connect to via signals.
type Component struct {
	// id is the HAL component ID assigned by hal_init().
	id int

	// name is the unique name of the component.
	name string

	// ready indicates whether the component has been marked ready.
	ready bool

	// running indicates whether the component should continue running.
	running bool

	// mu protects the component state.
	mu sync.RWMutex
}

// NewComponent creates and initializes a new HAL component.
//
// The name must be unique across all HAL components in the system and
// must not exceed 47 characters (HAL_NAME_LEN).
//
// In Phase 1, this is a stub implementation. Phase 2+ will add the actual
// CGO call to hal_init().
//
// Returns the component on success, or an error if initialization fails.
func NewComponent(name string) (*Component, error) {
	if name == "" || len(name) > 47 {
		return nil, newError("NewComponent", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	// Phase 1 stub: Just create the component structure
	// Phase 2+ will call: id := C.hal_init(C.CString(name))
	comp := &Component{
		id:      1, // Stub value; real ID comes from hal_init()
		name:    name,
		ready:   false,
		running: true,
	}

	return comp, nil
}

// Ready marks the component as ready for operation.
//
// This must be called after all pins and parameters have been created,
// but before the component enters its main loop. It allows halcmd's
// 'loadusr -W' to wait until the component is ready.
//
// In Phase 1, this is a stub implementation. Phase 2+ will add the actual
// CGO call to hal_ready().
func (c *Component) Ready() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.ready {
		return newError("Ready", ErrAlreadyReady.Message, ErrAlreadyReady.Code)
	}

	// Phase 1 stub: Just set the flag
	// Phase 2+ will call: ret := C.hal_ready(C.int(c.id))
	c.ready = true

	return nil
}

// Running returns true while the component should continue running.
//
// The main loop should check this periodically and exit cleanly when
// it returns false. This will be set to false when a shutdown signal
// (SIGTERM or SIGINT) is received.
func (c *Component) Running() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.running
}

// Exit cleans up the component and releases all HAL resources.
//
// This should be called (typically via defer) when the component is
// shutting down. It unregisters the component and removes all pins
// and parameters.
//
// In Phase 1, this is a stub implementation. Phase 2+ will add the actual
// CGO call to hal_exit().
func (c *Component) Exit() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// Phase 1 stub: Just mark as not running
	// Phase 2+ will call: ret := C.hal_exit(C.int(c.id))
	c.running = false

	return nil
}

// Name returns the component name.
func (c *Component) Name() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.name
}

// ID returns the HAL component ID.
//
// This ID is used internally for HAL API calls and is assigned by
// hal_init().
func (c *Component) ID() int {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.id
}

// IsReady returns true if the component has been marked ready.
func (c *Component) IsReady() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.ready
}

// Stop signals the component to stop running.
//
// This sets the running flag to false, which will cause Running() to
// return false and the main loop to exit. This is typically called
// by signal handlers.
func (c *Component) Stop() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.running = false
}

// String returns a string representation of the component.
func (c *Component) String() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return fmt.Sprintf("Component{name=%s, id=%d, ready=%t, running=%t}",
		c.name, c.id, c.ready, c.running)
}
