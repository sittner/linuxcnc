#ifndef RTAPI_USPACE_H
#define RTAPI_USPACE_H

#ifdef __cplusplus
extern "C" {
#endif

int rtapi_uspace_init(void);
int rtapi_load_module(const char *name, int argc, char **argv);
int rtapi_unload_module(const char *name);
int rtapi_get_realtime_context(void);
void rtapi_set_realtime_context(int is_rt);

#ifdef __cplusplus
}
#endif

#endif
