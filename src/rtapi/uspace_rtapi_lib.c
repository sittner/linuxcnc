/* Copyright (C) 2006-2026 Jeff Epler <jepler@unpythonic.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include "linuxcnc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <spawn.h>
#include <sched.h>
#include <pthread.h>
#ifdef HAVE_SYS_IO_H
#include <sys/io.h>
#endif
#include <sys/resource.h>
#include <sys/mman.h>
#ifdef __linux__
#include <malloc.h>
#include <sys/prctl.h>
#endif
#ifdef __FreeBSD__
#include <pthread_np.h>
#endif

#include "rtapi.h"
#include "rtapi_task.h"
#include "hal.h"
#include "hal/hal_priv.h"

/* Declarations for compatibility with uspace_common.h */
static uid_t euid = 0, ruid = 0;

/* Helper function for rtapi_timespec_less */
static int rtapi_timespec_less(const struct timespec ta, const struct timespec tb) {
    if(ta.tv_sec < tb.tv_sec) return 1;
    if(ta.tv_sec > tb.tv_sec) return 0;
    return ta.tv_nsec < tb.tv_nsec;
}

/* Forward declaration of rtapi_timespec_advance */
void rtapi_timespec_advance(struct timespec *result, const struct timespec *src, unsigned long nsec);

/* No-op stubs: under capabilities (cap_sys_nice, cap_ipc_lock, cap_sys_rawio),
 * euid == ruid so the old setfsuid() toggling is unnecessary. */
static void with_root_enter(void) {}
static void with_root_exit(void) {}

#include "rtapi/uspace_common.h"

void rtapi_set_namef(const char *fmt, ...) {
    char *buf = NULL;
    va_list ap;

    va_start(ap, fmt);
    if (vasprintf(&buf, fmt, ap) < 0) {
        va_end(ap);
        return;
    }
    va_end(ap);

    int res = pthread_setname_np(pthread_self(), buf);
    if (res) {
        fprintf(stderr, "pthread_setname_np() failed for %s: %d\n", buf, res);
    }
    free(buf);
}

/* Task and RTAPI structures */

struct rtapi_module {
    int magic;
};

#define MODULE_MAGIC  30812
#define SHMEM_MAGIC   25453

#define MAX_MODULES_INT  64
#define MODULE_OFFSET 32768

struct posix_task {
    struct rtapi_task task;
    pthread_t thr;
};

/* Global application state */
static int app_policy = SCHED_FIFO;
static long app_period = 0;
static int do_thread_lock = 0;

static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static pthread_key_t task_key;
static void init_task_key(void) {
    pthread_key_create(&task_key, NULL);
}

static pthread_once_t lock_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t thread_lock;
static void init_thread_lock(void) {
    pthread_mutex_init(&thread_lock, NULL);
}

static void signal_handler(int sig, siginfo_t *si, void *uctx)
{
    (void)si;
    (void)uctx;
    switch (sig) {
    case SIGXCPU:
        rtapi_print_msg(RTAPI_MSG_ERR,
                        "BUG: SIGXCPU received - exiting\n");
        exit(0);
        break;

    default:
        rtapi_print_msg(RTAPI_MSG_ERR,
                        "caught signal %d - dumping core\n", sig);
        sleep(1);
        signal(sig, SIG_DFL);
        raise(sig);
        break;
    }
    exit(1);
}

static const struct rlimit unlimited = {RLIM_INFINITY, RLIM_INFINITY};

/* Allocate memory suitable for realtime use: pre-fault + mlock. */
void *rtapi_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) return NULL;

    /* Pre-fault all pages (read+write) */
    long pagesize = sysconf(_SC_PAGESIZE);
    volatile char *c = (volatile char *)p;
    for (size_t i = 0; i < size; i += pagesize) {
        c[i] = c[i];
    }
    /* Touch the last page if size is not a multiple of pagesize */
    if (size > 0 && (size % (size_t)pagesize) != 0) {
        c[size - 1] = c[size - 1];
    }

    /* Lock into physical RAM */
    if (mlock(p, size) < 0) {
        rtapi_print_msg(RTAPI_MSG_WARN,
            "rtapi_malloc: mlock(%zu) failed: %s\n", size, strerror(errno));
    }
    return p;
}

