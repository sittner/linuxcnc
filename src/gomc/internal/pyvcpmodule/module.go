// Package pyvcpmodule registers the PyVCP panel module with the gomc module
// registry. When compiled into the gomc-server binary, this package's init()
// function registers a factory that creates PyVCP panel instances in response
// to HAL "load pyvcp" commands.
//
// Each instance parses a PyVCP XML file, extracts widget pin definitions,
// creates a HAL component with the required pins, and provides REST + WebSocket
// endpoints for the Python frontend to display/control the panel.
//
// Usage in a HAL file:
//
//	load pyvcp [mypanel] xml=panel.xml
//
// Parameters:
//   - xml=<path>  — path to the PyVCP XML file (required; resolved relative
//     to the INI file directory if not absolute)
package pyvcpmodule

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/hal"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func init() {
	gomc.RegisterModule("pyvcp", newPyVCPModule)

	// Register REST meta so the HTTP server knows about pyvcp routes.
	apiserver.RegisterMeta(&apiserver.APIMeta{
		Name:       "pyvcp",
		Version:    1,
		RESTExport: true,
		Prefix:     "pyvcp",
		Funcs: []apiserver.FuncMeta{
			{
				Name:     "list_panels",
				Method:   "GET",
				Path:     "/panels",
				Dispatch: dispatchListPanels,
			},
			{
				Name:     "get_panel",
				Method:   "GET",
				Path:     "/panel/{name}",
				Dispatch: dispatchGetPanel,
			},
		},
	})

	// Register watch factory so WebSocket subscriptions work.
	apiserver.RegisterWatchFactory("pyvcp", newPyVCPWatchAPI)
}

// pyvcpModule implements gomc.Module for a PyVCP panel.
type pyvcpModule struct {
	logger *slog.Logger
	comp   *hal.Component
	panel  *panel
}

func (m *pyvcpModule) Start() error { return nil }
func (m *pyvcpModule) Stop()        {}
func (m *pyvcpModule) Destroy() {
	if m.comp != nil {
		if err := m.comp.Exit(); err != nil {
			m.logger.Debug("pyvcp HAL component exit error", "name", m.panel.name, "error", err)
		}
	}
}

func newPyVCPModule(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	xmlPath := ""
	for _, arg := range args {
		k, v, ok := strings.Cut(arg, "=")
		if !ok {
			continue
		}
		if k == "xml" {
			xmlPath = v
		}
	}
	if xmlPath == "" {
		return nil, fmt.Errorf("pyvcp: missing required xml= parameter")
	}

	// Resolve relative paths against the INI file directory.
	if !filepath.IsAbs(xmlPath) {
		iniDir := filepath.Dir(ini.SourceFile())
		xmlPath = filepath.Join(iniDir, xmlPath)
	}

	logger = logger.With("module", "pyvcp", "name", name)
	logger.Info("loading PyVCP panel", "xml", xmlPath)

	// Parse XML and extract pin definitions.
	p, err := parsePanel(name, xmlPath)
	if err != nil {
		return nil, fmt.Errorf("pyvcp %q: %w", name, err)
	}

	// Create HAL component.
	comp, err := hal.NewComponent(name)
	if err != nil {
		return nil, fmt.Errorf("pyvcp %q: creating HAL component: %w", name, err)
	}

	// Create all pins.
	if err := p.createPins(comp); err != nil {
		return nil, fmt.Errorf("pyvcp %q: creating pins: %w", name, err)
	}

	if err := comp.Ready(); err != nil {
		return nil, fmt.Errorf("pyvcp %q: hal ready: %w", name, err)
	}

	// Register with the panel registry for REST/WS access.
	panelRegistry.register(p)

	// Register the API instance with the apiserver registry.
	cb := &pyvcpCallbacks{panel: p, comp: comp}
	if err := apiserver.DefaultRegistry().Register("pyvcp", 1, name, unsafe.Pointer(cb)); err != nil {
		return nil, fmt.Errorf("pyvcp %q: api register: %w", name, err)
	}

	// Register WebSocket watch API.
	watchAPI := newPyVCPWatchAPI(name, unsafe.Pointer(cb))
	apiserver.DefaultWatchRegistry().Register(watchAPI)

	logger.Info("PyVCP panel initialized", "name", name, "pins", len(p.pins))

	return &pyvcpModule{
		logger: logger,
		comp:   comp,
		panel:  p,
	}, nil
}

