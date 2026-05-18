/********************************************************************
* Description: emccanon_table.hh
*   Factory function for the emccanon callback table.
*
* License: GPL Version 2
********************************************************************/
#ifndef EMCCANON_TABLE_HH
#define EMCCANON_TABLE_HH

typedef struct canon_callbacks canon_callbacks_t;  // forward declaration
struct EMC_STAT;
class NML_INTERP_LIST;

#ifdef __cplusplus
extern "C" {
#endif

// Returns the emccanon callback table wrapping the existing global
// canon implementation. The returned pointer is valid for the lifetime
// of the process. The .ctx field is set by emccanon_init_context().
canon_callbacks_t *emccanon_get_callbacks(void);

// Initialize the canon context with pointers to shared state.
// Must be called before any canon callbacks are invoked.
void emccanon_init_context(EMC_STAT *stat, NML_INTERP_LIST *ilist);

#ifdef __cplusplus
}
#endif

#endif // EMCCANON_TABLE_HH