/* Free realtime-locked memory. */
void rtapi_free(void *p, size_t size) {
    if (!p) return;
    munlock(p, size);
    free(p);
}

static void configure_memory(void)
{
    /* Raise memlock rlimit — needed for per-region mlock() and SHM_LOCK */
    int res = setrlimit(RLIMIT_MEMLOCK, &unlimited);
    if(res < 0) perror("setrlimit");

    /* Do NOT mlockall() — it would lock the entire Go heap.
     * RT memory is locked individually:
     *   - SysV shmem segments: SHM_LOCK in rtapi_shmem_new()
     *   - Task structs: mlock() in rtapi_malloc()
     *   - Thread stacks: mlock() in task_start()
     */

#ifdef __linux__
    /* Prevent glibc from returning C-side malloc pages to OS
     * (avoids page faults on reuse). Does not affect Go allocator. */
    if (!mallopt(M_TRIM_THRESHOLD, -1)) {
        rtapi_print_msg(RTAPI_MSG_WARN,
                  "mallopt(M_TRIM_THRESHOLD, -1) failed\n");
    }
    if (!mallopt(M_MMAP_MAX, 0)) {
        rtapi_print_msg(RTAPI_MSG_WARN,
                  "mallopt(M_MMAP_MAX, 0) failed\n");
    }
#endif
}

static int harden_rt(void)
{
    /* Initialize euid/ruid here; used by uspace_common.h for shmem ownership. */
    euid = geteuid();
    ruid = getuid();

    /* With setcap-based privileges (cap_sys_nice, cap_ipc_lock, cap_sys_rawio)
     * we no longer need setuid or root.  Capabilities are inherited by the
     * process, so iopl/mlockall/SCHED_FIFO work without uid juggling. */

#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))
    if (iopl(3) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR,
                        "iopl() failed: %s\n"
                        "cannot gain I/O privileges - "
                        "missing cap_sys_rawio capability or using secure boot? -"
                        "parallel port access is not allowed\n",
                        strerror(errno));
    }
#endif

    struct sigaction sig_act;
    memset(&sig_act, 0, sizeof(sig_act));
#ifdef __linux__
    if (setrlimit(RLIMIT_RTPRIO, &unlimited) < 0)
    {
        rtapi_print_msg(RTAPI_MSG_WARN,
                  "setrlimit(RTLIMIT_RTPRIO): %s\n",
                  strerror(errno));
        return -errno;
    }

    if (setrlimit(RLIMIT_CORE, &unlimited) < 0)
        rtapi_print_msg(RTAPI_MSG_WARN,
                  "setrlimit: %s - core dumps may be truncated or non-existent\n",
                  strerror(errno));

    if (prctl(PR_SET_DUMPABLE, 1) < 0)
        rtapi_print_msg(RTAPI_MSG_WARN,
                  "prctl(PR_SET_DUMPABLE) failed: no core dumps will be created - %d - %s\n",
                  errno, strerror(errno));
#endif

    configure_memory();

    sigemptyset(&sig_act.sa_mask);
    sig_act.sa_handler = SIG_IGN;
    sig_act.sa_sigaction = NULL;

    sigaction(SIGTSTP, &sig_act, (struct sigaction *) NULL);

    sig_act.sa_sigaction = signal_handler;
    sig_act.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &sig_act, (struct sigaction *) NULL);
    sigaction(SIGILL,  &sig_act, (struct sigaction *) NULL);
    sigaction(SIGFPE,  &sig_act, (struct sigaction *) NULL);
    /* SIGTERM and SIGINT are handled by the Go runtime / launcher;
     * do not override them here to avoid conflicting handlers. */

#ifdef __linux__
    int fd = open("/dev/cpu_dma_latency", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        rtapi_print_msg(RTAPI_MSG_WARN, "failed to open /dev/cpu_dma_latency: %s\n", strerror(errno));
    } else {
        int r;
        r = write(fd, "\0\0\0\0", 4);
        if (r != 4) {
            rtapi_print_msg(RTAPI_MSG_WARN, "failed to write to /dev/cpu_dma_latency: %s\n", strerror(errno));
        }
    }