// --- Panel registry (shared across all pyvcp instances) ---

var panelRegistry = newPanelRegistry()

type panelRegistry_ struct {
	mu     sync.RWMutex
	panels map[string]*panel
}

func newPanelRegistry() *panelRegistry_ {
	return &panelRegistry_{panels: make(map[string]*panel)}
}

func (r *panelRegistry_) register(p *panel) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.panels[p.name] = p
}

func (r *panelRegistry_) get(name string) *panel {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.panels[name]
}

func (r *panelRegistry_) list() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()
	names := make([]string, 0, len(r.panels))
	for name := range r.panels {
		names = append(names, name)
	}
	return names
}

// pyvcpCallbacks holds the state for one panel's API callbacks.
type pyvcpCallbacks struct {
	panel *panel
	comp  *hal.Component
}

// --- REST dispatch functions ---

func dispatchListPanels(_ unsafe.Pointer, _ []byte) ([]byte, error) {
	return json.Marshal(panelRegistry.list())
}

func dispatchGetPanel(_ unsafe.Pointer, req []byte) ([]byte, error) {
	var params struct {
		Name string `json:"name"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, err
	}
	p := panelRegistry.get(params.Name)
	if p == nil {
		return nil, fmt.Errorf("panel %q not found", params.Name)
	}
	defs := make([]PinDef, len(p.pins))
	for i, pin := range p.pins {
		defs[i] = PinDef{
			Name:    pin.name,
			HalType: pin.halType,
			Dir:     pin.dir,
		}
	}
	return json.Marshal(&PanelInfo{
		Name: p.name,
		XML:  p.xml,
		Pins: defs,
	})
}

// --- WebSocket watch API ---

func newPyVCPWatchAPI(instance string, callbacks unsafe.Pointer) *apiserver.WatchAPI {
	cb := (*pyvcpCallbacks)(callbacks)
	return &apiserver.WatchAPI{
		APIName:  "pyvcp",
		Instance: instance,
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "watch_pins",
				DefaultRate: 100 * time.Millisecond,
				Watch: func() (json.RawMessage, error) {
					values := make([]PinValue, len(cb.panel.pins))
					for i, pin := range cb.panel.pins {
						values[i] = PinValue{
							Name:  pin.name,
							Value: pin.readValue(),
						}
					}
					return json.Marshal(values)
				},
			},
		},
		Commands: []apiserver.CommandMeta{
			{
				Name: "set_pin",
				Handler: func(req json.RawMessage) (json.RawMessage, error) {
					var args struct {
						Panel string `json:"panel"`
						Name  string `json:"name"`
						Value string `json:"value"`
					}
					if err := json.Unmarshal(req, &args); err != nil {
						return nil, err
					}
					for _, pin := range cb.panel.pins {
						if pin.name == args.Name {
							ok, err := pin.writeValue(args.Value)
							if err != nil {
								return nil, err
							}
							return json.Marshal(ok)
						}
					}
					return nil, fmt.Errorf("pin %q not found", args.Name)
				},
			},
		},
	}
}

// --- Types matching the GMI IDL ---

// PinDef describes a HAL pin definition.
type PinDef struct {
	Name    string `json:"name"`
	HalType int    `json:"hal_type"`
	Dir     int    `json:"dir"`
}

// PinValue carries a pin name and its string-encoded value.
type PinValue struct {
	Name  string `json:"name"`
	Value string `json:"value"`
}

// PanelInfo holds panel metadata for the REST response.
type PanelInfo struct {
	Name string   `json:"name"`
	XML  string   `json:"xml"`
	Pins []PinDef `json:"pins"`
}
