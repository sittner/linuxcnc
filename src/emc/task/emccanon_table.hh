/********************************************************************
* Description: emccanon_table.hh
*   Factory function for the emccanon callback table.
*
* License: GPL Version 2
********************************************************************/
#ifndef EMCCANON_TABLE_HH
#define EMCCANON_TABLE_HH

typedef struct canon_callbacks canon_callbacks_t;  // forward declaration

#ifdef __cplusplus
extern "C" {
#endif

// Returns the emccanon callback table wrapping the existing global
// canon implementation. The returned pointer is valid for the lifetime
// of the process.
const canon_callbacks_t *emccanon_get_callbacks(void);

#ifdef __cplusplus
}
#endif

#endif // EMCCANON_TABLE_HH