#endif
    return 0;
}

static void initialize_app(void)
{
    static int initialized = 0;
    if(initialized) return;
    initialized = 1;
    
    if(harden_rt() < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Note: Using POSIX non-realtime\n");
        app_policy = SCHED_OTHER;
        do_thread_lock = 1;
    } else {
        rtapi_print_msg(RTAPI_MSG_ERR, "Note: Using POSIX realtime\n");
        app_policy = SCHED_FIFO;
        do_thread_lock = 0;
    }
    
    pthread_once(&key_once, init_task_key);
    if(do_thread_lock) {
        pthread_once(&lock_once, init_thread_lock);
    }
}

struct rtapi_task *task_array[MAX_TASKS];

static int prio_highest(void)
{
    return sched_get_priority_max(app_policy);
}

static int prio_lowest(void)
{
    return sched_get_priority_min(app_policy);
}

static int prio_higher_delta(void) {
    if(rtapi_prio_highest() > rtapi_prio_lowest()) {
        return 1;
    }
    return -1;
}

static int prio_bound(int prio) {
    if(rtapi_prio_highest() > rtapi_prio_lowest()) {
        if (prio >= rtapi_prio_highest())
            return rtapi_prio_highest();
        if (prio < rtapi_prio_lowest())
            return rtapi_prio_lowest();
    } else {
        if (prio <= rtapi_prio_highest())
            return rtapi_prio_highest();
        if (prio > rtapi_prio_lowest())
            return rtapi_prio_lowest();
    }
    return prio;
}

static int prio_next_higher(int prio)
{
    prio = prio_bound(prio);
    if(prio != rtapi_prio_highest())
        return prio + prio_higher_delta();
    return prio;
}

static int prio_next_lower(int prio)
{
    prio = prio_bound(prio);
    if(prio != rtapi_prio_lowest())
        return prio - prio_higher_delta();
    return prio;
}

static int allocate_task_id(void)
{
    for(int n = 0; n < MAX_TASKS; n++)
    {
        struct rtapi_task **taskptr = &(task_array[n]);
        if(__sync_bool_compare_and_swap(taskptr, (struct rtapi_task*)0, TASK_MAGIC_INIT))
            return n;
    }
    return -ENOSPC;
}

static struct rtapi_task *get_task(int task_id) {
    if(task_id < 0 || task_id >= MAX_TASKS) return NULL;
    struct rtapi_task *task = task_array[task_id];
    if(!task || task == TASK_MAGIC_INIT || task->magic != TASK_MAGIC)
        return NULL;
    return task;
}

static void unexpected_realtime_delay(struct rtapi_task *task, int nperiod) {
    static int printed = 0;
    (void)nperiod;
    if(!printed)
    {
        rtapi_print_msg(RTAPI_MSG_ERR,
                "Unexpected realtime delay on task %d with period %ld\n"
                "This Message will only display once per session.\n"
                "Run the Latency Test and resolve before continuing.\n",
                task->id, task->period);
        printed = 1;
    }
}

static int find_rt_cpu_number(void) {
    if(getenv("RTAPI_CPU_NUMBER")) return atoi(getenv("RTAPI_CPU_NUMBER"));

#ifdef __linux__
    cpu_set_t cpuset_orig;
    int r = sched_getaffinity(getpid(), sizeof(cpuset_orig), &cpuset_orig);
    if(r < 0)
        return 0;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    long top_probe = sysconf(_SC_NPROCESSORS_CONF);
    for(long i = 0; i < top_probe && i < CPU_SETSIZE; i++) CPU_SET(i, &cpuset);

    r = sched_setaffinity(getpid(), sizeof(cpuset), &cpuset);
    if(r < 0)
        perror("sched_setaffinity");

    r = sched_getaffinity(getpid(), sizeof(cpuset), &cpuset);
    if(r < 0) {
        perror("sched_getaffinity");
        CPU_AND(&cpuset, &cpuset_orig, &cpuset);
    }

    int top = -1;
    for(int i = 0; i < CPU_SETSIZE; i++) {
        if(CPU_ISSET(i, &cpuset)) top = i;
    }
    return top;
#else
    return -1;
#endif
}

