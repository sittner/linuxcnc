/********************************************************************
* Description: emccanon_table.cc
*   Factory function creating a canon_callbacks_t that wraps the
*   existing global emccanon functions.
*
* License: GPL Version 2
*
* This file creates the bridge between the new per-instance canon
* callback table and the existing global canon implementation.
* Each thunk simply forwards to the original global function.
*
* The ctx pointer holds an emccanon_ctx_t that carries references to
* the global state (currently unused since the globals are still global,
* but prepared for Phase 5 multi-instance).
********************************************************************/

#include "config.h"
#include "canon.hh"
#include "canon_position.hh"
#include "modal_state.hh"
#include "tooldata.hh"

// Generated canon callback table from canon.gmi
#define CANON_API_CGO
#include "gomc/generated/gmi/canon/canon_api.h"
#include "emccanon_table.hh"

// ---- Thunks: each wraps a global canon function into the callback signature ----

static void t_init_canon(void *ctx) {
    INIT_CANON();
}

static void t_set_g5x_offset(void *ctx, int32_t origin,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w) {
    SET_G5X_OFFSET(origin, x, y, z, a, b, c, u, v, w);
}

static void t_set_g92_offset(void *ctx,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w) {
    SET_G92_OFFSET(x, y, z, a, b, c, u, v, w);
}

static void t_set_xy_rotation(void *ctx, double t) {
    SET_XY_ROTATION(t);
}

static void t_update_end_point(void *ctx,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w) {
    CANON_UPDATE_END_POINT(x, y, z, a, b, c, u, v, w);
}

static void t_use_length_units(void *ctx, int32_t units) {
    USE_LENGTH_UNITS((CANON_UNITS)units);
}

static void t_select_plane(void *ctx, int32_t plane) {
    SELECT_PLANE((CANON_PLANE)plane);
}

static void t_set_traverse_rate(void *ctx, double rate) {
    SET_TRAVERSE_RATE(rate);
}

static void t_straight_traverse(void *ctx, int32_t lineno,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w) {
    STRAIGHT_TRAVERSE(lineno, x, y, z, a, b, c, u, v, w);
}

static void t_set_feed_rate(void *ctx, double rate) {
    SET_FEED_RATE(rate);
}

static void t_set_feed_reference(void *ctx, int32_t reference) {
    SET_FEED_REFERENCE((CANON_FEED_REFERENCE)reference);
}

static void t_set_feed_mode(void *ctx, int32_t spindle, int32_t mode) {
    SET_FEED_MODE(spindle, mode);
}

static void t_set_motion_control_mode(void *ctx, int32_t mode, double tolerance) {
    SET_MOTION_CONTROL_MODE((CANON_MOTION_MODE)mode, tolerance);
}

static void t_set_naivecam_tolerance(void *ctx, double tolerance) {
    SET_NAIVECAM_TOLERANCE(tolerance);
}

static void t_set_cutter_radius_compensation(void *ctx, double radius) {
    SET_CUTTER_RADIUS_COMPENSATION(radius);
}

static void t_start_cutter_radius_compensation(void *ctx, int32_t direction) {
    START_CUTTER_RADIUS_COMPENSATION(direction);
}

static void t_stop_cutter_radius_compensation(void *ctx) {
    STOP_CUTTER_RADIUS_COMPENSATION();
}

static void t_start_speed_feed_synch(void *ctx, int32_t spindle,
    double feed_per_revolution, int32_t velocity_mode) {
    START_SPEED_FEED_SYNCH(spindle, feed_per_revolution, velocity_mode);
}

static void t_stop_speed_feed_synch(void *ctx) {
    STOP_SPEED_FEED_SYNCH();
}

static void t_arc_feed(void *ctx, int32_t lineno,
    double first_end, double second_end, double first_axis, double second_axis,
    int32_t rotation, double axis_end_point,
    double a, double b, double c, double u, double v, double w) {
    ARC_FEED(lineno, first_end, second_end, first_axis, second_axis,
             rotation, axis_end_point, a, b, c, u, v, w);
}

