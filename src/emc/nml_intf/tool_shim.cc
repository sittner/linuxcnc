/*
 * tool_shim.cc — C wrapper around tooldata shared memory API.
 */

#include "tool_shim.h"
#include "tooldata.hh"

static bool tool_shim_inited = false;

int tool_shim_init(void) {
    if (tool_shim_inited) return 0;
    int rc = tool_mmap_user();
    if (rc == 0) tool_shim_inited = true;
    return rc;
}

int tool_shim_last_index(void) {
    return tooldata_last_index_get();
}

static void to_shim(const CANON_TOOL_TABLE &src, tool_shim_entry_t *dst) {
    dst->toolno      = src.toolno;
    dst->pocketno    = src.pocketno;
    dst->x_offset    = src.offset.tran.x;
    dst->y_offset    = src.offset.tran.y;
    dst->z_offset    = src.offset.tran.z;
    dst->a_offset    = src.offset.a;
    dst->b_offset    = src.offset.b;
    dst->c_offset    = src.offset.c;
    dst->u_offset    = src.offset.u;
    dst->v_offset    = src.offset.v;
    dst->w_offset    = src.offset.w;
    dst->diameter    = src.diameter;
    dst->frontangle  = src.frontangle;
    dst->backangle   = src.backangle;
    dst->orientation = src.orientation;
}

static void from_shim(const tool_shim_entry_t *src, CANON_TOOL_TABLE &dst) {
    dst.toolno          = src->toolno;
    dst.pocketno        = src->pocketno;
    dst.offset.tran.x   = src->x_offset;
    dst.offset.tran.y   = src->y_offset;
    dst.offset.tran.z   = src->z_offset;
    dst.offset.a        = src->a_offset;
    dst.offset.b        = src->b_offset;
    dst.offset.c        = src->c_offset;
    dst.offset.u        = src->u_offset;
    dst.offset.v        = src->v_offset;
    dst.offset.w        = src->w_offset;
    dst.diameter         = src->diameter;
    dst.frontangle       = src->frontangle;
    dst.backangle        = src->backangle;
    dst.orientation      = src->orientation;
}

int tool_shim_get(int idx, tool_shim_entry_t *out) {
    CANON_TOOL_TABLE tdata;
    if (tooldata_get(&tdata, idx) != IDX_OK)
        return -1;
    to_shim(tdata, out);
    return 0;
}

int tool_shim_put(int idx, const tool_shim_entry_t *in) {
    CANON_TOOL_TABLE tdata = tooldata_entry_init();
    from_shim(in, tdata);
    toolidx_t rc = tooldata_put(tdata, idx);
    return (rc == IDX_FAIL) ? -1 : 0;
}

int tool_shim_find_by_toolno(int toolno) {
    return tooldata_find_index_for_tool(toolno);
}

int tool_shim_is_random(void) {
    return tool_mmap_is_random_toolchanger() ? 1 : 0;
}
