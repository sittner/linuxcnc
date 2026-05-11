// interp_ext_api.h — GMI-style API header for interpreter extension registration.
//
// This provides the same register/get pattern as generated GMI headers,
// but is hand-written because the API involves registering function-pointer
// callbacks (which the .gmi IDL doesn't support).
//
// Provider: milltask.so — registers this API after creating its Interp instance.
// Consumer: Any cmod that wants to register remap/oword handlers (e.g. stdglue.so).
//
// Usage (consumer):
//   #include "interp_ext_api.h"
//   const interp_ext_api_t *ext = interp_ext_api_get(env->api, "milltask");
//   ext->register_oword(ext->ctx, "my_sub", my_handler, my_data);

#ifndef INTERP_EXT_API_H
#define INTERP_EXT_API_H

#include "interp_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

// The API table exposed by milltask for consumer cmods to register handlers.
typedef struct interp_ext_api {
    void *ctx;  // opaque (Interp pointer), passed as first arg to each function

    int (*register_oword)(void *ctx, const char *name,
                          interp_oword_fn fn, void *user);
    int (*register_remap_prolog)(void *ctx, const char *name,
                                 interp_remap_prolog_fn fn, void *user);
    int (*register_remap_epilog)(void *ctx, const char *name,
                                 interp_remap_epilog_fn fn, void *user);
} interp_ext_api_t;

// --- Registration & Lookup (same pattern as generated GMI headers) ---

#ifndef INTERP_EXT_API_CGO

#include "gomc/pkg/cmodule/gomc_api.h"

static inline int interp_ext_api_register(
    const gomc_api_t *api,
    const char *instance_name,
    const interp_ext_api_t *callbacks)
{
    return api->register_api(api->ctx, "interp_ext", 1,
                             instance_name, callbacks);
}

static inline const interp_ext_api_t *interp_ext_api_get(
    const gomc_api_t *api,
    const char *instance_name)
{
    return (const interp_ext_api_t *)api->get_api(
        api->ctx, "interp_ext", 1, instance_name);
}

#endif // INTERP_EXT_API_CGO

#ifdef __cplusplus
}
#endif

#endif // INTERP_EXT_API_H
