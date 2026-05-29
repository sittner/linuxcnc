/* classicladder_rt.h — Shared data structures between RT C code and Go.
 *
 * The classicladder_rt_t struct is allocated by Go and contains all PLC
 * data. RT reads HAL pins, evaluates the ladder, and writes HAL pins.
 * Go handles file I/O, API dispatch, and config changes.
 *
 * Based on Classic Ladder Project by Marc Le Douarain.
 * Adapted for LinuxCNC gomc architecture.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License v2.1+.
 */

#ifndef CLASSICLADDER_RT_H
#define CLASSICLADDER_RT_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#include "hal.h"
#include "rtapi.h"

/* --- Sizing constants (defaults, overridable via config) --- */

#define CL_MAX_RUNGS            100
#define CL_MAX_BITS             500
#define CL_MAX_WORDS            100
#define CL_MAX_TIMERS           10
#define CL_MAX_MONOSTABLES      10
#define CL_MAX_COUNTERS         10
#define CL_MAX_TIMERS_IEC       10
#define CL_MAX_PHYS_INPUTS      50
#define CL_MAX_PHYS_OUTPUTS     50
#define CL_MAX_ARITHM_EXPR      100
#define CL_MAX_SECTIONS         10
#define CL_MAX_SYMBOLS          200
#define CL_MAX_S32_IN           10
#define CL_MAX_S32_OUT          10
#define CL_MAX_FLOAT_IN         10
#define CL_MAX_FLOAT_OUT        10
#define CL_MAX_ERROR_BITS       10
#define CL_MAX_STEPS            128

/* Rung grid dimensions */
#define CL_RUNG_WIDTH  10
#define CL_RUNG_HEIGHT 6

/* Arithmetic expression max length */
#define CL_ARITHM_EXPR_SIZE 50

/* Labels/comments */
#define CL_LGT_LABEL   10
#define CL_LGT_COMMENT 30

/* Symbol table */
#define CL_LGT_VAR_NAME       10
#define CL_LGT_SYMBOL_STRING  10
#define CL_LGT_SYMBOL_COMMENT 50

/* Section name */
#define CL_LGT_SECTION_NAME 20

/* --- Element types --- */
#define CL_ELE_FREE             0
#define CL_ELE_INPUT            1
#define CL_ELE_INPUT_NOT        2
#define CL_ELE_RISING_INPUT     3
#define CL_ELE_FALLING_INPUT    4
#define CL_ELE_CONNECTION       9
#define CL_ELE_TIMER            10
#define CL_ELE_MONOSTABLE       11
#define CL_ELE_COUNTER          12
#define CL_ELE_TIMER_IEC        13
#define CL_ELE_COMPAR           20
#define CL_ELE_OUTPUT           50
#define CL_ELE_OUTPUT_NOT       51
#define CL_ELE_OUTPUT_SET       52
#define CL_ELE_OUTPUT_RESET     53
#define CL_ELE_OUTPUT_JUMP      54
#define CL_ELE_OUTPUT_CALL      55
#define CL_ELE_OUTPUT_OPERATE   60
#define CL_ELE_UNUSABLE         99

/* --- Variable types --- */
#define CL_VAR_MEM_BIT          0
#define CL_VAR_TIMER_DONE       10
#define CL_VAR_TIMER_RUNNING    11
#define CL_VAR_TIMER_IEC_DONE   15
#define CL_VAR_MONOSTABLE_RUNNING 20
#define CL_VAR_COUNTER_DONE     25
#define CL_VAR_COUNTER_EMPTY    26
#define CL_VAR_COUNTER_FULL     27
#define CL_VAR_STEP_ACTIVITY    30
#define CL_VAR_PHYS_INPUT       50
#define CL_VAR_PHYS_OUTPUT      60
#define CL_VAR_ERROR_BIT        70
#define CL_VAR_ARE_WORD         199
#define CL_VAR_MEM_WORD         200
#define CL_VAR_STEP_TIME        220
#define CL_VAR_TIMER_PRESET     230
#define CL_VAR_TIMER_VALUE      231
#define CL_VAR_MONOSTABLE_PRESET 240
#define CL_VAR_MONOSTABLE_VALUE 241
#define CL_VAR_COUNTER_PRESET   250
#define CL_VAR_COUNTER_VALUE    251
#define CL_VAR_TIMER_IEC_PRESET 260
#define CL_VAR_TIMER_IEC_VALUE  261
#define CL_VAR_PHYS_WORD_INPUT  270
#define CL_VAR_PHYS_WORD_OUTPUT 280
#define CL_VAR_PHYS_FLOAT_INPUT 300
#define CL_VAR_PHYS_FLOAT_OUTPUT 310

/* Ladder states */
#define CL_STATE_LOADING 0
#define CL_STATE_STOP    1
#define CL_STATE_RUN     2

/* Section languages */
#define CL_SECTION_LADDER     0
#define CL_SECTION_SEQUENTIAL 1

/* Timer IEC modes */
#define CL_TIMER_IEC_TON  0
#define CL_TIMER_IEC_TOF  1
#define CL_TIMER_IEC_TP   2

/* --- Data structures --- */

typedef struct {
    int16_t type;
    int8_t  connected_with_top;
    int32_t var_type;
    int32_t var_num;
    /* Dynamic state (RT-only, not exposed to UI) */
    int8_t  dynamic_input;
    int8_t  dynamic_state;
    int8_t  dynamic_var_bak;
    int8_t  dynamic_output;
} cl_element_t;

