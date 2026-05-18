/********************************************************************
* Description: canon_context.hh
*   Per-instance context for canon callbacks, enabling multi-instance
*   interpreter usage. Replaces file-scope statics in emccanon.cc.
*
* License: GPL Version 2
********************************************************************/
#ifndef CANON_CONTEXT_HH
#define CANON_CONTEXT_HH

#include <stdio.h>
#include <vector>
#include "canon.hh"
#include "emc_nml.hh"
#include "modal_state.hh"
#include "posemath.h"

// Forward declarations
class NML_INTERP_LIST;

// Segment chaining point (for naive CAM detection)
struct canon_pt {
    double x, y, z, a, b, c, u, v, w;
    int line_no;
    StateTag tag;
};

// Per-instance canon state. Passed as void *ctx to all canon callbacks.
struct CanonContext {
    CanonConfig_t       canon;
    int                 debug_velacc;
    StateTag            tag;
    PM_QUATERNION       quat;
    std::vector<canon_pt> chained_points;
    FILE               *probefile;
    FILE               *logfile;
    EMC_STAT           *emcStatus;
    NML_INTERP_LIST    *interp_list;

    CanonContext()
        : canon{}
        , debug_velacc(0)
        , tag{}
        , quat(1, 0, 0, 0)
        , probefile(nullptr)
        , logfile(nullptr)
        , emcStatus(nullptr)
        , interp_list(nullptr)
    {}
};

// ---- Convenience macros for canon callback functions ----
// CANON_CTX sets the file-scope _cc from the ctx parameter.
// Helpers that don't take ctx directly will see the updated _cc.
// This enables multi-instance: each callback entry sets _cc to its ctx.
#define CANON_CTX   do { if (ctx) _cc = static_cast<CanonContext*>(ctx); } while(0)
#define CC          (_cc->canon)
#define STAT        (_cc->emcStatus)
#define ILIST       (*_cc->interp_list)
#define TAG         (_cc->tag)
#define QUAT        (_cc->quat)
#define CHAINED     (_cc->chained_points)

#endif // CANON_CONTEXT_HH
