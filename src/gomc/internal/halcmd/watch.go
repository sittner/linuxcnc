package halcmd

/*
#cgo CFLAGS: -I${SRCDIR}/../../../hal -I${SRCDIR}/../../.. -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -llinuxcnchal

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"

// hal_shim_watch_item holds the resolved pointer and metadata for one watched item.
typedef struct {
    void       *d_ptr;     // pointer to data in HAL shmem (pin dummysig, signal data, or param data)
    int         type_;     // hal_type_t (HAL_BIT=1, HAL_FLOAT=2, HAL_S32=3, HAL_U32=4)
    int         dir;       // direction (pin: HAL_IN=1/HAL_OUT=2/HAL_IO=3; param: HAL_RO=5/HAL_RW=6; signal: 0)
    int         kind;      // 0=pin, 1=param, 2=signal
    int         linked;    // pin: 1 if linked to signal; signal: 1 if has writers
    char        owner[HAL_NAME_LEN + 1];
    char        signal_name[HAL_NAME_LEN + 1]; // for pins: name of linked signal
} hal_shim_watch_item_t;

// hal_shim_watch_resolve resolves a HAL item name (pin, param, or signal) to
// its data pointer and metadata. Returns 0 on success, -ENOENT if not found.
// The caller must hold no lock; this function acquires the HAL mutex.
static int hal_shim_watch_resolve(const char *name, hal_shim_watch_item_t *out) {
    hal_pin_t   *pin;
    hal_sig_t   *sig;
    hal_param_t *param;
    hal_comp_t  *comp;

    if (hal_data == NULL) return -EINVAL;

    memset(out, 0, sizeof(*out));

    rtapi_mutex_get(&(hal_data->mutex));

    // Try pin first
    pin = halpr_find_pin_by_name(name);
    if (pin) {
        out->type_ = (int)pin->type;
        out->dir   = (int)pin->dir;
        out->kind  = 0;
        if (pin->signal != 0) {
            sig = (hal_sig_t *)SHMPTR(pin->signal);
            out->d_ptr = SHMPTR(sig->data_ptr);
            out->linked = 1;
            snprintf(out->signal_name, sizeof(out->signal_name), "%s", sig->name);
        } else {
            out->d_ptr = (void *)&pin->dummysig;
            out->linked = 0;
        }
        if (pin->owner_ptr != 0) {
            comp = (hal_comp_t *)SHMPTR(pin->owner_ptr);
            snprintf(out->owner, sizeof(out->owner), "%s", comp->name);
        }
        rtapi_mutex_give(&(hal_data->mutex));
        return 0;
    }

    // Try param
    param = halpr_find_param_by_name(name);
    if (param) {
        out->type_ = (int)param->type;
        out->dir   = (int)param->dir;
        out->kind  = 1;
        out->d_ptr = SHMPTR(param->data_ptr);
        out->linked = 0;
        if (param->owner_ptr != 0) {
            comp = (hal_comp_t *)SHMPTR(param->owner_ptr);
            snprintf(out->owner, sizeof(out->owner), "%s", comp->name);
        }
        rtapi_mutex_give(&(hal_data->mutex));
        return 0;
    }

    // Try signal
    sig = halpr_find_sig_by_name(name);
    if (sig) {
        out->type_ = (int)sig->type;
        out->dir   = 0;
        out->kind  = 2;
        out->d_ptr = SHMPTR(sig->data_ptr);
        out->linked = (sig->writers > 0) ? 1 : 0;
        rtapi_mutex_give(&(hal_data->mutex));
        return 0;
    }

    rtapi_mutex_give(&(hal_data->mutex));
    return -ENOENT;
}

// hal_shim_watch_poll_item reads a single HAL value via typed pointer dereference,
// compares against the previous snapshot (as uint64 bitcast), and if changed,
// formats the value into buf. Returns 1 if changed, 0 if unchanged.
// The prev pointer is updated in-place on change.
//
// This avoids intermediate byte arrays — the value is read once as its native
// type, bitcast to uint64 for comparison, and formatted directly from the
// typed local if needed.
static inline int hal_shim_watch_poll_item(void *d_ptr, int type_, uint64_t *prev, char *buf, int buf_size) {
    uint64_t cur = 0;

    switch (type_) {
    case HAL_BIT: {
        hal_bit_t v = *(hal_bit_t *)d_ptr;  // single volatile read
        cur = v ? 1 : 0;
        if (cur == *prev) return 0;
        *prev = cur;
        snprintf(buf, buf_size, "%s", v ? "TRUE" : "FALSE");
        return 1;
    }
    case HAL_S32: {
        int32_t v = (int32_t)(*(hal_s32_t *)d_ptr);  // single volatile read
        memcpy(&cur, &v, 4);
        if (cur == *prev) return 0;
        *prev = cur;
        snprintf(buf, buf_size, "%ld", (long)v);
        return 1;
    }
    case HAL_U32: {
        uint32_t v = (uint32_t)(*(hal_u32_t *)d_ptr);  // single volatile read
        memcpy(&cur, &v, 4);
        if (cur == *prev) return 0;
        *prev = cur;
        snprintf(buf, buf_size, "%lu", (unsigned long)v);
        return 1;
    }
    case HAL_FLOAT: {
        double v = (double)(*(hal_float_t *)d_ptr);  // single volatile read (aligned 8-byte)
        memcpy(&cur, &v, 8);
        if (cur == *prev) return 0;
        *prev = cur;
        snprintf(buf, buf_size, "%.7g", v);
        return 1;
    }
    default:
        return 0;
    }
}
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// WatchItemMeta holds resolved metadata for a single watched HAL item.
// This is computed once at subscribe time.
type WatchItemMeta struct {
	Name   string
	Type   string // "bit", "float", "s32", "u32"
	Dir    string // "IN", "OUT", "IO", "RO", "RW", "" (signal)
	Kind   string // "pin", "param", "signal"
	Owner  string
	Linked bool
	Signal string // for pins: the linked signal name
}

// WatchItem holds the resolved state for a single watched item.
type WatchItem struct {
	Meta WatchItemMeta
	dPtr unsafe.Pointer
	typ  C.int
	prev C.uint64_t // shadow: bitcast of last seen value
}

// WatchSet is a per-subscription set of resolved watch items with shadow buffers.
// It is NOT thread-safe — each subscription goroutine owns one instance.
type WatchSet struct {
	items []WatchItem
	first bool // true if first poll (send all values)
}

// NewWatchSet resolves a list of HAL item names and returns a WatchSet ready for polling.
// Names that cannot be resolved are silently skipped (they may have been removed).
func NewWatchSet(names []string) (*WatchSet, error) {
	ws := &WatchSet{
		items: make([]WatchItem, 0, len(names)),
		first: true,
	}

	for _, name := range names {
		cName := C.CString(name)
		var item C.hal_shim_watch_item_t
		rc := C.hal_shim_watch_resolve(cName, &item)
		C.free(unsafe.Pointer(cName))

		if rc != 0 {
			continue // skip unresolvable names
		}

		wi := WatchItem{
			Meta: WatchItemMeta{
				Name:   name,
				Type:   halTypeToString(int(item.type_)),
				Dir:    halDirToString(int(item.dir), int(item.kind)),
				Kind:   halKindToString(int(item.kind)),
				Owner:  C.GoString(&item.owner[0]),
				Linked: item.linked != 0,
				Signal: C.GoString(&item.signal_name[0]),
			},
			dPtr: item.d_ptr,
			typ:  item.type_,
		}
		ws.items = append(ws.items, wi)
	}

	if len(ws.items) == 0 && len(names) > 0 {
		return nil, fmt.Errorf("no valid watch items resolved")
	}

	return ws, nil
}

// Meta returns the metadata for all resolved items. Used for the initial response.
func (ws *WatchSet) Meta() []WatchItemMeta {
	metas := make([]WatchItemMeta, len(ws.items))
	for i := range ws.items {
		metas[i] = ws.items[i].Meta
	}
	return metas
}

// WatchValue holds a changed value from a poll.
type WatchValue struct {
	Name  string
	Value string
}

// Poll reads all watched items, compares against shadow buffer, and returns
// only items whose raw value has changed. On first call, returns all values.
// Returns nil if nothing changed.
func (ws *WatchSet) Poll() []WatchValue {
	var changed []WatchValue

	for i := range ws.items {
		item := &ws.items[i]
		var buf [64]C.char

		if ws.first {
			// First poll: always read and format, initialize shadow.
			// Use a dummy prev that can never match by inverting current.
			item.prev = ^item.prev
			C.hal_shim_watch_poll_item(item.dPtr, item.typ, &item.prev, &buf[0], 64)
			changed = append(changed, WatchValue{
				Name:  item.Meta.Name,
				Value: C.GoString(&buf[0]),
			})
		} else {
			rc := C.hal_shim_watch_poll_item(item.dPtr, item.typ, &item.prev, &buf[0], 64)
			if rc != 0 {
				changed = append(changed, WatchValue{
					Name:  item.Meta.Name,
					Value: C.GoString(&buf[0]),
				})
			}
		}
	}

	ws.first = false
	if len(changed) == 0 {
		return nil
	}
	return changed
}

func halTypeToString(t int) string {
	switch t {
	case 1:
		return "bit"
	case 2:
		return "float"
	case 3:
		return "s32"
	case 4:
		return "u32"
	default:
		return "unknown"
	}
}

func halDirToString(dir, kind int) string {
	if kind == 2 { // signal
		return ""
	}
	switch dir {
	case 1:
		return "IN"
	case 2:
		return "OUT"
	case 3:
		return "IO"
	case 5:
		return "RO"
	case 6:
		return "RW"
	default:
		return ""
	}
}

func halKindToString(kind int) string {
	switch kind {
	case 0:
		return "pin"
	case 1:
		return "param"
	case 2:
		return "signal"
	default:
		return "unknown"
	}
}
