/********************************************************************
* Description: emccanon.cc
*   Canonical definitions for 3-axis NC application
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
********************************************************************/
/*

  Notes:

  Units
  -----
  Values are stored internally as mm and degree units, e.g, program
  offsets, end point, tool length offset.  These are "internal
  units". "External units" are the units used by the EMC motion planner.
  All lengths and units output by the interpreter are converted to
  internal units here, using FROM_PROG_LEN,ANG, and then
  TO_EXT_LEN(),ANG are called to convert these to external units.

  Tool Length Offsets
  -------------------
  The interpreter does not subtract off tool length offsets. It calls
  USE_TOOL_LENGTH_OFFSETS(length), which we record here and apply to
  all appropriate values subsequently.
  */

#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>		// strncpy()
#include <ctype.h>		// isspace()
#include "emc.hh"		// EMC NML
#include "emc_nml.hh"
#include "canon.hh"
#include "canon_position.hh"		// data type for a machine position
#include "interpl.hh"		// interp_list
#include "emcglb.h"		// TRAJ_MAX_VELOCITY
#include <rtapi_string.h>
#include "modal_state.hh"
#include "tooldata.hh"

// Generated canon callback table
#define CANON_API_CGO
#include "gomc/generated/gmi/canon/canon_api.h"
#include "emccanon_table.hh"

/* Forward declarations of static canon functions (needed for internal cross-calls) */
static void SET_SPINDLE_SPEED(void *ctx, int32_t s, double r);
static void STOP_SPEED_FEED_SYNCH(void *ctx);
static void START_SPEED_FEED_SYNCH(void *ctx, int32_t spindle, double feed_per_revolution, int32_t velocity_mode);
static void ARC_FEED(void *ctx, int32_t line_number, double first_end, double second_end,
    double first_axis, double second_axis, int32_t rotation, double axis_end_point,
    double a, double b, double c, double u, double v, double w);
static void STRAIGHT_FEED(void *ctx, int32_t line_number, double x, double y, double z,
    double a, double b, double c, double u, double v, double w);
static void CANON_ERROR(const char *fmt, ...) __attribute__((format(printf,1,2)));
static void MESSAGE(void *ctx, const char *s);
static int32_t GET_EXTERNAL_OFFSET_APPLIED(void *ctx);
static void GET_EXTERNAL_OFFSETS(void *ctx, double offsets[9]);
static void SELECT_PLANE(void *ctx, int32_t in_plane);
static void SET_NAIVECAM_TOLERANCE(void *ctx, double tolerance);
static void SET_MOTION_CONTROL_MODE(void *ctx, int32_t mode, double tolerance);

//#define EMCCANON_DEBUG

//Simple compile-time debug macro
#ifdef EMCCANON_DEBUG
#define canon_debug(...) printf(__VA_ARGS__)
#else
#define canon_debug(...)
#endif

/*
  Origin offsets, length units, and active plane are all maintained
  here in this file. Controller runs in absolute mode, and does not
  have plane select concept.

  programOrigin is stored in mm always, and converted when set or read.
  When it's applied to positions, convert positions to mm units first
  and then add programOrigin.

  Units are then converted from mm to external units, as reported by
  the GET_EXTERNAL_LENGTH_UNITS() function.
  */

static CanonConfig_t canon;

static int debug_velacc = 0;

static StateTag _tag;

static void UPDATE_TAG(void */*ctx*/, uint64_t tag_ptr) {
    StateTag *tag = (StateTag *)(uintptr_t)tag_ptr;
    canon_debug("--Got UPDATE_TAG: %d--\n",tag->fields[GM_FIELD_LINE_NUMBER]);
    _tag = *tag;
}

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MIN3
#define MIN3(a,b,c) (MIN(MIN((a),(b)),(c)))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef MAX3
#define MAX3(a,b,c) (MAX(MAX((a),(b)),(c)))
#endif

#ifndef MAX4
#define MAX4(a,b,c,d) (MAX(MAX((a),(b)),MAX((c),(d))))
#endif

#ifndef MAX9
#define MAX9(a,b,c,d,e,f,g,h,i) (MAX3((MAX3(a,b,c)),(MAX3(d,e,f)),(MAX3(g,h,i))))
#endif

/* Forward declarations for functions used in macros below */
static double GET_EXTERNAL_LENGTH_UNITS(void *ctx);
static double GET_EXTERNAL_ANGLE_UNITS(void *ctx);

/* macros for converting internal (mm/deg) units to external units */
#define TO_EXT_LEN(mm) ((mm) * GET_EXTERNAL_LENGTH_UNITS(NULL))
#define TO_EXT_ANG(deg) ((deg) * GET_EXTERNAL_ANGLE_UNITS(NULL))

/* macros for converting external units to internal (mm/deg) units */
#define FROM_EXT_LEN(ext) ((ext) / GET_EXTERNAL_LENGTH_UNITS(NULL))
#define FROM_EXT_ANG(ext) ((ext) / GET_EXTERNAL_ANGLE_UNITS(NULL))

/* macros for converting internal (mm/deg) units to program units */
#define TO_PROG_LEN(mm) ((mm) / (canon.lengthUnits == CANON_UNITS_INCHES ? 25.4 : canon.lengthUnits == CANON_UNITS_CM ? 10.0 : 1.0))
#define TO_PROG_ANG(deg) (deg)

/* macros for converting program units to internal (mm/deg) units */
#define FROM_PROG_LEN(prog) ((prog) * (canon.lengthUnits == CANON_UNITS_INCHES ? 25.4 : canon.lengthUnits == CANON_UNITS_CM ? 10.0 : 1.0))
#define FROM_PROG_ANG(prog) (prog)

/* Certain axes are periodic.  Hardcode this for now */
#define IS_PERIODIC(axisnum) \
    ((axisnum) == 3 || (axisnum) == 4 || (axisnum) == 5)

// this doesn't quite work yet: disable
#undef IS_PERIODIC
#define IS_PERIODIC(axisnum) (0)

#define AXIS_PERIOD(axisnum) (IS_PERIODIC(axisnum) ? 360 : 0)

//KLUDGE kinematic data struct (instead of returning a single float value)
//FIXME This should really be refactored into a more general structure, but this
//means tearing up the getStraightXXX functions, which probably means
//converting to canon_position operators
struct VelData {
    double tmax;
    double vel;
    double dtot;
};

struct AccelData{
    double tmax;
    double acc;
    double dtot;
};

static PM_QUATERNION quat(1, 0, 0, 0);

static void flush_segments(void);

static inline void add_tag_to_msg(NMLmsg * msg, StateTag const &tag){
    //FIXME this better be an EMC_TRAJ message or bad things will happen
    ((EMC_TRAJ_CMD_MSG *) msg)->tag = tag.get_state_tag();
}


/**
 * Add the the given line number and append the message to the interp list.
 * Note that the append function takes the message by reference, so this also
 * needs to have the message passed in by reference or it barfs.
 */
static inline void tag_and_send(NMLmsg &msg, StateTag const &tag) {
    add_tag_to_msg(&msg,tag);
    interp_list.append(msg);
}

/*
  These decls were from the old 3-axis canon.hh, and refer functions
  defined here that are used for convenience but no longer have decls
  in the 6-axis canon.hh. So, we declare them here now.
*/

#ifndef D2R
#define D2R(r) ((r)*M_PI/180.0)
#endif

static void rotate(double &x, double &y, double theta) {
    double xx, yy;
    double t = D2R(theta);
    xx = x;
    yy = y;
    x = xx * cos(t) - yy * sin(t); 
    y = xx * sin(t) + yy * cos(t);
}


/**
 * Implementation of planar rotation for a 3D vector.
 * This is basically a shortcut for "rotate" when the values are stored in a
 * cartesian vector.
 * The use of static "xy_rotation" is ugly here, but is at least consistent.
 */
static void to_rotated(PM_CARTESIAN &vec) {
    rotate(vec.x,vec.y,canon.xy_rotation);
}
#if 0
static void from_rotated(PM_CARTESIAN &vec) {
    rotate(vec.x,vec.y,-canon.xy_rotation);
}
#endif
static void rotate_and_offset(CANON_POSITION & pos) {

    pos += canon.g92Offset;

    rotate(pos.x, pos.y, canon.xy_rotation);

    pos += canon.g5xOffset;

    pos += canon.toolOffset;
}

static void rotate_and_offset_xyz(PM_CARTESIAN & xyz) {

    xyz += canon.g92Offset.xyz();

    rotate(xyz.x, xyz.y, canon.xy_rotation);

    xyz += canon.g5xOffset.xyz();

    xyz += PM_CARTESIAN(canon.toolOffset.tran.x,
			canon.toolOffset.tran.y,
			canon.toolOffset.tran.z);
}

static CANON_POSITION unoffset_and_unrotate_pos(const CANON_POSITION pos) {
    CANON_POSITION res;

    res = pos;

    res -= canon.toolOffset;
    
    res -= canon.g5xOffset;

    rotate(res.x, res.y, -canon.xy_rotation);

    res -= canon.g92Offset;

    return res;
}

static void rotate_and_offset_pos(double &x, double &y, double &z, double &a, double &b, double &c, double &u, double &v, double &w) {
    x += canon.g92Offset.x;
    y += canon.g92Offset.y;
    z += canon.g92Offset.z;
    a += canon.g92Offset.a;
    b += canon.g92Offset.b;
    c += canon.g92Offset.c;
    u += canon.g92Offset.u;
    v += canon.g92Offset.v;
    w += canon.g92Offset.w;

    rotate(x, y, canon.xy_rotation);

    x += canon.g5xOffset.x;
    y += canon.g5xOffset.y;
    z += canon.g5xOffset.z;
    a += canon.g5xOffset.a;
    b += canon.g5xOffset.b;
    c += canon.g5xOffset.c;
    u += canon.g5xOffset.u;
    v += canon.g5xOffset.v;
    w += canon.g5xOffset.w;

    x += canon.toolOffset.tran.x;
    y += canon.toolOffset.tran.y;
    z += canon.toolOffset.tran.z;
    a += canon.toolOffset.a;
    b += canon.toolOffset.b;
    c += canon.toolOffset.c;
    u += canon.toolOffset.u;
    v += canon.toolOffset.v;
    w += canon.toolOffset.w;
}


static CANON_POSITION unoffset_and_unrotate_pos(const EmcPose pos) {
    CANON_POSITION res(pos);
    return unoffset_and_unrotate_pos(res);
}

static void from_prog(double &x, double &y, double &z, double &a, double &b, double &c, double &u, double &v, double &w) {
    x = FROM_PROG_LEN(x);
    y = FROM_PROG_LEN(y);
    z = FROM_PROG_LEN(z);
    a = FROM_PROG_ANG(a);
    b = FROM_PROG_ANG(b);
    c = FROM_PROG_ANG(c);
    u = FROM_PROG_LEN(u);
    v = FROM_PROG_LEN(v);
    w = FROM_PROG_LEN(w);
}

static void from_prog(CANON_POSITION &pos) {
    pos.x = FROM_PROG_LEN(pos.x);
    pos.y = FROM_PROG_LEN(pos.y);
    pos.z = FROM_PROG_LEN(pos.z);
    pos.a = FROM_PROG_ANG(pos.a);
    pos.b = FROM_PROG_ANG(pos.b);
    pos.c = FROM_PROG_ANG(pos.c);
    pos.u = FROM_PROG_LEN(pos.u);
    pos.v = FROM_PROG_LEN(pos.v);
    pos.w = FROM_PROG_LEN(pos.w);
}

static void from_prog_len(PM_CARTESIAN &vec) {
    vec.x = FROM_PROG_LEN(vec.x);
    vec.y = FROM_PROG_LEN(vec.y);
    vec.z = FROM_PROG_LEN(vec.z);
}
#if 0
static void to_ext(double &x, double &y, double &z, double &a, double &b, double &c, double &u, double &v, double &w) {
    x = TO_EXT_LEN(x);
    y = TO_EXT_LEN(y);
    z = TO_EXT_LEN(z);
    a = TO_EXT_ANG(a);
    b = TO_EXT_ANG(b);
    c = TO_EXT_ANG(c);
    u = TO_EXT_LEN(u);
    v = TO_EXT_LEN(v);
    w = TO_EXT_LEN(w);
}

static void to_ext(CANON_POSITION & pos) {
    pos.x=TO_EXT_LEN(pos.x);
    pos.y=TO_EXT_LEN(pos.y);
    pos.z=TO_EXT_LEN(pos.z);
    pos.a=TO_EXT_ANG(pos.a);
    pos.b=TO_EXT_ANG(pos.b);
    pos.c=TO_EXT_ANG(pos.c);
    pos.u=TO_EXT_LEN(pos.u);
    pos.v=TO_EXT_LEN(pos.v);
    pos.w=TO_EXT_LEN(pos.w);
}
#endif

static PM_CARTESIAN to_ext_len(const PM_CARTESIAN & pos) {
    PM_CARTESIAN ret;
    ret.x = TO_EXT_LEN(pos.x);
    ret.y = TO_EXT_LEN(pos.y);
    ret.z = TO_EXT_LEN(pos.z);
    return ret;
}

static EmcPose to_ext_pose(double x, double y, double z, double a, double b, double c, double u, double v, double w) {
    EmcPose result;
    result.tran.x = TO_EXT_LEN(x);
    result.tran.y = TO_EXT_LEN(y);
    result.tran.z = TO_EXT_LEN(z);
    result.a = TO_EXT_ANG(a);
    result.b = TO_EXT_ANG(b);
    result.c = TO_EXT_ANG(c);
    result.u = TO_EXT_LEN(u);
    result.v = TO_EXT_LEN(v);
    result.w = TO_EXT_LEN(w);
    return result;
}

static EmcPose to_ext_pose(const CANON_POSITION & pos) {
    EmcPose result;
    result.tran.x = TO_EXT_LEN(pos.x);
    result.tran.y = TO_EXT_LEN(pos.y);
    result.tran.z = TO_EXT_LEN(pos.z);
    result.a = TO_EXT_ANG(pos.a);
    result.b = TO_EXT_ANG(pos.b);
    result.c = TO_EXT_ANG(pos.c);
    result.u = TO_EXT_LEN(pos.u);
    result.v = TO_EXT_LEN(pos.v);
    result.w = TO_EXT_LEN(pos.w);
    return result;
}

static void to_prog(CANON_POSITION &e) {
    e.x = TO_PROG_LEN(e.x);
    e.y = TO_PROG_LEN(e.y);
    e.z = TO_PROG_LEN(e.z);
    e.a = TO_PROG_ANG(e.a);
    e.b = TO_PROG_ANG(e.b);
    e.c = TO_PROG_ANG(e.c);
    e.u = TO_PROG_LEN(e.u);
    e.v = TO_PROG_LEN(e.v);
    e.w = TO_PROG_LEN(e.w);
}

static int axis_valid(int n) {
    return emcStatus->motion.traj.axis_mask & (1<<n);
}

static void canonUpdateEndPoint(double x, double y, double z, 
                                double a, double b, double c,
                                double u, double v, double w)
{
    canon.endPoint.x = x;
    canon.endPoint.y = y;
    canon.endPoint.z = z;

    canon.endPoint.a = a;
    canon.endPoint.b = b;
    canon.endPoint.c = c;

    canon.endPoint.u = u;
    canon.endPoint.v = v;
    canon.endPoint.w = w;
}

static void canonUpdateEndPoint(const CANON_POSITION & pos)
{
    canon.endPoint = pos;
}

/* External call to update the canon end point.
   Called by emctask during skipping of lines (run-from-line) */
static void CANON_UPDATE_END_POINT(void */*ctx*/, double x, double y, double z, 
			    double a, double b, double c, 
			    double u, double v, double w)
{
    canonUpdateEndPoint(FROM_PROG_LEN(x),FROM_PROG_LEN(y),FROM_PROG_LEN(z),
    			FROM_PROG_ANG(a),FROM_PROG_ANG(b),FROM_PROG_ANG(c),
			FROM_PROG_LEN(u),FROM_PROG_LEN(v),FROM_PROG_LEN(w));
}

static double toExtVel(double vel) {
    if (canon.cartesian_move && !canon.angular_move) {
	return TO_EXT_LEN(vel);
    } else if (!canon.cartesian_move && canon.angular_move) {
	return TO_EXT_ANG(vel);
    } else if (canon.cartesian_move && canon.angular_move) {
	return TO_EXT_LEN(vel);
    } else { //seems this case was forgotten, neither linear, neither angular move (we are only sending vel)
	return TO_EXT_LEN(vel);
    }	
}

