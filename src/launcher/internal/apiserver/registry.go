package apiserver

import (
	"sync"
	"syscall"
	"unsafe"
)

// Registry stores registered API instances. Thread-safe for concurrent reads
// after startup. Writes (Register) happen during module init only.
type Registry struct {
	mu        sync.RWMutex
	instances map[string]*RegisteredAPI
}

// NewRegistry creates an empty registry.
func NewRegistry() *Registry {
	return &Registry{
		instances: make(map[string]*RegisteredAPI),
	}
}

// Register adds an API instance. Returns EEXIST if instance name is taken.
func (r *Registry) Register(meta *APIMeta, instance string, callbacks unsafe.Pointer) error {
	if meta == nil || instance == "" {
		return syscall.EINVAL
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	if _, exists := r.instances[instance]; exists {
		return syscall.EEXIST
	}

	r.instances[instance] = &RegisteredAPI{
		Meta:      meta,
		Instance:  instance,
		Callbacks: callbacks,
	}
	return nil
}

// GetAPI returns the callbacks pointer for direct inter-module calls.
// Returns ENOENT if not found, EINVAL on version mismatch.
func (r *Registry) GetAPI(instance string, requiredVersion int) (unsafe.Pointer, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	api := r.instances[instance]
	if api == nil {
		return nil, syscall.ENOENT
	}
	if api.Meta.Version != requiredVersion {
		return nil, syscall.EINVAL
	}
	return api.Callbacks, nil
}

// Get returns the full RegisteredAPI or nil if not found.
func (r *Registry) Get(instance string) *RegisteredAPI {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.instances[instance]
}

// Instances returns all registered instance names.
func (r *Registry) Instances() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	names := make([]string, 0, len(r.instances))
	for name := range r.instances {
		names = append(names, name)
	}
	return names
}

// defaultRegistry is the package-level registry used by cgo-exported register
// functions. Set by the launcher before loading any modules.
var defaultRegistry *Registry

// SetDefaultRegistry sets the package-level registry. Must be called before
// any cmod calls Register via cgo export.
func SetDefaultRegistry(r *Registry) {
	defaultRegistry = r
}

// DefaultRegistry returns the package-level registry.
func DefaultRegistry() *Registry {
	return defaultRegistry
}
