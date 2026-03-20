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

#include <stdatomic.h>

#ifdef __linux__
#include <sys/fsuid.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
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
static uid_t euid, ruid;

/* Forward declaration of rtapi_timespec_advance */
void rtapi_timespec_advance(struct timespec *result, const struct timespec *src, unsigned long nsec);
static _Atomic int with_root_level = 0;

static void with_root_enter(void) {
    if(atomic_fetch_add(&with_root_level, 1) == 0) {
#ifdef __linux__
        setfsuid(euid);
#endif
    }
}

static void with_root_exit(void) {
    if(atomic_fetch_sub(&with_root_level, 1) == 1) {
#ifdef __linux__
        setfsuid(ruid);
#endif
    }
}

void __attribute__((constructor)) init_root_func(void) {
    euid = geteuid();
    ruid = getuid();
}

#include "rtapi/uspace_common.h"

/* Module table */
#define MAX_MODULES 64
struct module_entry {
    char name[256];
    void *handle;
    int in_use;
};
static struct module_entry modules[MAX_MODULES];
static pthread_mutex_t modules_lock = PTHREAD_MUTEX_INITIALIZER;

/* Message queue */
#define MSG_QUEUE_SIZE 128
struct message_t {
    msg_level_t level;
    char msg[1024];
};
static struct message_t msg_queue[MSG_QUEUE_SIZE];
static _Atomic int msg_head = 0;
static _Atomic int msg_tail = 0;

static void msg_queue_push(msg_level_t level, const char *msg) {
    int head = atomic_load_explicit(&msg_head, memory_order_relaxed);
    int next = (head + 1) % MSG_QUEUE_SIZE;
    
    /* Check if queue is full (don't block, just drop) */
    if(next == atomic_load_explicit(&msg_tail, memory_order_acquire)) {
        return;  /* Queue full, message dropped */
    }
    
    /* Write the message */
    msg_queue[head].level = level;
    snprintf(msg_queue[head].msg, sizeof(msg_queue[head].msg), "%s", msg);
    
    /* Publish the new head (release ensures msg is visible before head update) */
    atomic_store_explicit(&msg_head, next, memory_order_release);
}

static int msg_queue_consume_all(void) {
    int processed = 0;
    int tail = atomic_load_explicit(&msg_tail, memory_order_relaxed);
    
    while(tail != atomic_load_explicit(&msg_head, memory_order_acquire)) {
        /* Copy message to local buffer before updating tail */
        msg_level_t level = msg_queue[tail].level;
        char msg_copy[sizeof(msg_queue[tail].msg)];
        strncpy(msg_copy, msg_queue[tail].msg, sizeof(msg_copy) - 1);
        msg_copy[sizeof(msg_copy) - 1] = '\0';
        
        /* Move tail forward after reading the message data */
        int next_tail = (tail + 1) % MSG_QUEUE_SIZE;
        atomic_store_explicit(&msg_tail, next_tail, memory_order_release);
        tail = next_tail;
        
        /* Now output the message (safe because we copied it) */
        fputs(msg_copy, level == RTAPI_MSG_ALL ? stdout : stderr);
        processed++;
    }
    return processed;
}

static pthread_t queue_thread;
static void *queue_function(void *arg) {
    (void)arg;
    rtapi_set_namef("rtapi_app:mesg");
    while(1) {
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        msg_queue_consume_all();
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        struct timespec ts = {0, 10000000};
        rtapi_clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL, NULL);
    }
    return NULL;
}

static int sim_rtapi_run_threads(int fd, int (*callback)(int fd));

static void *dlsym_helper(void *handle, const char *name) {
    return dlsym(handle, name);
}

static int instance_count = 0;
static int force_exit = 0;

static void *find_module(const char *name) {
    pthread_mutex_lock(&modules_lock);
    for(int i = 0; i < MAX_MODULES; i++) {
        if(modules[i].in_use && strcmp(modules[i].name, name) == 0) {
            void *handle = modules[i].handle;
            pthread_mutex_unlock(&modules_lock);
            return handle;
        }
    }
    pthread_mutex_unlock(&modules_lock);
    return NULL;
}

