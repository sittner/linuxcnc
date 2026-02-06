/*
**Notes:
**  1) This rtapi_app_main() creates a minimal component (tpmod)
**     allowing use with:
**     'halcmd loadrt tpmod' (loads module: tpmod.so)
**  2) tp.c, tc.c,...,etc provide all functions required by
**     subsequent load of motmod
*/
#include "rtapi.h"
#include "rtapi_app.h"
#include "tp_hal_interface.h"
#include "tp_rtapi_interface.h"
MODULE_LICENSE("GPL");

#define TPMOD_DEBUG
#undef  TPMOD_DEBUG

// provision for testing use of module parameters:
static char *tp_parms;
RTAPI_MP_STRING(tp_parms,"Example tp parms");

static int tpmod_id;
int rtapi_app_main(void)
{
#ifdef TPMOD_DEBUG
    if (tp_parms) {
        TP_PRINT("%s:%s: tp_parms=%s\n",__FILE__,__FUNCTION__,tp_parms);
    }
#endif

    char* emsg;
    tpmod_id = tp_hal_init("tpmod"); // dlopen(".../tpmod.so")
    if (tpmod_id < 0) {emsg="tp_hal_init()"; goto error;}

    tp_hal_ready(tpmod_id);
    return 0;

error:
    TP_PRINT_MSG(TP_MSG_ERR,"\ntpmod FAIL:<%s>\n",emsg);
    tp_hal_exit(tpmod_id);
    return -1;
}

void rtapi_app_exit(void)
{
    tp_hal_exit(tpmod_id);
    return;
}
