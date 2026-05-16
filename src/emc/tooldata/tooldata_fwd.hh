// Forward declarations for tooldata types and functions.
// This header exists to break the circular dependency between
// emc_nml.hh and tooldata.hh.
#ifndef TOOLDATA_FWD_HH
#define TOOLDATA_FWD_HH

#include "canon.hh" // CANON_TOOL_TABLE

#ifndef TOOLIDX_T_DEFINED
#define TOOLIDX_T_DEFINED

#ifdef __cplusplus
extern"C" {
#endif

typedef enum {
    IDX_OK = 0,
    IDX_NEW,
    IDX_FAIL,
} toolidx_t;

struct    CANON_TOOL_TABLE tooldata_entry_init(void);
toolidx_t tooldata_put(struct CANON_TOOL_TABLE tdata,int idx);
toolidx_t tooldata_get(CANON_TOOL_TABLE* pdata,int idx);

#ifdef __cplusplus
}
#endif

#endif // TOOLIDX_T_DEFINED

#endif // TOOLDATA_FWD_HH
