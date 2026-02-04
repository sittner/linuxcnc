/********************************************************************
* Description: tp_config.c
*   Runtime configuration implementation
********************************************************************/
#include "tp_config.h"
#include <stddef.h>

// Static function pointers (set by caller)
static tp_get_planner_type_fn planner_type_getter = NULL;
static tp_get_max_jerk_fn max_jerk_getter = NULL;

void tp_set_planner_type_getter(tp_get_planner_type_fn fn) {
    planner_type_getter = fn;
}

void tp_set_max_jerk_getter(tp_get_max_jerk_fn fn) {
    max_jerk_getter = fn;
}

int tp_get_planner_type(void) {
    if (planner_type_getter) {
        return planner_type_getter();
    }
    // Default to trapezoidal if not set
    return 0;
}

double tp_get_max_jerk(void) {
    if (max_jerk_getter) {
        return max_jerk_getter();
    }
    // Default jerk limit
    return 1000.0;
}