static double toExtAcc(double acc) { return toExtVel(acc); }

static void send_g5x_msg(int index) {
    flush_segments();

    /* append it to interp list so it gets updated at the right time, not at
       read-ahead time */
    EMC_TRAJ_SET_G5X set_g5x_msg;

    set_g5x_msg.g5x_index = index;

    set_g5x_msg.origin = to_ext_pose(canon.g5xOffset);

    for (int s = 0; s < emcStatus->motion.traj.spindles; s++){
        if(canon.spindle[s].css_maximum) {
            SET_SPINDLE_SPEED(NULL, s, canon.spindle[s].speed);
        }
    }
    interp_list.append(set_g5x_msg);
}

static void send_g92_msg(void) {
    flush_segments();

    /* append it to interp list so it gets updated at the right time, not at
       read-ahead time */
    EMC_TRAJ_SET_G92 set_g92_msg;

    set_g92_msg.origin = to_ext_pose(canon.g92Offset);

    for (int s = 0; s < emcStatus->motion.traj.spindles; s++){
        if(canon.spindle[s].css_maximum) {
            SET_SPINDLE_SPEED(NULL, s, canon.spindle[s].speed);
        }
    }
    interp_list.append(set_g92_msg);
}

static void SET_XY_ROTATION(void */*ctx*/, double t) {
    EMC_TRAJ_SET_ROTATION sr;
    sr.rotation = t;
    interp_list.append(sr);

    canon.xy_rotation = t;
}

static void SET_G5X_OFFSET(void */*ctx*/, int32_t index,
                    double x, double y, double z,
                    double a, double b, double c,
                    double u, double v, double w)
{
    CANON_POSITION pos(x,y,z,a,b,c,u,v,w);
    from_prog(pos);
    /* convert to mm units */
    canon.g5xOffset = pos;

    send_g5x_msg(index);
}

static void SET_G92_OFFSET(void */*ctx*/, double x, double y, double z,
                    double a, double b, double c,
                    double u, double v, double w) {
    /* convert to mm units */
    CANON_POSITION pos(x,y,z,a,b,c,u,v,w);
    from_prog(pos);

    canon.g92Offset = pos;

    send_g92_msg();
}

static void USE_LENGTH_UNITS(void */*ctx*/, int32_t in_unit)
{
    canon.lengthUnits = (CANON_UNITS)in_unit;

    emcStatus->task.programUnits = (CANON_UNITS)in_unit;
}

/* Free Space Motion */
static void SET_TRAVERSE_RATE(void */*ctx*/, double rate)
{
    // nothing need be done here
}

static void SET_FEED_MODE(void */*ctx*/, int32_t spindle, int32_t mode) {
    flush_segments();
    canon.feed_mode = mode;
    canon.spindle_num = spindle;
    if(canon.feed_mode == 0) STOP_SPEED_FEED_SYNCH(NULL);
}

static void SET_FEED_RATE(void */*ctx*/, double rate)
{

    if(canon.feed_mode) {
	START_SPEED_FEED_SYNCH(NULL, canon.spindle_num, rate, 1);
	canon.linearFeedRate = rate;
    } else {
	/* convert from /min to /sec */
	rate /= 60.0;


	/* convert to traj units (mm & deg) if needed */
	double newLinearFeedRate = FROM_PROG_LEN(rate),
	       newAngularFeedRate = FROM_PROG_ANG(rate);

	if(newLinearFeedRate != canon.linearFeedRate
		|| newAngularFeedRate != canon.angularFeedRate)
	    flush_segments();

	canon.linearFeedRate = newLinearFeedRate;
	canon.angularFeedRate = newAngularFeedRate;
    }
}

static void SET_FEED_REFERENCE(void */*ctx*/, int32_t reference)
{
    // nothing need be done here
}

/**
 * Get the shortest linear axis displacement that the TP can handle as a discrete move.
 *
 * If this looks dirty, it's because it is. Canon runs in its own units, but
 * the TP uses user units. Therefore, the minimum displacement has to be
 * computed the same way, with the same threshold, or short moves do strange
 * things (accel violations or infinite pauses).
 *
 * @todo revisit this when the TP is overhauled to use a consistent set of internal units.
 */
static double getMinLinearDisplacement()
{
    return FROM_EXT_LEN(CART_FUZZ);
}

/**
 * Equivalent of getMinLinearDisplacement for rotary axes.
 */
static double getMinAngularDisplacement()
{
    return FROM_EXT_ANG(CART_FUZZ);
}

/**
 * Apply the minimum displacement check to each axis delta.
 *
 * Checks that the axis is valid / active, and looks up the appropriate minimum
 * displacement for the axis type and user units.
 */
static void applyMinDisplacement(double &dx,
                                 double &dy,
                                 double &dz,
                                 double &da,
                                 double &db,
                                 double &dc,
                                 double &du,
                                 double &dv,
                                 double &dw
                                 )
{
    const double tiny_linear = getMinLinearDisplacement();
    const double tiny_angular = getMinAngularDisplacement();
    if(!axis_valid(0) || dx < tiny_linear) dx = 0.0;
    if(!axis_valid(1) || dy < tiny_linear) dy = 0.0;
    if(!axis_valid(2) || dz < tiny_linear) dz = 0.0;
    if(!axis_valid(3) || da < tiny_angular) da = 0.0;
    if(!axis_valid(4) || db < tiny_linear) db = 0.0;
    if(!axis_valid(5) || dc < tiny_linear) dc = 0.0;
    if(!axis_valid(6) || du < tiny_linear) du = 0.0;
    if(!axis_valid(7) || dv < tiny_linear) dv = 0.0;
    if(!axis_valid(8) || dw < tiny_linear) dw = 0.0;
}


/**
 * Get the limiting acceleration for a displacement from the current position to the given position.
 * returns a single acceleration that is the minimum of all axis accelerations.
 */
static AccelData getStraightAcceleration(double x, double y, double z,
                               double a, double b, double c,
                               double u, double v, double w)
{
    double dx, dy, dz, du, dv, dw, da, db, dc;
    double tx, ty, tz, tu, tv, tw, ta, tb, tc;
    AccelData out;

    out.acc = 0.0; // if a move to nowhere
    out.tmax = 0.0;
    out.dtot = 0.0;

    // Compute absolute travel distance for each axis:
    dx = fabs(x - canon.endPoint.x);
    dy = fabs(y - canon.endPoint.y);
    dz = fabs(z - canon.endPoint.z);
    da = fabs(a - canon.endPoint.a);
    db = fabs(b - canon.endPoint.b);
    dc = fabs(c - canon.endPoint.c);
    du = fabs(u - canon.endPoint.u);
    dv = fabs(v - canon.endPoint.v);
    dw = fabs(w - canon.endPoint.w);

    applyMinDisplacement(dx, dy, dz, da, db, dc, du, dv, dw);

    if(debug_velacc) 
        printf("getStraightAcceleration dx %g dy %g dz %g da %g db %g dc %g du %g dv %g dw %g ", 
               dx, dy, dz, da, db, dc, du, dv, dw);

    // Figure out what kind of move we're making.  This is used to determine
    // the units of vel/acc.
    if (dx <= 0.0 && dy <= 0.0 && dz <= 0.0 &&
        du <= 0.0 && dv <= 0.0 && dw <= 0.0) {
	canon.cartesian_move = 0;
    } else {
	canon.cartesian_move = 1;
    }
    if (da <= 0.0 && db <= 0.0 && dc <= 0.0) {
	canon.angular_move = 0;
    } else {
	canon.angular_move = 1;
    }

    // Pure linear move:
    if (canon.cartesian_move && !canon.angular_move) {
	tx = dx? (dx / FROM_EXT_LEN(emcAxisGetMaxAcceleration(0))): 0.0;
	ty = dy? (dy / FROM_EXT_LEN(emcAxisGetMaxAcceleration(1))): 0.0;
	tz = dz? (dz / FROM_EXT_LEN(emcAxisGetMaxAcceleration(2))): 0.0;
	tu = du? (du / FROM_EXT_LEN(emcAxisGetMaxAcceleration(6))): 0.0;
	tv = dv? (dv / FROM_EXT_LEN(emcAxisGetMaxAcceleration(7))): 0.0;
	tw = dw? (dw / FROM_EXT_LEN(emcAxisGetMaxAcceleration(8))): 0.0;
        out.tmax = MAX3(tx, ty ,tz);
        out.tmax = MAX4(tu, tv, tw, out.tmax);

        if(dx || dy || dz)
            out.dtot = sqrt(dx * dx + dy * dy + dz * dz);
        else
            out.dtot = sqrt(du * du + dv * dv + dw * dw);
        
	if (out.tmax > 0.0) {
	    out.acc = out.dtot / out.tmax;
	}
    }
    // Pure angular move:
    else if (!canon.cartesian_move && canon.angular_move) {
	ta = da? (da / FROM_EXT_ANG(emcAxisGetMaxAcceleration(3))): 0.0;
	tb = db? (db / FROM_EXT_ANG(emcAxisGetMaxAcceleration(4))): 0.0;
	tc = dc? (dc / FROM_EXT_ANG(emcAxisGetMaxAcceleration(5))): 0.0;
        out.tmax = MAX3(ta, tb, tc);

	out.dtot = sqrt(da * da + db * db + dc * dc);
	if (out.tmax > 0.0) {
	    out.acc = out.dtot / out.tmax;
	}
    }
    // Combination angular and linear move:
    else if (canon.cartesian_move && canon.angular_move) {
	tx = dx? (dx / FROM_EXT_LEN(emcAxisGetMaxAcceleration(0))): 0.0;
	ty = dy? (dy / FROM_EXT_LEN(emcAxisGetMaxAcceleration(1))): 0.0;
	tz = dz? (dz / FROM_EXT_LEN(emcAxisGetMaxAcceleration(2))): 0.0;
	ta = da? (da / FROM_EXT_ANG(emcAxisGetMaxAcceleration(3))): 0.0;
	tb = db? (db / FROM_EXT_ANG(emcAxisGetMaxAcceleration(4))): 0.0;
	tc = dc? (dc / FROM_EXT_ANG(emcAxisGetMaxAcceleration(5))): 0.0;
	tu = du? (du / FROM_EXT_LEN(emcAxisGetMaxAcceleration(6))): 0.0;
	tv = dv? (dv / FROM_EXT_LEN(emcAxisGetMaxAcceleration(7))): 0.0;
	tw = dw? (dw / FROM_EXT_LEN(emcAxisGetMaxAcceleration(8))): 0.0;
        out.tmax = MAX9(tx, ty, tz,
                    ta, tb, tc,
                    tu, tv, tw);

    if(debug_velacc)
        printf("getStraightAcceleration t^2 tx %g ty %g tz %g ta %g tb %g tc %g tu %g tv %g tw %g\n", 
               tx, ty, tz, ta, tb, tc, tu, tv, tw);
/*  According to NIST IR6556 Section 2.1.2.5 Paragraph A
    a combnation move is handled like a linear move, except
    that the angular axes are allowed sufficient time to
    complete their motion coordinated with the motion of
    the linear axes.
*/
        if(dx || dy || dz)
            out.dtot = sqrt(dx * dx + dy * dy + dz * dz);
        else
            out.dtot = sqrt(du * du + dv * dv + dw * dw);

	if (out.tmax > 0.0) {
	    out.acc = out.dtot / out.tmax;
	}
    }
    if(debug_velacc) 
        printf("cartesian %d ang %d acc %g\n", canon.cartesian_move, canon.angular_move, out.acc);
    return out;
}

static AccelData getStraightAcceleration(CANON_POSITION pos)
{

    return getStraightAcceleration(pos.x,
            pos.y,
            pos.z,
            pos.a,
            pos.b,
            pos.c,
            pos.u,
            pos.v,
            pos.w);
}

static VelData getStraightVelocity(double x, double y, double z,
			   double a, double b, double c,
                           double u, double v, double w)
{
    double dx, dy, dz, da, db, dc, du, dv, dw;
    double tx, ty, tz, ta, tb, tc, tu, tv, tw;
    VelData out;

/* If we get a move to nowhere (!canon.cartesian_move && !canon.angular_move)
   we might as well go there at the canon.linearFeedRate...
*/
    out.vel = canon.linearFeedRate;
    out.tmax = 0;
    out.dtot = 0;

    // Compute absolute travel distance for each axis:
    dx = fabs(x - canon.endPoint.x);
    dy = fabs(y - canon.endPoint.y);
    dz = fabs(z - canon.endPoint.z);
    da = fabs(a - canon.endPoint.a);
    db = fabs(b - canon.endPoint.b);
    dc = fabs(c - canon.endPoint.c);
    du = fabs(u - canon.endPoint.u);
    dv = fabs(v - canon.endPoint.v);
    dw = fabs(w - canon.endPoint.w);

    applyMinDisplacement(dx, dy, dz, da, db, dc, du, dv, dw);

    if(debug_velacc) 
        printf("getStraightVelocity dx %g dy %g dz %g da %g db %g dc %g du %g dv %g dw %g\n",
               dx, dy, dz, da, db, dc, du, dv, dw);

    // Figure out what kind of move we're making:
    if (dx <= 0.0 && dy <= 0.0 && dz <= 0.0 &&
        du <= 0.0 && dv <= 0.0 && dw <= 0.0) {
	canon.cartesian_move = 0;
    } else {
	canon.cartesian_move = 1;
    }
    if (da <= 0.0 && db <= 0.0 && dc <= 0.0) {
	canon.angular_move = 0;
    } else {
	canon.angular_move = 1;
    }

    // Pure linear move:
    if (canon.cartesian_move && !canon.angular_move) {
	tx = dx? fabs(dx / FROM_EXT_LEN(emcAxisGetMaxVelocity(0))): 0.0;
	ty = dy? fabs(dy / FROM_EXT_LEN(emcAxisGetMaxVelocity(1))): 0.0;
	tz = dz? fabs(dz / FROM_EXT_LEN(emcAxisGetMaxVelocity(2))): 0.0;
	tu = du? fabs(du / FROM_EXT_LEN(emcAxisGetMaxVelocity(6))): 0.0;
	tv = dv? fabs(dv / FROM_EXT_LEN(emcAxisGetMaxVelocity(7))): 0.0;
	tw = dw? fabs(dw / FROM_EXT_LEN(emcAxisGetMaxVelocity(8))): 0.0;
        out.tmax = MAX3(tx, ty ,tz);
        out.tmax = MAX4(tu, tv, tw, out.tmax);

        if(dx || dy || dz)
            out.dtot = sqrt(dx * dx + dy * dy + dz * dz);
        else
            out.dtot = sqrt(du * du + dv * dv + dw * dw);

        if (out.tmax <= 0.0) {
            out.vel = canon.linearFeedRate;
        } else {
            out.vel = out.dtot / out.tmax;
        }
    }
    // Pure angular move:
    else if (!canon.cartesian_move && canon.angular_move) {
	ta = da? fabs(da / FROM_EXT_ANG(emcAxisGetMaxVelocity(3))): 0.0;
	tb = db? fabs(db / FROM_EXT_ANG(emcAxisGetMaxVelocity(4))): 0.0;
	tc = dc? fabs(dc / FROM_EXT_ANG(emcAxisGetMaxVelocity(5))): 0.0;
        out.tmax = MAX3(ta, tb, tc);

	out.dtot = sqrt(da * da + db * db + dc * dc);
	if (out.tmax <= 0.0) {
	    out.vel = canon.angularFeedRate;
	} else {
	    out.vel = out.dtot / out.tmax;
	}
    }
    // Combination angular and linear move:
    else if (canon.cartesian_move && canon.angular_move) {
	tx = dx? fabs(dx / FROM_EXT_LEN(emcAxisGetMaxVelocity(0))): 0.0;
	ty = dy? fabs(dy / FROM_EXT_LEN(emcAxisGetMaxVelocity(1))): 0.0;
	tz = dz? fabs(dz / FROM_EXT_LEN(emcAxisGetMaxVelocity(2))): 0.0;
	ta = da? fabs(da / FROM_EXT_ANG(emcAxisGetMaxVelocity(3))): 0.0;
	tb = db? fabs(db / FROM_EXT_ANG(emcAxisGetMaxVelocity(4))): 0.0;
	tc = dc? fabs(dc / FROM_EXT_ANG(emcAxisGetMaxVelocity(5))): 0.0;
	tu = du? fabs(du / FROM_EXT_LEN(emcAxisGetMaxVelocity(6))): 0.0;
	tv = dv? fabs(dv / FROM_EXT_LEN(emcAxisGetMaxVelocity(7))): 0.0;
	tw = dw? fabs(dw / FROM_EXT_LEN(emcAxisGetMaxVelocity(8))): 0.0;
        out.tmax = MAX9(tx, ty, tz,
                    ta, tb, tc,
                    tu, tv, tw);

        if(debug_velacc)
            printf("getStraightVelocity times tx %g ty %g tz %g ta %g tb %g tc %g tu %g tv %g tw %g\n",
                    tx, ty, tz, ta, tb, tc, tu, tv, tw);

/*  According to NIST IR6556 Section 2.1.2.5 Paragraph A
    a combnation move is handled like a linear move, except
    that the angular axes are allowed sufficient time to
    complete their motion coordinated with the motion of
    the linear axes.
*/
        if(dx || dy || dz)
            out.dtot = sqrt(dx * dx + dy * dy + dz * dz);
        else
            out.dtot = sqrt(du * du + dv * dv + dw * dw);

        if (out.tmax <= 0.0) {
            out.vel = canon.linearFeedRate;
        } else {
            out.vel = out.dtot / out.tmax;
        }
    }
    if(debug_velacc) 
        printf("cartesian %d ang %d vel %g\n", canon.cartesian_move, canon.angular_move, out.vel);
    return out;
}

