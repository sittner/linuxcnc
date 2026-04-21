// Package main is a Go plugin (.so) that implements the manualtoolchange
// HAL component as a gomod.  It embeds the C thread function and GMI
// callbacks via cgo and registers REST dispatch at load time.
//
// Loaded via:   load manualtoolchange
package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../../hal -I${SRCDIR}/../../.. -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -llinuxcnchal

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "hal.h"

// --- HAL pin storage (allocated in HAL shared memory) ---

typedef struct {
    hal_bit_t *change;
    hal_s32_t *number;
    hal_bit_t *change_button;
    hal_bit_t *changed;
} mtc_hal_t;

typedef struct {
    int comp_id;
    char name[HAL_NAME_LEN + 1];
    mtc_hal_t *hal;
} mtc_inst_t;

// --- Thread function (called from RT servo thread) ---

static void mtc_funct(void *arg, long period) {
    mtc_inst_t *inst = (mtc_inst_t *)arg;
    hal_bit_t change  = *(inst->hal->change);
    hal_bit_t changed = *(inst->hal->changed);
    hal_bit_t button  = *(inst->hal->change_button);

    if (change && button && !changed) {
        *(inst->hal->changed) = 1;
    }
    if (!change) {
        *(inst->hal->changed) = 0;
    }
}

// --- GMI callbacks ---

typedef struct {
    bool change_requested;
    int32_t tool_number;
    bool change_confirmed;
} mtc_tool_change_state_t;

static mtc_tool_change_state_t mtc_get_state(void *ctx) {
    mtc_inst_t *inst = (mtc_inst_t *)ctx;
    mtc_tool_change_state_t state;
    state.change_requested = *(inst->hal->change);
    state.tool_number      = *(inst->hal->number);
    state.change_confirmed = *(inst->hal->changed);
    return state;
}

static bool mtc_confirm(void *ctx) {
    mtc_inst_t *inst = (mtc_inst_t *)ctx;
    if (*(inst->hal->change) && !*(inst->hal->changed)) {
        *(inst->hal->changed) = 1;
        return true;
    }
    return false;
}

// --- Dispatch helpers (cgo can't call function pointers directly) ---

static mtc_tool_change_state_t call_get_state(void *ctx) {
    return mtc_get_state(ctx);
}

static bool call_confirm(void *ctx) {
    return mtc_confirm(ctx);
}

// --- Instance lifecycle ---

static mtc_inst_t *mtc_create(const char *name, void *dl_handle) {
    mtc_inst_t *inst = (mtc_inst_t *)calloc(1, sizeof(mtc_inst_t));
    if (!inst) return NULL;

    snprintf(inst->name, sizeof(inst->name), "%s", name);

    inst->comp_id = hal_init_ex(name, dl_handle, COMPONENT_TYPE_REALTIME);
    if (inst->comp_id < 0) { free(inst); return NULL; }

    inst->hal = (mtc_hal_t *)hal_malloc(sizeof(mtc_hal_t));
    if (!inst->hal) goto err;
    memset(inst->hal, 0, sizeof(mtc_hal_t));

    if (hal_pin_bit_newf(HAL_IN, &inst->hal->change, inst->comp_id,
            "%s.change", name) != 0) goto err;
    if (hal_pin_s32_newf(HAL_IN, &inst->hal->number, inst->comp_id,
            "%s.number", name) != 0) goto err;
    if (hal_pin_bit_newf(HAL_IN, &inst->hal->change_button, inst->comp_id,
            "%s.change-button", name) != 0) goto err;
    if (hal_pin_bit_newf(HAL_OUT, &inst->hal->changed, inst->comp_id,
            "%s.changed", name) != 0) goto err;

    if (hal_export_funct(name, mtc_funct, inst, 0, 0, inst->comp_id) != 0)
        goto err;

    if (hal_ready(inst->comp_id) != 0) goto err;

    return inst;

err:
    hal_exit(inst->comp_id);
    free(inst);
    return NULL;
}

static void mtc_destroy(mtc_inst_t *inst) {
    if (!inst) return;
    if (inst->comp_id > 0)
        hal_exit(inst->comp_id);
    free(inst);
}
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"unsafe"

	"github.com/sittner/linuxcnc/src/launcher/pkg/gomodule"
	"github.com/sittner/linuxcnc/src/launcher/pkg/inifile"
)

// --- Go types mirroring C types ---

type ToolChangeState struct {
	ChangeRequested bool  `json:"change_requested"`
	ToolNumber      int32 `json:"tool_number"`
	ChangeConfirmed bool  `json:"change_confirmed"`
}

// --- Dispatch functions ---

func dispatchGetState(callbacks unsafe.Pointer, req []byte) ([]byte, error) {
	inst := (*C.mtc_inst_t)(callbacks)
	out := C.call_get_state(unsafe.Pointer(inst))
	result := ToolChangeState{
		ChangeRequested: bool(out.change_requested),
		ToolNumber:      int32(out.tool_number),
		ChangeConfirmed: bool(out.change_confirmed),
	}
	return json.Marshal(result)
}

func dispatchConfirm(callbacks unsafe.Pointer, req []byte) ([]byte, error) {
	inst := (*C.mtc_inst_t)(callbacks)
	out := C.call_confirm(unsafe.Pointer(inst))
	return json.Marshal(bool(out))
}

// --- API metadata ---

var meta = &gomodule.APIMeta{
	Name:       "manualtoolchange",
	Version:    1,
	RESTExport: true,
	Prefix:     "manualtoolchange",
	Funcs: []gomodule.FuncMeta{
		{
			Name:     "get_state",
			Method:   "GET",
			Path:     "/state",
			RTSafe:   false,
			Dispatch: dispatchGetState,
		},
		{
			Name:     "confirm",
			Method:   "POST",
			Path:     "/confirm",
			RTSafe:   false,
			Dispatch: dispatchConfirm,
		},
	},
}

// host is set by the Factory at load time; used for deferred registration.
var host gomodule.Host

// --- gomodule.Module implementation ---

type mtcModule struct {
	inst   *C.mtc_inst_t
	name   string
	logger *slog.Logger
}

func (m *mtcModule) Start() error { return nil }
func (m *mtcModule) Stop()        {}

func (m *mtcModule) Destroy() {
	C.mtc_destroy(m.inst)
	m.inst = nil
}

// New is the gomodule.Factory entry point.
var New gomodule.Factory = func(h gomodule.Host, ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomodule.Module, error) {
	host = h
	host.RegisterMeta(meta)

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	inst := C.mtc_create(cName, nil)
	if inst == nil {
		return nil, fmt.Errorf("manualtoolchange: C init failed for %q", name)
	}

	// Register API instance — dispatch functions call C directly via inst pointer.
	if err := host.Register("manualtoolchange", 1, name, unsafe.Pointer(inst)); err != nil {
		logger.Error("manualtoolchange: API registration failed", "error", err)
	}

	return &mtcModule{inst: inst, name: name, logger: logger}, nil
}
