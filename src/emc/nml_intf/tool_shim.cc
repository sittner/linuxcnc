/*
 * tool_shim.cc — C wrapper around tooldata shared memory API.
 */

#include "tool_shim.h"
#include "tooldata.hh"
#include <string.h>

static bool tool_shim_inited = false;
static char *ttcomments[TOOL_SHIM_MAX_POCKETS];
static bool comments_allocated = false;

static void ensure_comments(void) {
    if (comments_allocated) return;
    for (int i = 0; i < TOOL_SHIM_MAX_POCKETS; i++) {
        ttcomments[i] = new char[TOOL_SHIM_COMMENT_LEN];
        ttcomments[i][0] = '\0';
    }
    comments_allocated = true;
}

int tool_shim_init(void) {
    if (tool_shim_inited) return 0;
    ensure_comments();
    int rc = tool_mmap_user();
    if (rc == 0) tool_shim_inited = true;
    return rc;
}

int tool_shim_load(const char *filename) {
    ensure_comments();
    return tooldata_load(filename, ttcomments);
}

int tool_shim_save(const char *filename) {
    ensure_comments();
    return tooldata_save(filename, ttcomments);
}

int tool_shim_last_index(void) {
    return tooldata_last_index_get();
}

static void to_shim(const CANON_TOOL_TABLE &src, int idx, tool_shim_entry_t *dst) {
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
    if (idx >= 0 && idx < TOOL_SHIM_MAX_POCKETS && ttcomments[idx]) {
        strncpy(dst->comment, ttcomments[idx], TOOL_SHIM_COMMENT_LEN - 1);
        dst->comment[TOOL_SHIM_COMMENT_LEN - 1] = '\0';
    } else {
        dst->comment[0] = '\0';
    }
}

static void from_shim(const tool_shim_entry_t *src, int idx, CANON_TOOL_TABLE &dst) {
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
    if (idx >= 0 && idx < TOOL_SHIM_MAX_POCKETS && ttcomments[idx]) {
        memcpy(ttcomments[idx], src->comment, TOOL_SHIM_COMMENT_LEN - 1);
        ttcomments[idx][TOOL_SHIM_COMMENT_LEN - 1] = '\0';
    }
}

int tool_shim_get(int idx, tool_shim_entry_t *out) {
    CANON_TOOL_TABLE tdata;
    if (tooldata_get(&tdata, idx) != IDX_OK)
        return -1;
    to_shim(tdata, idx, out);
    return 0;
}

int tool_shim_put(int idx, const tool_shim_entry_t *in) {
    CANON_TOOL_TABLE tdata = tooldata_entry_init();
    from_shim(in, idx, tdata);
    toolidx_t rc = tooldata_put(tdata, idx);
    return (rc == IDX_FAIL) ? -1 : 0;
}

int tool_shim_find_by_toolno(int toolno) {
    return tooldata_find_index_for_tool(toolno);
}

int tool_shim_is_random(void) {
    return tool_mmap_is_random_toolchanger() ? 1 : 0;
}