static void *task_wrapper(void *arg);

static int task_start(int task_id, unsigned long int period_nsec)
{
    struct posix_task *task = (struct posix_task*)get_task(task_id);
    if(!task) return -EINVAL;

    if(period_nsec < (unsigned long)app_period) period_nsec = (unsigned long)app_period;
    task->task.period = period_nsec;
    task->task.ratio = period_nsec / app_period;

    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = task->task.prio;

    task->task.pll_correction_limit = period_nsec / 100;
    task->task.pll_correction = 0;

    int nprocs = sysconf(_SC_NPROCESSORS_ONLN);

    pthread_attr_t attr;
    int ret;
    if((ret = pthread_attr_init(&attr)) != 0)
        return -ret;
    if((ret = pthread_attr_setstacksize(&attr, task->task.stacksize)) != 0)
        return -ret;
    if((ret = pthread_attr_setschedpolicy(&attr, app_policy)) != 0)
        return -ret;
    if((ret = pthread_attr_setschedparam(&attr, &param)) != 0)
        return -ret;
    if((ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) != 0)
        return -ret;
    if(nprocs > 1) {
        static int rt_cpu_number = -2;  /* -2 means uninitialized, call find_rt_cpu_number() */
        int cpu_num;
        if(rt_cpu_number == -2) {
            rt_cpu_number = find_rt_cpu_number();
        }
        cpu_num = rt_cpu_number;
        if(cpu_num != -1) {
#ifdef __FreeBSD__
            cpuset_t cpuset;
#else
            cpu_set_t cpuset;
#endif
            CPU_ZERO(&cpuset);
            CPU_SET(cpu_num, &cpuset);
            if((ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset)) != 0)
                return -ret;
        }
    }
    if((ret = pthread_create(&task->thr, &attr, &task_wrapper, (void*)task)) != 0)
        return -ret;

    return 0;
}

#define RTAPI_CLOCK (CLOCK_MONOTONIC)

static void *task_wrapper(void *arg)
{
    struct posix_task *ptask = (struct posix_task*)arg;
    struct rtapi_task *task = &ptask->task;

    /* Lock our own stack into RAM — must happen before any RT work.
     * Uses pthread_self() so there is no race with the parent thread. */
#ifdef __linux__
    {
        pthread_attr_t self_attr;
        void *stackaddr;
        size_t stacksize, guardsize;
        if (pthread_getattr_np(pthread_self(), &self_attr) == 0) {
            if (pthread_attr_getstack(&self_attr, &stackaddr, &stacksize) == 0
                && pthread_attr_getguardsize(&self_attr, &guardsize) == 0) {
                /* Skip guard page(s) at the bottom — they are PROT_NONE,
                 * mlock() on them would fail with ENOMEM. */
                void *lockaddr = (char*)stackaddr + guardsize;
                size_t locksize = stacksize - guardsize;
                /* Pre-fault every page of the usable stack */
                volatile char *p = (volatile char *)lockaddr;
                long pagesize = sysconf(_SC_PAGESIZE);
                for (size_t i = 0; i < locksize; i += pagesize) {
                    (void)p[i];
                }
                if (mlock(lockaddr, locksize) < 0) {
                    rtapi_print_msg(RTAPI_MSG_WARN,
                        "task_wrapper: mlock stack (%zu bytes) failed: %s\n",
                        locksize, strerror(errno));
                }
            }
            pthread_attr_destroy(&self_attr);
        }
    }
#endif

    long int period = app_period;
    if(task->period < period) task->period = period;
    task->ratio = task->period / period;
    task->period = task->ratio * period;
    rtapi_print_msg(RTAPI_MSG_INFO, "task %p period = %lu ratio=%u\n",
          (void*)task, task->period, task->ratio);

    pthread_setspecific(task_key, arg);
    rtapi_set_namef("rtapi:T#%d", task->id);

    if(do_thread_lock)
        pthread_mutex_lock(&thread_lock);

    struct timespec now;
    clock_gettime(RTAPI_CLOCK, &now);
    rtapi_timespec_advance(&task->nextstart, &now, task->period + task->pll_correction);

    (task->taskcode)(task->arg);

    rtapi_print("ERROR: reached end of wrapper for task %d\n", task->id);
    return NULL;
}

