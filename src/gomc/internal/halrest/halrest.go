// Package halrest implements the server-side halcmd REST API handler.
// It registers with the apiserver and dispatches REST calls to the
// launcher's internal halcmd package (no liblinuxcnchal.so dependency).
package halrest

import (
	"encoding/json"
	"fmt"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/internal/halcmd"
	hal "github.com/sittner/linuxcnc/src/gomc/pkg/hal"
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
	meta := &apiserver.APIMeta{
		Name:       "halcmd",
		Version:    1,
		RESTExport: true,
		Prefix:     "halcmd",
		Funcs:      buildFuncMetas(),
	}
	apiserver.RegisterMeta(meta)
	return reg.Register("halcmd", 1, "halcmd", unsafe.Pointer(nil))
}

// ─── Response types (match the generated client types) ───

type cmdResult struct {
	Success bool    `json:"success"`
	Output  *string `json:"output,omitempty"`
	Error   *string `json:"error,omitempty"`
}

type pinInfo struct {
	Name   string  `json:"name"`
	Type   string  `json:"type"`
	Dir    string  `json:"dir"`
	Value  string  `json:"value"`
	Owner  string  `json:"owner"`
	Linked bool    `json:"linked"`
	Signal *string `json:"signal,omitempty"`
}

type paramInfo struct {
	Name  string `json:"name"`
	Type  string `json:"type"`
	Dir   string `json:"dir"`
	Value string `json:"value"`
	Owner string `json:"owner"`
}

type signalInfo struct {
	Name    string   `json:"name"`
	Type    string   `json:"type"`
	Value   string   `json:"value"`
	Writers []string `json:"writers"`
	Readers []string `json:"readers"`
	Bidirs  []string `json:"bidirs"`
}

type componentInfo struct {
	Name  string `json:"name"`
	Id    int    `json:"id"`
	Type  string `json:"type"`
	State string `json:"state"`
}

type functionInfo struct {
	Name    string `json:"name"`
	Owner   string `json:"owner"`
	Users   int32  `json:"users"`
	Runtime int64  `json:"runtime"`
	Fp      bool   `json:"fp"`
}

type threadInfo struct {
	Name      string   `json:"name"`
	Period    int64    `json:"period"`
	Fp        bool     `json:"fp"`
	CpuId     int32    `json:"cpu_id"`
	Functions []string `json:"functions"`
}

type halStatus struct {
	RtLock         bool  `json:"rt_lock"`
	MemLock        bool  `json:"mem_lock"`
	ThreadsRunning bool  `json:"threads_running"`
	Components     int32 `json:"components"`
	Pins           int32 `json:"pins"`
	Signals        int32 `json:"signals"`
	Params         int32 `json:"params"`
	Threads        int32 `json:"threads"`
	Functions      int32 `json:"functions"`
}

// ─── Helpers ───

func okResult() ([]byte, error) {
	return json.Marshal(cmdResult{Success: true})
}

func outputResult(output string) ([]byte, error) {
	return json.Marshal(cmdResult{Success: true, Output: &output})
}

func errResult(err error) ([]byte, error) {
	msg := err.Error()
	return json.Marshal(cmdResult{Success: false, Error: &msg})
}