static VelData getStraightVelocity(CANON_POSITION pos)
{

    return getStraightVelocity(pos.x,
            pos.y,
            pos.z,
            pos.a,
            pos.b,
            pos.c,
            pos.u,
            pos.v,
            pos.w);
}

#include <vector>
struct pt {
    double x, y, z, a, b, c, u, v, w;
    int line_no;
    StateTag tag;
};

static std::vector<struct pt> chained_points;

static void drop_segments(void) {
    chained_points.clear();
}

static void flush_segments(void) {
    if(chained_points.empty()) return;

    struct pt &pos = chained_points.back();

    double x = pos.x, y = pos.y, z = pos.z;
    double a = pos.a, b = pos.b, c = pos.c;
    double u = pos.u, v = pos.v, w = pos.w;
    
    int line_no = pos.line_no;

#ifdef SHOW_JOINED_SEGMENTS
    for(unsigned int i=0; i != chained_points.size(); i++) { printf("."); }
    printf("\n");
#endif

    VelData linedata = getStraightVelocity(x, y, z, a, b, c, u, v, w);
    double vel = linedata.vel;

    if (canon.cartesian_move && !canon.angular_move) {
        if (vel > canon.linearFeedRate) {
            vel = canon.linearFeedRate;
        }
    } else if (!canon.cartesian_move && canon.angular_move) {
        if (vel > canon.angularFeedRate) {
            vel = canon.angularFeedRate;
        }
    } else if (canon.cartesian_move && canon.angular_move) {
        if (vel > canon.linearFeedRate) {
            vel = canon.linearFeedRate;
        }
    }


    EMC_TRAJ_LINEAR_MOVE linearMoveMsg;
    linearMoveMsg.feed_mode = canon.feed_mode;

    // now x, y, z, and b are in absolute mm or degree units
    linearMoveMsg.end.tran.x = TO_EXT_LEN(x);
    linearMoveMsg.end.tran.y = TO_EXT_LEN(y);
    linearMoveMsg.end.tran.z = TO_EXT_LEN(z);

    linearMoveMsg.end.u = TO_EXT_LEN(u);
    linearMoveMsg.end.v = TO_EXT_LEN(v);
    linearMoveMsg.end.w = TO_EXT_LEN(w);

    // fill in the orientation
    linearMoveMsg.end.a = TO_EXT_ANG(a);
    linearMoveMsg.end.b = TO_EXT_ANG(b);
    linearMoveMsg.end.c = TO_EXT_ANG(c);

    linearMoveMsg.vel = toExtVel(vel);
    linearMoveMsg.ini_maxvel = toExtVel(linedata.vel);
    AccelData lineaccdata = getStraightAcceleration(x, y, z, a, b, c, u, v, w);
    double acc = lineaccdata.acc;
    linearMoveMsg.acc = toExtAcc(acc);

    linearMoveMsg.type = EMC_MOTION_TYPE_FEED;
    linearMoveMsg.indexer_jnum = -1;
    if ((vel && acc) || canon.spindle[canon.spindle_num].synched) {
        interp_list.set_line_number(line_no);
        tag_and_send(linearMoveMsg,pos.tag);
    }
    canonUpdateEndPoint(x, y, z, a, b, c, u, v, w);

    drop_segments();
}

static void get_last_pos(double &lx, double &ly, double &lz) {
    if(chained_points.empty()) {
        lx = canon.endPoint.x;
        ly = canon.endPoint.y;
        lz = canon.endPoint.z;
    } else {
        struct pt &pos = chained_points.back();
        lx = pos.x;
        ly = pos.y;
        lz = pos.z;
    }
}

static bool
linkable(double x, double y, double z, 
         double a, double b, double c, 
         double u, double v, double w) {
    struct pt &pos = chained_points.back();
    if(canon.motionMode != CANON_CONTINUOUS || canon.naivecamTolerance == 0)
        return false;
    //FIXME make this length controlled elsewhere?
    if(chained_points.size() > 100) return false;

    //If ABCUVW motion, then the tangent calculation fails?
    // TODO is there a fundamental reason that we can't handle 9D motion here?
    if(a != pos.a) return false;
    if(b != pos.b) return false;
    if(c != pos.c) return false;
    if(u != pos.u) return false;
    if(v != pos.v) return false;
    if(w != pos.w) return false;

    if(x==canon.endPoint.x && y==canon.endPoint.y && z==canon.endPoint.z) return false;
    
    for(std::vector<struct pt>::iterator it = chained_points.begin();
            it != chained_points.end(); it++) {
        PM_CARTESIAN M(x-canon.endPoint.x, y-canon.endPoint.y, z-canon.endPoint.z),
                     B(canon.endPoint.x, canon.endPoint.y, canon.endPoint.z),
                     P(it->x, it->y, it->z);
        double t0 = dot(M, P-B) / dot(M, M);
        if(t0 < 0) t0 = 0;
        if(t0 > 1) t0 = 1;

        double D = mag(P - (B + t0 * M));
        if(D > canon.naivecamTolerance) return false;
    }
    return true;
}

static void
see_segment(int line_number,
	    StateTag tag,
	    double x, double y, double z, 
            double a, double b, double c,
            double u, double v, double w) {
    bool changed_abc = (a != canon.endPoint.a)
        || (b != canon.endPoint.b)
        || (c != canon.endPoint.c);

    bool changed_uvw = (u != canon.endPoint.u)
        || (v != canon.endPoint.v)
        || (w != canon.endPoint.w);

    if(!chained_points.empty() && !linkable(x, y, z, a, b, c, u, v, w)) {
        flush_segments();
    }
    pt pos = {x, y, z, a, b, c, u, v, w, line_number, tag};
    chained_points.push_back(pos);
    if(changed_abc || changed_uvw) {
        flush_segments();
    }
}

static void FINISH(void */*ctx*/) {
    flush_segments();
}

static void ON_RESET(void */*ctx*/) {
    drop_segments();
}


static void STRAIGHT_TRAVERSE(void */*ctx*/, int32_t line_number,
                       double x, double y, double z,
		       double a, double b, double c,
                       double u, double v, double w)
{
    double vel, acc;

    flush_segments();

    EMC_TRAJ_LINEAR_MOVE linearMoveMsg;

    linearMoveMsg.feed_mode = 0;
    if (canon.rotary_unlock_for_traverse != -1)
        linearMoveMsg.type = EMC_MOTION_TYPE_INDEXROTARY;
    else
        linearMoveMsg.type = EMC_MOTION_TYPE_TRAVERSE;

    from_prog(x,y,z,a,b,c,u,v,w);
    rotate_and_offset_pos(x,y,z,a,b,c,u,v,w);

    VelData veldata = getStraightVelocity(x, y, z, a, b, c, u, v, w);
    AccelData accdata = getStraightAcceleration(x, y, z, a, b, c, u, v, w);

    vel = veldata.vel;
    acc = accdata.acc;

    linearMoveMsg.end = to_ext_pose(x,y,z,a,b,c,u,v,w);
    linearMoveMsg.vel = linearMoveMsg.ini_maxvel = toExtVel(vel);
    linearMoveMsg.acc = toExtAcc(acc);
    linearMoveMsg.indexer_jnum = canon.rotary_unlock_for_traverse;

    int old_feed_mode = canon.feed_mode;
    if(canon.feed_mode)
	STOP_SPEED_FEED_SYNCH(NULL);

    if(vel && acc)  {
        interp_list.set_line_number(line_number);
        tag_and_send(linearMoveMsg, _tag);
    }

    if(old_feed_mode)
	START_SPEED_FEED_SYNCH(NULL, canon.spindle_num, canon.linearFeedRate, 1);

    canonUpdateEndPoint(x, y, z, a, b, c, u, v, w);
}

static void STRAIGHT_FEED(void */*ctx*/, int32_t line_number,
                   double x, double y, double z, 
                   double a, double b, double c,
                   double u, double v, double w)
{
    EMC_TRAJ_LINEAR_MOVE linearMoveMsg;
    linearMoveMsg.feed_mode = canon.feed_mode;

    from_prog(x,y,z,a,b,c,u,v,w);
    rotate_and_offset_pos(x,y,z,a,b,c,u,v,w);
    see_segment(line_number, _tag, x, y, z, a, b, c, u, v, w);
}


static void RIGID_TAP(void */*ctx*/, int32_t line_number, double x, double y, double z, double scale)
{
    double ini_maxvel,acc;
    EMC_TRAJ_RIGID_TAP rigidTapMsg;
    double unused=0;

    from_prog(x,y,z,unused,unused,unused,unused,unused,unused);
    rotate_and_offset_pos(x,y,z,unused,unused,unused,unused,unused,unused);

    VelData veldata = getStraightVelocity(x, y, z, 
                              canon.endPoint.a, canon.endPoint.b, canon.endPoint.c, 
                              canon.endPoint.u, canon.endPoint.v, canon.endPoint.w);
    ini_maxvel = veldata.vel;
    
    AccelData accdata = getStraightAcceleration(x, y, z, 
                                  canon.endPoint.a, canon.endPoint.b, canon.endPoint.c,
                                  canon.endPoint.u, canon.endPoint.v, canon.endPoint.w);
    acc = accdata.acc;
    
    rigidTapMsg.pos = to_ext_pose(x,y,z,
                                 canon.endPoint.a, canon.endPoint.b, canon.endPoint.c,
                                 canon.endPoint.u, canon.endPoint.v, canon.endPoint.w);

    rigidTapMsg.vel = toExtVel(ini_maxvel);
    rigidTapMsg.ini_maxvel = toExtVel(ini_maxvel);
    rigidTapMsg.acc = toExtAcc(acc);
    rigidTapMsg.scale = scale;
    flush_segments();

    if(ini_maxvel && acc)  {
        interp_list.set_line_number(line_number);
        interp_list.append(rigidTapMsg);
    }

    // don't move the endpoint because after this move, we are back where we started
}


/*
  STRAIGHT_PROBE is exactly the same as STRAIGHT_FEED, except that it
  uses a probe message instead of a linear move message.
*/

static void STRAIGHT_PROBE(void */*ctx*/, int32_t line_number,
                    double x, double y, double z, 
                    double a, double b, double c,
                    double u, double v, double w,
                    uint8_t probe_type)
{
    double ini_maxvel, vel, acc;
    EMC_TRAJ_PROBE probeMsg;

    from_prog(x,y,z,a,b,c,u,v,w);
    rotate_and_offset_pos(x,y,z,a,b,c,u,v,w);

    flush_segments();

    VelData veldata = getStraightVelocity(x, y, z, a, b, c, u, v, w);
    ini_maxvel = vel = veldata.vel;

    if (canon.cartesian_move && !canon.angular_move) {
	if (vel > canon.linearFeedRate) {
	    vel = canon.linearFeedRate;
	}
    } else if (!canon.cartesian_move && canon.angular_move) {
	if (vel > canon.angularFeedRate) {
	    vel = canon.angularFeedRate;
	}
    } else if (canon.cartesian_move && canon.angular_move) {
	if (vel > canon.linearFeedRate) {
	    vel = canon.linearFeedRate;
	}
    }

    AccelData accdata = getStraightAcceleration(x, y, z, a, b, c, u, v, w);
    acc = accdata.acc;

    probeMsg.vel = toExtVel(vel);
    probeMsg.ini_maxvel = toExtVel(ini_maxvel);
    probeMsg.acc = toExtAcc(acc);

    probeMsg.type = EMC_MOTION_TYPE_PROBING;
    probeMsg.probe_type = probe_type;

    probeMsg.pos = to_ext_pose(x,y,z,a,b,c,u,v,w);

    if(vel && acc)  {
        interp_list.set_line_number(line_number);
        interp_list.append(probeMsg);
    }
    canonUpdateEndPoint(x, y, z, a, b, c, u, v, w);
}

/* Machining Attributes */

static void SET_MOTION_CONTROL_MODE(void */*ctx*/, int32_t mode, double tolerance)
{
    EMC_TRAJ_SET_TERM_COND setTermCondMsg;

    flush_segments();

    canon.motionMode = (CANON_MOTION_MODE)mode;
    canon.motionTolerance =  FROM_PROG_LEN(tolerance);

    switch (mode) {
    case CANON_CONTINUOUS:
        setTermCondMsg.cond = EMC_TRAJ_TERM_COND_BLEND;
        setTermCondMsg.tolerance = TO_EXT_LEN(canon.motionTolerance);
        break;
    case CANON_EXACT_PATH:
        setTermCondMsg.cond = EMC_TRAJ_TERM_COND_EXACT;
        break;

    case CANON_EXACT_STOP:
    default:
        setTermCondMsg.cond = EMC_TRAJ_TERM_COND_STOP;
        break;
    }

    interp_list.append(setTermCondMsg);
}

static void SET_NAIVECAM_TOLERANCE(void */*ctx*/, double tolerance)
{
    canon.naivecamTolerance =  FROM_PROG_LEN(tolerance);
}

static void SELECT_PLANE(void */*ctx*/, int32_t in_plane)
{
    canon.activePlane = (CANON_PLANE)in_plane;
}

static void SET_CUTTER_RADIUS_COMPENSATION(void */*ctx*/, double radius)
{
    // nothing need be done here
}

static void START_CUTTER_RADIUS_COMPENSATION(void */*ctx*/, int32_t side)
{
    // nothing need be done here
}

static void STOP_CUTTER_RADIUS_COMPENSATION(void */*ctx*/)
{
    // nothing need be done here
}



static void START_SPEED_FEED_SYNCH(void */*ctx*/, int32_t spindle, double feed_per_revolution, int32_t velocity_mode)
{
    flush_segments();
    EMC_TRAJ_SET_SPINDLESYNC spindlesyncMsg;
    spindlesyncMsg.spindle = spindle;
    spindlesyncMsg.feed_per_revolution = TO_EXT_LEN(FROM_PROG_LEN(feed_per_revolution));
    spindlesyncMsg.velocity_mode = velocity_mode;
    interp_list.append(spindlesyncMsg);
    canon.spindle[spindle].synched = 1;
}

static void STOP_SPEED_FEED_SYNCH(void */*ctx*/)
{
    flush_segments();
    EMC_TRAJ_SET_SPINDLESYNC spindlesyncMsg;
    spindlesyncMsg.feed_per_revolution = 0.0;
    spindlesyncMsg.velocity_mode = false;
    interp_list.append(spindlesyncMsg);
    canon.spindle[canon.spindle_num].synched = 0;
}

/* Machining Functions */
static double chord_deviation(double sx, double sy, double ex, double ey, double cx, double cy, int rotation, double &mx, double &my) {
    double th1 = atan2(sy-cy, sx-cx),
           th2 = atan2(ey-cy, ex-cx),
           r = hypot(sy-cy, sx-cx),
           dth = th2 - th1;

    if(rotation < 0) {
        if(dth >= -1e-5) th2 -= 2*M_PI;
        // in the edge case where atan2 gives you -pi and pi, a second iteration is needed
        // to get these in the right order
        dth = th2 - th1;
        if(dth >= -1e-5) th2 -= 2*M_PI;
    } else {
        if(dth <= 1e-5) th2 += 2*M_PI;
        dth = th2 - th1;
        if(dth <= 1e-5) th2 += 2*M_PI;
    }

    double included = fabs(th2 - th1);
    double mid = (th2 + th1) / 2;
    mx = cx + r * cos(mid);
    my = cy + r * sin(mid);
    double dev = r * (1 - cos(included/2));
    return dev;
}

