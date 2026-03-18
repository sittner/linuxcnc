#ifndef RTAPI_USPACE_H
#define RTAPI_USPACE_H

#ifdef __cplusplus
extern "C" {
#endif

int rtapi_uspace_init(void);
int rtapi_load_module(const char *name, int argc, char **argv);
int rtapi_unload_module(const char *name);

#ifdef __cplusplus
}
#endif

#endif
