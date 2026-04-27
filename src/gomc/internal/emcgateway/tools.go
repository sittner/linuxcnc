package emcgateway

/*
#include "tool_shim.h"
#include "nml_shim.h"
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// toolEntry is the JSON representation of a single tool.
type toolEntry struct {
	Toolno      int     `json:"toolno"`
	Pocketno    int     `json:"pocketno"`
	XOffset     float64 `json:"x_offset"`
	YOffset     float64 `json:"y_offset"`
	ZOffset     float64 `json:"z_offset"`
	AOffset     float64 `json:"a_offset"`
	BOffset     float64 `json:"b_offset"`
	COffset     float64 `json:"c_offset"`
	UOffset     float64 `json:"u_offset"`
	VOffset     float64 `json:"v_offset"`
	WOffset     float64 `json:"w_offset"`
	Diameter    float64 `json:"diameter"`
	FrontAngle  float64 `json:"frontangle"`
	BackAngle   float64 `json:"backangle"`
	Orientation int     `json:"orientation"`
}

func shimToEntry(s *C.tool_shim_entry_t) toolEntry {
	return toolEntry{
		Toolno:      int(s.toolno),
		Pocketno:    int(s.pocketno),
		XOffset:     float64(s.x_offset),
		YOffset:     float64(s.y_offset),
		ZOffset:     float64(s.z_offset),
		AOffset:     float64(s.a_offset),
		BOffset:     float64(s.b_offset),
		COffset:     float64(s.c_offset),
		UOffset:     float64(s.u_offset),
		VOffset:     float64(s.v_offset),
		WOffset:     float64(s.w_offset),
		Diameter:    float64(s.diameter),
		FrontAngle:  float64(s.frontangle),
		BackAngle:   float64(s.backangle),
		Orientation: int(s.orientation),
	}
}

func entryToShim(e *toolEntry) C.tool_shim_entry_t {
	return C.tool_shim_entry_t{
		toolno:      C.int(e.Toolno),
		pocketno:    C.int(e.Pocketno),
		x_offset:    C.double(e.XOffset),
		y_offset:    C.double(e.YOffset),
		z_offset:    C.double(e.ZOffset),
		a_offset:    C.double(e.AOffset),
		b_offset:    C.double(e.BOffset),
		c_offset:    C.double(e.COffset),
		u_offset:    C.double(e.UOffset),
		v_offset:    C.double(e.VOffset),
		w_offset:    C.double(e.WOffset),
		diameter:    C.double(e.Diameter),
		frontangle:  C.double(e.FrontAngle),
		backangle:   C.double(e.BackAngle),
		orientation: C.int(e.Orientation),
	}
}

func init() {
	registerToolMeta()
}

func registerToolMeta() {
	apiserver.RegisterMeta(&apiserver.APIMeta{
		Name:       "tools",
		Version:    1,
		RESTExport: true,
		Prefix:     "tools",
		Funcs:      toolRESTFuncs(),
	})
}

func toolRESTFuncs() []apiserver.FuncMeta {
	return []apiserver.FuncMeta{
		{
			Name:   "list_tools",
			Method: "GET",
			Path:   "/",
			Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
				return handleListTools()
			},
		},
		{
			Name:   "get_tool",
			Method: "GET",
			Path:   "/{toolno}",
			Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
				return handleGetTool(req)
			},
		},
		{
			Name:   "put_tool",
			Method: "PUT",
			Path:   "/{toolno}",
			Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
				return handlePutTool(req)
			},
		},
		{
			Name:   "delete_tool",
			Method: "DELETE",
			Path:   "/{toolno}",
			Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
				return handleDeleteTool(req)
			},
		},
		{
			Name:   "reload_tools",
			Method: "POST",
			Path:   "/reload",
			Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
				return handleReloadTools()
			},
		},
	}
}

func ensureToolMmap() error {
	if rc := C.tool_shim_init(); rc != 0 {
		return fmt.Errorf("tool mmap not available")
	}
	return nil
}

func handleListTools() ([]byte, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	lastIdx := int(C.tool_shim_last_index())
	// Return ALL entries in mmap index order (including empty slots).
	// Index 0 = spindle tool. Callers rely on positional indexing.
	tools := make([]toolEntry, lastIdx+1)
	for i := 0; i <= lastIdx; i++ {
		var s C.tool_shim_entry_t
		if C.tool_shim_get(C.int(i), &s) == 0 {
			tools[i] = shimToEntry(&s)
		}
	}
	return json.Marshal(tools)
}

func handleGetTool(req []byte) ([]byte, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	var params struct {
		Toolno int `json:"toolno"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid request: %w", err)
	}
	idx := int(C.tool_shim_find_by_toolno(C.int(params.Toolno)))
	if idx < 0 {
		return nil, fmt.Errorf("tool %d not found", params.Toolno)
	}
	var s C.tool_shim_entry_t
	if C.tool_shim_get(C.int(idx), &s) != 0 {
		return nil, fmt.Errorf("failed to read tool at index %d", idx)
	}
	return json.Marshal(shimToEntry(&s))
}

func handlePutTool(req []byte) ([]byte, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	var entry toolEntry
	if err := json.Unmarshal(req, &entry); err != nil {
		return nil, fmt.Errorf("invalid tool data: %w", err)
	}
	if entry.Toolno <= 0 {
		return nil, fmt.Errorf("toolno must be > 0")
	}

	// Find existing index or use next available slot
	idx := int(C.tool_shim_find_by_toolno(C.int(entry.Toolno)))
	if idx < 0 {
		// New tool — put at last_index + 1
		idx = int(C.tool_shim_last_index()) + 1
		if idx >= C.TOOL_SHIM_MAX_POCKETS {
			return nil, fmt.Errorf("tool table full")
		}
	}

	s := entryToShim(&entry)
	if C.tool_shim_put(C.int(idx), &s) != 0 {
		return nil, fmt.Errorf("failed to write tool at index %d", idx)
	}
	return json.Marshal(map[string]interface{}{"ok": true, "index": idx})
}

func handleDeleteTool(req []byte) ([]byte, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	var params struct {
		Toolno int `json:"toolno"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid request: %w", err)
	}
	idx := int(C.tool_shim_find_by_toolno(C.int(params.Toolno)))
	if idx < 0 {
		return nil, fmt.Errorf("tool %d not found", params.Toolno)
	}
	// Zero out the entry
	empty := C.tool_shim_entry_t{}
	if C.tool_shim_put(C.int(idx), &empty) != 0 {
		return nil, fmt.Errorf("failed to clear tool at index %d", idx)
	}
	return json.Marshal(map[string]string{"ok": "true"})
}

func handleReloadTools() ([]byte, error) {
	rc := C.nml_shim_load_tool_table()
	if rc != 0 {
		return nil, fmt.Errorf("failed to reload tool table")
	}
	return json.Marshal(map[string]string{"ok": "true"})
}