static int add_module(const char *name, void *handle) {
    pthread_mutex_lock(&modules_lock);
    for(int i = 0; i < MAX_MODULES; i++) {
        if(!modules[i].in_use) {
            strncpy(modules[i].name, name, sizeof(modules[i].name) - 1);
            modules[i].name[sizeof(modules[i].name) - 1] = '\0';
            modules[i].handle = handle;
            modules[i].in_use = 1;
            pthread_mutex_unlock(&modules_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&modules_lock);
    return -1;
}

static void remove_module(const char *name) {
    pthread_mutex_lock(&modules_lock);
    for(int i = 0; i < MAX_MODULES; i++) {
        if(modules[i].in_use && strcmp(modules[i].name, name) == 0) {
            modules[i].in_use = 0;
            modules[i].name[0] = '\0';
            modules[i].handle = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&modules_lock);
}

static int do_newinst_cmd(const char *type, const char *name, const char *arg) {
    hal_comp_t *comp = halpr_find_comp_by_name((char*)type);
    if(!comp) {
        rtapi_print_msg(RTAPI_MSG_ERR,
                "newinst: component %s not found\n", type);
        return -1;
    }

    return comp->make((char*)name, (char*)arg);
}

static int do_one_item(char item_type_char, const char *param_name, const char *param_value, void *vitem, int idx) {
    char *endp;
    switch(item_type_char) {
        case 'l': {
            long *litem = *(long**) vitem;
            litem[idx] = strtol(param_value, &endp, 0);
            if(*endp) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                        "`%s' invalid for parameter `%s'",
                        param_value, param_name);
                return -1;
            }
            return 0;
        }
        case 'i': {
            int *iitem = *(int**) vitem;
            iitem[idx] = strtol(param_value, &endp, 0);
            if(*endp) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                        "`%s' invalid for parameter `%s'",
                        param_value, param_name);
                return -1;
            }
            return 0;
        }
        case 's': {
            char **sitem = *(char***) vitem;
            sitem[idx] = strdup(param_value);
            return 0;
        }
        default:
            rtapi_print_msg(RTAPI_MSG_ERR,
                    "%s: Invalid type character `%c'\n",
                    param_name, item_type_char);
            return -1;
    }
}

static void remove_quotes(char *s) {
    char *src = s;
    char *dst = s;
    while(*src) {
        if(*src != '"') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

#define MAX_ARGS 64
static int do_comp_args(void *module, char **args, int nargs) {
    for(int i = 1; i < nargs; i++) {
        char *s = args[i];
        remove_quotes(s);
        char *eq = strchr(s, '=');
        if(!eq) {
            rtapi_print_msg(RTAPI_MSG_ERR, "Invalid parameter `%s'\n", s);
            return -1;
        }
        *eq = '\0';
        char *param_name = s;
        char *param_value = eq + 1;
        
        char sym_name[512];
        snprintf(sym_name, sizeof(sym_name), "rtapi_info_address_%s", param_name);
        void *item = dlsym_helper(module, sym_name);
        if(!item) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                    "Unknown parameter `%s'\n", param_name);
            return -1;
        }
        
        snprintf(sym_name, sizeof(sym_name), "rtapi_info_type_%s", param_name);
        char **item_type = (char**)dlsym_helper(module, sym_name);
        if(!item_type || !*item_type) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                    "Unknown parameter `%s' (type information missing)\n",
                    param_name);
            return -1;
        }

        snprintf(sym_name, sizeof(sym_name), "rtapi_info_size_%s", param_name);
        int *max_size_ptr = (int*)dlsym_helper(module, sym_name);

        char item_type_char = **item_type;
        if(max_size_ptr) {
            int max_size = *max_size_ptr;
            char *tok = param_value;
            int idx = 0;
            while(tok && *tok) {
                if(idx == max_size) {
                    rtapi_print_msg(RTAPI_MSG_ERR,
                            "%s: can only take %d arguments\n",
                            param_name, max_size);
                    return -1;
                }
                char *comma = strchr(tok, ',');
                char substr[256];
                if(comma) {
                    size_t len = comma - tok;
                    if(len >= sizeof(substr)) len = sizeof(substr) - 1;
                    strncpy(substr, tok, len);
                    substr[len] = '\0';
                    tok = comma + 1;
                } else {
                    strncpy(substr, tok, sizeof(substr) - 1);
                    substr[sizeof(substr) - 1] = '\0';
                    tok = NULL;
                }
                int result = do_one_item(item_type_char, param_name, substr, item, idx);
                if(result != 0) return result;
                idx++;
            }
        } else {
            int result = do_one_item(item_type_char, param_name, param_value, item, 0);
            if(result != 0) return result;
        }
    }
    return 0;
}

