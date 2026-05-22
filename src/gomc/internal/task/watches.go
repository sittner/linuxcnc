package task

import (
	"encoding/json"
	"time"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emccmdapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstatapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/toolsapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// registerWatches registers WebSocket watch APIs and commands for milltask.
func (m *milltaskModule) registerWatches(name string) {
	if apiserver.DefaultWatchRegistry() == nil {
		apiserver.SetDefaultWatchRegistry(apiserver.NewWatchRegistry())
	}
	wreg := apiserver.DefaultWatchRegistry()

	// emcstat: get_stat watch + get_positions watch + poslogger commands.
	emcstatCmds := emcstatapi.EmcstatCommands(m)
	emcstatCmds = append(emcstatCmds,
		apiserver.CommandMeta{Name: "start_logger", Handler: m.cmdStartLogger},
		apiserver.CommandMeta{Name: "stop_logger", Handler: m.cmdStopLogger},
		apiserver.CommandMeta{Name: "clear_logger", Handler: m.cmdClearLogger},
	)
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "emcstat",
		Instance: name,
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "get_stat",
				DefaultRate: 50 * time.Millisecond,
				Watch: func() (json.RawMessage, error) {
					result, err := m.GetStat()
					if err != nil {
						return nil, err
					}
					return json.Marshal(result)
				},
			},
			{
				Name:        "get_positions",
				DefaultRate: 100 * time.Millisecond,
				Watch:       m.pollPositions,
			},
		},
		Commands: emcstatCmds,
	})

	// emccmd: all command methods as WS commands.
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "emccmd",
		Instance: name,
		Commands: emccmdapi.EmccmdCommands(m),
	})
}

// registerTools registers the tools API (called from Start when INI is loaded).
func (m *milltaskModule) registerTools() {
	toolFile := m.ini.Get("EMCIO", "TOOL_TABLE")
	loadToolShim(toolFile)

	reg := apiserver.DefaultRegistry()
	if reg != nil {
		toolsapi.RegisterToolsAPI(reg, m.name, &toolsImpl{
			toolTableFile: toolFile,
			module:        m,
		})
	}
}

// Position logger WS command handlers.

func (m *milltaskModule) cmdStartLogger(req json.RawMessage) (json.RawMessage, error) {
	var args struct {
		IntervalUS int `json:"interval_us"`
	}
	if req != nil {
		json.Unmarshal(req, &args)
	}
	m.poslog.startLogger(m, args.IntervalUS)
	return json.RawMessage(`{"ok":true}`), nil
}

func (m *milltaskModule) cmdStopLogger(req json.RawMessage) (json.RawMessage, error) {
	m.poslog.stopLogger()
	return json.RawMessage(`{"ok":true}`), nil
}

func (m *milltaskModule) cmdClearLogger(req json.RawMessage) (json.RawMessage, error) {
	m.poslog.clearLogger()
	return json.RawMessage(`{"ok":true}`), nil
}