static int task_delete(int id)
{
    struct posix_task *task = (struct posix_task*)get_task(id);
    if(!task) return -EINVAL;

    pthread_cancel(task->thr);
    pthread_join(task->thr, 0);
    task->task.magic = 0;
    task_array[id] = 0;
    rtapi_free(task, sizeof(struct posix_task));
    return 0;
}

static int task_new(void (*taskcode)(void*), void *arg,
        int prio, int owner, unsigned long int stacksize, int uses_fp) {
    if ((prio > rtapi_prio_highest()) || (prio < rtapi_prio_lowest()))
    {
        return -EINVAL;
    }

    int n = allocate_task_id();
    if(n < 0) return n;

    struct posix_task *task = (struct posix_task*)rtapi_malloc(sizeof(struct posix_task));
    if(!task) {
        task_array[n] = 0;
        return -ENOMEM;
    }
    memset(task, 0, sizeof(*task));
    
    if(stacksize < (1024*1024)) stacksize = (1024*1024);
    task->task.id = n;
    task->task.owner = owner;
    task->task.uses_fp = uses_fp;
    task->task.arg = arg;
    task->task.stacksize = stacksize;
    task->task.taskcode = taskcode;
    task->task.prio = prio;
    task->task.magic = TASK_MAGIC;
    task_array[n] = &task->task;

    return n;
}

static long long task_pll_get_reference(void) {
    struct rtapi_task *task = (struct rtapi_task*)pthread_getspecific(task_key);
    if(!task) return 0;
    return task->nextstart.tv_sec * 1000000000LL + task->nextstart.tv_nsec;
}

static int task_pll_set_correction(long value) {
    struct rtapi_task *task = (struct rtapi_task*)pthread_getspecific(task_key);
    if(!task) return -EINVAL;
    if (value > task->pll_correction_limit) value = task->pll_correction_limit;
    if (value < -(task->pll_correction_limit)) value = -(task->pll_correction_limit);
    task->pll_correction = value;
    return 0;
}

static int task_pause(int task_id) {
    (void)task_id;
    return -ENOSYS;
}

static int task_resume(int task_id) {
    (void)task_id;
    return -ENOSYS;
}

static int task_self(void) {
    struct rtapi_task *task = (struct rtapi_task*)pthread_getspecific(task_key);
    if(!task) return -EINVAL;
    return task->id;
}

static void task_wait(void) {
    if(do_thread_lock)
        pthread_mutex_unlock(&thread_lock);
    pthread_testcancel();
    struct rtapi_task *task = (struct rtapi_task*)pthread_getspecific(task_key);
    if(!task) {
        rtapi_print_msg(RTAPI_MSG_ERR, "rtapi_wait called from non-task thread\n");
        if(do_thread_lock)
            pthread_mutex_lock(&thread_lock);
        return;
    }
    rtapi_timespec_advance(&task->nextstart, &task->nextstart, task->period + task->pll_correction);
    struct timespec now;
    clock_gettime(RTAPI_CLOCK, &now);
    if(rtapi_timespec_less(task->nextstart, now))
    {
        if(app_policy == SCHED_FIFO)
            unexpected_realtime_delay(task, 0);
    }
    else
    {
        int res = rtapi_clock_nanosleep(RTAPI_CLOCK, TIMER_ABSTIME, &task->nextstart, NULL, &now);
        if(res < 0) perror("clock_nanosleep");
    }
    if(do_thread_lock)
        pthread_mutex_lock(&thread_lock);
}

static unsigned char do_inb(unsigned int port)
{
#ifdef HAVE_SYS_IO_H
    return inb(port);
#else
    (void)port;
    return 0;
#endif
}

