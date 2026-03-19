/********************************************************************
* Description:  uspace_ulapi.c
*               This file, 'uspace_ulapi.c', implements the user-level
*               API functions for machines without RT (simultated
*               processes)
*
* Author: John Kasunich, Paul Corner
* License: LGPL Version 2
*
* Copyright (c) 2004 All rights reserved.
*
* Last change:
********************************************************************/

#define _GNU_SOURCE

#include <stddef.h>		/* NULL */
#include <stdio.h>		/* printf */
#include <stdlib.h>		/* malloc(), free() */
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include "rtapi.h"
#include <unistd.h>
#include <rtapi_errno.h>
#include "rtapi/uspace_common.h"


/* FIXME - no support for fifos */

int rtapi_fifo_new(int key, int module_id, unsigned long int size, char mode)

{
  return -ENOSYS;
}

int rtapi_fifo_delete(int fifo_id, int module_id)
{
  return -ENOSYS;
}

int rtapi_fifo_read(int fifo_id, char *buf, unsigned long size)
{
  return -ENOSYS;
}

int rtapi_fifo_write(int fifo_id, char *buf, unsigned long int size)
{
  return -ENOSYS;
}

__attribute__((weak)) long long rtapi_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

__attribute__((weak)) long int rtapi_delay_max() { return 999999999; }

__attribute__((weak)) void rtapi_delay(long ns) {
    if(ns > rtapi_delay_max()) ns = rtapi_delay_max();
    struct timespec ts = {0, ns};
    rtapi_clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL, NULL);
}

__attribute__((weak)) void default_rtapi_msg_handler(msg_level_t level, const char *fmt, va_list ap) {
    if(level == RTAPI_MSG_ALL) {
	vfprintf(stdout, fmt, ap);
        fflush(stdout);
    } else {
	vfprintf(stderr, fmt, ap);
        fflush(stderr);
    }
}

/* Task/thread related functions - weak stubs for ULAPI context.
   These allow liblinuxcnchal.so to link and work standalone (e.g. halcmd,
   Python _hal module). When the Go launcher loads librtapi_uspace.so
   in-process, the real implementations override these weak stubs. */

__attribute__((weak)) int rtapi_prio_highest(void) { return 0; }
__attribute__((weak)) int rtapi_prio_lowest(void) { return 0; }
__attribute__((weak)) int rtapi_prio_next_higher(int prio) { return prio; }
__attribute__((weak)) int rtapi_prio_next_lower(int prio) { return prio; }

__attribute__((weak)) long int rtapi_clock_set_period(long int nsecs) { return -ENOSYS; }

__attribute__((weak)) int rtapi_task_new(void (*taskcode)(void *), void *arg,
        int prio, int owner, unsigned long int stacksize, int uses_fp) {
    return -ENOSYS;
}

__attribute__((weak)) int rtapi_task_delete(int task_id) { return -ENOSYS; }
__attribute__((weak)) int rtapi_task_start(int task_id, unsigned long int period_nsec) { return -ENOSYS; }
__attribute__((weak)) int rtapi_task_pause(int task_id) { return -ENOSYS; }
__attribute__((weak)) int rtapi_task_resume(int task_id) { return -ENOSYS; }
__attribute__((weak)) int rtapi_task_self(void) { return -EINVAL; }
__attribute__((weak)) void rtapi_wait(void) { /* no-op in ULAPI context */ }
