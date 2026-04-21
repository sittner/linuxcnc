// Package apiserver implements the dynamic API registry and HTTP server
// for LinuxCNC's inter-module communication system.
package apiserver

import (
	"unsafe"

	"github.com/sittner/linuxcnc/src/launcher/pkg/gomodule"
)

// Type aliases re-exported from gomodule so existing code importing
// apiserver continues to compile unchanged.  The canonical definitions
// live in pkg/gomodule so that Go plugins can use them without pulling
// in the heavy net/http dependency of this package.
type DispatchFunc = gomodule.DispatchFunc
type FuncMeta = gomodule.FuncMeta
type APIMeta = gomodule.APIMeta

// RegisteredAPI is one registered API instance in the registry.
type RegisteredAPI struct {
	APIName   string         // "tp" — API name from registration
	Version   int            // API version from registration
	Meta      *APIMeta       // optional — REST routing/dispatch (nil for pure C-to-C)
	Instance  string         // "default" — unique instance name within an API
	Callbacks unsafe.Pointer // opaque — *tp_callbacks_t (cmod) or Go interface
}