static void t_straight_feed(void *ctx, int32_t lineno,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w) {
    STRAIGHT_FEED(lineno, x, y, z, a, b, c, u, v, w);
}

static void t_nurbs_feed(void *ctx, int32_t lineno,
    const canon_control_point_t *control_points, size_t control_points_len,
    uint32_t k) {
    std::vector<CONTROL_POINT> pts(control_points_len);
    for (size_t i = 0; i < control_points_len; i++) {
        pts[i].X = control_points[i].x;
        pts[i].Y = control_points[i].y;
        pts[i].W = control_points[i].w;
    }
    NURBS_FEED(lineno, pts, k);
}

static void t_rigid_tap(void *ctx, int32_t lineno,
    double x, double y, double z, double scale) {
    RIGID_TAP(lineno, x, y, z, scale);
}

static void t_straight_probe(void *ctx, int32_t lineno,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w, uint8_t probe_type) {
    STRAIGHT_PROBE(lineno, x, y, z, a, b, c, u, v, w, probe_type);
}

static void t_stop(void *ctx) {
    // STOP() is declared in canon.hh but never implemented in emccanon.
    // Other canon implementations may provide a real stop.
}

static void t_dwell(void *ctx, double seconds) {
    DWELL(seconds);
}

static void t_finish(void *ctx) {
    FINISH();
}

static void t_set_spindle_mode(void *ctx, int32_t spindle, double mode) {
    SET_SPINDLE_MODE(spindle, mode);
}

static void t_start_spindle_clockwise(void *ctx, int32_t spindle, int32_t wait) {
    START_SPINDLE_CLOCKWISE(spindle, wait);
}

static void t_start_spindle_counterclockwise(void *ctx, int32_t spindle, int32_t wait) {
    START_SPINDLE_COUNTERCLOCKWISE(spindle, wait);
}

static void t_set_spindle_speed(void *ctx, int32_t spindle, double rpm) {
    SET_SPINDLE_SPEED(spindle, rpm);
}

static void t_stop_spindle_turning(void *ctx, int32_t spindle) {
    STOP_SPINDLE_TURNING(spindle);
}

static void t_orient_spindle(void *ctx, int32_t spindle, double orientation, int32_t mode) {
    ORIENT_SPINDLE(spindle, orientation, mode);
}

static void t_wait_spindle_orient_complete(void *ctx, int32_t spindle, double timeout) {
    WAIT_SPINDLE_ORIENT_COMPLETE(spindle, timeout);
}

static void t_select_tool(void *ctx, int32_t tool) {
    SELECT_TOOL(tool);
}

static void t_start_change(void *ctx) {
    START_CHANGE();
}

static void t_change_tool(void *ctx, int32_t slot) {
    CHANGE_TOOL(slot);
}

static void t_change_tool_number(void *ctx, int32_t number) {
    CHANGE_TOOL_NUMBER(number);
}

static void t_reload_tooldata(void *ctx) {
    RELOAD_TOOLDATA();
}

static void t_set_tool_table_entry(void *ctx, int32_t pocket, int32_t toolno,
    double ox, double oy, double oz, double oa, double ob, double oc,
    double ou, double ov, double ow,
    double diameter, double frontangle, double backangle, int32_t orientation) {
    EmcPose offset;
    offset.tran.x = ox; offset.tran.y = oy; offset.tran.z = oz;
    offset.a = oa; offset.b = ob; offset.c = oc;
    offset.u = ou; offset.v = ov; offset.w = ow;
    SET_TOOL_TABLE_ENTRY(pocket, toolno, offset, diameter, frontangle,
                         backangle, orientation);
}

static void t_use_tool_length_offset(void *ctx,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w) {
    EmcPose offset;
    offset.tran.x = x; offset.tran.y = y; offset.tran.z = z;
    offset.a = a; offset.b = b; offset.c = c;
    offset.u = u; offset.v = v; offset.w = w;
    USE_TOOL_LENGTH_OFFSET(offset);
}

static void t_flood_on(void *ctx) { FLOOD_ON(); }
static void t_flood_off(void *ctx) { FLOOD_OFF(); }
static void t_mist_on(void *ctx) { MIST_ON(); }
static void t_mist_off(void *ctx) { MIST_OFF(); }

