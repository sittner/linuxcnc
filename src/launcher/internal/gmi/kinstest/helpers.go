package kinstest

// #cgo CFLAGS: -I${SRCDIR}/../../../generated/gmi/kins -I${SRCDIR}/../../../pkg/cmodule
// #cgo LDFLAGS: -ldl
//
// #include <dlfcn.h>
// #include <stdlib.h>
// #include "kins_api.h"
// #include "gomc_env.h"
//
// // Helpers to call through function pointers (cgo can't do it directly).
// static int call_forward(kins_forward_fn fn,
//     const double joints[KINS_MAX_JOINTS], kins_pose_t *world,
//     uint64_t fflags, uint64_t *iflags, int32_t *out) {
//     return fn(joints, world, fflags, iflags, out);
// }
// static int call_inverse(kins_inverse_fn fn,
//     const kins_pose_t *world, double joints[KINS_MAX_JOINTS],
//     uint64_t iflags, uint64_t *fflags, int32_t *out) {
//     return fn(world, joints, iflags, fflags, out);
// }
// static int call_type(kins_type_fn fn, kins_kinematics_type_t *out) {
//     return fn(out);
// }
// static int call_switchable(kins_switchable_fn fn, int32_t *out) {
//     return fn(out);
// }
//
// // --- Stub sub-APIs for testing ---
//
// // Log ring
// static gomc_log_ring_t *test_ring = NULL;
// static gomc_log_t       stub_log;
//
// static void init_stub_log(void) {
//     if (!test_ring) {
//         test_ring = gomc_ring_create();
//         stub_log.ring = test_ring;
//     }
// }
//
// // INI stub
// static const char *stub_ini_get(void *ctx, const char *section, const char *key) {
//     (void)ctx; (void)section; (void)key;
//     return NULL;
// }
// static const char **stub_ini_get_all(void *ctx, const char *section, const char *key, int *out) {
//     (void)ctx; (void)section; (void)key;
//     *out = 0;
//     return NULL;
// }
// static const char *stub_ini_source(void *ctx) {
//     (void)ctx;
//     return "/dev/null";
// }
// static gomc_ini_t stub_ini = {
//     .ctx         = NULL,
//     .get         = stub_ini_get,
//     .get_all     = stub_ini_get_all,
//     .source_file = stub_ini_source,
// };
//
// // API registry callbacks — forward-declared, implemented in Go via //export.
// extern int test_api_register_cb(void *ctx, char *api_name, int version,
//                                 char *instance_name, void *callbacks);
// extern void *test_api_get_cb(void *ctx, char *api_name, int version,
//                              char *instance_name);
//
// static gomc_api_t stub_api = {
//     .ctx          = NULL,
//     .register_api = (int(*)(void*,const char*,int,const char*,const void*))test_api_register_cb,
//     .get_api      = (const void*(*)(void*,const char*,int,const char*))test_api_get_cb,
// };
//
// // Combined env
// static cmod_env_t stub_env;
//
// static void init_stub_env(void) {
//     init_stub_log();
//     stub_env.dl_handle = NULL;
//     stub_env.log       = &stub_log;
//     stub_env.ini       = &stub_ini;
//     stub_env.hal       = NULL;
//     stub_env.rtapi     = NULL;
//     stub_env.api       = &stub_api;
// }
//
// // Load the .so via dlopen and call its New() function.
// static int load_trivkins(const char *so_path, cmod_t **out) {
//     init_stub_env();
//     void *handle = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
//     if (!handle) return -1;
//
//     cmod_new_fn factory = (cmod_new_fn)dlsym(handle, "New");
//     if (!factory) { dlclose(handle); return -2; }
//
//     stub_env.dl_handle = handle;
//     return factory(&stub_env, "trivkins", 0, NULL, out);
// }
//
// static void unload_trivkins(cmod_t *mod) {
//     if (mod) {
//         if (mod->Stop) mod->Stop(mod);
//         if (mod->Destroy) mod->Destroy(mod);
//     }
// }
import "C"

import (
	"os"
	"path/filepath"
	"runtime"
	"syscall"
	"unsafe"

	_ "github.com/sittner/linuxcnc/src/launcher/generated/gmi/kins" // registers KinsMeta via init()
	"github.com/sittner/linuxcnc/src/launcher/internal/apiserver"
)

