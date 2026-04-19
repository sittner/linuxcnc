// home_ctx.h — Reverse-callback context passed from motmod to homemod via GMI.
//
// motmod allocates a home_ctx_t on its stack/static storage, fills in the
// function pointers, and passes its address as ctx_ptr (uint64_t) in the
// home API init() call.  homemod casts it back and wires the pointers
// into homing.c via homeMotFunctions().

#ifndef HOME_CTX_H
#define HOME_CTX_H

typedef struct {
    void (*set_rotary_unlock)(int jnum, int unlock);
    int  (*get_rotary_is_unlocked)(int jnum);
} home_ctx_t;

#endif // HOME_CTX_H
