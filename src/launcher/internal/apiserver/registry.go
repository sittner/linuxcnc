package apiserver

import (
	"fmt"
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

// registryKey returns a composite key for the registry map.
func registryKey(apiName, instance string) string {
	return apiName + ":" + instance
}

// Register adds an API instance.  Only apiName, version, instance, and
// callbacks are required — all supplied by the C module at runtime.
// If an APIMeta with matching name+version was registered (e.g. via a
// generated Go package init()), it is automatically attached for REST
// dispatch.  Returns EEXIST if the api:instance pair is taken.
func (r *Registry) Register(apiName string, version int, instance string, callbacks unsafe.Pointer) error {
	if apiName == "" || instance == "" {
		return syscall.EINVAL
	}

	key := registryKey(apiName, instance)

	r.mu.Lock()
	defer r.mu.Unlock()

	if _, exists := r.instances[key]; exists {
		return syscall.EEXIST
	}

	// Attach REST metadata if available (optional — nil is fine).
	meta := GetMeta(apiName, version)

	r.instances[key] = &RegisteredAPI{
		APIName:   apiName,
		Version:   version,
		Meta:      meta,
		Instance:  instance,
		Callbacks: callbacks,
	}
	return nil
}

// GetAPI returns the callbacks pointer for direct inter-module calls.
// Returns ENOENT if not found, EINVAL on version mismatch.
func (r *Registry) GetAPI(apiName string, instance string, requiredVersion int) (unsafe.Pointer, error) {
	key := registryKey(apiName, instance)

	r.mu.RLock()
	defer r.mu.RUnlock()

	api := r.instances[key]
	if api == nil {
		return nil, syscall.ENOENT
	}
	if api.Version != requiredVersion {
		return nil, syscall.EINVAL
	}
	return api.Callbacks, nil
}

// Get returns the full RegisteredAPI matching the given instance name, or nil
// if not found.  This performs a linear scan because the internal map is keyed
// by api:instance.  Used by the REST server where the URL path contains only
// the instance name and the map is small.
func (r *Registry) Get(instance string) *RegisteredAPI {
	r.mu.RLock()
	defer r.mu.RUnlock()
	for _, api := range r.instances {
		if api.Instance == instance {
			return api
		}
	}
	return nil
}

// Instances returns all registered instance names (without the api: prefix).
func (r *Registry) Instances() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	names := make([]string, 0, len(r.instances))
	for _, api := range r.instances {
		names = append(names, api.Instance)
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

// ─── Meta Registry ───
//
// APIMeta objects are registered by generated cgo packages at init() time.
// When a C plugin calls env->api->register_api("kins", 1, ...), the generic
// callback looks up the meta here to pair it with the callbacks.

var metaRegistry = map[string]*APIMeta{}

// metaKey returns the lookup key for the meta registry.
func metaKey(name string, version int) string {
	return name + ":" + fmt.Sprintf("%d", version)
}

// RegisterMeta registers an APIMeta for later use by the generic API callbacks.
// Called from generated cgo packages' init() functions.
func RegisterMeta(meta *APIMeta) {
	metaRegistry[metaKey(meta.Name, meta.Version)] = meta
}

// GetMeta looks up a registered APIMeta by name and version.
func GetMeta(name string, version int) *APIMeta {
	return metaRegistry[metaKey(name, version)]
}
