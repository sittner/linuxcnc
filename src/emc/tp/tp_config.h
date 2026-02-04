/********************************************************************
* Description: tp_config.h
*   Runtime configuration for trajectory planner
*   Provides callback-based configuration access
*
* Author: LinuxCNC Contributors
* License: GPL Version 2
* System: Linux
********************************************************************/
#ifndef TP_CONFIG_H
#define TP_CONFIG_H

// Planner type access
typedef int (*tp_get_planner_type_fn)(void);
typedef double (*tp_get_max_jerk_fn)(void);

// Set configuration getters
void tp_set_planner_type_getter(tp_get_planner_type_fn fn);
void tp_set_max_jerk_getter(tp_get_max_jerk_fn fn);

// Get current planner configuration
int tp_get_planner_type(void);      // Returns 0=trapezoid, 1=S-curve
double tp_get_max_jerk(void);       // Returns jerk limit

#endif // TP_CONFIG_H