/* Spline and NURBS additional functions; */

static double max(double a, double b) {
    if(a < b) return b;
    return a;
}
static void unit(double *x, double *y) {
    double h = hypot(*x, *y);
    if(h != 0) { *x/=h; *y/=h; }
}

static void
arc(int lineno, double x0, double y0, double x1, double y1, double dx, double dy) {
    double small = 0.000001;
    double x = x1-x0, y=y1-y0;
    double den = 2 * (y*dx - x*dy);
    CANON_POSITION p = unoffset_and_unrotate_pos(canon.endPoint);
    to_prog(p);
    if (fabs(den) > small) {
        double r = -(x*x+y*y)/den;
        double i = dy*r, j = -dx*r;
        double cx = x0+i, cy=y0+j;
        ARC_FEED(NULL, lineno, x1, y1, cx, cy, r<0 ? 1 : -1,
                 p.z, p.a, p.b, p.c, p.u, p.v, p.w);
    } else { 
        STRAIGHT_FEED(NULL, lineno, x1, y1, p.z, p.a, p.b, p.c, p.u, p.v, p.w);
    }
}

static int
biarc(int lineno, double p0x, double p0y, double tsx, double tsy,
      double p4x, double p4y, double tex, double tey, double r=1.0) {
    unit(&tsx, &tsy);
    unit(&tex, &tey);

    double vx = p0x - p4x, vy = p0y - p4y;
    double c = vx*vx + vy*vy;
    double b = 2 * (vx * (r*tsx + tex) + vy * (r*tsy + tey));
    double a = 2 * r * (tsx * tex + tsy * tey - 1);

    double discr = b*b - 4*a*c;
    if(discr < 0) return 0;

    double disq = sqrt(discr);
    double beta1 = (-b-disq) / 2 / a;
    double beta2 = (-b+disq) / 2 / a;

    if(beta1 > 0 && beta2 > 0)
      return 0;
    double beta = max(beta1, beta2);
    double alpha = beta * r;
    double ab = alpha + beta;
    double p1x = p0x + alpha * tsx, p1y = p0y + alpha * tsy,
         p3x = p4x - beta * tex, p3y = p4y - beta * tey,
         p2x = (p1x*beta + p3x*alpha) / ab,
         p2y = (p1y*beta + p3y*alpha) / ab;
    double tmx = p3x-p2x, tmy = p3y-p2y;
    unit(&tmx, &tmy);

    arc(lineno, p0x, p0y, p2x, p2y, tsx, tsy);
    arc(lineno, p2x, p2y, p4x, p4y, tmx, tmy);
    return 1;
}


/* Canon calls */

static void NURBS_FEED(void */*ctx*/, int32_t lineno, const canon_control_point_t *control_points, size_t npts, uint32_t k) {
    flush_segments();

    // Convert C array to C++ vector for existing implementation
    std::vector<CONTROL_POINT> nurbs_control_points(npts);
    for (size_t i = 0; i < npts; i++) {
        nurbs_control_points[i].X = control_points[i].x;
        nurbs_control_points[i].Y = control_points[i].y;
        nurbs_control_points[i].W = control_points[i].w;
    }
    unsigned int n = nurbs_control_points.size() - 1;
    double umax = n - k + 2;
    unsigned int div = nurbs_control_points.size()*4;
    std::vector<unsigned int> knot_vector = knot_vector_creator(n, k);	
    PLANE_POINT P0, P0T, P1, P1T;

    P0 = nurbs_point(0,k,nurbs_control_points,knot_vector);
    P0T = nurbs_tangent(0, k, nurbs_control_points, knot_vector);

    for(unsigned int i=1; i<=div; i++) {
	double u = umax * i / div;
        P1 = nurbs_point(u,k,nurbs_control_points,knot_vector);
	P1T = nurbs_tangent(u,k,nurbs_control_points,knot_vector);
        biarc(lineno, P0.X,P0.Y, P0T.X,P0T.Y, P1.X,P1.Y, P1T.X,P1T.Y);
        P0 = P1;
        P0T = P1T;
    }
    knot_vector.clear();
}


/**
 * Simple circular shift function for PM_CARTESIAN type.
 * Cycle around axes without changing the individual values. A circshift of -1
 * makes the X value become the new Y, Y become the Z, and Z become the new X.
 */
static PM_CARTESIAN circshift(PM_CARTESIAN & vec, int steps)
{
    int X=0,Y=1,Z=2;

    int s = 3;
    // Use mod to cycle indices around by steps
    X = (X + steps + s) % s;
    Y = (Y + steps + s) % s;
    Z = (Z + steps + s) % s;
    return PM_CARTESIAN(vec[X],vec[Y],vec[Z]);
}

#if 0
static CANON_POSITION get_axis_max_velocity()
{
    CANON_POSITION maxvel;
    maxvel.x = axis_valid(0) ? FROM_EXT_LEN(emcAxisGetMaxVelocity(0)) : 0.0;
    maxvel.y = axis_valid(1) ? FROM_EXT_LEN(emcAxisGetMaxVelocity(1)) : 0.0;
    maxvel.z = axis_valid(2) ? FROM_EXT_LEN(emcAxisGetMaxVelocity(2)) : 0.0;

    maxvel.a = axis_valid(3) ? FROM_EXT_ANG(emcAxisGetMaxVelocity(3)) : 0.0;
    maxvel.b = axis_valid(4) ? FROM_EXT_ANG(emcAxisGetMaxVelocity(4)) : 0.0;
    maxvel.c = axis_valid(5) ? FROM_EXT_ANG(emcAxisGetMaxVelocity(5)) : 0.0;

    maxvel.u = axis_valid(6) ? FROM_EXT_LEN(emcAxisGetMaxVelocity(6)) : 0.0;
    maxvel.v = axis_valid(7) ? FROM_EXT_LEN(emcAxisGetMaxVelocity(7)) : 0.0;
    maxvel.w = axis_valid(8) ? FROM_EXT_LEN(emcAxisGetMaxVelocity(8)) : 0.0;
    return maxvel;
}

static CANON_POSITION get_axis_max_acceleration()
{
    CANON_POSITION maxacc;
    maxacc.x = axis_valid(0) ? FROM_EXT_LEN(emcAxisGetMaxAcceleration(0)) : 0.0;
    maxacc.y = axis_valid(1) ? FROM_EXT_LEN(emcAxisGetMaxAcceleration(1)) : 0.0;
    maxacc.z = axis_valid(2) ? FROM_EXT_LEN(emcAxisGetMaxAcceleration(2)) : 0.0;

    maxacc.a = axis_valid(3) ? FROM_EXT_ANG(emcAxisGetMaxAcceleration(3)) : 0.0;
    maxacc.b = axis_valid(4) ? FROM_EXT_ANG(emcAxisGetMaxAcceleration(4)) : 0.0;
    maxacc.c = axis_valid(5) ? FROM_EXT_ANG(emcAxisGetMaxAcceleration(5)) : 0.0;

    maxacc.u = axis_valid(6) ? FROM_EXT_LEN(emcAxisGetMaxAcceleration(6)) : 0.0;
    maxacc.v = axis_valid(7) ? FROM_EXT_LEN(emcAxisGetMaxAcceleration(7)) : 0.0;
    maxacc.w = axis_valid(8) ? FROM_EXT_LEN(emcAxisGetMaxAcceleration(8)) : 0.0;
    return maxacc;
}

static double axis_motion_time(const CANON_POSITION & start, const CANON_POSITION & end)
{

    CANON_POSITION disp = end - start;
    CANON_POSITION times; 
    CANON_POSITION maxvel = get_axis_max_velocity();

    canon_debug(" in axis_motion_time\n");
    // For active axes, find the time required to reach the displacement in each axis
    int ind = 0;
    for (ind = 0; ind < 9; ++ind) {
        double v = maxvel[ind];
        if (v > 0.0) {
            times[ind] = fabs(disp[ind]) / v;
        } else {
            times[ind]=0;
        }
        canon_debug("  ind = %d, maxvel = %f, disp = %f, time = %f\n", ind, v, disp[ind], times[ind]);
    }

    return times.max();
}

// NOTE: not exactly times, comment TODO
static double axis_acc_time(const CANON_POSITION & start, const CANON_POSITION & end)
{

    CANON_POSITION disp = end - start;
    CANON_POSITION times; 
    CANON_POSITION maxacc = get_axis_max_acceleration();

    for (int i = 0; i < 9; ++i) {
        double a = maxacc[i];
        if (a > 0.0) {
            times[i] = fabs(disp[i]) / a;
        } else {
            times[i]=0;
        }
    }

    return times.max();
}
#endif

static void ARC_FEED(void */*ctx*/, int32_t line_number,
              double first_end, double second_end,
	      double first_axis, double second_axis, int32_t rotation,
	      double axis_end_point, 
              double a, double b, double c,
              double u, double v, double w)
{

    EMC_TRAJ_CIRCULAR_MOVE circularMoveMsg;
    EMC_TRAJ_LINEAR_MOVE linearMoveMsg;

    canon_debug("line = %d\n", line_number);
    canon_debug("first_end = %f, second_end = %f\n", first_end,second_end);

    if( canon.activePlane == CANON_PLANE_XY && canon.motionMode == CANON_CONTINUOUS) {
        double mx, my;
        double lx, ly, lz;
        double unused = 0;

        get_last_pos(lx, ly, lz);

        double fe=FROM_PROG_LEN(first_end), se=FROM_PROG_LEN(second_end), ae=FROM_PROG_LEN(axis_end_point);
        double fa=FROM_PROG_LEN(first_axis), sa=FROM_PROG_LEN(second_axis);
        rotate_and_offset_pos(fe, se, ae, unused, unused, unused, unused, unused, unused);
        rotate_and_offset_pos(fa, sa, unused, unused, unused, unused, unused, unused, unused);
        if (chord_deviation(lx, ly, fe, se, fa, sa, rotation, mx, my) < canon.naivecamTolerance) {
            a = FROM_PROG_ANG(a);
            b = FROM_PROG_ANG(b);
            c = FROM_PROG_ANG(c);
            u = FROM_PROG_LEN(u);
            v = FROM_PROG_LEN(v);
            w = FROM_PROG_LEN(w);

            rotate_and_offset_pos(unused, unused, unused, a, b, c, u, v, w);
            see_segment(line_number, _tag, mx, my,
                        (lz + ae)/2, 
                        (canon.endPoint.a + a)/2, 
                        (canon.endPoint.b + b)/2, 
                        (canon.endPoint.c + c)/2, 
                        (canon.endPoint.u + u)/2, 
                        (canon.endPoint.v + v)/2, 
                        (canon.endPoint.w + w)/2);
            see_segment(line_number, _tag, fe, se, ae, a, b, c, u, v, w);
            return;
        }
    }

    linearMoveMsg.feed_mode = canon.feed_mode;
    circularMoveMsg.feed_mode = canon.feed_mode;
    flush_segments();

    // Start by defining 3D points for the motion end and center.
    PM_CARTESIAN end_cart(first_end, second_end, axis_end_point);
    PM_CARTESIAN center_cart(first_axis, second_axis, axis_end_point);
    PM_CARTESIAN normal_cart(0.0,0.0,1.0);
    PM_CARTESIAN plane_x(1.0,0.0,0.0);
    PM_CARTESIAN plane_y(0.0,1.0,0.0);


    canon_debug("start = %f %f %f\n",
            canon.endPoint.x,
            canon.endPoint.y,
            canon.endPoint.z);
    canon_debug("end = %f %f %f\n",
            end_cart.x,
            end_cart.y,
            end_cart.z);
    canon_debug("center = %f %f %f\n",
            center_cart.x,
            center_cart.y,
            center_cart.z);

    // Rearrange the X Y Z coordinates in the correct order based on the active plane (XY, YZ, or XZ)
    // KLUDGE CANON_PLANE is 1-indexed, hence the subtraction here to make a 0-index value
    int shift_ind = 0;
    switch(canon.activePlane) {
        case CANON_PLANE_XY:
            shift_ind = 0;
            break;
        case CANON_PLANE_XZ:
            shift_ind = -2;
            break;
        case CANON_PLANE_YZ:
            shift_ind = -1;
            break;
        case CANON_PLANE_UV:
        case CANON_PLANE_VW:
        case CANON_PLANE_UW:
            CANON_ERROR("Can't set plane in UVW axes, assuming XY");
            break;
    }

    canon_debug("active plane is %d, shift_ind is %d\n",canon.activePlane,shift_ind);
    end_cart = circshift(end_cart, shift_ind);
    center_cart = circshift(center_cart, shift_ind);
    normal_cart = circshift(normal_cart, shift_ind);
    plane_x = circshift(plane_x, shift_ind);
    plane_y = circshift(plane_y, shift_ind);

    canon_debug("normal = %f %f %f\n",
            normal_cart.x,
            normal_cart.y,
            normal_cart.z);

    canon_debug("plane_x = %f %f %f\n",
            plane_x.x,
            plane_x.y,
            plane_x.z);

    canon_debug("plane_y = %f %f %f\n",
            plane_y.x,
            plane_y.y,
            plane_y.z);
    // Define end point in PROGRAM units and convert to CANON
    CANON_POSITION endpt(0,0,0,a,b,c,u,v,w);
    from_prog(endpt);

    // Store permuted XYZ end position
    from_prog_len(end_cart);
    endpt.set_xyz(end_cart);

    // Convert to CANON units
    from_prog_len(center_cart);

    // Rotate and offset the new end point to be in the same coordinate system as the current end point
    rotate_and_offset(endpt);
    rotate_and_offset_xyz(center_cart);
    rotate_and_offset_xyz(end_cart);
    // Also rotate the basis vectors
    to_rotated(plane_x);
    to_rotated(plane_y);
    to_rotated(normal_cart);

    canon_debug("end = %f %f %f\n",
            end_cart.x,
            end_cart.y,
            end_cart.z);

    canon_debug("endpt = %f %f %f\n",
            endpt.x,
            endpt.y,
            endpt.z);
    canon_debug("center = %f %f %f\n",
            center_cart.x,
            center_cart.y,
            center_cart.z);

    canon_debug("normal = %f %f %f\n",
            normal_cart.x,
            normal_cart.y,
            normal_cart.z);
    // Note that the "start" point is already rotated and offset

    // Define displacement vectors from center to end and center to start (3D)
    PM_CARTESIAN end_rel = end_cart - center_cart;
    PM_CARTESIAN start_rel = canon.endPoint.xyz() - center_cart;

    // Project each displacement onto the active plane
    double p_end_1 = dot(end_rel,plane_x);
    double p_end_2 = dot(end_rel,plane_y);
    double p_start_1 = dot(start_rel,plane_x);
    double p_start_2 = dot(start_rel,plane_y);

    canon_debug("planar end = %f %f\n", p_end_1, p_end_2);
    canon_debug("planar start = %f %f\n", p_start_1, p_start_2);

    canon_debug("rotation = %d\n",rotation);

    // Use the "X" (1) and Y" (2) components of the planar projections to get
    // the starting and ending angle. Note that atan2 arguments are atan2(Y,X).
    double theta_start = atan2(p_start_2, p_start_1);
    double theta_end= atan2(p_end_2,p_end_1);
    double start_radius = hypot(p_start_1, p_start_2);
    double end_radius = hypot(p_end_1, p_end_2);
    canon_debug("radius = %f\n",start_radius);
    canon_debug("raw values: theta_end = %.17e, theta_start = %.17e\n", theta_end, theta_start);

    // Correct for angle wrap so that theta_end - theta_start > 0
    int is_clockwise = rotation < 0;

    // FIXME should be a constant in canon.hh or elsewhere
    const double min_arc_angle = 1e-12;

    if (is_clockwise) {
        if((theta_end + min_arc_angle) >= theta_start) theta_end -= M_PI * 2.0;
    } else {
        if((theta_end - min_arc_angle) <= theta_start) theta_end += M_PI * 2.0;
    }

    canon_debug("theta_end = %f, theta_start = %f\n", theta_end, theta_start);

    /*
       mapping of rotation to full turns:

       rotation full COUNTERCLOCKWISE turns (- implies clockwise)
       -------- -----
              0 none (linear move)
              1 0
              2 1
             -1 0
             -2 -1 */

    // Compute the number of FULL turns in addition to the principal angle
    int full_turns = 0;
    if (rotation > 1) {
        full_turns = rotation - 1;
    }
    if (rotation < -1) {
        full_turns = rotation + 1;
    }

    double angle = theta_end - theta_start;
    double full_angle = angle + 2.0 * M_PI * (double)full_turns;
    canon_debug("angle = %f\n", angle);
    canon_debug("full turns = %d\n", full_turns);

	canon_debug("full_angle = %.17e\n", full_angle);

    //Use total angle to get spiral properties
    double spiral = end_radius - start_radius;
    double dr = spiral / fabs(full_angle);
    double min_radius = fmin(start_radius, end_radius);
    double effective_radius = sqrt(dr*dr + min_radius*min_radius);

    // KLUDGE: assumes 0,1,2 for X Y Z
    // Find normal axis
    int norm_axis_ind = (2 - shift_ind) % 3;
    // Find maximum velocities and accelerations for planar axes
    int axis1 = (norm_axis_ind + 1) % 3;
    int axis2 = (norm_axis_ind + 2) % 3;

    canon_debug("axis1 = %d, axis2 = %d\n",axis1, axis2);

    // Get planar velocity bounds
    double v1 = FROM_EXT_LEN(emcAxisGetMaxVelocity(axis1));
    double v2 = FROM_EXT_LEN(emcAxisGetMaxVelocity(axis2));

    // Get planar acceleration bounds
    double a1 = FROM_EXT_LEN(emcAxisGetMaxAcceleration(axis1));
    double a2 = FROM_EXT_LEN(emcAxisGetMaxAcceleration(axis2));
    double v_max_axes = MIN(v1, v2);
    double a_max_axes = MIN(a1, a2);

    if(canon.xy_rotation && canon.activePlane != CANON_PLANE_XY) {
        // also consider the third plane's constraint, which may get
        // involved since we're rotated.

        int axis3 = (norm_axis_ind + 3) % 3;
        if (axis_valid(axis3)) {
            double v3 = FROM_EXT_LEN(emcAxisGetMaxVelocity(axis3));
            double a3 = FROM_EXT_LEN(emcAxisGetMaxAcceleration(axis3));
            v_max_axes = MIN(v3, v_max_axes);
            a_max_axes = MIN(a3, a_max_axes);
        }
    }

    //FIXME allow tangential acceleration like in TP
    double a_max_normal = a_max_axes * sqrt(3.0)/2.0;
    canon_debug("a_max_axes = %f\n", a_max_axes);

    // Compute the centripetal acceleration
    double v_max_radial = sqrt(a_max_normal * effective_radius);
    canon_debug("v_max_radial = %f\n", v_max_radial);

    // Restrict our maximum velocity in-plane if need be
    double v_max_planar = MIN(v_max_radial, v_max_axes);
    canon_debug("v_max_planar = %f\n", v_max_planar);

    // Find the equivalent maximum velocity for a linear displacement
    // This accounts for speed restrictions due to helical and other axes
    VelData veldata = getStraightVelocity(endpt);

    // Compute spiral length, first by the minimum circular arc length
    double circular_length = min_radius * fabs(full_angle);
    // Then by linear approximation of the spiral arc length function of angle
    // TODO use quadratic approximation
    double spiral_length = hypot(circular_length, spiral);

    // Compute length along normal axis and total XYZ arc length
    double axis_len = dot(end_cart - canon.endPoint.xyz(), normal_cart);
    double total_xyz_length = hypot(spiral_length, axis_len);

    // Next, compute the minimum time that we must take to complete the segment. 
    // The motion computation gives us min time needed for the helical and auxiliary axes
    double t_max_motion = veldata.tmax;
    // Assumes worst case that velocity can be in any direction in the plane, so
    // we assume tangential velocity is always less than the planar velocity limit.
    // The spiral time is the min time needed to stay under the planar velocity limit.
    double t_max_spiral = spiral_length / v_max_planar;

    // Now, compute actual XYZ max velocity from this min time and the total arc length
    double t_max = fmax(t_max_motion, t_max_spiral);

    double v_max = total_xyz_length / t_max;
    canon_debug("v_max = %f\n", v_max);


//COMPUTE ACCEL
    
    // Use "straight" acceleration measure to compute acceleration bounds due
    // to non-circular components (helical axis, other axes)
    AccelData accdata = getStraightAcceleration(endpt);

    double tt_max_motion = accdata.tmax;
    double tt_max_spiral = spiral_length / a_max_axes;
    double tt_max = fmax(tt_max_motion, tt_max_spiral);

    // a_max could be higher than a_max_axes, but the projection onto the
    // circle plane and helical axis will still be within limits
    double a_max = total_xyz_length / tt_max;

    // Limit velocity by maximum
    double vel = MIN(canon.linearFeedRate, v_max);
    canon_debug("current F = %f\n",canon.linearFeedRate);
    canon_debug("vel = %f\n",vel);

    canon_debug("v_max = %f\n",v_max);
    canon_debug("a_max = %f\n",a_max);

    canon.cartesian_move = 1;

    if (rotation == 0) {
        // linear move
        // FIXME (Rob) Am I missing something? the P word should never be zero,
        // or we wouldn't be calling ARC_FEED
        linearMoveMsg.end = to_ext_pose(endpt);
        linearMoveMsg.type = EMC_MOTION_TYPE_ARC;
        linearMoveMsg.vel = toExtVel(vel);
        linearMoveMsg.ini_maxvel = toExtVel(v_max);
        linearMoveMsg.acc = toExtAcc(a_max);
        linearMoveMsg.indexer_jnum = -1;
        if(vel && a_max){
            interp_list.set_line_number(line_number);
            tag_and_send(linearMoveMsg, _tag);
        }
    } else {
        circularMoveMsg.end = to_ext_pose(endpt);

        // Convert internal center and normal to external units
        circularMoveMsg.center = to_ext_len(center_cart);
        circularMoveMsg.normal = to_ext_len(normal_cart);

        if (rotation > 0)
            circularMoveMsg.turn = rotation - 1;
        else
            // reverse turn
            circularMoveMsg.turn = rotation;

        circularMoveMsg.type = EMC_MOTION_TYPE_ARC;

        circularMoveMsg.vel = toExtVel(vel);
        circularMoveMsg.ini_maxvel = toExtVel(v_max);
        circularMoveMsg.acc = toExtAcc(a_max);

        //FIXME what happens if accel or vel is zero?
        // The end point is still updated, but nothing is added to the interp list
        // seems to be a crude way to indicate a zero length segment?
        if(vel && a_max) {
            interp_list.set_line_number(line_number);
            tag_and_send(circularMoveMsg, _tag);
        }
    }
    // update the end point
    canonUpdateEndPoint(endpt);
}


