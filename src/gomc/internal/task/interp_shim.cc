// interp_shim.cc — thin C++ wrapper exposing InterpBase virtual calls as plain C.
// This is the ONLY C++ code in the Go milltask module.

#include "config.h"
#include "emc/rs274ngc/interp_base.hh"

// Include the generated canon callback table header
#define CANON_API_CGO
#include "gomc/generated/gmi/canon/canon_api.h"

extern "C" {

// interp_new creates the default interpreter (rs274ngc).
void *interp_new(void) {
    return static_cast<void*>(makeInterp());
}

// interp_from_lib loads an interpreter from a shared library.
void *interp_from_lib(const char *shlib) {
    return static_cast<void*>(interp_from_shlib(shlib));
}

// interp_delete destroys an interpreter instance.
void interp_delete(void *handle) {
    delete static_cast<InterpBase*>(handle);
}

// interp_ini_load loads INI configuration.
int interp_ini_load(void *handle, const char *inifile) {
    return static_cast<InterpBase*>(handle)->ini_load(inifile);
}

// interp_init initializes the interpreter.
int interp_init(void *handle) {
    return static_cast<InterpBase*>(handle)->init();
}

// interp_open opens a G-code file for interpretation.
int interp_open(void *handle, const char *filename) {
    return static_cast<InterpBase*>(handle)->open(filename);
}

// interp_read reads the next line from the open file.
int interp_read(void *handle) {
    return static_cast<InterpBase*>(handle)->read();
}

// interp_read_string reads a line from a string.
int interp_read_string(void *handle, const char *line) {
    return static_cast<InterpBase*>(handle)->read(line);
}

// interp_execute executes the last read line.
int interp_execute(void *handle) {
    return static_cast<InterpBase*>(handle)->execute();
}

// interp_execute_string executes a string directly.
int interp_execute_string(void *handle, const char *line) {
    return static_cast<InterpBase*>(handle)->execute(line);
}

// interp_execute_string_lineno executes a string with explicit line number.
int interp_execute_string_lineno(void *handle, const char *line, int line_number) {
    return static_cast<InterpBase*>(handle)->execute(line, line_number);
}

// interp_synch synchronizes interpreter state with the machine.
int interp_synch(void *handle) {
    return static_cast<InterpBase*>(handle)->synch();
}

// interp_close closes the currently open file.
int interp_close(void *handle) {
    return static_cast<InterpBase*>(handle)->close();
}

// interp_reset resets the interpreter to initial state.
int interp_reset(void *handle) {
    return static_cast<InterpBase*>(handle)->reset();
}

// interp_exit exits the interpreter.
int interp_exit(void *handle) {
    return static_cast<InterpBase*>(handle)->exit();
}

// interp_on_abort notifies the interpreter of an abort condition.
int interp_on_abort(void *handle, int reason, const char *message) {
    return static_cast<InterpBase*>(handle)->on_abort(reason, message);
}

// interp_set_canon_callbacks sets the canon callback table.
void interp_set_canon_callbacks(void *handle, const canon_callbacks_t *cb) {
    static_cast<InterpBase*>(handle)->set_canon_callbacks(cb);
}

// interp_line returns the current line number.
int interp_line(void *handle) {
    return static_cast<InterpBase*>(handle)->line();
}

// interp_sequence_number returns the current sequence number (N-word).
int interp_sequence_number(void *handle) {
    return static_cast<InterpBase*>(handle)->sequence_number();
}

// interp_call_level returns the current subroutine call level.
int interp_call_level(void *handle) {
    return static_cast<InterpBase*>(handle)->call_level();
}

// interp_error_text retrieves the error text for a return code.
const char *interp_error_text(void *handle, int errcode, char *buf, size_t buflen) {
    return static_cast<InterpBase*>(handle)->error_text(errcode, buf, buflen);
}

// interp_line_text retrieves the current line text.
const char *interp_line_text(void *handle, char *buf, size_t buflen) {
    return static_cast<InterpBase*>(handle)->line_text(buf, buflen);
}

// interp_file_name retrieves the current file name.
const char *interp_file_name(void *handle, char *buf, size_t buflen) {
    return static_cast<InterpBase*>(handle)->file_name(buf, buflen);
}

// interp_command retrieves the current command text.
const char *interp_command(void *handle, char *buf, size_t buflen) {
    return static_cast<InterpBase*>(handle)->command(buf, buflen);
}

// interp_set_loglevel sets the interpreter log level.
void interp_set_loglevel(void *handle, int level) {
    static_cast<InterpBase*>(handle)->set_loglevel(level);
}

// interp_set_loop_on_main_m99 controls M99 loop behavior.
void interp_set_loop_on_main_m99(void *handle, int state) {
    static_cast<InterpBase*>(handle)->set_loop_on_main_m99(state != 0);
}

} // extern "C"