// getField extracts a string field from a JSON object.
func getField(data map[string]interface{}, name string) string {
	if v, ok := data[name]; ok {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return ""
}

// getStringSlice extracts a []string from a JSON array field.
func getStringSlice(data map[string]interface{}, name string) []string {
	arr, ok := data[name]
	if !ok {
		return nil
	}
	items, ok := arr.([]interface{})
	if !ok {
		return nil
	}
	result := make([]string, 0, len(items))
	for _, item := range items {
		if s, ok := item.(string); ok {
			result = append(result, s)
		}
	}
	return result
}

// parseReq parses JSON request body into a map.
func parseReq(body []byte) (map[string]interface{}, error) {
	if len(body) == 0 {
		return make(map[string]interface{}), nil
	}
	var m map[string]interface{}
	if err := json.Unmarshal(body, &m); err != nil {
		return nil, fmt.Errorf("invalid JSON: %w", err)
	}
	return m, nil
}

// ─── Dispatch functions ───

func dispatchListPins(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pattern := getField(params, "pattern")

	var patterns []string
	if pattern != "" {
		patterns = []string{pattern}
	}

	result, err := halcmd.Show("pin", patterns...)
	if err != nil {
		return nil, err
	}

	out := make([]pinInfo, 0, len(result.Pins))
	for _, p := range result.Pins {
		pi := pinInfo{
			Name:   p.Name,
			Type:   p.Type,
			Dir:    p.Direction,
			Value:  p.Value,
			Owner:  p.Owner,
			Linked: p.Signal != "",
		}
		if p.Signal != "" {
			sig := p.Signal
			pi.Signal = &sig
		}
		out = append(out, pi)
	}
	return json.Marshal(out)
}

func dispatchListParams(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pattern := getField(params, "pattern")

	var patterns []string
	if pattern != "" {
		patterns = []string{pattern}
	}

	result, err := halcmd.Show("param", patterns...)
	if err != nil {
		return nil, err
	}

	out := make([]paramInfo, 0, len(result.Params))
	for _, p := range result.Params {
		out = append(out, paramInfo{
			Name:  p.Name,
			Type:  p.Type,
			Dir:   p.Direction,
			Value: p.Value,
			Owner: p.Owner,
		})
	}
	return json.Marshal(out)
}

func dispatchListSignals(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pattern := getField(params, "pattern")

	var patterns []string
	if pattern != "" {
		patterns = []string{pattern}
	}

	result, err := halcmd.Show("sig", patterns...)
	if err != nil {
		return nil, err
	}

	out := make([]signalInfo, 0, len(result.Signals))
	for _, s := range result.Signals {
		out = append(out, signalInfo{
			Name:    s.Name,
			Type:    s.Type,
			Value:   s.Value,
			Writers: []string{},
			Readers: []string{},
			Bidirs:  []string{},
		})
	}
	return json.Marshal(out)
}

func dispatchListComponents(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pattern := getField(params, "pattern")

	var patterns []string
	if pattern != "" {
		patterns = []string{pattern}
	}

	result, err := halcmd.Show("comp", patterns...)
	if err != nil {
		return nil, err
	}

	out := make([]componentInfo, 0, len(result.Comps))
	for _, c := range result.Comps {
		out = append(out, componentInfo{
			Name:  c.Name,
			Id:    c.ID,
			Type:  c.Type,
			State: "Ready",
		})
	}
	return json.Marshal(out)
}

func dispatchListFunctions(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pattern := getField(params, "pattern")

	var patterns []string
	if pattern != "" {
		patterns = []string{pattern}
	}

	result, err := halcmd.Show("funct", patterns...)
	if err != nil {
		return nil, err
	}

	out := make([]functionInfo, 0, len(result.Functs))
	for _, f := range result.Functs {
		out = append(out, functionInfo{
			Name:  f.Name,
			Owner: f.Owner,
		})
	}
	return json.Marshal(out)
}

func dispatchListThreads(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pattern := getField(params, "pattern")

	var patterns []string
	if pattern != "" {
		patterns = []string{pattern}
	}

	result, err := halcmd.Show("thread", patterns...)
	if err != nil {
		return nil, err
	}

	out := make([]threadInfo, 0, len(result.Threads))
	for _, t := range result.Threads {
		out = append(out, threadInfo{
			Name:      t.Name,
			Period:    t.Period,
			CpuId:     -1,
			Functions: t.Functs,
		})
	}
	return json.Marshal(out)
}

func dispatchGetPin(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing pin name")
	}

	// Get pin value and type
	value, err := halcmd.GetP(name)
	if err != nil {
		return nil, err
	}
	ptype, err := halcmd.PType(name)
	if err != nil {
		return nil, err
	}

	pi := pinInfo{
		Name:  name,
		Type:  ptype.String(),
		Value: value,
	}
	return json.Marshal(pi)
}

func dispatchGetParam(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing param name")
	}

	value, err := halcmd.GetP(name)
	if err != nil {
		return nil, err
	}
	ptype, err := halcmd.PType(name)
	if err != nil {
		return nil, err
	}

	pi := paramInfo{
		Name:  name,
		Type:  ptype.String(),
		Value: value,
	}
	return json.Marshal(pi)
}