static int do_load_cmd(const char *name, char **args, int nargs) {
    void *w = find_module(name);
    if(w == NULL) {
        char what[LINELEN+1];
        snprintf(what, LINELEN, "%s/%s.so", EMC2_RTLIB_DIR, name);
        void *module = dlopen(what, RTLD_GLOBAL | RTLD_NOW);
        if(!module) {
            rtapi_print_msg(RTAPI_MSG_ERR, "%s: dlopen: %s\n", name, dlerror());
            return -1;
        }
        
        int (*start)(void) = (int(*)(void))dlsym_helper(module, "rtapi_app_main");
        if(!start) {
            rtapi_print_msg(RTAPI_MSG_ERR, "%s: dlsym: %s\n", name, dlerror());
            dlclose(module);
            return -1;
        }
        
        int result = do_comp_args(module, args, nargs);
        if(result < 0) {
            dlclose(module);
            return -1;
        }

        if ((result = start()) < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "%s: rtapi_app_main: %s (%d)\n",
                name, strerror(-result), result);
            dlclose(module);
            return result;
        }
        
        if(add_module(name, module) < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "%s: too many modules\n", name);
            dlclose(module);
            return -1;
        }
        
        instance_count++;
        return 0;
    } else {
        rtapi_print_msg(RTAPI_MSG_ERR, "%s: already exists\n", name);
        return -1;
    }
}

static int do_unload_cmd(const char *name) {
    void *w = find_module(name);
    if(w == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, "%s: not loaded\n", name);
        return -1;
    } else {
        int (*stop)(void) = (int(*)(void))dlsym_helper(w, "rtapi_app_exit");
        if(stop) stop();
        remove_module(name);
        dlclose(w);
        instance_count--;
    }
    return 0;
}

static int read_number(int fd) {
    int r = 0, neg = 1;
    char ch;

    while(1) {
        int res = read(fd, &ch, 1);
        if(res != 1) return -1;
        if(ch == '-') neg = -1;
        else if(ch == ' ') return r * neg;
        else r = 10 * r + ch - '0';
    }
}

static int read_string(int fd, char *buf, int maxlen) {
    int len = read_number(fd);
    if(len < 0 || len >= maxlen) return -1;
    if(read(fd, buf, len) != len) return -1;
    buf[len] = '\0';
    return len;
}

static int read_strings(int fd, char **args, int maxargs) {
    int count = read_number(fd);
    if(count < 0 || count > maxargs) return -1;
    
    for(int i = 0; i < count; i++) {
        args[i] = malloc(1024);
        if(!args[i]) {
            for(int j = 0; j < i; j++) free(args[j]);
            return -1;
        }
        if(read_string(fd, args[i], 1024) < 0) {
            for(int j = 0; j <= i; j++) free(args[j]);
            return -1;
        }
    }
    return count;
}

static void write_number(char *buf, int *pos, int bufsize, int num) {
    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%d ", num);
    int len = strlen(numbuf);
    if(*pos + len < bufsize) {
        strcpy(buf + *pos, numbuf);
        *pos += len;
    }
}

static void write_string(char *buf, int *pos, int bufsize, const char *s) {
    write_number(buf, pos, bufsize, strlen(s));
    int len = strlen(s);
    if(*pos + len < bufsize) {
        memcpy(buf + *pos, s, len);
        *pos += len;
    }
}

