// tp_ctx.h — Reverse-callback context passed from motmod to tpmod via GMI.
//
// motmod allocates a tp_ctx_t, fills in the function pointers and data
// pointers, and passes its address as ctx_ptr (uint64_t) in the tp API
// init() call.  tpmod casts it back and wires the pointers into tp.c
// via tpMotFunctions() + tpMotData().

#ifndef TP_CTX_H
#define TP_CTX_H

#include "motion.h"

typedef struct {
    // 6 function callbacks into motmod
    void   (*dio_write)(int index, char value);
    void   (*aio_write)(int index, double value);
    void   (*set_rotary_unlock)(int joint, int unlock);
    int    (*get_rotary_unlock)(int joint);
    double (*axis_get_vel_limit)(int axis);
    double (*axis_get_acc_limit)(int axis);

    // 2 shared-memory data pointers
    emcmot_status_t *status;
    emcmot_config_t *config;
} tp_ctx_t;

#endif // TP_CTX_H