static void t_enable_feed_override(void *ctx) { ENABLE_FEED_OVERRIDE(); }
static void t_disable_feed_override(void *ctx) { DISABLE_FEED_OVERRIDE(); }
static void t_enable_speed_override(void *ctx, int32_t spindle) { ENABLE_SPEED_OVERRIDE(spindle); }
static void t_disable_speed_override(void *ctx, int32_t spindle) { DISABLE_SPEED_OVERRIDE(spindle); }
static void t_enable_feed_hold(void *ctx) { ENABLE_FEED_HOLD(); }
static void t_disable_feed_hold(void *ctx) { DISABLE_FEED_HOLD(); }
static void t_enable_adaptive_feed(void *ctx) { ENABLE_ADAPTIVE_FEED(); }
static void t_disable_adaptive_feed(void *ctx) { DISABLE_ADAPTIVE_FEED(); }

static void t_set_motion_output_bit(void *ctx, int32_t index) { SET_MOTION_OUTPUT_BIT(index); }
static void t_clear_motion_output_bit(void *ctx, int32_t index) { CLEAR_MOTION_OUTPUT_BIT(index); }
static void t_set_aux_output_bit(void *ctx, int32_t index) { SET_AUX_OUTPUT_BIT(index); }
static void t_clear_aux_output_bit(void *ctx, int32_t index) { CLEAR_AUX_OUTPUT_BIT(index); }
static void t_set_motion_output_value(void *ctx, int32_t index, double value) { SET_MOTION_OUTPUT_VALUE(index, value); }
static void t_set_aux_output_value(void *ctx, int32_t index, double value) { SET_AUX_OUTPUT_VALUE(index, value); }

static int32_t t_wait_input(void *ctx, int32_t index, int32_t input_type,
    int32_t wait_type, double timeout) {
    return WAIT(index, input_type, wait_type, timeout);
}

static void t_clamp_axis(void *ctx, int32_t axis) {
    CLAMP_AXIS((CANON_AXIS)axis);
}

static void t_unclamp_axis(void *ctx, int32_t axis) {
    UNCLAMP_AXIS((CANON_AXIS)axis);
}

static int32_t t_lock_rotary(void *ctx, int32_t lineno, int32_t joint) {
    return LOCK_ROTARY(lineno, joint);
}

static int32_t t_unlock_rotary(void *ctx, int32_t lineno, int32_t joint) {
    return UNLOCK_ROTARY(lineno, joint);
}

static void t_program_stop(void *ctx) { PROGRAM_STOP(); }
static void t_optional_program_stop(void *ctx) { OPTIONAL_PROGRAM_STOP(); }
static void t_program_end(void *ctx) { PROGRAM_END(); }
static void t_pallet_shuttle(void *ctx) { PALLET_SHUTTLE(); }

static void t_comment(void *ctx, const char *s) { COMMENT(s); }
static void t_message(void *ctx, const char *s) { MESSAGE((char *)s); }
static void t_log_msg(void *ctx, const char *s) { LOG((char *)s); }
static void t_logopen(void *ctx, const char *s) { LOGOPEN((char *)s); }
static void t_logappend(void *ctx, const char *s) { LOGAPPEND((char *)s); }
static void t_logclose(void *ctx) { LOGCLOSE(); }
static void t_canon_error(void *ctx, const char *msg) { CANON_ERROR("%s", msg); }

static void t_turn_probe_on(void *ctx) { TURN_PROBE_ON(); }
static void t_turn_probe_off(void *ctx) { TURN_PROBE_OFF(); }

static void t_set_block_delete(void *ctx, int32_t enabled) {
    SET_BLOCK_DELETE(enabled);
}

static int32_t t_get_block_delete(void *ctx) {
    return GET_BLOCK_DELETE() ? 1 : 0;
}

static void t_set_optional_program_stop(void *ctx, int32_t enabled) {
    SET_OPTIONAL_PROGRAM_STOP(enabled);
}

static int32_t t_get_optional_program_stop(void *ctx) {
    return GET_OPTIONAL_PROGRAM_STOP() ? 1 : 0;
}