static int write_strings(int fd, char **strings, int count) {
    char buf[8192];
    int pos = 0;
    write_number(buf, &pos, sizeof(buf), count);
    for(int i = 0; i < count; i++) {
        write_string(buf, &pos, sizeof(buf), strings[i]);
    }
    return write(fd, buf, pos) == pos ? 0 : -1;
}

static int handle_command(char **args, int nargs) {
    if(nargs == 0) { return 0; }
    if(nargs == 1 && strcmp(args[0], "exit") == 0) {
        force_exit = 1;
        return 0;
    } else if(nargs >= 2 && strcmp(args[0], "load") == 0) {
        return do_load_cmd(args[1], args + 1, nargs - 1);
    } else if(nargs == 2 && strcmp(args[0], "unload") == 0) {
        return do_unload_cmd(args[1]);
    } else if(nargs == 3 && strcmp(args[0], "newinst") == 0) {
        return do_newinst_cmd(args[1], args[2], "");
    } else if(nargs == 4 && strcmp(args[0], "newinst") == 0) {
        return do_newinst_cmd(args[1], args[2], args[3]);
    } else {
        rtapi_print_msg(RTAPI_MSG_ERR,
                "Unrecognized command starting with %s\n",
                args[0]);
        return -1;
    }
}

static int slave(int fd, char **args, int nargs) {
    if(write_strings(fd, args, nargs) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "rtapi_app: failed to write to master: %s\n", strerror(errno));
        return -1;
    }

    int result = read_number(fd);
    return result;
}

static int callback(int fd)
{
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t len = sizeof(client_addr);
    int fd1 = accept(fd, (struct sockaddr*)&client_addr, &len);
    if(fd1 < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "rtapi_app: failed to accept connection from slave: %s\n", strerror(errno));
        return -1;
    }
    
    char *args[MAX_ARGS];
    int nargs = read_strings(fd1, args, MAX_ARGS);
    int result;
    
    if(nargs < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "rtapi_app: failed to read from slave: %s\n", strerror(errno));
        close(fd1);
        return -1;
    }
    
    result = handle_command(args, nargs);
    
    for(int i = 0; i < nargs; i++) {
        free(args[i]);
    }
    
    char buf[32];
    int pos = 0;
    write_number(buf, &pos, sizeof(buf), result);
    if(write(fd1, buf, pos) != pos) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "rtapi_app: failed to write to slave: %s\n", strerror(errno));
    }
    close(fd1);
    
    return !force_exit && instance_count > 0;
}

static pthread_t main_thread;

static void msg_handler(msg_level_t level, const char *fmt, va_list ap) {
    if(main_thread && pthread_self() != main_thread) {
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        msg_queue_push(level, buf);
    } else {
        vfprintf(level == RTAPI_MSG_ALL ? stdout : stderr, fmt, ap);
    }
}

static int master(int fd, char **args, int nargs) {
    main_thread = pthread_self();
    rtapi_set_msg_handler(msg_handler);
    int result;
    if((result = pthread_create(&queue_thread, NULL, &queue_function, NULL)) != 0) {
        errno = result;
        perror("pthread_create (queue function)");
        goto out0;
    }

    if((result = halpr_rtapi_app_main()) != 0) {
        errno = result;
        perror("halpr_rtapi_app_main failed");
        goto out1;
    }
    
    if(nargs) {
        result = handle_command(args, nargs);
        if(result != 0) goto out2;
        if(force_exit || instance_count == 0) goto out2;
    }
    sim_rtapi_run_threads(fd, callback);
out2:
    halpr_rtapi_app_exit();
out1:
    pthread_cancel(queue_thread);
    pthread_join(queue_thread, NULL);
    msg_queue_consume_all();
out0:
    return result;
}