// soPath finds the trivkins.so relative to the source tree.
func soPath() string {
	_, file, _, _ := runtime.Caller(0)
	launcherDir := filepath.Dir(filepath.Dir(filepath.Dir(filepath.Dir(file))))
	root := filepath.Dir(launcherDir) // src/
	root = filepath.Dir(root)         // linuxcnc/
	return filepath.Join(root, "cmod", "trivkins.so")
}

// loadTrivkins loads the trivkins.so and calls New().
func loadTrivkins() (*C.cmod_t, error) {
	path := soPath()
	if _, err := os.Stat(path); err != nil {
		return nil, err
	}
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	var mod *C.cmod_t
	rc := C.load_trivkins(cpath, &mod)
	if rc != 0 {
		return nil, os.ErrInvalid
	}
	return mod, nil
}

// unloadTrivkins cleans up.
func unloadTrivkins(mod *C.cmod_t) {
	C.unload_trivkins(mod)
}

// getKinsCallbacks retrieves the kins callbacks from the Go registry.
func getKinsCallbacks() *C.kins_callbacks_t {
	reg := apiserver.DefaultRegistry()
	if reg == nil {
		return nil
	}
	cbs, err := reg.GetAPI("kins", "kinematics", 1)
	if err != nil {
		return nil
	}
	return (*C.kins_callbacks_t)(cbs)
}

// --- API registry callback implementations (exported to C for test stub) ---

//export test_api_register_cb
func test_api_register_cb(ctx unsafe.Pointer, apiName *C.char, version C.int,
	instanceName *C.char, callbacks unsafe.Pointer) C.int {

	reg := apiserver.DefaultRegistry()
	if reg == nil {
		return -C.int(syscall.EINVAL)
	}

	name := C.GoString(apiName)
	ver := int(version)
	instance := C.GoString(instanceName)

	meta := apiserver.GetMeta(name, ver)
	if meta == nil {
		return -C.int(syscall.EINVAL)
	}

	err := reg.Register(meta, instance, callbacks)
	if err != nil {
		switch err {
		case syscall.EEXIST:
			return -C.int(syscall.EEXIST)
		default:
			return -C.int(syscall.EINVAL)
		}
	}
	return 0
}

//export test_api_get_cb
func test_api_get_cb(ctx unsafe.Pointer, apiName *C.char, version C.int,
	instanceName *C.char) unsafe.Pointer {

	reg := apiserver.DefaultRegistry()
	if reg == nil {
		return nil
	}

	instance := C.GoString(instanceName)
	ver := int(version)

	cbs, err := reg.GetAPI(C.GoString(apiName), instance, ver)
	if err != nil {
		return nil
	}
	return cbs
}

// callForward calls the forward kinematics through function pointers.
func callForward(cbs *C.kins_callbacks_t, joints [16]float64) (C.kins_pose_t, int32) {
	var cJoints [16]C.double
	for i := 0; i < 16; i++ {
		cJoints[i] = C.double(joints[i])
	}
	var world C.kins_pose_t
	var fflags, iflags C.uint64_t
	var out C.int32_t
	C.call_forward(cbs.forward, &cJoints[0], &world, fflags, &iflags, &out)
	return world, int32(out)
}

// callInverse calls the inverse kinematics through function pointers.
func callInverse(cbs *C.kins_callbacks_t, world C.kins_pose_t) ([16]float64, int32) {
	var cJoints [16]C.double
	var iflags, fflags C.uint64_t
	var out C.int32_t
	C.call_inverse(cbs.inverse, &world, &cJoints[0], iflags, &fflags, &out)
	var joints [16]float64
	for i := 0; i < 16; i++ {
		joints[i] = float64(cJoints[i])
	}
	return joints, int32(out)
}

// callType retrieves the kinematics type.
func callType(cbs *C.kins_callbacks_t) (int, int32) {
	var out C.kins_kinematics_type_t
	C.call_type(cbs._type, &out)
	return int(out), 0
}

// callSwitchable checks if kinematics are switchable.
func callSwitchable(cbs *C.kins_callbacks_t) int32 {
	var out C.int32_t
	C.call_switchable(cbs.switchable, &out)
	return int32(out)
}