static void do_outb(unsigned char val, unsigned int port)
{
#ifdef HAVE_SYS_IO_H
    outb(val, port);
#else
    (void)val;
    (void)port;
#endif
}

static void do_delay(long ns) {
    struct timespec ts = {0, ns};
    rtapi_clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL, NULL);
}

static long long do_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long clock_set_period(long nsecs)
{
    if(nsecs == 0) return app_period;
    if(app_period != 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "attempt to set period twice\n");
        return -EINVAL;
    }
    app_period = nsecs;
    return app_period;
}

/* Public API functions */

int rtapi_prio_highest(void)
{
    initialize_app();
    return prio_highest();
}

int rtapi_prio_lowest(void)
{
    initialize_app();
    return prio_lowest();
}

int rtapi_prio_next_higher(int prio)
{
    initialize_app();
    return prio_next_higher(prio);
}

int rtapi_prio_next_lower(int prio)
{
    initialize_app();
    return prio_next_lower(prio);
}

long rtapi_clock_set_period(long nsecs)
{
    initialize_app();
    return clock_set_period(nsecs);
}

int rtapi_task_new(void (*taskcode)(void*), void *arg,
        int prio, int owner, unsigned long int stacksize, int uses_fp) {
    initialize_app();
    return task_new(taskcode, arg, prio, owner, stacksize, uses_fp);
}

int rtapi_task_delete(int id) {
    return task_delete(id);
}

int rtapi_task_start(int task_id, unsigned long period_nsec)
{
    int ret = task_start(task_id, period_nsec);
    if(ret != 0) {
        errno = -ret;
        perror("rtapi_task_start()");
    }
    return ret;
}

int rtapi_task_pause(int task_id)
{
    return task_pause(task_id);
}

int rtapi_task_resume(int task_id)
{
    return task_resume(task_id);
}

int rtapi_task_self(void)
{
    return task_self();
}

long long rtapi_task_pll_get_reference(void)
{
    return task_pll_get_reference();
}

int rtapi_task_pll_set_correction(long value)
{
    return task_pll_set_correction(value);
}

void rtapi_wait(void)
{
    task_wait();
}

void rtapi_outb(unsigned char byte, unsigned int port)
{
    do_outb(byte, port);
}

unsigned char rtapi_inb(unsigned int port)
{
    return do_inb(port);
}

long int simple_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

long long rtapi_get_time(void) {
    return do_get_time();
}

void default_rtapi_msg_handler(msg_level_t level, const char *fmt, va_list ap) {
    if(level == RTAPI_MSG_ALL) {
	vfprintf(stdout, fmt, ap);
        fflush(stdout);
    } else {
	vfprintf(stderr, fmt, ap);
        fflush(stderr);
    }
}

long int rtapi_delay_max(void) { return 10000; }

void rtapi_delay(long ns) {
    if(ns > rtapi_delay_max()) ns = rtapi_delay_max();
    do_delay(ns);
}

const unsigned long ONE_SEC_IN_NS = 1000000000;
void rtapi_timespec_advance(struct timespec *result, const struct timespec *src, unsigned long nsec)
{
    time_t sec = src->tv_sec;
    while(nsec >= ONE_SEC_IN_NS)
    {
        ++sec;
        nsec -= ONE_SEC_IN_NS;
    }
    nsec += src->tv_nsec;
    if(nsec >= ONE_SEC_IN_NS)
    {
        ++sec;
        nsec -= ONE_SEC_IN_NS;
    }
    result->tv_sec = sec;
    result->tv_nsec = nsec;
}

int rtapi_open_as_root(const char *filename, int mode) {
    int r = open(filename, mode);
    if(r < 0) return -errno;
    return r;
}

int rtapi_spawn_as_root(pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *attrp,
    char *const argv[], char *const envp[])
{
    return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

int rtapi_spawnp_as_root(pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *attrp,
    char *const argv[], char *const envp[])
{
    return posix_spawnp(pid, path, file_actions, attrp, argv, envp);
}
