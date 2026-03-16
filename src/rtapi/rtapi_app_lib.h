#ifndef RTAPI_APP_LIB_H
#define RTAPI_APP_LIB_H

/* Public API for the in-process RTAPI engine library (librtapi_app.so).
 *
 * This header is the CGO bridge between the Go launcher and the
 * C realtime subsystem.  The implementation lives in uspace_rtapi_app.c
 * compiled with -DRTAPI_APP_LIB.
 *
 * Lifecycle
 * ---------
 *   1. rtapi_app_master_start()  — call on a dedicated OS thread (blocking)
 *      Creates the Unix socket, loads hal_lib, runs the master callback loop.
 *      The socket is the same one that halcmd connects to.
 *   2. rtapi_app_load() / rtapi_app_unload()  — direct in-process calls
 *      Bypass the socket IPC; thread-safe via modules_lock mutex.
 *   3. rtapi_app_master_stop()  — signal the master loop to exit
 *      Sets force_exit = 1 so the loop in rtapi_app_master_start() returns.
 */

/* Initialize the rtapi_app master.  This function:
 *   - Captures euid/ruid for privilege management
 *   - Performs harden_rt() if running with appropriate capabilities
 *   - Creates Unix domain socket, binds and listens on fifo_path
 *   - Loads hal_lib into this process
 *   - Runs the master callback loop (blocking — run on a dedicated thread)
 *
 * fifo_path  path for the Unix domain socket
 *            (e.g. "$HOME/.rtapi_fifo")
 *            If NULL, the path is derived from RTAPI_FIFO_PATH or $HOME.
 *
 * Returns  0 on success, negative errno on failure.
 * The function blocks until rtapi_app_master_stop() is called.
 */
int rtapi_app_master_start(const char *fifo_path);

/* Signal the master loop to exit and clean up the socket file. */
void rtapi_app_master_stop(void);

/* Load a realtime module directly (bypassing socket IPC).
 *
 * name   module name, e.g. "threads", "tpmod", "homemod"
 * args   argument vector: args[0] == name, args[1..nargs-1] are "key=value"
 * nargs  length of args
 *
 * Returns 0 on success, -1 on failure.
 * Thread-safe: protected internally by modules_lock.
 */
int rtapi_app_load(const char *name, char **args, int nargs);

/* Unload a previously loaded realtime module.
 *
 * Returns 0 on success, -1 on failure.
 */
int rtapi_app_unload(const char *name);

/* Returns non-zero if the engine is running in hard-RT mode (SCHED_FIFO). */
int rtapi_app_is_realtime(void);

#endif /* RTAPI_APP_LIB_H */
