/********************************************************************
* Description: interp_ext.cc
*   Extension handler registry and dispatch for the RS274NGC interpreter.
*   Replaces the removed Python dispatch with a C callback mechanism.
*
* License: GPL Version 2
********************************************************************/

#include "rs274ngc_interp.hh"
#include "interp_ext.h"
#include <unordered_map>
#include <string>

// --- Registry data structure (opaque to header consumers) ---

struct OwordEntry {
    interp_oword_fn fn;
    void *user;
};

struct RemapPrologEntry {
    interp_remap_prolog_fn fn;
    void *user;
};

struct RemapEpilogEntry {
    interp_remap_epilog_fn fn;
    void *user;
};

struct InterpExtRegistry {
    std::unordered_map<std::string, OwordEntry> owords;
    std::unordered_map<std::string, RemapPrologEntry> prologs;
    std::unordered_map<std::string, RemapEpilogEntry> epilogs;
};

// --- Context callback implementations ---

static double ctx_get_param(void *interp, const char *name)
{
    Interp *ip = static_cast<Interp*>(interp);
    int status;
    double value = 0.0;
    ip->find_named_param(name, &status, &value);
    return value;
}

static int ctx_set_param(void *interp, const char *name, double val)
{
    Interp *ip = static_cast<Interp*>(interp);
    return ip->store_named_param(&ip->_setup, name, val);
}

static int ctx_find_tool_pocket(void *interp, int tool_number)
{
    Interp *ip = static_cast<Interp*>(interp);
    int pocket = -1;
    ip->find_tool_pocket(&ip->_setup, tool_number, &pocket);
    return pocket;
}

static void ctx_set_error(void *interp, const char *msg)
{
    Interp *ip = static_cast<Interp*>(interp);
    ip->setSavedError(msg);
}

static void fill_ctx(interp_ext_ctx_t *ctx, Interp *ip, void *user, int phase)
{
    ctx->interp = ip;
    ctx->get_param = ctx_get_param;
    ctx->set_param = ctx_set_param;
    ctx->find_tool_pocket = ctx_find_tool_pocket;
    ctx->set_error = ctx_set_error;
    ctx->phase = phase;
    ctx->user = user;
}

// --- Interp C++ registration methods ---

int Interp::ext_register_oword(const char *name, interp_oword_fn fn, void *user)
{
    if (!ext_registry) ext_registry = new InterpExtRegistry;
    ext_registry->owords[name] = {fn, user};
    return 0;
}

int Interp::ext_register_remap_prolog(const char *name, interp_remap_prolog_fn fn, void *user)
{
    if (!ext_registry) ext_registry = new InterpExtRegistry;
    ext_registry->prologs[name] = {fn, user};
    return 0;
}

int Interp::ext_register_remap_epilog(const char *name, interp_remap_epilog_fn fn, void *user)
{
    if (!ext_registry) ext_registry = new InterpExtRegistry;
    ext_registry->epilogs[name] = {fn, user};
    return 0;
}

bool Interp::ext_has_oword(const char *name)
{
    if (!ext_registry) return false;
    return ext_registry->owords.count(name) > 0;
}

bool Interp::ext_has_remap_handler(const char *name)
{
    if (!ext_registry) return false;
    return ext_registry->prologs.count(name) > 0 ||
           ext_registry->epilogs.count(name) > 0;
}

int Interp::ext_call_oword(const char *name, const double *args, int n_args,
                           double *retval, int phase)
{
    if (!ext_registry) return INTERP_EXT_ERROR;
    auto it = ext_registry->owords.find(name);
    if (it == ext_registry->owords.end()) return INTERP_EXT_ERROR;

    interp_ext_ctx_t ctx;
    fill_ctx(&ctx, this, it->second.user, phase);
    return it->second.fn(&ctx, name, args, n_args, retval);
}

int Interp::ext_call_remap_prolog(const char *name, int phase)
{
    if (!ext_registry) return INTERP_EXT_ERROR;
    auto it = ext_registry->prologs.find(name);
    if (it == ext_registry->prologs.end()) return INTERP_EXT_ERROR;

    interp_ext_ctx_t ctx;
    fill_ctx(&ctx, this, it->second.user, phase);
    return it->second.fn(&ctx, name);
}

int Interp::ext_call_remap_epilog(const char *name, int phase)
{
    if (!ext_registry) return INTERP_EXT_ERROR;
    auto it = ext_registry->epilogs.find(name);
    if (it == ext_registry->epilogs.end()) return INTERP_EXT_ERROR;

    interp_ext_ctx_t ctx;
    fill_ctx(&ctx, this, it->second.user, phase);
    return it->second.fn(&ctx, name);
}

// --- C-linkage wrappers (exported from librs274.so) ---

extern "C" {

int interp_ext_register_oword(void *interp, const char *name,
                              interp_oword_fn fn, void *user)
{
    return static_cast<Interp*>(interp)->ext_register_oword(name, fn, user);
}

int interp_ext_register_remap_prolog(void *interp, const char *name,
                                     interp_remap_prolog_fn fn, void *user)
{
    return static_cast<Interp*>(interp)->ext_register_remap_prolog(name, fn, user);
}

int interp_ext_register_remap_epilog(void *interp, const char *name,
                                     interp_remap_epilog_fn fn, void *user)
{
    return static_cast<Interp*>(interp)->ext_register_remap_epilog(name, fn, user);
}

int interp_ext_has_oword(void *interp, const char *name)
{
    return static_cast<Interp*>(interp)->ext_has_oword(name) ? 1 : 0;
}

int interp_ext_has_remap_handler(void *interp, const char *name)
{
    return static_cast<Interp*>(interp)->ext_has_remap_handler(name) ? 1 : 0;
}

} // extern "C"

// Called from Interp destructor (avoids incomplete-type delete warning)
void interp_ext_registry_destroy(InterpExtRegistry *reg)
{
    delete reg;
}