static void t_update_tag(void *ctx, uint64_t tag_ptr) {
    StateTag *tag = (StateTag *)(uintptr_t)tag_ptr;
    UPDATE_TAG(*tag);
}

static void t_set_parameter_file_name(void *ctx, const char *name) {
    SET_PARAMETER_FILE_NAME(name);
}

static void t_on_reset(void *ctx) {
    ON_RESET();
}

static double t_get_user_defined_result(void *ctx) {
    return GET_USER_DEFINED_RESULT();
}

// ---- Getters ----

static double t_get_external_feed_rate(void *ctx) { return GET_EXTERNAL_FEED_RATE(); }
static double t_get_external_traverse_rate(void *ctx) { return GET_EXTERNAL_TRAVERSE_RATE(); }

static int32_t t_get_external_length_unit_type(void *ctx) {
    return (int32_t)GET_EXTERNAL_LENGTH_UNIT_TYPE();
}

static double t_get_external_length_units(void *ctx) { return GET_EXTERNAL_LENGTH_UNITS(); }
static double t_get_external_angle_units(void *ctx) { return GET_EXTERNAL_ANGLE_UNITS(); }

static int32_t t_get_external_motion_control_mode(void *ctx) {
    return (int32_t)GET_EXTERNAL_MOTION_CONTROL_MODE();
}

static double t_get_external_motion_control_tolerance(void *ctx) {
    return GET_EXTERNAL_MOTION_CONTROL_TOLERANCE();
}

static double t_get_external_motion_control_naivecam_tolerance(void *ctx) {
    return GET_EXTERNAL_MOTION_CONTROL_NAIVECAM_TOLERANCE();
}

static int32_t t_get_external_flood(void *ctx) { return GET_EXTERNAL_FLOOD(); }
static int32_t t_get_external_mist(void *ctx) { return GET_EXTERNAL_MIST(); }

static double t_get_external_position_x(void *ctx) { return GET_EXTERNAL_POSITION_X(); }
static double t_get_external_position_y(void *ctx) { return GET_EXTERNAL_POSITION_Y(); }
static double t_get_external_position_z(void *ctx) { return GET_EXTERNAL_POSITION_Z(); }
static double t_get_external_position_a(void *ctx) { return GET_EXTERNAL_POSITION_A(); }
static double t_get_external_position_b(void *ctx) { return GET_EXTERNAL_POSITION_B(); }
static double t_get_external_position_c(void *ctx) { return GET_EXTERNAL_POSITION_C(); }
static double t_get_external_position_u(void *ctx) { return GET_EXTERNAL_POSITION_U(); }
static double t_get_external_position_v(void *ctx) { return GET_EXTERNAL_POSITION_V(); }
static double t_get_external_position_w(void *ctx) { return GET_EXTERNAL_POSITION_W(); }

static double t_get_external_probe_position_x(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_X(); }
static double t_get_external_probe_position_y(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_Y(); }
static double t_get_external_probe_position_z(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_Z(); }
static double t_get_external_probe_position_a(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_A(); }
static double t_get_external_probe_position_b(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_B(); }
static double t_get_external_probe_position_c(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_C(); }
static double t_get_external_probe_position_u(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_U(); }
static double t_get_external_probe_position_v(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_V(); }
static double t_get_external_probe_position_w(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_W(); }

static double t_get_external_probe_value(void *ctx) { return GET_EXTERNAL_PROBE_VALUE(); }
static int32_t t_get_external_probe_tripped_value(void *ctx) { return GET_EXTERNAL_PROBE_TRIPPED_VALUE(); }

static double t_get_external_speed(void *ctx, int32_t spindle) {
    return GET_EXTERNAL_SPEED(spindle);
}

static int32_t t_get_external_spindle(void *ctx, int32_t spindle) {
    return (int32_t)GET_EXTERNAL_SPINDLE(spindle);
}