static const char *get_fifo_path_internal(void) {
    static char path[512] = {0};
    if(path[0] != '\0') return path;
    
    const char *env_path = getenv("RTAPI_FIFO_PATH");
    if(env_path) {
        strncpy(path, env_path, sizeof(path) - 1);
    } else {
        const char *home = getenv("HOME");
        if(home) {
            snprintf(path, sizeof(path), "%s/.rtapi_fifo", home);
        } else {
            rtapi_print_msg(RTAPI_MSG_ERR,
                "rtapi_app: RTAPI_FIFO_PATH and HOME are unset. rtapi fifo creation is unsafe.");
            return NULL;
        }
    }
    
    if(strlen(path) + 1 > sizeof(((struct sockaddr_un*)0)->sun_path)) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "rtapi_app: rtapi fifo path is too long (arch limit %zd): %s",
                sizeof(((struct sockaddr_un*)0)->sun_path), path);
        return NULL;
    }
    return path;
}

static const char *get_fifo_path(void) {
    return get_fifo_path_internal();
}

static int get_fifo_path_buf(char *buf, size_t bufsize) {
    const char *s = get_fifo_path();
    if(!s) return -1;
    snprintf(buf, bufsize, "%s", s);
    return 0;
}

int rtapi_become_master(char **args, int nargs) {
  while (1) {
    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(fd == -1) { perror("socket"); exit(1); }

    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if(get_fifo_path_buf(addr.sun_path, sizeof(addr.sun_path)) < 0)
       exit(1);
    int result = bind(fd, (struct sockaddr*)&addr, sizeof(addr));

    if(result == 0) {
        int result = listen(fd, 10);
        if(result != 0) { perror("listen"); exit(1); }
        setsid();
        result = master(fd, args, nargs);
        unlink(get_fifo_path());
        return result;
    } else if(errno == EADDRINUSE) {
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        gettimeofday(&t1, NULL);
        for(int i = 0; i < 3 || (t1.tv_sec < 3 + t0.tv_sec); i++) {
            result = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
            if(result == 0) break;
            if(i == 0) srand48(t0.tv_sec ^ t0.tv_usec);
            usleep(lrand48() % 100000);
            gettimeofday(&t1, NULL);
        }
        if(result < 0 && errno == ECONNREFUSED) {
            unlink(get_fifo_path());
            fprintf(stderr, "Waited 3 seconds for master. Giving up.\n");
            close(fd);
            continue;
        }
        if(result < 0) { fprintf(stderr, "connect %s: %s\n", addr.sun_path, strerror(errno)); exit(1); }
        return slave(fd, args, nargs);
    } else {
        perror("bind"); exit(1);
    }
  }
}

int main(int argc, char **argv) {
    if(getuid() == 0) {
        char *fallback_uid_str = getenv("RTAPI_UID");
        int fallback_uid = fallback_uid_str ? atoi(fallback_uid_str) : 0;
        if(fallback_uid == 0)
        {
            fprintf(stderr,
                "Refusing to run as root without fallback UID specified\n"
                "To run under a debugger with I/O, use e.g.,\n"
                "    sudo env RTAPI_UID=`id -u` RTAPI_FIFO_PATH=$HOME/.rtapi_fifo gdb " EMC2_BIN_DIR "/rtapi_app\n");
            exit(1);
        }
        if (setreuid(fallback_uid, 0) != 0) { perror("setreuid"); abort(); }
        fprintf(stderr,
            "Running with fallback_uid.  getuid()=%d geteuid()=%d\n",
            getuid(), geteuid());
    }
    ruid = getuid();
    euid = geteuid();
    if (setresuid(euid, euid, ruid) != 0) { perror("setresuid"); abort(); }
#ifdef __linux__
    setfsuid(ruid);
#endif

    char *args[MAX_ARGS];
    int nargs = 0;
    for(int i = 1; i < argc && nargs < MAX_ARGS; i++) {
        args[nargs++] = argv[i];
    }

    return rtapi_become_master(args, nargs);
}


static int run_threads(int fd, int(*callback)(int fd)) {
    while(callback(fd)) { /* nothing */ }
    return 0;
}

int sim_rtapi_run_threads(int fd, int (*callback)(int fd)) {
    return run_threads(fd, callback);
}

