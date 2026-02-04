/********************************************************************
* Description: tp_callbacks.h
*   Callback interface for TP
*   Defines callbacks from TP to motion controller/axes
********************************************************************/
#ifndef TP_CALLBACKS_H
#define TP_CALLBACKS_H

// Callback function types
typedef void (*tp_dio_write_fn)(int index, char value);
typedef void (*tp_aio_write_fn)(int index, double value);
typedef void (*tp_set_rotary_unlock_fn)(int axis, int state);
typedef int  (*tp_get_rotary_is_unlocked_fn)(int axis);
typedef double (*tp_get_axis_vel_limit_fn)(int axis);
typedef double (*tp_get_axis_acc_limit_fn)(int axis);

/**
 * TP callbacks structure
 * All callbacks are optional (can be NULL if functionality not needed)
 */
typedef struct tp_callbacks {
    tp_dio_write_fn              dio_write;
    tp_aio_write_fn              aio_write;
    tp_set_rotary_unlock_fn      set_rotary_unlock;
    tp_get_rotary_is_unlocked_fn get_rotary_is_unlocked;
    tp_get_axis_vel_limit_fn     get_axis_vel_limit;
    tp_get_axis_acc_limit_fn     get_axis_acc_limit;
} tp_callbacks_t;

/**
 * Set TP callbacks
 * @param tp TP instance
 * @param callbacks Callback structure (copied into TP)
 * @return 0 on success
 */
int tp_set_callbacks(void *tp, const tp_callbacks_t *callbacks);

#endif // TP_CALLBACKS_H