static double t_get_external_tool_length_xoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_XOFFSET(); }
static double t_get_external_tool_length_yoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_YOFFSET(); }
static double t_get_external_tool_length_zoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_ZOFFSET(); }
static double t_get_external_tool_length_aoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_AOFFSET(); }
static double t_get_external_tool_length_boffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_BOFFSET(); }
static double t_get_external_tool_length_coffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_COFFSET(); }
static double t_get_external_tool_length_uoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_UOFFSET(); }
static double t_get_external_tool_length_voffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_VOFFSET(); }
static double t_get_external_tool_length_woffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_WOFFSET(); }

static int32_t t_get_external_tool_slot(void *ctx) { return GET_EXTERNAL_TOOL_SLOT(); }
static int32_t t_get_external_selected_tool_slot(void *ctx) { return GET_EXTERNAL_SELECTED_TOOL_SLOT(); }

static int32_t t_get_external_tool_table(void *ctx, int32_t pocket,
    int32_t *toolno, double offset[9], double *diameter,
    double *frontangle, double *backangle, int32_t *orientation) {
    CANON_TOOL_TABLE tdata = GET_EXTERNAL_TOOL_TABLE(pocket);
    *toolno = tdata.toolno;
    offset[0] = tdata.offset.tran.x;
    offset[1] = tdata.offset.tran.y;
    offset[2] = tdata.offset.tran.z;
    offset[3] = tdata.offset.a;
    offset[4] = tdata.offset.b;
    offset[5] = tdata.offset.c;
    offset[6] = tdata.offset.u;
    offset[7] = tdata.offset.v;
    offset[8] = tdata.offset.w;
    *diameter = tdata.diameter;
    *frontangle = tdata.frontangle;
    *backangle = tdata.backangle;
    *orientation = tdata.orientation;
    return 0;
}

static int32_t t_get_external_tc_fault(void *ctx) { return GET_EXTERNAL_TC_FAULT(); }
static int32_t t_get_external_tc_reason(void *ctx) { return GET_EXTERNAL_TC_REASON(); }
static int32_t t_get_external_queue_empty(void *ctx) { return GET_EXTERNAL_QUEUE_EMPTY(); }
static int32_t t_get_external_axis_mask(void *ctx) { return GET_EXTERNAL_AXIS_MASK(); }

static int32_t t_get_external_digital_input(void *ctx, int32_t index, int32_t def) {
    return GET_EXTERNAL_DIGITAL_INPUT(index, def);
}

static double t_get_external_analog_input(void *ctx, int32_t index, double def) {
    return GET_EXTERNAL_ANALOG_INPUT(index, def);
}

static int32_t t_get_external_feed_override_enable(void *ctx) { return GET_EXTERNAL_FEED_OVERRIDE_ENABLE(); }
static int32_t t_get_external_spindle_override_enable(void *ctx, int32_t spindle) {
    return GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE(spindle);
}
static int32_t t_get_external_adaptive_feed_enable(void *ctx) { return GET_EXTERNAL_ADAPTIVE_FEED_ENABLE(); }
static int32_t t_get_external_feed_hold_enable(void *ctx) { return GET_EXTERNAL_FEED_HOLD_ENABLE(); }

static int32_t t_get_external_plane(void *ctx) {
    return (int32_t)GET_EXTERNAL_PLANE();
}

static void t_get_external_parameter_file_name(void *ctx, const char **buf) {
    static char filename[256];
    GET_EXTERNAL_PARAMETER_FILE_NAME(filename, sizeof(filename));
    *buf = filename;
}

static int32_t t_get_external_offset_applied(void *ctx) { return GET_EXTERNAL_OFFSET_APPLIED(); }

static void t_get_external_offsets(void *ctx, double offsets[9]) {
    EmcPose o = GET_EXTERNAL_OFFSETS();
    offsets[0] = o.tran.x; offsets[1] = o.tran.y; offsets[2] = o.tran.z;
    offsets[3] = o.a; offsets[4] = o.b; offsets[5] = o.c;
    offsets[6] = o.u; offsets[7] = o.v; offsets[8] = o.w;
}

// ---- Factory function ----

