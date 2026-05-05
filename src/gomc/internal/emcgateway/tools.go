package emcgateway

/*
#include "tool_shim.h"
#include "nml_shim.h"
*/
import "C"

import (
	"fmt"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/toolsapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// toolsImpl implements toolsapi.ToolsCallbacks via the tool_shim C interface.
type toolsImpl struct{}

func init() {
	apiserver.RegisterMeta(toolsapi.ToolsMeta)
}

func shimToToolEntry(s *C.tool_shim_entry_t) toolsapi.ToolEntry {
	return toolsapi.ToolEntry{
		Toolno:      int32(s.toolno),
		Pocketno:    int32(s.pocketno),
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
		Frontangle:  float64(s.frontangle),
		Backangle:   float64(s.backangle),
		Orientation: int32(s.orientation),
	}
}

func toolEntryToShim(e *toolsapi.ToolEntry) C.tool_shim_entry_t {
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
		frontangle:  C.double(e.Frontangle),
		backangle:   C.double(e.Backangle),
		orientation: C.int(e.Orientation),
	}
}

func ensureToolMmap() error {
	if rc := C.tool_shim_init(); rc != 0 {
		return fmt.Errorf("tool mmap not available")
	}
	return nil
}

func (t *toolsImpl) ListTools() ([]toolsapi.ToolEntry, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	lastIdx := int(C.tool_shim_last_index())
	tools := make([]toolsapi.ToolEntry, lastIdx+1)
	for i := 0; i <= lastIdx; i++ {
		var s C.tool_shim_entry_t
		if C.tool_shim_get(C.int(i), &s) == 0 {
			tools[i] = shimToToolEntry(&s)
		}
	}
	return tools, nil
}

func (t *toolsImpl) GetTool(toolno int32) (*toolsapi.ToolEntry, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	idx := int(C.tool_shim_find_by_toolno(C.int(toolno)))
	if idx < 0 {
		return nil, fmt.Errorf("tool %d not found", toolno)
	}
	var s C.tool_shim_entry_t
	if C.tool_shim_get(C.int(idx), &s) != 0 {
		return nil, fmt.Errorf("failed to read tool at index %d", idx)
	}
	entry := shimToToolEntry(&s)
	return &entry, nil
}

func (t *toolsImpl) PutTool(toolno int32, entry toolsapi.ToolEntry) (*toolsapi.PutToolResult, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	if toolno <= 0 {
		return nil, fmt.Errorf("toolno must be > 0")
	}
	entry.Toolno = toolno

	idx := int(C.tool_shim_find_by_toolno(C.int(toolno)))
	if idx < 0 {
		idx = int(C.tool_shim_last_index()) + 1
		if idx >= C.TOOL_SHIM_MAX_POCKETS {
			return nil, fmt.Errorf("tool table full")
		}
	}

	s := toolEntryToShim(&entry)
	if C.tool_shim_put(C.int(idx), &s) != 0 {
		return nil, fmt.Errorf("failed to write tool at index %d", idx)
	}
	return &toolsapi.PutToolResult{Ok: true, Index: int32(idx)}, nil
}

func (t *toolsImpl) DeleteTool(toolno int32) (*toolsapi.CmdResult, error) {
	if err := ensureToolMmap(); err != nil {
		return nil, err
	}
	idx := int(C.tool_shim_find_by_toolno(C.int(toolno)))
	if idx < 0 {
		return nil, fmt.Errorf("tool %d not found", toolno)
	}
	empty := C.tool_shim_entry_t{}
	if C.tool_shim_put(C.int(idx), &empty) != 0 {
		return nil, fmt.Errorf("failed to clear tool at index %d", idx)
	}
	return &toolsapi.CmdResult{Ok: "true"}, nil
}

func (t *toolsImpl) ReloadTools() (*toolsapi.CmdResult, error) {
	rc := C.nml_shim_load_tool_table()
	if rc != 0 {
		return nil, fmt.Errorf("failed to reload tool table")
	}
	return &toolsapi.CmdResult{Ok: "true"}, nil
}