typedef struct {
    int     used;
    int     prev_rung;
    int     next_rung;
    char    label[CL_LGT_LABEL];
    char    comment[CL_LGT_COMMENT];
    cl_element_t elements[CL_RUNG_WIDTH][CL_RUNG_HEIGHT];
} cl_rung_t;

typedef struct {
    int  preset;
    int  value;
    int  base;
    char input_enable;
    char input_control;
    char output_done;
    char output_running;
} cl_timer_t;

typedef struct {
    int  preset;
    int  value;
    int  base;
    char input;
    char input_bak;
    char output_running;
} cl_monostable_t;

typedef struct {
    int  preset;
    int  value;
    int  value_bak;
    char input_reset;
    char input_preset;
    char input_count_up;
    char input_count_up_bak;
    char input_count_down;
    char input_count_down_bak;
    char output_done;
    char output_empty;
    char output_full;
} cl_counter_t;

typedef struct {
    int  preset;
    int  value;
    int  base;
    char timer_mode;
    char input;
    char input_bak;
    char output;
    char timer_started;
    int  value_to_reach_one_base_unit;
} cl_timer_iec_t;

typedef struct {
    char expr[CL_ARITHM_EXPR_SIZE];
} cl_arithm_expr_t;

typedef struct {
    char used;
    char name[CL_LGT_SECTION_NAME];
    int  language;
    int  sub_routine_number;
    int  first_rung;
    int  last_rung;
    int  sequential_page;
} cl_section_t;

typedef struct {
    char var_name[CL_LGT_VAR_NAME];
    char symbol[CL_LGT_SYMBOL_STRING];
    char comment[CL_LGT_SYMBOL_COMMENT];
} cl_symbol_t;

/* PLC size configuration */
typedef struct {
    int nbr_rungs;
    int nbr_bits;
    int nbr_words;
    int nbr_timers;
    int nbr_monostables;
    int nbr_counters;
    int nbr_timers_iec;
    int nbr_phys_inputs;
    int nbr_phys_outputs;
    int nbr_arithm_expr;
    int nbr_sections;
    int nbr_symbols;
    int nbr_s32_in;
    int nbr_s32_out;
    int nbr_float_in;
    int nbr_float_out;
    int nbr_error_bits;
} cl_sizes_t;

/* Main RT instance — allocated by Go, shared with RT function */
typedef struct {
    /* State (atomic for RT/Go coordination) */
    _Atomic int         state;          /* CL_STATE_xxx */
    _Atomic int         hide_gui;
    _Atomic int32_t     duration_of_last_scan_ns;
    _Atomic uint32_t    generation;     /* bumped on any program change */

    /* Configuration */
    cl_sizes_t          sizes;
    int                 periodic_refresh_ms;
    int                 first_rung;
    int                 last_rung;
    int                 current_rung;

    /* Program data — protected by mutex in Go; RT reads only */
    cl_rung_t           rungs[CL_MAX_RUNGS];
    cl_section_t        sections[CL_MAX_SECTIONS];
    cl_arithm_expr_t    arithm_exprs[CL_MAX_ARITHM_EXPR];

    /* Runtime data — written by RT, read by Go for monitoring */
    cl_timer_t          timers[CL_MAX_TIMERS];
    cl_monostable_t     monostables[CL_MAX_MONOSTABLES];
    cl_counter_t        counters[CL_MAX_COUNTERS];
    cl_timer_iec_t      timers_iec[CL_MAX_TIMERS_IEC];

    /* Variable arrays — written by RT */
    char    var_bits[CL_MAX_BITS + CL_MAX_PHYS_INPUTS + CL_MAX_PHYS_OUTPUTS + CL_MAX_STEPS + CL_MAX_ERROR_BITS];
    int32_t var_words[CL_MAX_WORDS + CL_MAX_S32_IN + CL_MAX_S32_OUT + CL_MAX_STEPS];
    double  var_floats[CL_MAX_FLOAT_IN + CL_MAX_FLOAT_OUT];

    /* Symbol table (non-RT, for UI) */
    cl_symbol_t         symbols[CL_MAX_SYMBOLS];

    /* HAL pin pointers (set up during init, used by RT) */
    hal_bit_t          *hal_inputs[CL_MAX_PHYS_INPUTS];
    hal_bit_t          *hal_outputs[CL_MAX_PHYS_OUTPUTS];
    hal_s32_t          *hal_s32_inputs[CL_MAX_S32_IN];
    hal_s32_t          *hal_s32_outputs[CL_MAX_S32_OUT];
    hal_float_t        *hal_float_inputs[CL_MAX_FLOAT_IN];
    hal_float_t        *hal_float_outputs[CL_MAX_FLOAT_OUT];
    hal_bit_t          *hal_hide_gui;
} classicladder_rt_t;

/* --- RT functions (called from HAL thread) --- */

/* The main scan function — exported to HAL via hal_export_funct().
 * Reads inputs, evaluates all sections, writes outputs. */
void classicladder_refresh(void *arg, long period);

/* Allocate and initialize a classicladder_rt_t instance. */
classicladder_rt_t *classicladder_rt_alloc(const cl_sizes_t *sizes);

/* Free an instance. */
void classicladder_rt_free(classicladder_rt_t *rt);

/* Initialize all runtime data (vars, timers, counters). */
void classicladder_rt_init_data(classicladder_rt_t *rt);

/* Write a variable (for forcing from UI). Thread-safe for single-writer. */
void write_var_ext(classicladder_rt_t *rt, int type, int offset, int value);

#endif /* CLASSICLADDER_RT_H */