static canon_callbacks_t emccanon_table = {
    .ctx = NULL,  // set by emccanon_init_context()
    .init_canon = t_init_canon,
    .set_g5x_offset = t_set_g5x_offset,
    .set_g92_offset = t_set_g92_offset,
    .set_xy_rotation = t_set_xy_rotation,
    .update_end_point = t_update_end_point,
    .use_length_units = t_use_length_units,
    .select_plane = t_select_plane,
    .set_traverse_rate = t_set_traverse_rate,
    .straight_traverse = t_straight_traverse,
    .set_feed_rate = t_set_feed_rate,
    .set_feed_reference = t_set_feed_reference,
    .set_feed_mode = t_set_feed_mode,
    .set_motion_control_mode = t_set_motion_control_mode,
    .set_naivecam_tolerance = t_set_naivecam_tolerance,
    .set_cutter_radius_compensation = t_set_cutter_radius_compensation,
    .start_cutter_radius_compensation = t_start_cutter_radius_compensation,
    .stop_cutter_radius_compensation = t_stop_cutter_radius_compensation,
    .start_speed_feed_synch = t_start_speed_feed_synch,
    .stop_speed_feed_synch = t_stop_speed_feed_synch,
    .arc_feed = t_arc_feed,
    .straight_feed = t_straight_feed,
    .nurbs_feed = t_nurbs_feed,
    .rigid_tap = t_rigid_tap,
    .straight_probe = t_straight_probe,
    .stop = t_stop,
    .dwell = t_dwell,
    .finish = t_finish,
    .set_spindle_mode = t_set_spindle_mode,
    .start_spindle_clockwise = t_start_spindle_clockwise,
    .start_spindle_counterclockwise = t_start_spindle_counterclockwise,
    .set_spindle_speed = t_set_spindle_speed,
    .stop_spindle_turning = t_stop_spindle_turning,
    .orient_spindle = t_orient_spindle,
    .wait_spindle_orient_complete = t_wait_spindle_orient_complete,
    .select_tool = t_select_tool,
    .start_change = t_start_change,
    .change_tool = t_change_tool,
    .change_tool_number = t_change_tool_number,
    .reload_tooldata = t_reload_tooldata,
    .set_tool_table_entry = t_set_tool_table_entry,
    .use_tool_length_offset = t_use_tool_length_offset,
    .flood_on = t_flood_on,
    .flood_off = t_flood_off,
    .mist_on = t_mist_on,
    .mist_off = t_mist_off,
    .enable_feed_override = t_enable_feed_override,
    .disable_feed_override = t_disable_feed_override,
    .enable_speed_override = t_enable_speed_override,
    .disable_speed_override = t_disable_speed_override,
    .enable_feed_hold = t_enable_feed_hold,
    .disable_feed_hold = t_disable_feed_hold,
    .enable_adaptive_feed = t_enable_adaptive_feed,
    .disable_adaptive_feed = t_disable_adaptive_feed,
    .set_motion_output_bit = t_set_motion_output_bit,
    .clear_motion_output_bit = t_clear_motion_output_bit,
    .set_aux_output_bit = t_set_aux_output_bit,
    .clear_aux_output_bit = t_clear_aux_output_bit,
    .set_motion_output_value = t_set_motion_output_value,
    .set_aux_output_value = t_set_aux_output_value,
    .wait_input = t_wait_input,
    .clamp_axis = t_clamp_axis,
    .unclamp_axis = t_unclamp_axis,
    .lock_rotary = t_lock_rotary,
    .unlock_rotary = t_unlock_rotary,
    .program_stop = t_program_stop,
    .optional_program_stop = t_optional_program_stop,
    .program_end = t_program_end,
    .pallet_shuttle = t_pallet_shuttle,
    .comment = t_comment,
    .message = t_message,
    .log_msg = t_log_msg,
    .logopen = t_logopen,
    .logappend = t_logappend,
    .logclose = t_logclose,
    .canon_error = t_canon_error,
    .turn_probe_on = t_turn_probe_on,
    .turn_probe_off = t_turn_probe_off,
    .set_block_delete = t_set_block_delete,
    .get_block_delete = t_get_block_delete,
    .set_optional_program_stop = t_set_optional_program_stop,
    .get_optional_program_stop = t_get_optional_program_stop,
    .update_tag = t_update_tag,
    .set_parameter_file_name = t_set_parameter_file_name,
    .on_reset = t_on_reset,
    .get_user_defined_result = t_get_user_defined_result,
    .get_external_feed_rate = t_get_external_feed_rate,
    .get_external_traverse_rate = t_get_external_traverse_rate,
    .get_external_length_unit_type = t_get_external_length_unit_type,
    .get_external_length_units = t_get_external_length_units,
    .get_external_angle_units = t_get_external_angle_units,
    .get_external_motion_control_mode = t_get_external_motion_control_mode,
    .get_external_motion_control_tolerance = t_get_external_motion_control_tolerance,
    .get_external_motion_control_naivecam_tolerance = t_get_external_motion_control_naivecam_tolerance,
    .get_external_flood = t_get_external_flood,
    .get_external_mist = t_get_external_mist,
    .get_external_position_x = t_get_external_position_x,
    .get_external_position_y = t_get_external_position_y,
    .get_external_position_z = t_get_external_position_z,
    .get_external_position_a = t_get_external_position_a,
    .get_external_position_b = t_get_external_position_b,
    .get_external_position_c = t_get_external_position_c,
    .get_external_position_u = t_get_external_position_u,
    .get_external_position_v = t_get_external_position_v,
    .get_external_position_w = t_get_external_position_w,
    .get_external_probe_position_x = t_get_external_probe_position_x,
    .get_external_probe_position_y = t_get_external_probe_position_y,
    .get_external_probe_position_z = t_get_external_probe_position_z,
    .get_external_probe_position_a = t_get_external_probe_position_a,
    .get_external_probe_position_b = t_get_external_probe_position_b,
    .get_external_probe_position_c = t_get_external_probe_position_c,
    .get_external_probe_position_u = t_get_external_probe_position_u,
    .get_external_probe_position_v = t_get_external_probe_position_v,
    .get_external_probe_position_w = t_get_external_probe_position_w,
    .get_external_probe_value = t_get_external_probe_value,
    .get_external_probe_tripped_value = t_get_external_probe_tripped_value,
    .get_external_speed = t_get_external_speed,
    .get_external_spindle = t_get_external_spindle,
    .get_external_tool_length_xoffset = t_get_external_tool_length_xoffset,
    .get_external_tool_length_yoffset = t_get_external_tool_length_yoffset,
    .get_external_tool_length_zoffset = t_get_external_tool_length_zoffset,
    .get_external_tool_length_aoffset = t_get_external_tool_length_aoffset,
    .get_external_tool_length_boffset = t_get_external_tool_length_boffset,
    .get_external_tool_length_coffset = t_get_external_tool_length_coffset,
    .get_external_tool_length_uoffset = t_get_external_tool_length_uoffset,
    .get_external_tool_length_voffset = t_get_external_tool_length_voffset,
    .get_external_tool_length_woffset = t_get_external_tool_length_woffset,
    .get_external_tool_slot = t_get_external_tool_slot,
    .get_external_selected_tool_slot = t_get_external_selected_tool_slot,
    .get_external_tool_table = t_get_external_tool_table,
    .get_external_tc_fault = t_get_external_tc_fault,
    .get_external_tc_reason = t_get_external_tc_reason,
    .get_external_queue_empty = t_get_external_queue_empty,
    .get_external_axis_mask = t_get_external_axis_mask,
    .get_external_digital_input = t_get_external_digital_input,
    .get_external_analog_input = t_get_external_analog_input,
    .get_external_feed_override_enable = t_get_external_feed_override_enable,
    .get_external_spindle_override_enable = t_get_external_spindle_override_enable,
    .get_external_adaptive_feed_enable = t_get_external_adaptive_feed_enable,
    .get_external_feed_hold_enable = t_get_external_feed_hold_enable,
    .get_external_plane = t_get_external_plane,
    .get_external_parameter_file_name = t_get_external_parameter_file_name,
    .get_external_offset_applied = t_get_external_offset_applied,
    .get_external_offsets = t_get_external_offsets,
};

canon_callbacks_t *emccanon_get_callbacks(void) {
    return &emccanon_table;
}
