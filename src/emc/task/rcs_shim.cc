// rcs_shim.cc — Lightweight replacements for libnml rcs_print/timer
// functions used by milltask.  Avoids linking libnml.so entirely.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "rcs_print.hh"
#include "timer.hh"

// --- rcs_print ---

static RCS_PRINT_DESTINATION_TYPE rcs_print_dest = RCS_PRINT_TO_STDOUT;
int max_rcs_errors_to_print = -1;
int rcs_errors_printed = 0;

void set_rcs_print_destination(RCS_PRINT_DESTINATION_TYPE _dest)
{
    rcs_print_dest = _dest;
}

RCS_PRINT_DESTINATION_TYPE get_rcs_print_destination(void)
{
    return rcs_print_dest;
}

int rcs_print(const char *_fmt, ...)
{
    if (rcs_print_dest == RCS_PRINT_TO_NULL)
        return 0;
    va_list ap;
    va_start(ap, _fmt);
    int r = vfprintf(stderr, _fmt, ap);
    va_end(ap);
    return r;
}

static const char *rcs_error_file = NULL;
static int rcs_error_line = -1;

int set_print_rcs_error_info(const char *file, int line)
{
    rcs_error_file = file;
    rcs_error_line = line;
    return 0;
}

int print_rcs_error_new(const char *_fmt, ...)
{
    if (rcs_print_dest == RCS_PRINT_TO_NULL)
        return 0;
    va_list ap;
    va_start(ap, _fmt);
    int r = vfprintf(stderr, _fmt, ap);
    va_end(ap);
    rcs_error_file = NULL;
    rcs_error_line = -1;
    return r;
}

void set_rcs_print_flag(long) {}
void clear_rcs_print_flag(long) {}

int rcs_print_debug(long, const char *_fmt, ...)
{
    return 0;
}

// --- etime / esleep ---

double etime(void)
{
    struct timeval tp;
    if (0 != gettimeofday(&tp, NULL))
        return 0.0;
    return ((double)tp.tv_sec) + ((double)tp.tv_usec) / 1000000.0;
}

void esleep(double seconds_to_sleep)
{
    if (seconds_to_sleep <= 0.0)
        return;
    usleep((useconds_t)(seconds_to_sleep * 1e6));
}

// --- RCS_TIMER ---

RCS_TIMER::RCS_TIMER(double _timeout, const char *, const char *)
{
    zero_timer();
    set_timeout(_timeout);
}

RCS_TIMER::~RCS_TIMER() {}

void RCS_TIMER::zero_timer()
{
    timeout = 0.0;
    last_time = etime();
    start_time = last_time;
    idle = 0.0;
    counts = 0;
    counts_since_real_sleep = 0;
    counts_per_real_sleep = 0;
    time_since_real_sleep = 0.0;
    num_sems = 0;
    id = 0;
    function = NULL;
    arg = NULL;
    clk_tck_val = 0.01;
}

void RCS_TIMER::set_timeout(double _timeout)
{
    timeout = _timeout;
}

int RCS_TIMER::wait()
{
    double time_in = etime();
    double interval = time_in - last_time;
    counts++;
    double remaining = timeout - interval;
    idle += interval;
    if (remaining > 0.0)
        esleep(remaining);
    last_time = etime();
    return (interval > timeout) ? 1 : 0;
}
