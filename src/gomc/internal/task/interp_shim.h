// interp_shim.h — C declarations for the interpreter shim functions.
// This header is included by interp.go via cgo.

#ifndef INTERP_SHIM_H
#define INTERP_SHIM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Interpreter lifecycle
void *interp_new(void);
void *interp_from_lib(const char *shlib);
void interp_delete(void *handle);

// Configuration and initialization
int interp_ini_load(void *handle, const char *inifile);
int interp_init(void *handle);

// File operations
int interp_open(void *handle, const char *filename);
int interp_close(void *handle);
int interp_reset(void *handle);
int interp_exit(void *handle);

// Read/Execute
int interp_read(void *handle);
int interp_read_string(void *handle, const char *line);
int interp_execute(void *handle);
int interp_execute_string(void *handle, const char *line);
int interp_execute_string_lineno(void *handle, const char *line, int line_number);
int interp_synch(void *handle);

// State queries
int interp_line(void *handle);
int interp_sequence_number(void *handle);
int interp_call_level(void *handle);
int interp_on_abort(void *handle, int reason, const char *message);

// String queries
const char *interp_error_text(void *handle, int errcode, char *buf, size_t buflen);
const char *interp_line_text(void *handle, char *buf, size_t buflen);
const char *interp_file_name(void *handle, char *buf, size_t buflen);
const char *interp_command(void *handle, char *buf, size_t buflen);

// Configuration
void interp_set_loglevel(void *handle, int level);
void interp_set_loop_on_main_m99(void *handle, int state);

// Canon callback wiring
struct canon_callbacks;
typedef struct canon_callbacks canon_callbacks_t;
void interp_set_canon_callbacks(void *handle, const canon_callbacks_t *cb);

// M-code handler registration (M100-M199)
// Register a user-defined function slot in the interpreter.
// The slot will call the provided function pointer when M(100+idx) is encountered.
void interp_set_user_defined_function(void *handle, int idx,
    void (*fn)(int num, double arg1, double arg2));

// Active G/M codes and settings
// These copy the interpreter's internal arrays into caller-provided buffers.
void interp_active_g_codes(void *handle, int *gcodes, int max_len);
void interp_active_m_codes(void *handle, int *mcodes, int max_len);
void interp_active_settings(void *handle, double *settings, int max_len);

#ifdef __cplusplus
}
#endif

#endif // INTERP_SHIM_H