static void DWELL(void */*ctx*/, double seconds)
{
    EMC_TRAJ_DELAY delayMsg;

    flush_segments();

    delayMsg.delay = seconds;

    interp_list.append(delayMsg);
}

/* Spindle Functions */
static void SET_SPINDLE_MODE(void */*ctx*/, int32_t spindle, double css_max) {
   canon.spindle[spindle].css_maximum = fabs(css_max);
}

static void START_SPINDLE_CLOCKWISE(void */*ctx*/, int32_t s, int32_t wait_for_atspeed)
{
    EMC_SPINDLE_ON emc_spindle_on_msg;

    flush_segments();
    canon.spindle[s].dir = 1;
    emc_spindle_on_msg.spindle = s;
    if(canon.spindle[s].css_maximum) {
        if(canon.lengthUnits == CANON_UNITS_INCHES){
            canon.spindle[s].css_factor = 12 / (2 * M_PI) * canon.spindle[s].speed * TO_EXT_LEN(25.4);
        } else {
            canon.spindle[s].css_factor = 1000 / (2 * M_PI) * canon.spindle[s].speed * TO_EXT_LEN(1);
		}
		emc_spindle_on_msg.speed = canon.spindle[s].dir * canon.spindle[s].css_maximum;
		emc_spindle_on_msg.factor = canon.spindle[s].dir * canon.spindle[s].css_factor;
		emc_spindle_on_msg.xoffset = TO_EXT_LEN(canon.g5xOffset.x + canon.g92Offset.x + canon.toolOffset.tran.x);
    } else {
        emc_spindle_on_msg.speed = canon.spindle[s].dir * canon.spindle[s].speed;
     //   canon.css_numerator = 0; FIXME: Do we need this?
    }
    emc_spindle_on_msg.wait_for_spindle_at_speed = wait_for_atspeed;
    interp_list.append(emc_spindle_on_msg);
}

static void START_SPINDLE_COUNTERCLOCKWISE(void */*ctx*/, int32_t s, int32_t wait_for_atspeed)
{
    EMC_SPINDLE_ON emc_spindle_on_msg;

    flush_segments();
    canon.spindle[s].dir = -1;
    emc_spindle_on_msg.spindle = s;
    if(canon.spindle[s].css_maximum) {
        if(canon.lengthUnits == CANON_UNITS_INCHES){
            canon.spindle[s].css_factor = 12 / (2 * M_PI) * canon.spindle[s].speed * TO_EXT_LEN(25.4);
        } else {
            canon.spindle[s].css_factor = 1000 / (2 * M_PI) * canon.spindle[s].speed * TO_EXT_LEN(1);
		}
		emc_spindle_on_msg.speed = canon.spindle[s].dir * canon.spindle[s].css_maximum;
		emc_spindle_on_msg.factor = canon.spindle[s].dir * canon.spindle[s].css_factor;
		emc_spindle_on_msg.xoffset = TO_EXT_LEN(canon.g5xOffset.x + canon.g92Offset.x + canon.toolOffset.tran.x);
    } else {
        emc_spindle_on_msg.speed = canon.spindle[s].dir * canon.spindle[s].speed;
     //   canon.css_numerator = 0; FIXME: Do we need this?
    }
    emc_spindle_on_msg.wait_for_spindle_at_speed = wait_for_atspeed;
    interp_list.append(emc_spindle_on_msg);
}

static void SET_SPINDLE_SPEED(void */*ctx*/, int32_t s, double r)
{
    // speed is in RPMs everywhere

	canon.spindle[s].speed = fabs(r); // interp will never send negative anyway ...

    EMC_SPINDLE_SPEED emc_spindle_speed_msg;

    flush_segments();

    emc_spindle_speed_msg.spindle = s;
    if(canon.spindle[s].css_maximum) {
		if(canon.lengthUnits == CANON_UNITS_INCHES){
			canon.spindle[s].css_factor = 12 / (2 * M_PI) * canon.spindle[s].speed * TO_EXT_LEN(25.4);
		} else {
			canon.spindle[s].css_factor = 1000 / (2 * M_PI) * canon.spindle[s].speed * TO_EXT_LEN(1);
		}
		emc_spindle_speed_msg.speed =  canon.spindle[s].dir * canon.spindle[s].css_maximum;
		emc_spindle_speed_msg.factor =  canon.spindle[s].dir * canon.spindle[s].css_factor;
		emc_spindle_speed_msg.xoffset = TO_EXT_LEN(canon.g5xOffset.x + canon.g92Offset.x + canon.toolOffset.tran.x);
	} else {
        emc_spindle_speed_msg.speed = canon.spindle[s].dir * canon.spindle[s].speed;
		//   canon.css_numerator = 0; FIXME: Do we need this?
    }
    interp_list.append(emc_spindle_speed_msg);
}

static void STOP_SPINDLE_TURNING(void */*ctx*/, int32_t s)
{
    EMC_SPINDLE_OFF emc_spindle_off_msg;

    flush_segments();
    emc_spindle_off_msg.spindle = s;
    interp_list.append(emc_spindle_off_msg);
    // Added by atp 6/1/18 not sure this is right. There is a problem that the _second_ S word starts the spindle without M3/M4
    canon.spindle[s].dir = 0;
}

static void ORIENT_SPINDLE(void */*ctx*/, int32_t s, double orientation, int32_t mode)
{
    EMC_SPINDLE_ORIENT o;

    flush_segments();
    o.spindle = s;
    o.orientation = orientation;
    o.mode = mode;
    interp_list.append(o);
}

static void WAIT_SPINDLE_ORIENT_COMPLETE(void */*ctx*/, int32_t s, double timeout)
{
    EMC_SPINDLE_WAIT_ORIENT_COMPLETE o;

    flush_segments();
    o.spindle = s;
    o.timeout = timeout;
    interp_list.append(o);
}

/* Tool Functions */

/* this is called with distances in external (machine) units */
static void SET_TOOL_TABLE_ENTRY(void */*ctx*/, int32_t pocket, int32_t toolno,
    double ox, double oy, double oz, double oa, double ob, double oc,
    double ou, double ov, double ow,
    double diameter, double frontangle, double backangle, int32_t orientation) {
    EMC_TOOL_SET_OFFSET o;
    flush_segments();
    o.pocket = pocket;
    o.toolno = toolno;
    o.offset.tran.x = ox; o.offset.tran.y = oy; o.offset.tran.z = oz;
    o.offset.a = oa; o.offset.b = ob; o.offset.c = oc;
    o.offset.u = ou; o.offset.v = ov; o.offset.w = ow;
    o.diameter = diameter;
    o.frontangle = frontangle;
    o.backangle = backangle;
    o.orientation = orientation;
    interp_list.append(o);
}

/*
  EMC has no tool length offset. To implement it, we save it here,
  and apply it when necessary
  */
static void USE_TOOL_LENGTH_OFFSET(void */*ctx*/,
    double x, double y, double z, double a, double b, double c,
    double u, double v, double w)
{
    EMC_TRAJ_SET_OFFSET set_offset_msg;

    flush_segments();

    /* convert to mm units for internal canonical use */
    canon.toolOffset.tran.x = FROM_PROG_LEN(x);
    canon.toolOffset.tran.y = FROM_PROG_LEN(y);
    canon.toolOffset.tran.z = FROM_PROG_LEN(z);
    canon.toolOffset.a = FROM_PROG_ANG(a);
    canon.toolOffset.b = FROM_PROG_ANG(b);
    canon.toolOffset.c = FROM_PROG_ANG(c);
    canon.toolOffset.u = FROM_PROG_LEN(u);
    canon.toolOffset.v = FROM_PROG_LEN(v);
    canon.toolOffset.w = FROM_PROG_LEN(w);

    /* append it to interp list so it gets updated at the right time, not at
       read-ahead time */
    set_offset_msg.offset.tran.x = TO_EXT_LEN(canon.toolOffset.tran.x);
    set_offset_msg.offset.tran.y = TO_EXT_LEN(canon.toolOffset.tran.y);
    set_offset_msg.offset.tran.z = TO_EXT_LEN(canon.toolOffset.tran.z);
    set_offset_msg.offset.a = TO_EXT_ANG(canon.toolOffset.a);
    set_offset_msg.offset.b = TO_EXT_ANG(canon.toolOffset.b);
    set_offset_msg.offset.c = TO_EXT_ANG(canon.toolOffset.c);
    set_offset_msg.offset.u = TO_EXT_LEN(canon.toolOffset.u);
    set_offset_msg.offset.v = TO_EXT_LEN(canon.toolOffset.v);
    set_offset_msg.offset.w = TO_EXT_LEN(canon.toolOffset.w);

    for (int s = 0; s < emcStatus->motion.traj.spindles; s++){
        if(canon.spindle[s].css_maximum) {
            SET_SPINDLE_SPEED(NULL, s, canon.spindle[s].speed);
        }
    }
    interp_list.append(set_offset_msg);
}

/* issued at very start of an M6 command. Notification. */
static void START_CHANGE(void */*ctx*/)
{
    EMC_TOOL_START_CHANGE emc_start_change_msg;

    flush_segments();

    interp_list.append(emc_start_change_msg);
}

