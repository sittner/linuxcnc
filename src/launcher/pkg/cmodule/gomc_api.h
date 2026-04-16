// gomc_api.h — Dynamic API registry callbacks for gomc C modules.
//
// Provides a generic API registration and lookup mechanism.  C modules
// register typed callback tables (defined in generated *_api.h headers)
// through the env->api callbacks, and consumers look them up by name.
//
// Usage (provider — e.g. trivkins.c):
//   #include "kins_api.h"
//   static kins_callbacks_t my_callbacks = { ... };
//   kins_api_register(env->api, "kinematics", &my_callbacks);
//
// Usage (consumer):
//   const kins_callbacks_t *k = kins_api_get(env->api, "kinematics");
//
// The generated *_api.h headers provide typed static inline wrappers
// around the generic register/get callbacks below.

#ifndef GOMC_API_H
#define GOMC_API_H

#ifdef __cplusplus
extern "C" {
#endif

// gomc_api_t — API registry callbacks.
//
// ctx:          opaque pointer passed as first argument to each callback.
// register_api: register a callback table for the named API.
//               Returns 0 on success, -EEXIST if already registered,
//               -EINVAL if arguments are invalid.
// get_api:      look up a registered callback table by API name.
//               Returns pointer to the callbacks struct or NULL.
//               The returned pointer is valid for the process lifetime.
typedef struct {
    void *ctx;

    int (*register_api)(
        void *ctx,
        const char *api_name,
        int version,
        const char *instance_name,
        const void *callbacks
    );

    const void *(*get_api)(
        void *ctx,
        const char *api_name,
        int version,
        const char *instance_name
    );
} gomc_api_t;

#ifdef __cplusplus
}
#endif

#endif // GOMC_API_H