func dispatchGetSignal(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing signal name")
	}

	value, err := halcmd.GetS(name)
	if err != nil {
		return nil, err
	}
	stype, err := halcmd.SType(name)
	if err != nil {
		return nil, err
	}

	si := signalInfo{
		Name:    name,
		Type:    stype.String(),
		Value:   value,
		Writers: []string{},
		Readers: []string{},
		Bidirs:  []string{},
	}
	return json.Marshal(si)
}

func dispatchGetStatus(_ unsafe.Pointer, body []byte) ([]byte, error) {
	st, err := halcmd.Status()
	if err != nil {
		return nil, err
	}

	// Get counts via Show("all")
	result, err := halcmd.Show("all")
	if err != nil {
		return nil, err
	}

	out := halStatus{
		RtLock:     st.LockLevel != "NONE",
		Components: int32(len(result.Comps)),
		Pins:       int32(len(result.Pins)),
		Signals:    int32(len(result.Signals)),
		Params:     int32(len(result.Params)),
		Threads:    int32(len(result.Threads)),
		Functions:  int32(len(result.Functs)),
	}
	return json.Marshal(out)
}

func dispatchSetPin(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	value := getField(params, "value")
	if name == "" {
		return nil, fmt.Errorf("missing pin name")
	}

	if err := halcmd.SetP(name, value); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchSetParam(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	value := getField(params, "value")
	if name == "" {
		return nil, fmt.Errorf("missing param name")
	}

	if err := halcmd.SetP(name, value); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchAliasPin(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	alias := getField(params, "alias")
	if name == "" || alias == "" {
		return nil, fmt.Errorf("missing name or alias")
	}
	if err := halcmd.Alias("pin", name, alias); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchAliasParam(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	alias := getField(params, "alias")
	if name == "" || alias == "" {
		return nil, fmt.Errorf("missing name or alias")
	}
	if err := halcmd.Alias("param", name, alias); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnaliasPin(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing pin name")
	}
	if err := halcmd.UnAlias("pin", name); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnaliasParam(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing param name")
	}
	if err := halcmd.UnAlias("param", name); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchSetSignal(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	value := getField(params, "value")
	if name == "" {
		return nil, fmt.Errorf("missing signal name")
	}
	if err := halcmd.SetS(name, value); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchNewSignal(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	sigType := getField(params, "type")
	if name == "" || sigType == "" {
		return nil, fmt.Errorf("missing signal name or type")
	}

	halType, err := parseHalType(sigType)
	if err != nil {
		return errResult(err)
	}
	if err := halcmd.NewSig(name, halType); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchDeleteSignal(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing signal name")
	}
	if err := halcmd.DelSig(name); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchLink(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pin := getField(params, "pin")
	signal := getField(params, "signal")
	if pin == "" || signal == "" {
		return nil, fmt.Errorf("missing pin or signal")
	}
	if err := halcmd.LinkPS(pin, signal); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchLinkPP(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pin1 := getField(params, "pin1")
	pin2 := getField(params, "pin2")
	if pin1 == "" || pin2 == "" {
		return nil, fmt.Errorf("missing pin1 or pin2")
	}
	// linkpp creates an implicit signal and links both pins
	sigName := pin1
	if err := halcmd.Net(sigName, pin1, pin2); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnlink(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	pin := getField(params, "pin")
	if pin == "" {
		return nil, fmt.Errorf("missing pin name")
	}
	if err := halcmd.UnlinkP(pin); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchNet(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	signal := getField(params, "signal")
	pins := getStringSlice(params, "pins")
	if signal == "" {
		return nil, fmt.Errorf("missing signal name")
	}
	if err := halcmd.Net(signal, pins...); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchLoad(_ unsafe.Pointer, body []byte) ([]byte, error) {
	if loadModuleHook == nil {
		return errResult(fmt.Errorf("load: not supported (gomc-server launcher not initialized)"))
	}
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	module := getField(params, "module")
	if module == "" {
		return nil, fmt.Errorf("missing module name")
	}
	args := getStringSlice(params, "args")
	if err := loadModuleHook(module, args); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchLoadRT(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	module := getField(params, "module")
	if module == "" {
		return nil, fmt.Errorf("missing module name")
	}
	args := getStringSlice(params, "args")
	if err := halcmd.LoadRT(module, args...); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnloadRT(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	module := getField(params, "module")
	if module == "" {
		return nil, fmt.Errorf("missing module name")
	}
	if err := halcmd.UnloadRT(module); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchLoadUSR(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing component name")
	}
	args := getStringSlice(params, "args")

	opts := &halcmd.LoadUSROptions{}
	if w, ok := params["wait"]; ok {
		if b, ok := w.(bool); ok && b {
			opts.WaitReady = true
		}
	}

	if err := halcmd.LoadUSR(opts, name, args...); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnloadUSR(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing component name")
	}
	if err := halcmd.UnloadUSR(name); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchWaitUSR(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing component name")
	}
	if err := halcmd.WaitUSR(name); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnload(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	module := getField(params, "module")
	if module == "" {
		return nil, fmt.Errorf("missing module name")
	}
	if err := halcmd.Unload(module); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchNewThread(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing thread name")
	}
	periodNs, _ := params["period_ns"].(float64)
	if periodNs == 0 {
		return nil, fmt.Errorf("missing or invalid period_ns")
	}

	usesFP := 0
	if fp, ok := params["fp"]; ok {
		if b, ok := fp.(bool); ok && b {
			usesFP = 1
		}
	}

	cpuID := -1
	if cpu, ok := params["cpu_id"]; ok {
		if f, ok := cpu.(float64); ok {
			cpuID = int(f)
		}
	}

	if err := halcmd.CreateThreadCPU(name, int64(periodNs), usesFP, cpuID); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchDelThread(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	name := getField(params, "name")
	if name == "" {
		return nil, fmt.Errorf("missing thread name")
	}
	if err := halcmd.ThreadDelete(name); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchAddF(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	thread := getField(params, "thread")
	function := getField(params, "function")
	if thread == "" || function == "" {
		return nil, fmt.Errorf("missing thread or function")
	}

	pos := -1
	if p, ok := params["position"]; ok {
		if f, ok := p.(float64); ok {
			pos = int(f)
		}
	}

	if err := halcmd.AddF(function, thread, pos); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchDelF(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	thread := getField(params, "thread")
	function := getField(params, "function")
	if thread == "" || function == "" {
		return nil, fmt.Errorf("missing thread or function")
	}
	if err := halcmd.DelF(function, thread); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchStart(_ unsafe.Pointer, body []byte) ([]byte, error) {
	if err := halcmd.StartThreads(); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchStop(_ unsafe.Pointer, body []byte) ([]byte, error) {
	if err := halcmd.StopThreads(); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchLock(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	level := getField(params, "level")
	if level == "" {
		level = "all"
	}
	if err := halcmd.Lock(level); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchUnlock(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	level := getField(params, "level")
	if level == "" {
		level = "all"
	}
	if err := halcmd.Unlock(level); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchSetDebug(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	level, ok := params["level"].(float64)
	if !ok {
		return nil, fmt.Errorf("missing or invalid debug level")
	}
	if err := halcmd.SetDebug(int(level)); err != nil {
		return errResult(err)
	}
	return okResult()
}

func dispatchSave(_ unsafe.Pointer, body []byte) ([]byte, error) {
	params, err := parseReq(body)
	if err != nil {
		return nil, err
	}
	saveType := getField(params, "type")
	if saveType == "" {
		saveType = "all"
	}

	lines, err := halcmd.Save(saveType, "")
	if err != nil {
		return errResult(err)
	}

	output := ""
	for _, line := range lines {
		output += line + "\n"
	}
	return outputResult(output)
}

// ─── HAL type parsing ───

func parseHalType(s string) (hal.PinType, error) {
	switch s {
	case "bit":
		return hal.TypeBit, nil
	case "float":
		return hal.TypeFloat, nil
	case "s32":
		return hal.TypeS32, nil
	case "u32":
		return hal.TypeU32, nil
	default:
		return 0, fmt.Errorf("unknown HAL type: %s", s)
	}
}

// ─── FuncMeta table ───

func buildFuncMetas() []apiserver.FuncMeta {
	return []apiserver.FuncMeta{
		// Query commands
		{Name: "list_pins", Method: "GET", Path: "/pins", Dispatch: dispatchListPins},
		{Name: "list_params", Method: "GET", Path: "/params", Dispatch: dispatchListParams},
		{Name: "list_signals", Method: "GET", Path: "/signals", Dispatch: dispatchListSignals},
		{Name: "list_components", Method: "GET", Path: "/components", Dispatch: dispatchListComponents},
		{Name: "list_functions", Method: "GET", Path: "/functions", Dispatch: dispatchListFunctions},
		{Name: "list_threads", Method: "GET", Path: "/threads", Dispatch: dispatchListThreads},
		{Name: "get_pin", Method: "GET", Path: "/pin/{name}", Dispatch: dispatchGetPin},
		{Name: "get_param", Method: "GET", Path: "/param/{name}", Dispatch: dispatchGetParam},
		{Name: "get_signal", Method: "GET", Path: "/signal/{name}", Dispatch: dispatchGetSignal},
		{Name: "get_status", Method: "GET", Path: "/status", Dispatch: dispatchGetStatus},

		// Pin/param commands
		{Name: "set_pin", Method: "PUT", Path: "/pin/{name}", Dispatch: dispatchSetPin},
		{Name: "set_param", Method: "PUT", Path: "/param/{name}", Dispatch: dispatchSetParam},
		{Name: "alias_pin", Method: "POST", Path: "/pin/{name}/alias", Dispatch: dispatchAliasPin},
		{Name: "alias_param", Method: "POST", Path: "/param/{name}/alias", Dispatch: dispatchAliasParam},
		{Name: "unalias_pin", Method: "DELETE", Path: "/pin/{name}/alias", Dispatch: dispatchUnaliasPin},
		{Name: "unalias_param", Method: "DELETE", Path: "/param/{name}/alias", Dispatch: dispatchUnaliasParam},

		// Signal commands
		{Name: "set_signal", Method: "PUT", Path: "/signal/{name}", Dispatch: dispatchSetSignal},
		{Name: "new_signal", Method: "POST", Path: "/signal", Dispatch: dispatchNewSignal},
		{Name: "delete_signal", Method: "DELETE", Path: "/signal/{name}", Dispatch: dispatchDeleteSignal},

		// Link commands
		{Name: "link", Method: "POST", Path: "/link", Dispatch: dispatchLink},
		{Name: "link_pp", Method: "POST", Path: "/linkpp", Dispatch: dispatchLinkPP},
		{Name: "unlink", Method: "DELETE", Path: "/link/{pin}", Dispatch: dispatchUnlink},
		{Name: "net", Method: "POST", Path: "/net", Dispatch: dispatchNet},

		// Module commands
		{Name: "load", Method: "POST", Path: "/load", Dispatch: dispatchLoad},
		{Name: "loadrt", Method: "POST", Path: "/loadrt", Dispatch: dispatchLoadRT},
		{Name: "unloadrt", Method: "DELETE", Path: "/loadrt/{module}", Dispatch: dispatchUnloadRT},
		{Name: "loadusr", Method: "POST", Path: "/loadusr", Dispatch: dispatchLoadUSR},
		{Name: "unloadusr", Method: "DELETE", Path: "/loadusr/{name}", Dispatch: dispatchUnloadUSR},
		{Name: "waitusr", Method: "POST", Path: "/waitusr/{name}", Dispatch: dispatchWaitUSR},
		{Name: "unload", Method: "DELETE", Path: "/module/{module}", Dispatch: dispatchUnload},

		// Thread commands
		{Name: "newthread", Method: "POST", Path: "/thread", Dispatch: dispatchNewThread},
		{Name: "delthread", Method: "DELETE", Path: "/thread/{name}", Dispatch: dispatchDelThread},
		{Name: "addf", Method: "POST", Path: "/thread/{thread}/function", Dispatch: dispatchAddF},
		{Name: "delf", Method: "DELETE", Path: "/thread/{thread}/function/{function}", Dispatch: dispatchDelF},
		{Name: "start", Method: "POST", Path: "/start", Dispatch: dispatchStart},
		{Name: "stop", Method: "POST", Path: "/stop", Dispatch: dispatchStop},

		// Lock/debug/save
		{Name: "lock", Method: "POST", Path: "/lock", Dispatch: dispatchLock},
		{Name: "unlock", Method: "POST", Path: "/unlock", Dispatch: dispatchUnlock},
		{Name: "set_debug", Method: "PUT", Path: "/debug", Dispatch: dispatchSetDebug},
		{Name: "save", Method: "GET", Path: "/save", Dispatch: dispatchSave},
	}
}