/* CHANGE_TOOL results from M6 */
static void CHANGE_TOOL(void */*ctx*/, int32_t slot)
{
    EMC_TRAJ_LINEAR_MOVE linearMoveMsg;
    linearMoveMsg.feed_mode = canon.feed_mode;
    EMC_TOOL_LOAD load_tool_msg;

    flush_segments();

    /* optional move to tool change position.  This
     * is a mess because we really want a configurable chain
     * of events to happen when a tool change is called for.
     * Since they'll probably involve motion, we can't just
     * do it in HAL.  This is basic support for making one
     * move to a particular coordinate before the tool change
     * is called.  */
    
    if (have_tool_change_position) {
        double vel, acc, x, y, z, a, b, c, u, v, w;

        x = FROM_EXT_LEN(tool_change_position.tran.x);
        y = FROM_EXT_LEN(tool_change_position.tran.y);
        z = FROM_EXT_LEN(tool_change_position.tran.z);
        a = canon.endPoint.a;
        b = canon.endPoint.b;
        c = canon.endPoint.c;
        u = canon.endPoint.u;
        v = canon.endPoint.v;
        w = canon.endPoint.w;

        if (have_tool_change_position > 3) {
            a = FROM_EXT_ANG(tool_change_position.a);
            b = FROM_EXT_ANG(tool_change_position.b);
            c = FROM_EXT_ANG(tool_change_position.c);
        }

        if (have_tool_change_position > 6) {
            u = FROM_EXT_LEN(tool_change_position.u);
            v = FROM_EXT_LEN(tool_change_position.v);
            w = FROM_EXT_LEN(tool_change_position.w);
        }

        VelData veldata = getStraightVelocity(x, y, z, a, b, c, u, v, w);
        AccelData accdata = getStraightAcceleration(x, y, z, a, b, c, u, v, w);
        vel = veldata.vel;
        acc = accdata.acc;

        linearMoveMsg.end = to_ext_pose(x, y, z, a, b, c, u, v, w);

        linearMoveMsg.vel = linearMoveMsg.ini_maxvel = toExtVel(vel);
        linearMoveMsg.acc = toExtAcc(acc);
        linearMoveMsg.type = EMC_MOTION_TYPE_TOOLCHANGE;
	linearMoveMsg.feed_mode = 0;
        linearMoveMsg.indexer_jnum = -1;

	int old_feed_mode = canon.feed_mode;
	if(canon.feed_mode)
	    STOP_SPEED_FEED_SYNCH(NULL);

    if(vel && acc)
        tag_and_send(linearMoveMsg, _tag);

    if(old_feed_mode)
        START_SPEED_FEED_SYNCH(NULL, canon.spindle_num, canon.linearFeedRate, 1);

    canonUpdateEndPoint(x, y, z, a, b, c, u, v, w);
    }

    /* regardless of optional moves above, we'll always send a load tool
       message */
    interp_list.append(load_tool_msg);
}

/* SELECT_TOOL results from Tn */
static void SELECT_TOOL(void */*ctx*/, int32_t tool)
{
    EMC_TOOL_PREPARE prep_for_tool_msg;

    prep_for_tool_msg.tool = tool;

    interp_list.append(prep_for_tool_msg);
}

/* CHANGE_TOOL_NUMBER results from M61 */
static void CHANGE_TOOL_NUMBER(void */*ctx*/, int32_t pocket_number)
{
    EMC_TOOL_SET_NUMBER emc_tool_set_number_msg;
    
    emc_tool_set_number_msg.tool = pocket_number;

    interp_list.append(emc_tool_set_number_msg);
}

static void RELOAD_TOOLDATA(void */*ctx*/)
{
    EMC_TOOL_LOAD_TOOL_TABLE load_tool_table_msg;
    interp_list.append(load_tool_table_msg);
}

/* Misc Functions */

static void CLAMP_AXIS(void */*ctx*/, int32_t axis)
{
    /*! \todo FIXME-- unimplemented */
}

/*
  setString and addString initializes or adds src to dst, never exceeding
  dst's maxlen chars.
*/

static char *setString(char *dst, const char *src, int maxlen)
{
    dst[0] = 0;
    strncat(dst, src, maxlen - 1);
    dst[maxlen - 1] = 0;
    return dst;
}

static char *addString(char *dst, const char *src, int maxlen)
{
    int dstlen = strlen(dst);
    int srclen = strlen(src);
    int actlen;

    if (srclen >= maxlen - dstlen) {
	actlen = maxlen - dstlen - 1;
	dst[maxlen - 1] = 0;
    } else {
	actlen = srclen;
    }

    strncat(dst, src, actlen);

    return dst;
}

/*
  The probe file is opened with a hot-comment (PROBEOPEN <filename>),
  and the results of each probed point are written to that file.
  The file is closed with a (PROBECLOSE) comment.
*/

static FILE *probefile = NULL;

static void COMMENT(void */*ctx*/, const char *comment)
{
    // nothing need be done here, but you can play tricks with hot comments

    char msg[LINELEN];
    char probefilename[LINELEN];
    const char *ptr;

    // set RPY orientation for subsequent moves
    if (!strncmp(comment, "RPY", strlen("RPY"))) {
	PM_RPY rpy;
	// it's RPY <R> <P> <Y>
	if (3 !=
	    sscanf(comment, "%*s %lf %lf %lf", &rpy.r, &rpy.p, &rpy.y)) {
	    // print current orientation
	    printf("rpy = %f %f %f, quat = %f %f %f %f\n",
		   rpy.r, rpy.p, rpy.y, quat.s, quat.x, quat.y, quat.z);
	} else {
	    // set and print orientation
	    quat = rpy;
	    printf("rpy = %f %f %f, quat = %f %f %f %f\n",
		   rpy.r, rpy.p, rpy.y, quat.s, quat.x, quat.y, quat.z);
	}
	return;
    }
    // open probe output file
    if (!strncmp(comment, "PROBEOPEN", strlen("PROBEOPEN"))) {
	// position ptr to first char after PROBEOPEN
	ptr = &comment[strlen("PROBEOPEN")];
	// and step over white space to name, or NULL
	while (isspace(*ptr)) {
	    ptr++;
	}
	setString(probefilename, ptr, LINELEN);
	if (NULL == (probefile = fopen(probefilename, "wt"))) {
	    // pop up a warning message
	    setString(msg, "can't open probe file ", LINELEN);
	    addString(msg, probefilename, LINELEN);
	    MESSAGE(NULL, msg);
	    probefile = NULL;
	}
	return;
    }
    // close probe output file
    if (!strncmp(comment, "PROBECLOSE", strlen("PROBECLOSE"))) {
	if (probefile != NULL) {
	    fclose(probefile);
	    probefile = NULL;
	}
	return;
    }

    return;
}

// refers to feed rate
static void DISABLE_FEED_OVERRIDE(void */*ctx*/)
{
    EMC_TRAJ_SET_FO_ENABLE set_fo_enable_msg;
    flush_segments();
    
    set_fo_enable_msg.mode = 0;
    interp_list.append(set_fo_enable_msg);
}

static void ENABLE_FEED_OVERRIDE(void */*ctx*/)
{
    EMC_TRAJ_SET_FO_ENABLE set_fo_enable_msg;
    flush_segments();
    
    set_fo_enable_msg.mode = 1;
    interp_list.append(set_fo_enable_msg);
}


//refers to adaptive feed override (HAL input, useful for EDM for example)
static void DISABLE_ADAPTIVE_FEED(void */*ctx*/)
{
    EMC_MOTION_ADAPTIVE emcmotAdaptiveMsg;
    flush_segments();

    emcmotAdaptiveMsg.status = 0;
    interp_list.append(emcmotAdaptiveMsg);
}

static void ENABLE_ADAPTIVE_FEED(void */*ctx*/)
{
    EMC_MOTION_ADAPTIVE emcmotAdaptiveMsg;
    flush_segments();

    emcmotAdaptiveMsg.status = 1;
    interp_list.append(emcmotAdaptiveMsg);
}

//refers to spindle speed
static void DISABLE_SPEED_OVERRIDE(void */*ctx*/, int32_t spindle)
{
    EMC_TRAJ_SET_SO_ENABLE set_so_enable_msg;
    flush_segments();
    
    set_so_enable_msg.mode = 0;
    set_so_enable_msg.spindle = spindle;
    interp_list.append(set_so_enable_msg);
}


static void ENABLE_SPEED_OVERRIDE(void */*ctx*/, int32_t spindle)
{
    EMC_TRAJ_SET_SO_ENABLE set_so_enable_msg;
    flush_segments();
    
    set_so_enable_msg.mode = 1;
    set_so_enable_msg.spindle = spindle;
    interp_list.append(set_so_enable_msg);
}

static void ENABLE_FEED_HOLD(void */*ctx*/)
{
    EMC_TRAJ_SET_FH_ENABLE set_feed_hold_msg;
    flush_segments();
    
    set_feed_hold_msg.mode = 1;
    interp_list.append(set_feed_hold_msg);
}

static void DISABLE_FEED_HOLD(void */*ctx*/)
{
    EMC_TRAJ_SET_FH_ENABLE set_feed_hold_msg;
    flush_segments();
    
    set_feed_hold_msg.mode = 0;
    interp_list.append(set_feed_hold_msg);
}

static void FLOOD_OFF(void */*ctx*/)
{
    EMC_COOLANT_FLOOD_OFF flood_off_msg;

    flush_segments();

    interp_list.append(flood_off_msg);
}

static void FLOOD_ON(void */*ctx*/)
{
    EMC_COOLANT_FLOOD_ON flood_on_msg;

    flush_segments();

    interp_list.append(flood_on_msg);
}

static void MESSAGE(void */*ctx*/, const char *s)
{
    EMC_OPERATOR_DISPLAY operator_display_msg;

    flush_segments();
    operator_display_msg.id = 0;
    strncpy(operator_display_msg.display, s, LINELEN);
    operator_display_msg.display[LINELEN - 1] = 0;
    interp_list.append(operator_display_msg);
}

static FILE *logfile = NULL;

static void LOG(void */*ctx*/, const char *s) {
    flush_segments();
    if(logfile) { fprintf(logfile, "%s\n", s); fflush(logfile); }
    fprintf(stderr, "LOG(%s)\n", s);

}

static void LOGOPEN(void */*ctx*/, const char *name) {
    if(logfile) fclose(logfile);
    logfile = fopen(name, "wt");
    fprintf(stderr, "LOGOPEN(%s) -> %p\n", name, (void*)logfile);
}

static void LOGAPPEND(void */*ctx*/, const char *name) {
    if(logfile) fclose(logfile);
    logfile = fopen(name, "at");
    fprintf(stderr, "LOGAPPEND(%s) -> %p\n", name, (void*)logfile);
}


static void LOGCLOSE(void */*ctx*/) {
    if(logfile) fclose(logfile);
    logfile = NULL;
    fprintf(stderr, "LOGCLOSE()\n");
}

static void MIST_OFF(void */*ctx*/)
{
    EMC_COOLANT_MIST_OFF mist_off_msg;

    flush_segments();

    interp_list.append(mist_off_msg);
}

static void MIST_ON(void */*ctx*/)
{
    EMC_COOLANT_MIST_ON mist_on_msg;

    flush_segments();

    interp_list.append(mist_on_msg);
}

static void PALLET_SHUTTLE(void */*ctx*/)
{
    /*! \todo FIXME-- unimplemented */
}

static void TURN_PROBE_OFF(void */*ctx*/)
{
    // don't do anything-- this is called when the probing is done
}

static void TURN_PROBE_ON(void */*ctx*/)
{
    EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG clearMsg;

    interp_list.append(clearMsg);
}

static void UNCLAMP_AXIS(void */*ctx*/, int32_t axis)
{
    /*! \todo FIXME-- unimplemented */
}

/* Program Functions */

static void PROGRAM_STOP(void */*ctx*/)
{
    /* 
       implement this as a pause. A resume will cause motion to proceed. */
    EMC_TASK_PLAN_PAUSE pauseMsg;

    flush_segments();

    interp_list.append(pauseMsg);
}

static void SET_BLOCK_DELETE(void */*ctx*/, int32_t state)
{
    canon.block_delete = state; //state == ON, means we don't interpret lines starting with "/"
}

static int32_t GET_BLOCK_DELETE(void */*ctx*/)
{
    return canon.block_delete; //state == ON, means we  don't interpret lines starting with "/"
}


static void SET_OPTIONAL_PROGRAM_STOP(void */*ctx*/, int32_t state)
{
    canon.optional_program_stop = state; //state == ON, means we stop
}

static int32_t GET_OPTIONAL_PROGRAM_STOP(void */*ctx*/)
{
    return canon.optional_program_stop; //state == ON, means we stop
}

static void OPTIONAL_PROGRAM_STOP(void */*ctx*/)
{
    EMC_TASK_PLAN_OPTIONAL_STOP stopMsg;

    flush_segments();

    interp_list.append(stopMsg);
}

static void PROGRAM_END(void */*ctx*/)
{
    flush_segments();

    EMC_TASK_PLAN_END endMsg;

    interp_list.append(endMsg);
}

static double GET_EXTERNAL_TOOL_LENGTH_XOFFSET(void */*ctx*/)
{
    return TO_PROG_LEN(canon.toolOffset.tran.x);
}

static double GET_EXTERNAL_TOOL_LENGTH_YOFFSET(void */*ctx*/)
{
    return TO_PROG_LEN(canon.toolOffset.tran.y);
}

static double GET_EXTERNAL_TOOL_LENGTH_ZOFFSET(void */*ctx*/)
{
    return TO_PROG_LEN(canon.toolOffset.tran.z);
}

static double GET_EXTERNAL_TOOL_LENGTH_AOFFSET(void */*ctx*/)
{
    return TO_PROG_ANG(canon.toolOffset.a);
}

static double GET_EXTERNAL_TOOL_LENGTH_BOFFSET(void */*ctx*/)
{
    return TO_PROG_ANG(canon.toolOffset.b);
}

static double GET_EXTERNAL_TOOL_LENGTH_COFFSET(void */*ctx*/)
{
    return TO_PROG_ANG(canon.toolOffset.c);
}

static double GET_EXTERNAL_TOOL_LENGTH_UOFFSET(void */*ctx*/)
{
    return TO_PROG_LEN(canon.toolOffset.u);
}

static double GET_EXTERNAL_TOOL_LENGTH_VOFFSET(void */*ctx*/)
{
    return TO_PROG_LEN(canon.toolOffset.v);
}

static double GET_EXTERNAL_TOOL_LENGTH_WOFFSET(void */*ctx*/)
{
    return TO_PROG_LEN(canon.toolOffset.w);
}

/*
  INIT_CANON()
  Initialize canonical local variables to defaults
  */
static void INIT_CANON(void */*ctx*/)
{
    double units;

    chained_points.clear();

    // initialize locals to original values
    canon.xy_rotation = 0.0;
    canon.rotary_unlock_for_traverse = -1;
    canon.feed_mode = 0;
    canon.g5xOffset.x = 0.0;
    canon.g5xOffset.y = 0.0;
    canon.g5xOffset.z = 0.0;
    canon.g5xOffset.a = 0.0;
    canon.g5xOffset.b = 0.0;
    canon.g5xOffset.c = 0.0;
    canon.g5xOffset.u = 0.0;
    canon.g5xOffset.v = 0.0;
    canon.g5xOffset.w = 0.0;
    canon.g92Offset.x = 0.0;
    canon.g92Offset.y = 0.0;
    canon.g92Offset.z = 0.0;
    canon.g92Offset.a = 0.0;
    canon.g92Offset.b = 0.0;
    canon.g92Offset.c = 0.0;
    canon.g92Offset.u = 0.0;
    canon.g92Offset.v = 0.0;
    canon.g92Offset.w = 0.0;
    SELECT_PLANE(NULL, (int32_t)CANON_PLANE_XY);
    canonUpdateEndPoint(0, 0, 0, 0, 0, 0, 0, 0, 0);
    SET_NAIVECAM_TOLERANCE(NULL, 0);
    for (int s = 0; s < EMCMOT_MAX_SPINDLES; s++) {
        canon.spindle[s].speed = 0.0;
        canon.spindle[s].synched = 0;
    }
    canon.optional_program_stop = ON; //set enabled by default (previous EMC behaviour)
    canon.block_delete = ON; //set enabled by default (previous EMC behaviour)
    canon.cartesian_move = 0;
    canon.angular_move = 0;
    canon.linearFeedRate = 0.0;
    canon.angularFeedRate = 0.0;
    ZERO_EMC_POSE(canon.toolOffset);

    /* 
       to set the units, note that GET_EXTERNAL_LENGTH_UNITS() returns
       traj->linearUnits, which is already set from the INI file in
       iniTraj(). This is a floating point number, in user units per mm. We
       can compare this against known values and set the symbolic values
       accordingly. If it doesn't match, we have an error. */
    units = GET_EXTERNAL_LENGTH_UNITS(NULL);
    if (fabs(units - 1.0 / 25.4) < 1.0e-3) {
	canon.lengthUnits = CANON_UNITS_INCHES;
    } else if (fabs(units - 1.0) < 1.0e-3) {
	canon.lengthUnits = CANON_UNITS_MM;
    } else {
	CANON_ERROR
	    ("non-standard length units, setting interpreter to mm");
	canon.lengthUnits = CANON_UNITS_MM;
    }
    /* Set blending tolerance default depending on units machine is based on*/
    if (canon.lengthUnits == CANON_UNITS_INCHES) {
        SET_MOTION_CONTROL_MODE(NULL, (int32_t)CANON_CONTINUOUS, .001);
    } else {
        SET_MOTION_CONTROL_MODE(NULL, (int32_t)CANON_CONTINUOUS, .001 * MM_PER_INCH);
    }
}

