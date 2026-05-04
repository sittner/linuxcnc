// Package halrest implements the server-side halcmd REST API handler.
// It registers with the apiserver and dispatches REST calls to the
// launcher's internal halcmd package (no liblinuxcnchal.so dependency).
package halrest

import (
	"encoding/json"
	"time"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/halcmdapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/internal/halcmd"
)

// LoadModuleFunc is the callback signature for dynamically loading a
// cmod plugin at runtime.  The launcher sets this via SetLoadModuleFunc.
type LoadModuleFunc func(module string, args []string) error

var loadModuleHook LoadModuleFunc

// SetLoadModuleFunc sets the callback used by the "load" command to
// dynamically load a cmod .so into gomc-server.
func SetLoadModuleFunc(fn LoadModuleFunc) {
	loadModuleHook = fn
}

// Register registers the halcmd REST API with the given registry.
// This makes the /api/v1/halcmd/* endpoints available.
func Register(reg *apiserver.Registry) error {
	apiserver.RegisterMeta(halcmdapi.HalcmdMeta)
	return halcmdapi.RegisterHalcmdAPI(reg, "halcmd", &halcmdImpl{})
}

// ─── Watch support ───

// RegisterWatch registers the halcmd watch API with the given watch registry.
// This enables WebSocket clients to subscribe to live HAL pin/signal values.
// The interval parameter sets the default push rate; 0 uses the default (100ms).
// Configurable via [HAL]WATCH_INTERVAL in the INI file.
func RegisterWatch(wreg *apiserver.WatchRegistry, interval time.Duration) {
	if interval <= 0 {
		interval = 100 * time.Millisecond
	}
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "halcmd",
		Instance: "halcmd",
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "watch_items",
				DefaultRate: interval,
				Watch:       watchItems,
			},
		},
	})
}

// watchItems polls all pins and returns their current values as JSON.
// TODO: support per-subscription name filtering via watch args.
func watchItems() (json.RawMessage, error) {
	result, err := halcmd.Show("pin")
	if err != nil {
		return nil, err
	}

	out := make([]halcmdapi.PinInfo, 0, len(result.Pins))
	for _, p := range result.Pins {
		pi := halcmdapi.PinInfo{
			Name:   p.Name,
			Type:   p.Type,
			Dir:    p.Direction,
			Value:  p.Value,
			Owner:  p.Owner,
			Linked: p.Signal != "",
		}
		if p.Signal != "" {
			pi.Signal = p.Signal
		}
		out = append(out, pi)
	}
	return json.Marshal(out)
}