/* Sends error message */
static void CANON_ERROR(const char *fmt, ...)
{
    va_list ap;
    EMC_OPERATOR_ERROR operator_error_msg;

    flush_segments();

    operator_error_msg.id = 0;
    if (fmt != NULL) {
	va_start(ap, fmt);
	vsnprintf(operator_error_msg.error,sizeof(operator_error_msg.error), fmt, ap);
	va_end(ap);
    } else {
	operator_error_msg.error[0] = 0;
    }

    interp_list.append(operator_error_msg);
}

static void CANON_ERROR_CB(void */*ctx*/, const char *msg) {
    CANON_ERROR("%s", msg);
}

/*
  GET_EXTERNAL_TOOL_TABLE(int pocket)

  Returns the tool table structure associated with pocket. Note that
  pocket can run from 0 (by definition, the spindle), to pocket CANON_POCKETS_MAX - 1.

  Tool table is always in machine units.

  */
static int32_t GET_EXTERNAL_TOOL_TABLE(void */*ctx*/, int32_t idx,
    int32_t *toolno, double offset[9], double *diameter,
    double *frontangle, double *backangle, int32_t *orientation)
{
    CANON_TOOL_TABLE tdata;

    if (idx < 0 || idx >= CANON_POCKETS_MAX) {
        tdata.toolno = -1;
        tdata.pocketno = 0;
        ZERO_EMC_POSE(tdata.offset);
        tdata.frontangle = 0.0;
        tdata.backangle = 0.0;
        tdata.diameter = 0.0;
        tdata.orientation = 0;
    } else {
        if (tooldata_get(&tdata,idx) != IDX_OK) {
            fprintf(stderr,"UNEXPECTED idx %s %d\n",__FILE__,__LINE__);
        }
    }
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

CANON_POSITION GET_EXTERNAL_POSITION()
{
    CANON_POSITION position;
    EmcPose pos;

    drop_segments();

    pos = emcStatus->motion.traj.position;

    if (GET_EXTERNAL_OFFSET_APPLIED(NULL) ) {
        double offsets[9];
        GET_EXTERNAL_OFFSETS(NULL, offsets);
        pos.tran.x -= offsets[0];
        pos.tran.y -= offsets[1];
        pos.tran.z -= offsets[2];
        pos.a      -= offsets[3];
        pos.b      -= offsets[4];
        pos.c      -= offsets[5];
        pos.u      -= offsets[6];
        pos.v      -= offsets[7];
        pos.w      -= offsets[8];
    }

    // first update internal record of last position
    canonUpdateEndPoint(FROM_EXT_LEN(pos.tran.x), FROM_EXT_LEN(pos.tran.y), FROM_EXT_LEN(pos.tran.z),
                        FROM_EXT_ANG(pos.a), FROM_EXT_ANG(pos.b), FROM_EXT_ANG(pos.c),
                        FROM_EXT_LEN(pos.u), FROM_EXT_LEN(pos.v), FROM_EXT_LEN(pos.w));

    // now calculate position in program units, for interpreter
    position = unoffset_and_unrotate_pos(canon.endPoint);
    to_prog(position);

    return position;
}

CANON_POSITION GET_EXTERNAL_PROBE_POSITION()
{
    CANON_POSITION position;
    EmcPose pos;
    static CANON_POSITION last_probed_position;

    flush_segments();

    pos = emcStatus->motion.traj.probedPosition;

    // first update internal record of last position
    pos.tran.x = FROM_EXT_LEN(pos.tran.x);
    pos.tran.y = FROM_EXT_LEN(pos.tran.y);
    pos.tran.z = FROM_EXT_LEN(pos.tran.z);

    pos.a = FROM_EXT_ANG(pos.a);
    pos.b = FROM_EXT_ANG(pos.b);
    pos.c = FROM_EXT_ANG(pos.c);

    pos.u = FROM_EXT_LEN(pos.u);
    pos.v = FROM_EXT_LEN(pos.v);
    pos.w = FROM_EXT_LEN(pos.w);

    // now calculate position in program units, for interpreter
    position = unoffset_and_unrotate_pos(pos);
    to_prog(position);

    if (probefile != NULL) {
	if (last_probed_position != position) {
	    fprintf(probefile, "%f %f %f %f %f %f %f %f %f\n",
                    position.x, position.y, position.z,
                    position.a, position.b, position.c,
                    position.u, position.v, position.w);
	    last_probed_position = position;
	}
    }

    return position;
}

static int32_t GET_EXTERNAL_PROBE_TRIPPED_VALUE(void */*ctx*/)
{
    return emcStatus->motion.traj.probe_tripped;
}

static double GET_EXTERNAL_PROBE_VALUE(void */*ctx*/)
{
    // only for analog non-contact probe, so force a 0
    return 0.0;
}

// feed rate wanted is in program units per minute
static double GET_EXTERNAL_FEED_RATE(void */*ctx*/)
{
    double feed;

    if (canon.feed_mode) {
        // We're in G95 "Units per Revolution" mode, so linearFeedRate
        // is the FPR and we should just return it, unchanged.
        feed = canon.linearFeedRate;
    } else {
        // We're in G94 "Units per Minute" mode so unhork linearFeedRate
        // before returning it, by converting from internal to program
        // units, and from "per second" to "per minute".
        feed = TO_PROG_LEN(canon.linearFeedRate);
        feed *= 60.0;
    }

    return feed;
}

// traverse rate wanted is in program units per minute
static double GET_EXTERNAL_TRAVERSE_RATE(void */*ctx*/)
{
    double traverse;

    // convert from external to program units
    traverse =
	TO_PROG_LEN(FROM_EXT_LEN(emcStatus->motion.traj.maxVelocity));

    // now convert from per-sec to per-minute
    traverse *= 60.0;

    return traverse;
}

static double GET_EXTERNAL_LENGTH_UNITS(void */*ctx*/)
{
    double u;

    u = emcStatus->motion.traj.linearUnits;

    if (u == 0) {
	CANON_ERROR("external length units are zero");
	return 1.0;
    } else {
	return u;
    }
}

static double GET_EXTERNAL_ANGLE_UNITS(void */*ctx*/)
{
    double u;

    u = emcStatus->motion.traj.angularUnits;

    if (u == 0) {
	CANON_ERROR("external angle units are zero");
	return 1.0;
    } else {
	return u;
    }
}

static int32_t GET_EXTERNAL_MIST(void */*ctx*/)
{
    return emcStatus->io.coolant.mist;
}

static int32_t GET_EXTERNAL_FLOOD(void */*ctx*/)
{
    return emcStatus->io.coolant.flood;
}

static double GET_EXTERNAL_SPEED(void */*ctx*/, int32_t spindle)
{
    // speed is in RPMs everywhere
    return canon.spindle[spindle].speed;
}

static int32_t GET_EXTERNAL_SPINDLE(void */*ctx*/, int32_t spindle)
{
    if (emcStatus->motion.spindle[spindle].speed == 0) {
	return CANON_STOPPED;
    }

    if (emcStatus->motion.spindle[spindle].speed >= 0.0) {
	return CANON_CLOCKWISE;
    }

    return CANON_COUNTERCLOCKWISE;
}

static char _parameter_file_name[LINELEN];

static void SET_PARAMETER_FILE_NAME(void */*ctx*/, const char *name)
{
  strncpy(_parameter_file_name, name, PARAMETER_FILE_NAME_LENGTH);
}

static void GET_EXTERNAL_PARAMETER_FILE_NAME_internal(char *file_name, int max_size)
{
    if (0 == file_name)
	return;
    if (max_size < 0)
	return;
    if (strlen(_parameter_file_name) < ((size_t) max_size))
	strcpy(file_name, _parameter_file_name);
    else
	file_name[0] = 0;
}

static void GET_EXTERNAL_PARAMETER_FILE_NAME(void */*ctx*/, const char **buf)
{
    static char filename[256];
    GET_EXTERNAL_PARAMETER_FILE_NAME_internal(filename, sizeof(filename));
    *buf = filename;
}

static double GET_EXTERNAL_POSITION_X(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.x;
}

static double GET_EXTERNAL_POSITION_Y(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.y;
}

static double GET_EXTERNAL_POSITION_Z(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.z;
}

static double GET_EXTERNAL_POSITION_A(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.a;
}

static double GET_EXTERNAL_POSITION_B(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.b;
}

static double GET_EXTERNAL_POSITION_C(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.c;
}

static double GET_EXTERNAL_POSITION_U(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.u;
}

static double GET_EXTERNAL_POSITION_V(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.v;
}

static double GET_EXTERNAL_POSITION_W(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_POSITION();
    return position.w;
}

static double GET_EXTERNAL_PROBE_POSITION_X(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.x;
}

static double GET_EXTERNAL_PROBE_POSITION_Y(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.y;
}

static double GET_EXTERNAL_PROBE_POSITION_Z(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.z;
}

static double GET_EXTERNAL_PROBE_POSITION_A(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.a;
}

static double GET_EXTERNAL_PROBE_POSITION_B(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.b;
}

static double GET_EXTERNAL_PROBE_POSITION_C(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.c;
}

static double GET_EXTERNAL_PROBE_POSITION_U(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.u;
}

static double GET_EXTERNAL_PROBE_POSITION_V(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.v;
}

static double GET_EXTERNAL_PROBE_POSITION_W(void */*ctx*/)
{
    CANON_POSITION position;
    position = GET_EXTERNAL_PROBE_POSITION();
    return position.w;
}

static int32_t GET_EXTERNAL_MOTION_CONTROL_MODE(void */*ctx*/)
{
    return (int32_t)canon.motionMode;
}

static double GET_EXTERNAL_MOTION_CONTROL_TOLERANCE(void */*ctx*/)
{
    return TO_PROG_LEN(canon.motionTolerance);
}

static double GET_EXTERNAL_MOTION_CONTROL_NAIVECAM_TOLERANCE(void */*ctx*/)
{
    return TO_PROG_LEN(canon.naivecamTolerance);
}


static int32_t GET_EXTERNAL_LENGTH_UNIT_TYPE(void */*ctx*/)
{
    return (int32_t)canon.lengthUnits;
}

static int32_t GET_EXTERNAL_QUEUE_EMPTY(void */*ctx*/)
{
    flush_segments();

    return emcStatus->motion.traj.queue == 0 ? 1 : 0;
}

// Returns the "home pocket" of the tool currently in the spindle, ie the
// pocket that the current tool was loaded from.  Returns 0 if there is no
// tool in the spindle.

static int32_t GET_EXTERNAL_TOOL_SLOT(void */*ctx*/)
{
    int toolno = emcStatus->io.tool.toolInSpindle;

    return tooldata_find_index_for_tool(toolno);
}

// If the tool changer has prepped a pocket (after a Txxx command) and is
// ready to perform a tool change, return the currently prepped pocket
// number.  If the tool changer is idle (because no Txxx command has been
// run, or because an M6 tool change has completed), return -1.
static int32_t GET_EXTERNAL_SELECTED_TOOL_SLOT(void */*ctx*/)
{
    return emcStatus->io.tool.pocketPrepped; //idx
}

static int32_t GET_EXTERNAL_TC_FAULT(void */*ctx*/)
{
    return emcStatus->io.fault;
}

static int32_t GET_EXTERNAL_TC_REASON(void */*ctx*/)
{
    return emcStatus->io.reason;
}

static int32_t GET_EXTERNAL_FEED_OVERRIDE_ENABLE(void */*ctx*/)
{
    return emcStatus->motion.traj.feed_override_enabled;
}

static int32_t GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE(void */*ctx*/, int32_t spindle)
{
    return emcStatus->motion.spindle[spindle].spindle_override_enabled;
}

static int32_t GET_EXTERNAL_ADAPTIVE_FEED_ENABLE(void */*ctx*/)
{
    return emcStatus->motion.traj.adaptive_feed_enabled;
}

static int32_t GET_EXTERNAL_FEED_HOLD_ENABLE(void */*ctx*/)
{
    return emcStatus->motion.traj.feed_hold_enabled;
}

static int32_t GET_EXTERNAL_AXIS_MASK(void */*ctx*/) {
    return emcStatus->motion.traj.axis_mask;
}

static int32_t GET_EXTERNAL_OFFSET_APPLIED(void */*ctx*/) {
    return emcGetExternalOffsetApplied();
}

static void GET_EXTERNAL_OFFSETS(void */*ctx*/, double offsets[9]) {
    EmcPose o = emcGetExternalOffsets();
    offsets[0] = o.tran.x; offsets[1] = o.tran.y; offsets[2] = o.tran.z;
    offsets[3] = o.a; offsets[4] = o.b; offsets[5] = o.c;
    offsets[6] = o.u; offsets[7] = o.v; offsets[8] = o.w;
}

static int32_t GET_EXTERNAL_PLANE(void */*ctx*/)
{
    return (int32_t)canon.activePlane;
}

/* returns current value of the digital input selected by index.*/
static int32_t GET_EXTERNAL_DIGITAL_INPUT(void */*ctx*/, int32_t index, int32_t def)
{
    if ((index < 0) || (index >= EMCMOT_MAX_DIO))
	return -1;

    if (emcStatus->task.input_timeout == 1)
	return -1;

#ifdef INPUT_DEBUG
    printf("GET_EXTERNAL_DIGITAL_INPUT called\n di[%d]=%d \n timeout=%d \n",index,emcStatus->motion.synch_di[index],emcStatus->task.input_timeout);
#endif
    return (emcStatus->motion.synch_di[index] != 0) ? 1 : 0;
}

static double GET_EXTERNAL_ANALOG_INPUT(void */*ctx*/, int32_t index, double def)
{
/* returns current value of the analog input selected by index.*/
#ifdef INPUT_DEBUG
    printf("GET_EXTERNAL_ANALOG_INPUT called\n ai[%d]=%g \n timeout=%d \n",index,emcStatus->motion.analog_input[index],emcStatus->task.input_timeout);
#endif
    if ((index < 0) || (index >= EMCMOT_MAX_AIO))
	return -1;

    if (emcStatus->task.input_timeout == 1)
	return -1;

    return emcStatus->motion.analog_input[index];
}


USER_DEFINED_FUNCTION_TYPE USER_DEFINED_FUNCTION[USER_DEFINED_FUNCTION_NUM]
    = { 0 };

int USER_DEFINED_FUNCTION_ADD(USER_DEFINED_FUNCTION_TYPE func, int num)
{
    if (num < 0 || num >= USER_DEFINED_FUNCTION_NUM) {
	return -1;
    }

    USER_DEFINED_FUNCTION[num] = func;

    return 0;
}

static double GET_USER_DEFINED_RESULT(void */*ctx*/)
{
  return emcStatus->task.user_defined_result;
}

/*! \function SET_MOTION_OUTPUT_BIT

  sets a DIO pin
  this message goes to task, then to motion which sets the DIO 
  when the first motion starts.
  The pin gets set with value 1 at the begin of motion, and stays 1 at the end of motion
  (this behaviour can be changed if needed)
  
  warning: setting more then one for a motion segment will clear out the previous ones 
  (the TP doesn't implement a queue of these), 
  use SET_AUX_OUTPUT_BIT instead, that allows to set the value right away
*/
static void SET_MOTION_OUTPUT_BIT(void */*ctx*/, int32_t index)
{
  EMC_MOTION_SET_DOUT dout_msg;

  flush_segments();

  dout_msg.index = index;
  dout_msg.start = 1;		// startvalue = 1
  dout_msg.end = 1;		// endvalue = 1, means it doesn't get reset after current motion
  dout_msg.now = 0;		// not immediate, but synched with motion (goes to the TP)

  interp_list.append(dout_msg);

  return;
}

/*! \function CLEAR_MOTION_OUTPUT_BIT

  clears a DIO pin
  this message goes to task, then to motion which clears the DIO 
  when the first motion starts.
  The pin gets set with value 0 at the begin of motion, and stays 0 at the end of motion
  (this behaviour can be changed if needed)
  
  warning: setting more then one for a motion segment will clear out the previous ones 
  (the TP doesn't implement a queue of these), 
  use CLEAR_AUX_OUTPUT_BIT instead, that allows to set the value right away
*/
static void CLEAR_MOTION_OUTPUT_BIT(void */*ctx*/, int32_t index)
{
  EMC_MOTION_SET_DOUT dout_msg;

  flush_segments();

  dout_msg.index = index;
  dout_msg.start = 0;           // startvalue = 1
  dout_msg.end = 0;		// endvalue = 0, means it stays 0 after current motion
  dout_msg.now = 0;		// not immediate, but synched with motion (goes to the TP)

  interp_list.append(dout_msg);

  return;
}

/*! \function SET_AUX_OUTPUT_BIT

  sets a DIO pin
  this message goes to task, then to motion which sets the DIO 
  right away.
  The pin gets set with value 1 at the begin of motion, and stays 1 at the end of motion
  (this behaviour can be changed if needed)
  you can use any number of these, as the effect is immediate  
*/
static void SET_AUX_OUTPUT_BIT(void */*ctx*/, int32_t index)
{

  EMC_MOTION_SET_DOUT dout_msg;

  flush_segments();

  dout_msg.index = index;
  dout_msg.start = 1;		// startvalue = 1
  dout_msg.end = 1;		// endvalue = 1, means it doesn't get reset after current motion
  dout_msg.now = 1;		// immediate, we don't care about syncing for AUX

  interp_list.append(dout_msg);

  return;
}

/*! \function CLEAR_AUX_OUTPUT_BIT

  clears a DIO pin
  this message goes to task, then to motion which clears the DIO 
  right away.
  The pin gets set with value 0 at the begin of motion, and stays 0 at the end of motion
  (this behaviour can be changed if needed)
  you can use any number of these, as the effect is immediate  
*/
static void CLEAR_AUX_OUTPUT_BIT(void */*ctx*/, int32_t index)
{
  EMC_MOTION_SET_DOUT dout_msg;

  flush_segments();

  dout_msg.index = index;
  dout_msg.start = 0;           // startvalue = 1
  dout_msg.end = 0;		// endvalue = 0, means it stays 0 after current motion
  dout_msg.now = 1;		// immediate, we don't care about syncing for AUX

  interp_list.append(dout_msg);

  return;
}

/*! \function SET_MOTION_OUTPUT_VALUE

  sets a AIO value, not used by the RS274 Interp,
  not fully implemented in the motion controller either
*/
static void SET_MOTION_OUTPUT_VALUE(void */*ctx*/, int32_t index, double value)
{
  EMC_MOTION_SET_AOUT aout_msg;

  flush_segments();

  aout_msg.index = index;	// which output
  aout_msg.start = value;	// start value
  aout_msg.end = value;		// end value
  aout_msg.now = 0;		// immediate=1, or synched when motion start=0

  interp_list.append(aout_msg);

  return;
}

/*! \function SET_AUX_OUTPUT_VALUE

  sets a AIO value, not used by the RS274 Interp,
  not fully implemented in the motion controller either
*/
static void SET_AUX_OUTPUT_VALUE(void */*ctx*/, int32_t index, double value)
{
  EMC_MOTION_SET_AOUT aout_msg;

  flush_segments();

  aout_msg.index = index;	// which output
  aout_msg.start = value;	// start value
  aout_msg.end = value;		// end value
  aout_msg.now = 1;		// immediate=1, or synched when motion start=0

  interp_list.append(aout_msg);

  return;
}

/*! \function WAIT
   program execution and interpreting is stopped until the input selected by 
   index changed to the needed state (specified by wait_type).
   Return value: either wait_type if timeout didn't occur, or -1 otherwise. */

static int32_t WAIT(void */*ctx*/, int32_t index, int32_t input_type, int32_t wait_type, double timeout)
{
  if (input_type == DIGITAL_INPUT) {
    if ((index < 0) || (index >= EMCMOT_MAX_DIO))
	return -1;
  } else if (input_type == ANALOG_INPUT) {
    if ((index < 0) || (index >= EMCMOT_MAX_AIO))
	return -1;
  }

 EMC_AUX_INPUT_WAIT wait_msg;
 
 flush_segments();
 
 wait_msg.index = index;
 wait_msg.input_type = input_type;
 wait_msg.wait_type = wait_type;
 wait_msg.timeout = timeout;
 
 interp_list.append(wait_msg);
 return 0;
}

static int32_t UNLOCK_ROTARY(void */*ctx*/, int32_t line_number, int32_t joint_num) {
    EMC_TRAJ_LINEAR_MOVE m;
    // first, set up a zero length move to interrupt blending and get to final position
    m.type = EMC_MOTION_TYPE_TRAVERSE;
    m.feed_mode = 0;
    m.end = to_ext_pose(canon.endPoint.x, canon.endPoint.y, canon.endPoint.z,
                        canon.endPoint.a, canon.endPoint.b, canon.endPoint.c,
                        canon.endPoint.u, canon.endPoint.v, canon.endPoint.w);
    m.vel = m.acc = 1; // nonzero but otherwise doesn't matter
    m.indexer_jnum = -1;

    // issue it
    int old_feed_mode = canon.feed_mode;
    if(canon.feed_mode)
	STOP_SPEED_FEED_SYNCH(NULL);
    interp_list.set_line_number(line_number);
    interp_list.append(m);
    // no need to update endpoint
    if(old_feed_mode)
	START_SPEED_FEED_SYNCH(NULL, canon.spindle_num, canon.linearFeedRate, 1);

    // now, the next move is the real indexing move, so be ready
    canon.rotary_unlock_for_traverse = joint_num;
    return 0;
}

static int32_t LOCK_ROTARY(void */*ctx*/, int32_t line_number, int32_t joint_num) {
    canon.rotary_unlock_for_traverse = -1;
    return 0;
}

// ---- Callback table (populated directly from static functions above) ----

static const canon_callbacks_t emccanon_table = {
    .init_canon = INIT_CANON,
    .set_g5x_offset = SET_G5X_OFFSET,
    .set_g92_offset = SET_G92_OFFSET,
    .set_xy_rotation = SET_XY_ROTATION,
    .update_end_point = CANON_UPDATE_END_POINT,
    .use_length_units = USE_LENGTH_UNITS,
    .select_plane = SELECT_PLANE,
    .set_traverse_rate = SET_TRAVERSE_RATE,
    .straight_traverse = STRAIGHT_TRAVERSE,
    .set_feed_rate = SET_FEED_RATE,
    .set_feed_reference = SET_FEED_REFERENCE,
    .set_feed_mode = SET_FEED_MODE,
    .set_motion_control_mode = SET_MOTION_CONTROL_MODE,
    .set_naivecam_tolerance = SET_NAIVECAM_TOLERANCE,
    .set_cutter_radius_compensation = SET_CUTTER_RADIUS_COMPENSATION,
    .start_cutter_radius_compensation = START_CUTTER_RADIUS_COMPENSATION,
    .stop_cutter_radius_compensation = STOP_CUTTER_RADIUS_COMPENSATION,
    .start_speed_feed_synch = START_SPEED_FEED_SYNCH,
    .stop_speed_feed_synch = STOP_SPEED_FEED_SYNCH,
    .arc_feed = ARC_FEED,
    .straight_feed = STRAIGHT_FEED,
    .nurbs_feed = NURBS_FEED,
    .rigid_tap = RIGID_TAP,
    .straight_probe = STRAIGHT_PROBE,
    .stop = NULL,  // STOP not implemented in emccanon
    .dwell = DWELL,
    .finish = FINISH,
    .set_spindle_mode = SET_SPINDLE_MODE,
    .start_spindle_clockwise = START_SPINDLE_CLOCKWISE,
    .start_spindle_counterclockwise = START_SPINDLE_COUNTERCLOCKWISE,
    .set_spindle_speed = SET_SPINDLE_SPEED,
    .stop_spindle_turning = STOP_SPINDLE_TURNING,
    .orient_spindle = ORIENT_SPINDLE,
    .wait_spindle_orient_complete = WAIT_SPINDLE_ORIENT_COMPLETE,
    .select_tool = SELECT_TOOL,
    .start_change = START_CHANGE,
    .change_tool = CHANGE_TOOL,
    .change_tool_number = CHANGE_TOOL_NUMBER,
    .reload_tooldata = RELOAD_TOOLDATA,
    .set_tool_table_entry = SET_TOOL_TABLE_ENTRY,
    .use_tool_length_offset = USE_TOOL_LENGTH_OFFSET,
    .flood_on = FLOOD_ON,
    .flood_off = FLOOD_OFF,
    .mist_on = MIST_ON,
    .mist_off = MIST_OFF,
    .enable_feed_override = ENABLE_FEED_OVERRIDE,
    .disable_feed_override = DISABLE_FEED_OVERRIDE,
    .enable_speed_override = ENABLE_SPEED_OVERRIDE,
    .disable_speed_override = DISABLE_SPEED_OVERRIDE,
    .enable_feed_hold = ENABLE_FEED_HOLD,
    .disable_feed_hold = DISABLE_FEED_HOLD,
    .enable_adaptive_feed = ENABLE_ADAPTIVE_FEED,
    .disable_adaptive_feed = DISABLE_ADAPTIVE_FEED,
    .set_motion_output_bit = SET_MOTION_OUTPUT_BIT,
    .clear_motion_output_bit = CLEAR_MOTION_OUTPUT_BIT,
    .set_aux_output_bit = SET_AUX_OUTPUT_BIT,
    .clear_aux_output_bit = CLEAR_AUX_OUTPUT_BIT,
    .set_motion_output_value = SET_MOTION_OUTPUT_VALUE,
    .set_aux_output_value = SET_AUX_OUTPUT_VALUE,
    .wait_input = WAIT,
    .clamp_axis = CLAMP_AXIS,
    .unclamp_axis = UNCLAMP_AXIS,
    .lock_rotary = LOCK_ROTARY,
    .unlock_rotary = UNLOCK_ROTARY,
    .program_stop = PROGRAM_STOP,
    .optional_program_stop = OPTIONAL_PROGRAM_STOP,
    .program_end = PROGRAM_END,
    .pallet_shuttle = PALLET_SHUTTLE,
    .comment = COMMENT,
    .message = MESSAGE,
    .log_msg = LOG,
    .logopen = LOGOPEN,
    .logappend = LOGAPPEND,
    .logclose = LOGCLOSE,
    .canon_error = CANON_ERROR_CB,
    .turn_probe_on = TURN_PROBE_ON,
    .turn_probe_off = TURN_PROBE_OFF,
    .set_block_delete = SET_BLOCK_DELETE,
    .get_block_delete = GET_BLOCK_DELETE,
    .set_optional_program_stop = SET_OPTIONAL_PROGRAM_STOP,
    .get_optional_program_stop = GET_OPTIONAL_PROGRAM_STOP,
    .update_tag = UPDATE_TAG,
    .set_parameter_file_name = SET_PARAMETER_FILE_NAME,
    .on_reset = ON_RESET,
    .get_user_defined_result = GET_USER_DEFINED_RESULT,
    .get_external_feed_rate = GET_EXTERNAL_FEED_RATE,
    .get_external_traverse_rate = GET_EXTERNAL_TRAVERSE_RATE,
    .get_external_length_unit_type = GET_EXTERNAL_LENGTH_UNIT_TYPE,
    .get_external_length_units = GET_EXTERNAL_LENGTH_UNITS,
    .get_external_angle_units = GET_EXTERNAL_ANGLE_UNITS,
    .get_external_motion_control_mode = GET_EXTERNAL_MOTION_CONTROL_MODE,
    .get_external_motion_control_tolerance = GET_EXTERNAL_MOTION_CONTROL_TOLERANCE,
    .get_external_motion_control_naivecam_tolerance = GET_EXTERNAL_MOTION_CONTROL_NAIVECAM_TOLERANCE,
    .get_external_flood = GET_EXTERNAL_FLOOD,
    .get_external_mist = GET_EXTERNAL_MIST,
    .get_external_position_x = GET_EXTERNAL_POSITION_X,
    .get_external_position_y = GET_EXTERNAL_POSITION_Y,
    .get_external_position_z = GET_EXTERNAL_POSITION_Z,
    .get_external_position_a = GET_EXTERNAL_POSITION_A,
    .get_external_position_b = GET_EXTERNAL_POSITION_B,
    .get_external_position_c = GET_EXTERNAL_POSITION_C,
    .get_external_position_u = GET_EXTERNAL_POSITION_U,
    .get_external_position_v = GET_EXTERNAL_POSITION_V,
    .get_external_position_w = GET_EXTERNAL_POSITION_W,
    .get_external_probe_position_x = GET_EXTERNAL_PROBE_POSITION_X,
    .get_external_probe_position_y = GET_EXTERNAL_PROBE_POSITION_Y,
    .get_external_probe_position_z = GET_EXTERNAL_PROBE_POSITION_Z,
    .get_external_probe_position_a = GET_EXTERNAL_PROBE_POSITION_A,
    .get_external_probe_position_b = GET_EXTERNAL_PROBE_POSITION_B,
    .get_external_probe_position_c = GET_EXTERNAL_PROBE_POSITION_C,
    .get_external_probe_position_u = GET_EXTERNAL_PROBE_POSITION_U,
    .get_external_probe_position_v = GET_EXTERNAL_PROBE_POSITION_V,
    .get_external_probe_position_w = GET_EXTERNAL_PROBE_POSITION_W,
    .get_external_probe_value = GET_EXTERNAL_PROBE_VALUE,
    .get_external_probe_tripped_value = GET_EXTERNAL_PROBE_TRIPPED_VALUE,
    .get_external_speed = GET_EXTERNAL_SPEED,
    .get_external_spindle = GET_EXTERNAL_SPINDLE,
    .get_external_tool_length_xoffset = GET_EXTERNAL_TOOL_LENGTH_XOFFSET,
    .get_external_tool_length_yoffset = GET_EXTERNAL_TOOL_LENGTH_YOFFSET,
    .get_external_tool_length_zoffset = GET_EXTERNAL_TOOL_LENGTH_ZOFFSET,
    .get_external_tool_length_aoffset = GET_EXTERNAL_TOOL_LENGTH_AOFFSET,
    .get_external_tool_length_boffset = GET_EXTERNAL_TOOL_LENGTH_BOFFSET,
    .get_external_tool_length_coffset = GET_EXTERNAL_TOOL_LENGTH_COFFSET,
    .get_external_tool_length_uoffset = GET_EXTERNAL_TOOL_LENGTH_UOFFSET,
    .get_external_tool_length_voffset = GET_EXTERNAL_TOOL_LENGTH_VOFFSET,
    .get_external_tool_length_woffset = GET_EXTERNAL_TOOL_LENGTH_WOFFSET,
    .get_external_tool_slot = GET_EXTERNAL_TOOL_SLOT,
    .get_external_selected_tool_slot = GET_EXTERNAL_SELECTED_TOOL_SLOT,
    .get_external_tool_table = GET_EXTERNAL_TOOL_TABLE,
    .get_external_tc_fault = GET_EXTERNAL_TC_FAULT,
    .get_external_tc_reason = GET_EXTERNAL_TC_REASON,
    .get_external_queue_empty = GET_EXTERNAL_QUEUE_EMPTY,
    .get_external_axis_mask = GET_EXTERNAL_AXIS_MASK,
    .get_external_digital_input = GET_EXTERNAL_DIGITAL_INPUT,
    .get_external_analog_input = GET_EXTERNAL_ANALOG_INPUT,
    .get_external_feed_override_enable = GET_EXTERNAL_FEED_OVERRIDE_ENABLE,
    .get_external_spindle_override_enable = GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE,
    .get_external_adaptive_feed_enable = GET_EXTERNAL_ADAPTIVE_FEED_ENABLE,
    .get_external_feed_hold_enable = GET_EXTERNAL_FEED_HOLD_ENABLE,
    .get_external_plane = GET_EXTERNAL_PLANE,
    .get_external_parameter_file_name = GET_EXTERNAL_PARAMETER_FILE_NAME,
    .get_external_offset_applied = GET_EXTERNAL_OFFSET_APPLIED,
    .get_external_offsets = GET_EXTERNAL_OFFSETS,
};

const canon_callbacks_t *emccanon_get_callbacks(void) {
    return &emccanon_table;
}
