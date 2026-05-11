//    This is a component of AXIS, a front-end for emc
//    Copyright 2004, 2005, 2006 Jeff Epler <jepler@unpythonic.net> and 
//    Chris Radek <chris@timeguy.com>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <sys/time.h>

#include <Python.h>
#include <structmember.h>

#include "rs274ngc.hh"
#include "rs274ngc_interp.hh"
#include "interp_return.hh"
#include "canon.hh"
#include "config.h"		// LINELEN
#include "units.h"
#include "tooldata.hh"

int _task = 0; // control preview behaviour when remapping

char _parameter_file_name[LINELEN];

extern "C" struct _inittab builtin_modules[];
struct _inittab builtin_modules[] = {
    { NULL, NULL }
};


static PyObject *int_array(int *arr, int sz) {
    PyObject *res = PyTuple_New(sz);
    for(int i = 0; i < sz; i++) {
        PyTuple_SET_ITEM(res, i, PyLong_FromLong(arr[i]));
    }
    return res;
}

typedef struct {
    PyObject_HEAD
    double settings[ACTIVE_SETTINGS];
    int gcodes[ACTIVE_G_CODES];
    int mcodes[ACTIVE_M_CODES];
} LineCode;

static PyObject *LineCode_gcodes(LineCode *l) {
    return int_array(l->gcodes, ACTIVE_G_CODES);
}
static PyObject *LineCode_mcodes(LineCode *l) {
    return int_array(l->mcodes, ACTIVE_M_CODES);
}

static PyGetSetDef LineCodeGetSet[] = {
    {(char*)"gcodes", (getter)LineCode_gcodes},
    {(char*)"mcodes", (getter)LineCode_mcodes},
    {NULL, NULL},
};

static PyMemberDef LineCodeMembers[] = {
    {(char*)"sequence_number", T_INT, offsetof(LineCode, gcodes[0]), READONLY},

    {(char*)"feed_rate", T_DOUBLE, offsetof(LineCode, settings[1]), READONLY},
    {(char*)"speed", T_DOUBLE, offsetof(LineCode, settings[2]), READONLY},
    {(char*)"motion_mode", T_INT, offsetof(LineCode, gcodes[1]), READONLY},
    {(char*)"block", T_INT, offsetof(LineCode, gcodes[2]), READONLY},
    {(char*)"plane", T_INT, offsetof(LineCode, gcodes[3]), READONLY},
    {(char*)"cutter_side", T_INT, offsetof(LineCode, gcodes[4]), READONLY},
    {(char*)"units", T_INT, offsetof(LineCode, gcodes[5]), READONLY},
    {(char*)"distance_mode", T_INT, offsetof(LineCode, gcodes[6]), READONLY},
    {(char*)"feed_mode", T_INT, offsetof(LineCode, gcodes[7]), READONLY},
    {(char*)"origin", T_INT, offsetof(LineCode, gcodes[8]), READONLY},
    {(char*)"tool_length_offset", T_INT, offsetof(LineCode, gcodes[9]), READONLY},
    {(char*)"retract_mode", T_INT, offsetof(LineCode, gcodes[10]), READONLY},
    {(char*)"path_mode", T_INT, offsetof(LineCode, gcodes[11]), READONLY},

    {(char*)"stopping", T_INT, offsetof(LineCode, mcodes[1]), READONLY},
    {(char*)"spindle", T_INT, offsetof(LineCode, mcodes[2]), READONLY},
    {(char*)"toolchange", T_INT, offsetof(LineCode, mcodes[3]), READONLY},
    {(char*)"mist", T_INT, offsetof(LineCode, mcodes[4]), READONLY},
    {(char*)"flood", T_INT, offsetof(LineCode, mcodes[5]), READONLY},
    {(char*)"overrides", T_INT, offsetof(LineCode, mcodes[6]), READONLY},
    {NULL}
};

static PyTypeObject LineCodeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "gcode.linecode",       /*tp_name*/
    sizeof(LineCode),       /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    0,                      /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    0,                      /*tp_call*/
    0,                      /*tp_str*/
    0,                      /*tp_getattro*/
    0,                      /*tp_setattro*/
    0,                      /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    0,                      /*tp_doc*/
    0,                      /*tp_traverse*/
    0,                      /*tp_clear*/
    0,                      /*tp_richcompare*/
    0,                      /*tp_weaklistoffset*/
    0,                      /*tp_iter*/
    0,                      /*tp_iternext*/
    0,                      /*tp_methods*/
    LineCodeMembers,     /*tp_members*/
    LineCodeGetSet,      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    0,                      /*tp_init*/
    0,                      /*tp_alloc*/
    PyType_GenericNew,      /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

static PyObject *callback;
static int interp_error;
static int last_sequence_number;
static bool metric;
static double _pos_x, _pos_y, _pos_z, _pos_a, _pos_b, _pos_c, _pos_u, _pos_v, _pos_w;
EmcPose tool_offset;

static InterpBase *pinterp;

#define callmethod(o, m, f, ...) PyObject_CallMethod((o), (char*)(m), (char*)(f), ## __VA_ARGS__)

static void maybe_new_line(int sequence_number=pinterp->sequence_number());
static void maybe_new_line(int sequence_number) {
    if(!pinterp) return;
    if(interp_error) return;
    if(sequence_number == last_sequence_number)
        return;
    LineCode *new_line_code =
        (LineCode*)(PyObject_New(LineCode, &LineCodeType));
    pinterp->active_settings(new_line_code->settings);
    pinterp->active_g_codes(new_line_code->gcodes);
    pinterp->active_m_codes(new_line_code->mcodes);
    new_line_code->gcodes[0] = sequence_number;
    last_sequence_number = sequence_number;
    PyObject *result = 
        callmethod(callback, "next_line", "O", new_line_code);
    Py_DECREF(new_line_code);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void NURBS_FEED(int line_number, std::vector<CONTROL_POINT> nurbs_control_points, unsigned int k) {
    double u = 0.0;
    unsigned int n = nurbs_control_points.size() - 1;
    double umax = n - k + 2;
    unsigned int div = nurbs_control_points.size()*15;
    std::vector<unsigned int> knot_vector = knot_vector_creator(n, k);	
    PLANE_POINT P1;
    while (u+umax/div < umax) {
        PLANE_POINT P1 = nurbs_point(u+umax/div,k,nurbs_control_points,knot_vector);
        STRAIGHT_FEED(line_number, P1.X,P1.Y, _pos_z, _pos_a, _pos_b, _pos_c, _pos_u, _pos_v, _pos_w);
        u = u + umax/div;
    } 
    P1.X = nurbs_control_points[n].X;
    P1.Y = nurbs_control_points[n].Y;
    STRAIGHT_FEED(line_number, P1.X,P1.Y, _pos_z, _pos_a, _pos_b, _pos_c, _pos_u, _pos_v, _pos_w);
    knot_vector.clear();
}

void ARC_FEED(int line_number,
              double first_end, double second_end, double first_axis,
              double second_axis, int rotation, double axis_end_point,
              double a_position, double b_position, double c_position,
              double u_position, double v_position, double w_position) {
    // XXX: set _pos_*
    if(metric) {
        first_end /= 25.4;
        second_end /= 25.4;
        first_axis /= 25.4;
        second_axis /= 25.4;
        axis_end_point /= 25.4;
        u_position /= 25.4;
        v_position /= 25.4;
        w_position /= 25.4;
    }
    maybe_new_line(line_number);
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "arc_feed", "ffffifffffff",
                            first_end, second_end, first_axis, second_axis,
                            rotation, axis_end_point, 
                            a_position, b_position, c_position,
                            u_position, v_position, w_position);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void STRAIGHT_FEED(int line_number,
                   double x, double y, double z,
                   double a, double b, double c,
                   double u, double v, double w) {
    _pos_x=x; _pos_y=y; _pos_z=z; 
    _pos_a=a; _pos_b=b; _pos_c=c;
    _pos_u=u; _pos_v=v; _pos_w=w;
    if(metric) { x /= 25.4; y /= 25.4; z /= 25.4; u /= 25.4; v /= 25.4; w /= 25.4; }
    maybe_new_line(line_number);
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "straight_feed", "fffffffff",
                            x, y, z, a, b, c, u, v, w);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void STRAIGHT_TRAVERSE(int line_number,
                       double x, double y, double z,
                       double a, double b, double c,
                       double u, double v, double w) {
    _pos_x=x; _pos_y=y; _pos_z=z; 
    _pos_a=a; _pos_b=b; _pos_c=c;
    _pos_u=u; _pos_v=v; _pos_w=w;
    if(metric) { x /= 25.4; y /= 25.4; z /= 25.4; u /= 25.4; v /= 25.4; w /= 25.4; }
    maybe_new_line(line_number);
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "straight_traverse", "fffffffff",
                            x, y, z, a, b, c, u, v, w);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_G5X_OFFSET(int g5x_index,
                    double x, double y, double z,
                    double a, double b, double c,
                    double u, double v, double w) {
    if(metric) { x /= 25.4; y /= 25.4; z /= 25.4; u /= 25.4; v /= 25.4; w /= 25.4; }
    maybe_new_line();
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "set_g5x_offset", "ifffffffff",
                            g5x_index, x, y, z, a, b, c, u, v, w);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_G92_OFFSET(double x, double y, double z,
                    double a, double b, double c,
                    double u, double v, double w) {
    if(metric) { x /= 25.4; y /= 25.4; z /= 25.4; u /= 25.4; v /= 25.4; w /= 25.4; }
    maybe_new_line();
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "set_g92_offset", "fffffffff",
                            x, y, z, a, b, c, u, v, w);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_XY_ROTATION(double t) {
    maybe_new_line();
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "set_xy_rotation", "f", t);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
};

void USE_LENGTH_UNITS(CANON_UNITS u) { metric = u == CANON_UNITS_MM; }

void SELECT_PLANE(CANON_PLANE pl) {
    maybe_new_line();   
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "set_plane", "i", pl);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_TRAVERSE_RATE(double rate) {
    maybe_new_line();   
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "set_traverse_rate", "f", rate);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_FEED_MODE(int spindle, int mode) {
#if 0
    maybe_new_line();   
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "set_feed_mode", "i", mode);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
#endif
}

void CHANGE_TOOL(int pocket) {
    maybe_new_line();
    if(interp_error) return;
    PyObject *result = 
        callmethod(callback, "change_tool", "i", pocket);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void CHANGE_TOOL_NUMBER(int pocket) {
    maybe_new_line();
    if(interp_error) return;
}

void RELOAD_TOOLDATA(void) {
    return;
}

/* XXX: This needs to be re-thought.  Sometimes feed rate is not in linear
 * units--e.g., it could be inverse time feed mode.  in that case, it's wrong
 * to convert from mm to inch here.  but the gcode time estimate gets inverse
 * time feed wrong anyway..
 */
void SET_FEED_RATE(double rate) {
    maybe_new_line();   
    if(interp_error) return;
    if(metric) rate /= 25.4;
    PyObject *result =
        callmethod(callback, "set_feed_rate", "f", rate);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void DWELL(double time) {
    maybe_new_line();   
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "dwell", "f", time);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void MESSAGE(char *comment) {
    maybe_new_line();   
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "message", "s", comment);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void LOG(char *s) {}
void LOGOPEN(char *f) {}
void LOGAPPEND(char *f) {}
void LOGCLOSE() {}

void COMMENT(const char *comment) {
    maybe_new_line();   
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "comment", "s", comment);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_TOOL_TABLE_ENTRY(int pocket, int toolno, EmcPose offset, double diameter,
                          double frontangle, double backangle, int orientation) {
}

void USE_TOOL_LENGTH_OFFSET(EmcPose offset) {
    tool_offset = offset;
    maybe_new_line();
    if(interp_error) return;
    if(metric) {
        offset.tran.x /= 25.4; offset.tran.y /= 25.4; offset.tran.z /= 25.4;
        offset.u /= 25.4; offset.v /= 25.4; offset.w /= 25.4; }
    PyObject *result = callmethod(callback, "tool_offset", "ddddddddd", offset.tran.x, offset.tran.y, offset.tran.z,
        offset.a, offset.b, offset.c, offset.u, offset.v, offset.w);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}

void SET_FEED_REFERENCE(double reference) { }
void SET_CUTTER_RADIUS_COMPENSATION(double radius) {}
void START_CUTTER_RADIUS_COMPENSATION(int direction) {}
void STOP_CUTTER_RADIUS_COMPENSATION(int direction) {}
void START_SPEED_FEED_SYNCH() {}
void START_SPEED_FEED_SYNCH(int spindle, double sync, bool vel) {}
void STOP_SPEED_FEED_SYNCH() {}
void START_SPINDLE_COUNTERCLOCKWISE(int spindle, int wait_for_at_speed) {}
void START_SPINDLE_CLOCKWISE(int spindle, int wait_for_at_speed) {}
void SET_SPINDLE_MODE(int spindle, double) {}
void STOP_SPINDLE_TURNING(int spindle) {}
void SET_SPINDLE_SPEED(int spindle, double rpm) {}
void ORIENT_SPINDLE(int spindle, double d, int i) {}
void WAIT_SPINDLE_ORIENT_COMPLETE(int s, double timeout) {}
void PROGRAM_STOP() {}
void PROGRAM_END() {}
void FINISH() {}
void ON_RESET() {}
void PALLET_SHUTTLE() {}
void SELECT_TOOL(int tool) {}
void UPDATE_TAG(StateTag tag) {}
void OPTIONAL_PROGRAM_STOP() {}
void START_CHANGE() {}
int  GET_EXTERNAL_TC_FAULT() {return 0;}
int  GET_EXTERNAL_TC_REASON() {return 0;}


extern bool GET_BLOCK_DELETE(void) { 
    int bd = 0;
    if(interp_error) return 0;
    PyObject *result =
        callmethod(callback, "get_block_delete", "");
    if(result == NULL) {
        interp_error++;
    } else {
        bd = PyObject_IsTrue(result);
    }
    Py_XDECREF(result);
    return bd;
}

void CANON_ERROR(const char *fmt, ...) {};
void CLAMP_AXIS(CANON_AXIS axis) {}
bool GET_OPTIONAL_PROGRAM_STOP() { return false;}
void SET_OPTIONAL_PROGRAM_STOP(bool state) {}
void SPINDLE_RETRACT_TRAVERSE() {}
void SPINDLE_RETRACT() {}
void STOP_CUTTER_RADIUS_COMPENSATION() {}
void USE_NO_SPINDLE_FORCE() {}
void SET_BLOCK_DELETE(bool enabled) {}

void DISABLE_FEED_OVERRIDE() {}
void DISABLE_FEED_HOLD() {}
void ENABLE_FEED_HOLD() {}
void DISABLE_SPEED_OVERRIDE(int spindle) {}
void ENABLE_FEED_OVERRIDE() {}
void ENABLE_SPEED_OVERRIDE(int spindle) {}
void MIST_OFF() {}
void FLOOD_OFF() {}
void MIST_ON() {}
void FLOOD_ON() {}
void CLEAR_AUX_OUTPUT_BIT(int bit) {}
void SET_AUX_OUTPUT_BIT(int bit) {}
void SET_AUX_OUTPUT_VALUE(int index, double value) {}
void CLEAR_MOTION_OUTPUT_BIT(int bit) {}
void SET_MOTION_OUTPUT_BIT(int bit) {}
void SET_MOTION_OUTPUT_VALUE(int index, double value) {}
void TURN_PROBE_ON() {}
void TURN_PROBE_OFF() {}
int UNLOCK_ROTARY(int line_no, int joint_num) {return 0;}
int LOCK_ROTARY(int line_no, int joint_num) {return 0;}
void INTERP_ABORT(int reason,const char *message) {}
void PLUGIN_CALL(int len, const char *call) {}
void IO_PLUGIN_CALL(int len, const char *call) {}

void STRAIGHT_PROBE(int line_number, 
                    double x, double y, double z, 
                    double a, double b, double c,
                    double u, double v, double w, unsigned char probe_type) {
    _pos_x=x; _pos_y=y; _pos_z=z; 
    _pos_a=a; _pos_b=b; _pos_c=c;
    _pos_u=u; _pos_v=v; _pos_w=w;
    if(metric) { x /= 25.4; y /= 25.4; z /= 25.4; u /= 25.4; v /= 25.4; w /= 25.4; }
    maybe_new_line(line_number);
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "straight_probe", "fffffffff",
                            x, y, z, a, b, c, u, v, w);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);

}
void RIGID_TAP(int line_number,
               double x, double y, double z, double scale) {
    if(metric) { x /= 25.4; y /= 25.4; z /= 25.4; }
    maybe_new_line(line_number);
    if(interp_error) return;
    PyObject *result =
        callmethod(callback, "rigid_tap", "fff",
            x, y, z);
    if(result == NULL) interp_error ++;
    Py_XDECREF(result);
}
double GET_EXTERNAL_MOTION_CONTROL_TOLERANCE() { return 0.1; }
double GET_EXTERNAL_MOTION_CONTROL_NAIVECAM_TOLERANCE() { return 0.1; }
double GET_EXTERNAL_PROBE_POSITION_X() { return _pos_x; }
double GET_EXTERNAL_PROBE_POSITION_Y() { return _pos_y; }
double GET_EXTERNAL_PROBE_POSITION_Z() { return _pos_z; }
double GET_EXTERNAL_PROBE_POSITION_A() { return _pos_a; }
double GET_EXTERNAL_PROBE_POSITION_B() { return _pos_b; }
double GET_EXTERNAL_PROBE_POSITION_C() { return _pos_c; }
double GET_EXTERNAL_PROBE_POSITION_U() { return _pos_u; }
double GET_EXTERNAL_PROBE_POSITION_V() { return _pos_v; }
double GET_EXTERNAL_PROBE_POSITION_W() { return _pos_w; }
double GET_EXTERNAL_PROBE_VALUE() { return 0.0; }
int GET_EXTERNAL_PROBE_TRIPPED_VALUE() { return 0; }
double GET_EXTERNAL_POSITION_X() { return _pos_x; }
double GET_EXTERNAL_POSITION_Y() { return _pos_y; }
double GET_EXTERNAL_POSITION_Z() { return _pos_z; }
double GET_EXTERNAL_POSITION_A() { return _pos_a; }
double GET_EXTERNAL_POSITION_B() { return _pos_b; }
double GET_EXTERNAL_POSITION_C() { return _pos_c; }
double GET_EXTERNAL_POSITION_U() { return _pos_u; }
double GET_EXTERNAL_POSITION_V() { return _pos_v; }
double GET_EXTERNAL_POSITION_W() { return _pos_w; }
void INIT_CANON() {}

void SET_PARAMETER_FILE_NAME(const char *name)
{
  strncpy(_parameter_file_name, name, PARAMETER_FILE_NAME_LENGTH);
}

void GET_EXTERNAL_PARAMETER_FILE_NAME(char *name, int max_size) {
    PyObject *result = PyObject_GetAttrString(callback, "parameter_file");
    if(!result) { name[0] = 0; return; }
    char *s = (char*)PyUnicode_AsUTF8(result);
    if(!s) { name[0] = 0; return; }
    memset(name, 0, max_size);
    strncpy(name, s, max_size - 1);
}
CANON_UNITS GET_EXTERNAL_LENGTH_UNIT_TYPE() { return CANON_UNITS_INCHES; }
CANON_TOOL_TABLE GET_EXTERNAL_TOOL_TABLE(int pocket) {
    CANON_TOOL_TABLE tdata = {-1,-1,{{0,0,0},0,0,0,0,0,0},0,0,0,0};
    if(interp_error) return tdata;
    PyObject *result =
        callmethod(callback, "get_tool", "i", pocket);
    if(result == NULL ||
       !PyArg_ParseTuple(result, "iddddddddddddi",
             &tdata.toolno,
             &tdata.offset.tran.x, &tdata.offset.tran.y, &tdata.offset.tran.z,
             &tdata.offset.a,      &tdata.offset.b,      &tdata.offset.c,
             &tdata.offset.u,      &tdata.offset.v,      &tdata.offset.w,
             &tdata.diameter,      &tdata.frontangle,    &tdata.backangle,
             &tdata.orientation)) {
       interp_error ++;
    }
    Py_XDECREF(result);
    return tdata;
}

int GET_EXTERNAL_DIGITAL_INPUT(int index, int def) { return def; }
double GET_EXTERNAL_ANALOG_INPUT(int index, double def) { return def; }
int WAIT(int index, int input_type, int wait_type, double timeout) { return 0;}

static void user_defined_function(int num, double arg1, double arg2) {
    if(interp_error) return;
    maybe_new_line();
    PyObject *result =
        callmethod(callback, "user_defined_function",
                            "idd", num, arg1, arg2);
    if(result == NULL) interp_error++;
    Py_XDECREF(result);
}

void SET_FEED_REFERENCE(CANON_FEED_REFERENCE ref) {}
int GET_EXTERNAL_QUEUE_EMPTY() { return true; }
CANON_DIRECTION GET_EXTERNAL_SPINDLE(int) { return CANON_STOPPED; }
int GET_EXTERNAL_TOOL_SLOT() { return 0; }
int GET_EXTERNAL_SELECTED_TOOL_SLOT() { return 0; }
double GET_EXTERNAL_FEED_RATE() { return 1; }
double GET_EXTERNAL_TRAVERSE_RATE() { return 0; }
int GET_EXTERNAL_FLOOD() { return 0; }
int GET_EXTERNAL_MIST() { return 0; }
CANON_PLANE GET_EXTERNAL_PLANE() { return CANON_PLANE_XY; }
double GET_EXTERNAL_SPEED(int spindle) { return 0; }
void DISABLE_ADAPTIVE_FEED() {} 
void ENABLE_ADAPTIVE_FEED() {} 

int GET_EXTERNAL_FEED_OVERRIDE_ENABLE() {return 1;}
int GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE(int spindle) {return 1;}
int GET_EXTERNAL_ADAPTIVE_FEED_ENABLE() {return 0;}
int GET_EXTERNAL_FEED_HOLD_ENABLE() {return 1;}

int GET_EXTERNAL_OFFSET_APPLIED() {return 0;}
EmcPose GET_EXTERNAL_OFFSETS() {
    EmcPose e;
    e.tran.x = 0;
    e.tran.y = 0;
    e.tran.z = 0;
    e.a      = 0;
    e.b      = 0;
    e.c      = 0;
    e.u      = 0;
    e.v      = 0;
    e.w      = 0;
    return e;
};

int GET_EXTERNAL_AXIS_MASK() {
    if(interp_error) return 7;
    PyObject *result =
        callmethod(callback, "get_axis_mask", "");
    if(!result) { interp_error ++; return 7 /* XYZABC */; }
    if(!PyLong_Check(result)) { interp_error ++; return 7 /* XYZABC */; }
    int mask = PyLong_AsLong(result);
    Py_DECREF(result);
    return mask;
}

double GET_EXTERNAL_TOOL_LENGTH_XOFFSET() {
    return tool_offset.tran.x;
}
double GET_EXTERNAL_TOOL_LENGTH_YOFFSET() {
    return tool_offset.tran.y;
}
double GET_EXTERNAL_TOOL_LENGTH_ZOFFSET() {
    return tool_offset.tran.z;
}
double GET_EXTERNAL_TOOL_LENGTH_AOFFSET() {
    return tool_offset.a;
}
double GET_EXTERNAL_TOOL_LENGTH_BOFFSET() {
    return tool_offset.b;
}
double GET_EXTERNAL_TOOL_LENGTH_COFFSET() {
    return tool_offset.c;
}
double GET_EXTERNAL_TOOL_LENGTH_UOFFSET() {
    return tool_offset.u;
}
double GET_EXTERNAL_TOOL_LENGTH_VOFFSET() {
    return tool_offset.v;
}
double GET_EXTERNAL_TOOL_LENGTH_WOFFSET() {
    return tool_offset.w;
}

static bool PyLong_CheckAndError(const char *func, PyObject *p)  {
    if(PyLong_Check(p)) return true;
    PyErr_Format(PyExc_TypeError,
            "%s: Expected int, got %s", func, Py_TYPE(p)->tp_name);
    return false;
}

static bool PyFloat_CheckAndError(const char *func, PyObject *p)  {
    if(PyFloat_Check(p)) return true;
    PyErr_Format(PyExc_TypeError,
            "%s: Expected float, got %s", func, Py_TYPE(p)->tp_name);
    return false;
}

double GET_EXTERNAL_ANGLE_UNITS() {
    PyObject *result =
        callmethod(callback, "get_external_angular_units", "");
    if(result == NULL) interp_error++;

    double dresult = 1.0;
    if(!result || !PyFloat_CheckAndError("get_external_angle_units", result)) {
        interp_error++;
    } else {
        dresult = PyFloat_AsDouble(result);
    }
    Py_XDECREF(result);
    return dresult;
}

double GET_EXTERNAL_LENGTH_UNITS() {
    PyObject *result =
        callmethod(callback, "get_external_length_units", "");
    if(result == NULL) interp_error++;

    double dresult = 0.03937007874016;
    if(!result || !PyFloat_CheckAndError("get_external_length_units", result)) {
        interp_error++;
    } else {
        dresult = PyFloat_AsDouble(result);
    }
    Py_XDECREF(result);
    return dresult;
}

static bool check_abort() {
    PyObject *result =
        callmethod(callback, "check_abort", "");
    if(!result) return 1;
    if(PyObject_IsTrue(result)) {
        Py_DECREF(result);
        PyErr_Format(PyExc_KeyboardInterrupt, "Load aborted");
        return 1;
    }
    Py_DECREF(result);
    return 0;
}

USER_DEFINED_FUNCTION_TYPE USER_DEFINED_FUNCTION[USER_DEFINED_FUNCTION_NUM];
double GET_USER_DEFINED_RESULT() { return 0.0; }

CANON_MOTION_MODE motion_mode;
void SET_MOTION_CONTROL_MODE(CANON_MOTION_MODE mode, double tolerance) { motion_mode = mode; }
void SET_MOTION_CONTROL_MODE(double tolerance) { }
void SET_MOTION_CONTROL_MODE(CANON_MOTION_MODE mode) { motion_mode = mode; }
CANON_MOTION_MODE GET_EXTERNAL_MOTION_CONTROL_MODE() { return motion_mode; }
void SET_NAIVECAM_TOLERANCE(double tolerance) { }

// ---- Canon callback table for gcode preview ----
// Wraps the local preview canon functions into the generated callback struct.

#define CANON_API_CGO
#include "gomc/generated/gmi/canon/canon_api.h"
#undef CANON_API_CGO

static void gc_init_canon(void *) { INIT_CANON(); }
static void gc_set_g5x_offset(void *ctx, int32_t origin, double x, double y, double z, double a, double b, double c, double u, double v, double w) { SET_G5X_OFFSET(origin, x, y, z, a, b, c, u, v, w); }
static void gc_set_g92_offset(void *ctx, double x, double y, double z, double a, double b, double c, double u, double v, double w) { SET_G92_OFFSET(x, y, z, a, b, c, u, v, w); }
static void gc_set_xy_rotation(void *ctx, double t) { SET_XY_ROTATION(t); }
static void gc_update_end_point(void *ctx, double x, double y, double z, double a, double b, double c, double u, double v, double w) {
    _pos_x=x; _pos_y=y; _pos_z=z; _pos_a=a; _pos_b=b; _pos_c=c; _pos_u=u; _pos_v=v; _pos_w=w;
}
static void gc_use_length_units(void *ctx, int32_t u) { USE_LENGTH_UNITS((CANON_UNITS)u); }
static void gc_select_plane(void *ctx, int32_t pl) { SELECT_PLANE((CANON_PLANE)pl); }
static void gc_set_traverse_rate(void *ctx, double rate) { SET_TRAVERSE_RATE(rate); }
static void gc_straight_traverse(void *ctx, int32_t ln, double x, double y, double z, double a, double b, double c, double u, double v, double w) { STRAIGHT_TRAVERSE(ln, x, y, z, a, b, c, u, v, w); }
static void gc_set_feed_rate(void *ctx, double rate) { SET_FEED_RATE(rate); }
static void gc_set_feed_reference(void *ctx, int32_t ref) { (void)ref; }
static void gc_set_feed_mode(void *ctx, int32_t spindle, int32_t mode) { SET_FEED_MODE(spindle, mode); }
static void gc_set_motion_control_mode(void *ctx, int32_t mode, double tol) { SET_MOTION_CONTROL_MODE((CANON_MOTION_MODE)mode, tol); }
static void gc_set_naivecam_tolerance(void *ctx, double tol) { SET_NAIVECAM_TOLERANCE(tol); }
static void gc_set_cutter_radius_compensation(void *ctx, double r) { SET_CUTTER_RADIUS_COMPENSATION(r); }
static void gc_start_cutter_radius_compensation(void *ctx, int32_t d) { START_CUTTER_RADIUS_COMPENSATION(d); }
static void gc_stop_cutter_radius_compensation(void *ctx) { STOP_CUTTER_RADIUS_COMPENSATION(); }
static void gc_start_speed_feed_synch(void *ctx, int32_t spindle, double sync, int32_t vel) { START_SPEED_FEED_SYNCH(spindle, sync, vel); }
static void gc_stop_speed_feed_synch(void *ctx) { STOP_SPEED_FEED_SYNCH(); }
static void gc_arc_feed(void *ctx, int32_t ln, double first_end, double second_end, double first_axis, double second_axis, int32_t rotation, double axis_end_point, double a, double b, double c, double u, double v, double w) { ARC_FEED(ln, first_end, second_end, first_axis, second_axis, rotation, axis_end_point, a, b, c, u, v, w); }
static void gc_straight_feed(void *ctx, int32_t ln, double x, double y, double z, double a, double b, double c, double u, double v, double w) { STRAIGHT_FEED(ln, x, y, z, a, b, c, u, v, w); }
static void gc_nurbs_feed(void *ctx, int32_t ln, const canon_control_point_t *pts, size_t npts, uint32_t k) {
    std::vector<CONTROL_POINT> cpts(npts);
    for (uint32_t i = 0; i < npts; i++) { cpts[i].X = pts[i].x; cpts[i].Y = pts[i].y; cpts[i].W = pts[i].w; }
    NURBS_FEED(ln, cpts, k);
}
static void gc_rigid_tap(void *ctx, int32_t ln, double x, double y, double z, double scale) { RIGID_TAP(ln, x, y, z, scale); }
static void gc_straight_probe(void *ctx, int32_t ln, double x, double y, double z, double a, double b, double c, double u, double v, double w, uint8_t pt) { STRAIGHT_PROBE(ln, x, y, z, a, b, c, u, v, w, pt); }
static void gc_stop(void *ctx) {}
static void gc_dwell(void *ctx, double s) { DWELL(s); }
static void gc_finish(void *ctx) { FINISH(); }
static void gc_set_spindle_mode(void *ctx, int32_t spindle, double m) { SET_SPINDLE_MODE(spindle, m); }
static void gc_start_spindle_clockwise(void *ctx, int32_t spindle, int32_t wait) { START_SPINDLE_CLOCKWISE(spindle, wait); }
static void gc_start_spindle_counterclockwise(void *ctx, int32_t spindle, int32_t wait) { START_SPINDLE_COUNTERCLOCKWISE(spindle, wait); }
static void gc_set_spindle_speed(void *ctx, int32_t spindle, double rpm) { SET_SPINDLE_SPEED(spindle, rpm); }
static void gc_stop_spindle_turning(void *ctx, int32_t spindle) { STOP_SPINDLE_TURNING(spindle); }
static void gc_orient_spindle(void *ctx, int32_t spindle, double d, int32_t i) { ORIENT_SPINDLE(spindle, d, i); }
static void gc_wait_spindle_orient_complete(void *ctx, int32_t s, double t) { WAIT_SPINDLE_ORIENT_COMPLETE(s, t); }
static void gc_select_tool(void *ctx, int32_t t) { SELECT_TOOL(t); }
static void gc_start_change(void *ctx) { START_CHANGE(); }
static void gc_change_tool(void *ctx, int32_t p) { CHANGE_TOOL(p); }
static void gc_change_tool_number(void *ctx, int32_t p) { CHANGE_TOOL_NUMBER(p); }
static void gc_reload_tooldata(void *ctx) { RELOAD_TOOLDATA(); }
static void gc_set_tool_table_entry(void *ctx, int32_t pocket, int32_t toolno, double ox, double oy, double oz, double oa, double ob, double oc, double ou, double ov, double ow, double diameter, double frontangle, double backangle, int32_t orient) {
    EmcPose offset;
    offset.tran.x = ox; offset.tran.y = oy; offset.tran.z = oz;
    offset.a = oa; offset.b = ob; offset.c = oc;
    offset.u = ou; offset.v = ov; offset.w = ow;
    SET_TOOL_TABLE_ENTRY(pocket, toolno, offset, diameter, frontangle, backangle, orient);
}
static void gc_use_tool_length_offset(void *ctx, double ox, double oy, double oz, double oa, double ob, double oc, double ou, double ov, double ow) {
    EmcPose offset;
    offset.tran.x = ox; offset.tran.y = oy; offset.tran.z = oz;
    offset.a = oa; offset.b = ob; offset.c = oc;
    offset.u = ou; offset.v = ov; offset.w = ow;
    USE_TOOL_LENGTH_OFFSET(offset);
}
static void gc_flood_on(void *ctx) { FLOOD_ON(); }
static void gc_flood_off(void *ctx) { FLOOD_OFF(); }
static void gc_mist_on(void *ctx) { MIST_ON(); }
static void gc_mist_off(void *ctx) { MIST_OFF(); }
static void gc_enable_feed_override(void *ctx) { ENABLE_FEED_OVERRIDE(); }
static void gc_disable_feed_override(void *ctx) { DISABLE_FEED_OVERRIDE(); }
static void gc_enable_speed_override(void *ctx, int32_t s) { ENABLE_SPEED_OVERRIDE(s); }
static void gc_disable_speed_override(void *ctx, int32_t s) { DISABLE_SPEED_OVERRIDE(s); }
static void gc_enable_feed_hold(void *ctx) { ENABLE_FEED_HOLD(); }
static void gc_disable_feed_hold(void *ctx) { DISABLE_FEED_HOLD(); }
static void gc_enable_adaptive_feed(void *ctx) { ENABLE_ADAPTIVE_FEED(); }
static void gc_disable_adaptive_feed(void *ctx) { DISABLE_ADAPTIVE_FEED(); }
static void gc_set_motion_output_bit(void *ctx, int32_t b) { SET_MOTION_OUTPUT_BIT(b); }
static void gc_clear_motion_output_bit(void *ctx, int32_t b) { CLEAR_MOTION_OUTPUT_BIT(b); }
static void gc_set_aux_output_bit(void *ctx, int32_t b) { SET_AUX_OUTPUT_BIT(b); }
static void gc_clear_aux_output_bit(void *ctx, int32_t b) { CLEAR_AUX_OUTPUT_BIT(b); }
static void gc_set_motion_output_value(void *ctx, int32_t i, double v) { SET_MOTION_OUTPUT_VALUE(i, v); }
static void gc_set_aux_output_value(void *ctx, int32_t i, double v) { SET_AUX_OUTPUT_VALUE(i, v); }
static int32_t gc_wait_input(void *ctx, int32_t index, int32_t input_type, int32_t wait_type, double timeout) { return WAIT(index, input_type, wait_type, timeout); }
static void gc_clamp_axis(void *ctx, int32_t a) { CLAMP_AXIS((CANON_AXIS)a); }
static void gc_unclamp_axis(void *ctx, int32_t a) { (void)a; }
static int32_t gc_lock_rotary(void *ctx, int32_t ln, int32_t j) { return LOCK_ROTARY(ln, j); }
static int32_t gc_unlock_rotary(void *ctx, int32_t ln, int32_t j) { return UNLOCK_ROTARY(ln, j); }
static void gc_program_stop(void *ctx) { PROGRAM_STOP(); }
static void gc_optional_program_stop(void *ctx) { OPTIONAL_PROGRAM_STOP(); }
static void gc_program_end(void *ctx) { PROGRAM_END(); }
static void gc_pallet_shuttle(void *ctx) { PALLET_SHUTTLE(); }
static void gc_comment(void *ctx, const char *s) { COMMENT(s); }
static void gc_message(void *ctx, const char *s) { MESSAGE((char*)s); }
static void gc_log_msg(void *ctx, const char *s) { LOG((char*)s); }
static void gc_logopen(void *ctx, const char *s) { LOGOPEN((char*)s); }
static void gc_logappend(void *ctx, const char *s) { LOGAPPEND((char*)s); }
static void gc_logclose(void *ctx) { LOGCLOSE(); }
static void gc_canon_error(void *ctx, const char *msg) { CANON_ERROR("%s", msg); }
static void gc_turn_probe_on(void *ctx) { TURN_PROBE_ON(); }
static void gc_turn_probe_off(void *ctx) { TURN_PROBE_OFF(); }
static void gc_set_block_delete(void *ctx, int32_t e) { SET_BLOCK_DELETE(e); }
static int32_t gc_get_block_delete(void *ctx) { return GET_BLOCK_DELETE(); }
static void gc_set_optional_program_stop(void *ctx, int32_t e) { SET_OPTIONAL_PROGRAM_STOP(e); }
static int32_t gc_get_optional_program_stop(void *ctx) { return GET_OPTIONAL_PROGRAM_STOP(); }
static void gc_update_tag(void *ctx, uint64_t tag) { (void)tag; }
static void gc_set_parameter_file_name(void *ctx, const char *n) { SET_PARAMETER_FILE_NAME(n); }
static void gc_on_reset(void *ctx) { ON_RESET(); }
static double gc_get_user_defined_result(void *ctx) { return GET_USER_DEFINED_RESULT(); }
static double gc_get_external_feed_rate(void *ctx) { return GET_EXTERNAL_FEED_RATE(); }
static double gc_get_external_traverse_rate(void *ctx) { return GET_EXTERNAL_TRAVERSE_RATE(); }
static int32_t gc_get_external_length_unit_type(void *ctx) { return (int32_t)GET_EXTERNAL_LENGTH_UNIT_TYPE(); }
static double gc_get_external_length_units(void *ctx) { return GET_EXTERNAL_LENGTH_UNITS(); }
static double gc_get_external_angle_units(void *ctx) { return GET_EXTERNAL_ANGLE_UNITS(); }
static int32_t gc_get_external_motion_control_mode(void *ctx) { return (int32_t)GET_EXTERNAL_MOTION_CONTROL_MODE(); }
static double gc_get_external_motion_control_tolerance(void *ctx) { return GET_EXTERNAL_MOTION_CONTROL_TOLERANCE(); }
static double gc_get_external_motion_control_naivecam_tolerance(void *ctx) { return GET_EXTERNAL_MOTION_CONTROL_NAIVECAM_TOLERANCE(); }
static int32_t gc_get_external_flood(void *ctx) { return GET_EXTERNAL_FLOOD(); }
static int32_t gc_get_external_mist(void *ctx) { return GET_EXTERNAL_MIST(); }
static double gc_get_external_position_x(void *ctx) { return GET_EXTERNAL_POSITION_X(); }
static double gc_get_external_position_y(void *ctx) { return GET_EXTERNAL_POSITION_Y(); }
static double gc_get_external_position_z(void *ctx) { return GET_EXTERNAL_POSITION_Z(); }
static double gc_get_external_position_a(void *ctx) { return GET_EXTERNAL_POSITION_A(); }
static double gc_get_external_position_b(void *ctx) { return GET_EXTERNAL_POSITION_B(); }
static double gc_get_external_position_c(void *ctx) { return GET_EXTERNAL_POSITION_C(); }
static double gc_get_external_position_u(void *ctx) { return GET_EXTERNAL_POSITION_U(); }
static double gc_get_external_position_v(void *ctx) { return GET_EXTERNAL_POSITION_V(); }
static double gc_get_external_position_w(void *ctx) { return GET_EXTERNAL_POSITION_W(); }
static double gc_get_external_probe_position_x(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_X(); }
static double gc_get_external_probe_position_y(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_Y(); }
static double gc_get_external_probe_position_z(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_Z(); }
static double gc_get_external_probe_position_a(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_A(); }
static double gc_get_external_probe_position_b(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_B(); }
static double gc_get_external_probe_position_c(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_C(); }
static double gc_get_external_probe_position_u(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_U(); }
static double gc_get_external_probe_position_v(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_V(); }
static double gc_get_external_probe_position_w(void *ctx) { return GET_EXTERNAL_PROBE_POSITION_W(); }
static double gc_get_external_probe_value(void *ctx) { return GET_EXTERNAL_PROBE_VALUE(); }
static int32_t gc_get_external_probe_tripped_value(void *ctx) { return GET_EXTERNAL_PROBE_TRIPPED_VALUE(); }
static double gc_get_external_speed(void *ctx, int32_t s) { return GET_EXTERNAL_SPEED(s); }
static int32_t gc_get_external_spindle(void *ctx, int32_t s) { return (int32_t)GET_EXTERNAL_SPINDLE(s); }
static double gc_get_external_tool_length_xoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_XOFFSET(); }
static double gc_get_external_tool_length_yoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_YOFFSET(); }
static double gc_get_external_tool_length_zoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_ZOFFSET(); }
static double gc_get_external_tool_length_aoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_AOFFSET(); }
static double gc_get_external_tool_length_boffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_BOFFSET(); }
static double gc_get_external_tool_length_coffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_COFFSET(); }
static double gc_get_external_tool_length_uoffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_UOFFSET(); }
static double gc_get_external_tool_length_voffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_VOFFSET(); }
static double gc_get_external_tool_length_woffset(void *ctx) { return GET_EXTERNAL_TOOL_LENGTH_WOFFSET(); }
static int32_t gc_get_external_tool_slot(void *ctx) { return GET_EXTERNAL_TOOL_SLOT(); }
static int32_t gc_get_external_selected_tool_slot(void *ctx) { return GET_EXTERNAL_SELECTED_TOOL_SLOT(); }
static int32_t gc_get_external_tool_table(void *ctx, int32_t pocket, int32_t *toolno, double offset[9], double *diameter, double *frontangle, double *backangle, int32_t *orientation) {
    CANON_TOOL_TABLE t = GET_EXTERNAL_TOOL_TABLE(pocket);
    *toolno = t.toolno;
    offset[0] = t.offset.tran.x; offset[1] = t.offset.tran.y; offset[2] = t.offset.tran.z;
    offset[3] = t.offset.a; offset[4] = t.offset.b; offset[5] = t.offset.c;
    offset[6] = t.offset.u; offset[7] = t.offset.v; offset[8] = t.offset.w;
    *diameter = t.diameter; *frontangle = t.frontangle; *backangle = t.backangle;
    *orientation = t.orientation;
    return 0;
}
static int32_t gc_get_external_tc_fault(void *ctx) { return GET_EXTERNAL_TC_FAULT(); }
static int32_t gc_get_external_tc_reason(void *ctx) { return GET_EXTERNAL_TC_REASON(); }
static int32_t gc_get_external_queue_empty(void *ctx) { return GET_EXTERNAL_QUEUE_EMPTY(); }
static int32_t gc_get_external_axis_mask(void *ctx) { return GET_EXTERNAL_AXIS_MASK(); }
static int32_t gc_get_external_digital_input(void *ctx, int32_t index, int32_t def) { return GET_EXTERNAL_DIGITAL_INPUT(index, def); }
static double gc_get_external_analog_input(void *ctx, int32_t index, double def) { return GET_EXTERNAL_ANALOG_INPUT(index, def); }
static int32_t gc_get_external_feed_override_enable(void *ctx) { return GET_EXTERNAL_FEED_OVERRIDE_ENABLE(); }
static int32_t gc_get_external_spindle_override_enable(void *ctx, int32_t s) { return GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE(s); }
static int32_t gc_get_external_adaptive_feed_enable(void *ctx) { return GET_EXTERNAL_ADAPTIVE_FEED_ENABLE(); }
static int32_t gc_get_external_feed_hold_enable(void *ctx) { return GET_EXTERNAL_FEED_HOLD_ENABLE(); }
static int32_t gc_get_external_plane(void *ctx) { return (int32_t)GET_EXTERNAL_PLANE(); }
static void gc_get_external_parameter_file_name(void *ctx, const char **buf) {
    static char filename[LINELEN];
    GET_EXTERNAL_PARAMETER_FILE_NAME(filename, sizeof(filename));
    *buf = filename;
}
static int32_t gc_get_external_offset_applied(void *ctx) { return GET_EXTERNAL_OFFSET_APPLIED(); }
static void gc_get_external_offsets(void *ctx, double offsets[9]) {
    EmcPose o = GET_EXTERNAL_OFFSETS();
    offsets[0] = o.tran.x; offsets[1] = o.tran.y; offsets[2] = o.tran.z;
    offsets[3] = o.a; offsets[4] = o.b; offsets[5] = o.c;
    offsets[6] = o.u; offsets[7] = o.v; offsets[8] = o.w;
}

static const canon_callbacks_t gcodemodule_canon_table = {
    .init_canon = gc_init_canon,
    .set_g5x_offset = gc_set_g5x_offset,
    .set_g92_offset = gc_set_g92_offset,
    .set_xy_rotation = gc_set_xy_rotation,
    .update_end_point = gc_update_end_point,
    .use_length_units = gc_use_length_units,
    .select_plane = gc_select_plane,
    .set_traverse_rate = gc_set_traverse_rate,
    .straight_traverse = gc_straight_traverse,
    .set_feed_rate = gc_set_feed_rate,
    .set_feed_reference = gc_set_feed_reference,
    .set_feed_mode = gc_set_feed_mode,
    .set_motion_control_mode = gc_set_motion_control_mode,
    .set_naivecam_tolerance = gc_set_naivecam_tolerance,
    .set_cutter_radius_compensation = gc_set_cutter_radius_compensation,
    .start_cutter_radius_compensation = gc_start_cutter_radius_compensation,
    .stop_cutter_radius_compensation = gc_stop_cutter_radius_compensation,
    .start_speed_feed_synch = gc_start_speed_feed_synch,
    .stop_speed_feed_synch = gc_stop_speed_feed_synch,
    .arc_feed = gc_arc_feed,
    .straight_feed = gc_straight_feed,
    .nurbs_feed = gc_nurbs_feed,
    .rigid_tap = gc_rigid_tap,
    .straight_probe = gc_straight_probe,
    .stop = gc_stop,
    .dwell = gc_dwell,
    .finish = gc_finish,
    .set_spindle_mode = gc_set_spindle_mode,
    .start_spindle_clockwise = gc_start_spindle_clockwise,
    .start_spindle_counterclockwise = gc_start_spindle_counterclockwise,
    .set_spindle_speed = gc_set_spindle_speed,
    .stop_spindle_turning = gc_stop_spindle_turning,
    .orient_spindle = gc_orient_spindle,
    .wait_spindle_orient_complete = gc_wait_spindle_orient_complete,
    .select_tool = gc_select_tool,
    .start_change = gc_start_change,
    .change_tool = gc_change_tool,
    .change_tool_number = gc_change_tool_number,
    .reload_tooldata = gc_reload_tooldata,
    .set_tool_table_entry = gc_set_tool_table_entry,
    .use_tool_length_offset = gc_use_tool_length_offset,
    .flood_on = gc_flood_on,
    .flood_off = gc_flood_off,
    .mist_on = gc_mist_on,
    .mist_off = gc_mist_off,
    .enable_feed_override = gc_enable_feed_override,
    .disable_feed_override = gc_disable_feed_override,
    .enable_speed_override = gc_enable_speed_override,
    .disable_speed_override = gc_disable_speed_override,
    .enable_feed_hold = gc_enable_feed_hold,
    .disable_feed_hold = gc_disable_feed_hold,
    .enable_adaptive_feed = gc_enable_adaptive_feed,
    .disable_adaptive_feed = gc_disable_adaptive_feed,
    .set_motion_output_bit = gc_set_motion_output_bit,
    .clear_motion_output_bit = gc_clear_motion_output_bit,
    .set_aux_output_bit = gc_set_aux_output_bit,
    .clear_aux_output_bit = gc_clear_aux_output_bit,
    .set_motion_output_value = gc_set_motion_output_value,
    .set_aux_output_value = gc_set_aux_output_value,
    .wait_input = gc_wait_input,
    .clamp_axis = gc_clamp_axis,
    .unclamp_axis = gc_unclamp_axis,
    .lock_rotary = gc_lock_rotary,
    .unlock_rotary = gc_unlock_rotary,
    .program_stop = gc_program_stop,
    .optional_program_stop = gc_optional_program_stop,
    .program_end = gc_program_end,
    .pallet_shuttle = gc_pallet_shuttle,
    .comment = gc_comment,
    .message = gc_message,
    .log_msg = gc_log_msg,
    .logopen = gc_logopen,
    .logappend = gc_logappend,
    .logclose = gc_logclose,
    .canon_error = gc_canon_error,
    .turn_probe_on = gc_turn_probe_on,
    .turn_probe_off = gc_turn_probe_off,
    .set_block_delete = gc_set_block_delete,
    .get_block_delete = gc_get_block_delete,
    .set_optional_program_stop = gc_set_optional_program_stop,
    .get_optional_program_stop = gc_get_optional_program_stop,
    .update_tag = gc_update_tag,
    .set_parameter_file_name = gc_set_parameter_file_name,
    .on_reset = gc_on_reset,
    .get_user_defined_result = gc_get_user_defined_result,
    .get_external_feed_rate = gc_get_external_feed_rate,
    .get_external_traverse_rate = gc_get_external_traverse_rate,
    .get_external_length_unit_type = gc_get_external_length_unit_type,
    .get_external_length_units = gc_get_external_length_units,
    .get_external_angle_units = gc_get_external_angle_units,
    .get_external_motion_control_mode = gc_get_external_motion_control_mode,
    .get_external_motion_control_tolerance = gc_get_external_motion_control_tolerance,
    .get_external_motion_control_naivecam_tolerance = gc_get_external_motion_control_naivecam_tolerance,
    .get_external_flood = gc_get_external_flood,
    .get_external_mist = gc_get_external_mist,
    .get_external_position_x = gc_get_external_position_x,
    .get_external_position_y = gc_get_external_position_y,
    .get_external_position_z = gc_get_external_position_z,
    .get_external_position_a = gc_get_external_position_a,
    .get_external_position_b = gc_get_external_position_b,
    .get_external_position_c = gc_get_external_position_c,
    .get_external_position_u = gc_get_external_position_u,
    .get_external_position_v = gc_get_external_position_v,
    .get_external_position_w = gc_get_external_position_w,
    .get_external_probe_position_x = gc_get_external_probe_position_x,
    .get_external_probe_position_y = gc_get_external_probe_position_y,
    .get_external_probe_position_z = gc_get_external_probe_position_z,
    .get_external_probe_position_a = gc_get_external_probe_position_a,
    .get_external_probe_position_b = gc_get_external_probe_position_b,
    .get_external_probe_position_c = gc_get_external_probe_position_c,
    .get_external_probe_position_u = gc_get_external_probe_position_u,
    .get_external_probe_position_v = gc_get_external_probe_position_v,
    .get_external_probe_position_w = gc_get_external_probe_position_w,
    .get_external_probe_value = gc_get_external_probe_value,
    .get_external_probe_tripped_value = gc_get_external_probe_tripped_value,
    .get_external_speed = gc_get_external_speed,
    .get_external_spindle = gc_get_external_spindle,
    .get_external_tool_length_xoffset = gc_get_external_tool_length_xoffset,
    .get_external_tool_length_yoffset = gc_get_external_tool_length_yoffset,
    .get_external_tool_length_zoffset = gc_get_external_tool_length_zoffset,
    .get_external_tool_length_aoffset = gc_get_external_tool_length_aoffset,
    .get_external_tool_length_boffset = gc_get_external_tool_length_boffset,
    .get_external_tool_length_coffset = gc_get_external_tool_length_coffset,
    .get_external_tool_length_uoffset = gc_get_external_tool_length_uoffset,
    .get_external_tool_length_voffset = gc_get_external_tool_length_voffset,
    .get_external_tool_length_woffset = gc_get_external_tool_length_woffset,
    .get_external_tool_slot = gc_get_external_tool_slot,
    .get_external_selected_tool_slot = gc_get_external_selected_tool_slot,
    .get_external_tool_table = gc_get_external_tool_table,
    .get_external_tc_fault = gc_get_external_tc_fault,
    .get_external_tc_reason = gc_get_external_tc_reason,
    .get_external_queue_empty = gc_get_external_queue_empty,
    .get_external_axis_mask = gc_get_external_axis_mask,
    .get_external_digital_input = gc_get_external_digital_input,
    .get_external_analog_input = gc_get_external_analog_input,
    .get_external_feed_override_enable = gc_get_external_feed_override_enable,
    .get_external_spindle_override_enable = gc_get_external_spindle_override_enable,
    .get_external_adaptive_feed_enable = gc_get_external_adaptive_feed_enable,
    .get_external_feed_hold_enable = gc_get_external_feed_hold_enable,
    .get_external_plane = gc_get_external_plane,
    .get_external_parameter_file_name = gc_get_external_parameter_file_name,
    .get_external_offset_applied = gc_get_external_offset_applied,
    .get_external_offsets = gc_get_external_offsets,
};

#define RESULT_OK (result == INTERP_OK || result == INTERP_EXECUTE_FINISH)
static PyObject *parse_file(PyObject *self, PyObject *args) {
    char *f;
    char *unitcode=0, *initcode=0, *interpname=0;
    PyObject *initcodes=0;
    int error_line_offset = 0;
    struct timeval t0, t1;
    int wait = 1;

    // Ensure tool data mmap is available for the interpreter.
    // Previously this was done implicitly by linuxcnc.stat().poll().
    static bool tool_mmap_tried = false;
    if (!tool_mmap_tried) {
        tool_mmap_tried = true;
        tool_mmap_user();
    }

    if(!PyArg_ParseTuple(args, "sOO!|s:new-parse",
            &f, &callback, &PyList_Type, &initcodes, &interpname))
    {
        initcodes = nullptr;
        PyErr_Clear();
        if(!PyArg_ParseTuple(args, "sO|sss:parse",
                &f, &callback, &unitcode, &initcode, &interpname))
            return NULL;
    }

    if(pinterp) {
        delete pinterp;
        pinterp = 0;
    }
    if(interpname && *interpname)
        pinterp = interp_from_shlib(interpname);
    if(!pinterp)
        pinterp = new Interp;

    for(int i=0; i<USER_DEFINED_FUNCTION_NUM; i++) 
        USER_DEFINED_FUNCTION[i] = user_defined_function;

    gettimeofday(&t0, NULL);

    metric=false;
    interp_error = 0;
    last_sequence_number = -1;

    _pos_x = _pos_y = _pos_z = _pos_a = _pos_b = _pos_c = 0;
    _pos_u = _pos_v = _pos_w = 0;

    pinterp->set_canon_callbacks(&gcodemodule_canon_table);
    pinterp->init();
    pinterp->open(f);

    maybe_new_line();

    int result = INTERP_OK;
    if(initcodes) {
        for(int i=0; i<PyList_Size(initcodes) && RESULT_OK; i++)
        {
            PyObject *item = PyList_GetItem(initcodes, i);
            if(!item) return NULL;
            const char *code = PyUnicode_AsUTF8(item);
            if(!code) return NULL;
            result = pinterp->read(code);
            if(!RESULT_OK) goto out_error;
            result = pinterp->execute();
        }
    }
    if(unitcode && RESULT_OK) {
        result = pinterp->read(unitcode);
        if(!RESULT_OK) goto out_error;
        result = pinterp->execute();
    }

    if(initcode && RESULT_OK) {
        result = pinterp->read(initcode);
        if(!RESULT_OK) goto out_error;
        result = pinterp->execute();
    }

    while(!interp_error && RESULT_OK) {
        error_line_offset = 1;
        result = pinterp->read();
        gettimeofday(&t1, NULL);
        if(t1.tv_sec > t0.tv_sec + wait) {
            if(check_abort()) return NULL;
            t0 = t1;
        }
        if(!RESULT_OK) break;
        error_line_offset = 0;
        result = pinterp->execute();
    }
out_error:
    if(pinterp && !interp_error)
    {
        // Emit a final next_line before closing — must happen while
        // the interpreter is still open so sequence_number() is valid.
        PyErr_Clear();
        maybe_new_line();
        if(PyErr_Occurred()) { interp_error = 1; }
    }
    if(pinterp)
    {
        auto interp = dynamic_cast<Interp*>(pinterp);
        if(interp) interp->_setup.use_lazy_close = false;
        pinterp->close();
    }
    if(interp_error) {
        if(!PyErr_Occurred()) {
            PyErr_Format(PyExc_RuntimeError,
                    "interp_error > 0 but no Python exception set");
        } else {
            // seems a PyErr_Ocurred(), but no exception was set ?
            // so return error info that can be caught and handled
            PyErr_Format(PyExc_RuntimeError,"parse_file interp_error");
            fprintf(stderr,"!!!%s: parse_file() f=%s\n"
                    "!!!interp_error=%d result=%d last_sequence_number=%d\n",
                    __FILE__,f,interp_error,result,last_sequence_number);
        }
        return NULL;
    }
    PyObject *retval = PyTuple_New(2);
    PyTuple_SetItem(retval, 0, PyLong_FromLong(result));
    PyTuple_SetItem(retval, 1, PyLong_FromLong(last_sequence_number + error_line_offset));
    return retval;
}


static int maxerror = -1;

static char savedError[LINELEN+1];
static PyObject *rs274_strerror(PyObject *s, PyObject *o) {
    int err;
    if(!PyArg_ParseTuple(o, "i", &err)) return nullptr;
    pinterp->error_text(err, savedError, LINELEN);
    return PyUnicode_FromString(savedError);
}

static PyObject *rs274_calc_extents(PyObject *self, PyObject *args) {
    double min_x = 9e99, min_y = 9e99, min_z = 9e99,
           min_xt = 9e99, min_yt = 9e99, min_zt = 9e99,
           max_x = -9e99, max_y = -9e99, max_z = -9e99,
           max_xt = -9e99, max_yt = -9e99, max_zt = -9e99;
    for(int i=0; i<PySequence_Length(args); i++) {
        PyObject *si = PyTuple_GetItem(args, i);
        if(!si) return NULL;
        int j;
        double xs, ys, zs, xe, ye, ze, xt, yt, zt;
        for(j=0; j<PySequence_Length(si); j++) {
            PyObject *sj = PySequence_GetItem(si, j);
            PyObject *unused;
            int r;
            if(PyTuple_Size(sj) == 4)
                r = PyArg_ParseTuple(sj,
                    "O(dddOOOOOO)(dddOOOOOO)(ddd):calc_extents item",
                    &unused,
                    &xs, &ys, &zs, &unused, &unused, &unused, &unused, &unused, &unused,
                    &xe, &ye, &ze, &unused, &unused, &unused, &unused, &unused, &unused,
                    &xt, &yt, &zt);
            else
                r = PyArg_ParseTuple(sj,
                    "O(dddOOOOOO)(dddOOOOOO)O(ddd):calc_extents item",
                    &unused,
                    &xs, &ys, &zs, &unused, &unused, &unused, &unused, &unused, &unused,
                    &xe, &ye, &ze, &unused, &unused, &unused, &unused, &unused, &unused,
                    &unused, &xt, &yt, &zt);
            Py_DECREF(sj);
            if(!r) return NULL;
            max_x = std::max(max_x, xs);
            max_y = std::max(max_y, ys);
            max_z = std::max(max_z, zs);
            min_x = std::min(min_x, xs);
            min_y = std::min(min_y, ys);
            min_z = std::min(min_z, zs);
            max_xt = std::max(max_xt, xs+xt);
            max_yt = std::max(max_yt, ys+yt);
            max_zt = std::max(max_zt, zs+zt);
            min_xt = std::min(min_xt, xs+xt);
            min_yt = std::min(min_yt, ys+yt);
            min_zt = std::min(min_zt, zs+zt);
        }
        if(j > 0) {
            max_x = std::max(max_x, xe);
            max_y = std::max(max_y, ye);
            max_z = std::max(max_z, ze);
            min_x = std::min(min_x, xe);
            min_y = std::min(min_y, ye);
            min_z = std::min(min_z, ze);
            max_xt = std::max(max_xt, xe+xt);
            max_yt = std::max(max_yt, ye+yt);
            max_zt = std::max(max_zt, ze+zt);
            min_xt = std::min(min_xt, xe+xt);
            min_yt = std::min(min_yt, ye+yt);
            min_zt = std::min(min_zt, ze+zt);
        }
    }
    return Py_BuildValue("[ddd][ddd][ddd][ddd]",
        min_x, min_y, min_z,  max_x, max_y, max_z,
        min_xt, min_yt, min_zt,  max_xt, max_yt, max_zt);
}

static bool get_attr(PyObject *o, const char *attr_name, int *v) {
    PyObject *attr = PyObject_GetAttrString(o, attr_name);
    if(attr && PyLong_CheckAndError(attr_name, attr)) {
        *v = PyLong_AsLong(attr);
        Py_DECREF(attr);
        return true;
    }
    Py_XDECREF(attr);
    return false;
}

static bool get_attr(PyObject *o, const char *attr_name, double *v) {
    PyObject *attr = PyObject_GetAttrString(o, attr_name);
    if(attr && PyFloat_CheckAndError(attr_name, attr)) {
        *v = PyFloat_AsDouble(attr);
        Py_DECREF(attr);
        return true;
    }
    Py_XDECREF(attr);
    return false;
}

static bool get_attr(PyObject *o, const char *attr_name, const char *fmt, ...) {
    bool result = false;
    va_list ap;
    va_start(ap, fmt);
    PyObject *attr = PyObject_GetAttrString(o, attr_name);
    if(attr) result = PyArg_VaParse(attr, fmt, ap);
    va_end(ap);
    Py_XDECREF(attr);
    return result;
}

static void unrotate(double &x, double &y, double c, double s) {
    double tx = x * c + y * s;
    y = -x * s + y * c;
    x = tx;
}

static void rotate(double &x, double &y, double c, double s) {
    double tx = x * c - y * s;
    y = x * s + y * c;
    x = tx;
}

static PyObject *rs274_arc_to_segments(PyObject *self, PyObject *args) {
    PyObject *canon;
    double x1, y1, cx, cy, z1, a, b, c, u, v, w;
    double o[9], n[9], g5xoffset[9], g92offset[9];
    int rot, plane;
    int X, Y, Z;
    double rotation_cos, rotation_sin;
    int max_segments = 128;

    if(!PyArg_ParseTuple(args, "Oddddiddddddd|i:arcs_to_segments",
        &canon, &x1, &y1, &cx, &cy, &rot, &z1, &a, &b, &c, &u, &v, &w, &max_segments)) return NULL;
    if(!get_attr(canon, "lo", "ddddddddd:arcs_to_segments lo", &o[0], &o[1], &o[2],
                    &o[3], &o[4], &o[5], &o[6], &o[7], &o[8]))
        return NULL;
    if(!get_attr(canon, "plane", &plane)) return NULL;
    if(!get_attr(canon, "rotation_cos", &rotation_cos)) return NULL;
    if(!get_attr(canon, "rotation_sin", &rotation_sin)) return NULL;
    if(!get_attr(canon, "g5x_offset_x", &g5xoffset[0])) return NULL;
    if(!get_attr(canon, "g5x_offset_y", &g5xoffset[1])) return NULL;
    if(!get_attr(canon, "g5x_offset_z", &g5xoffset[2])) return NULL;
    if(!get_attr(canon, "g5x_offset_a", &g5xoffset[3])) return NULL;
    if(!get_attr(canon, "g5x_offset_b", &g5xoffset[4])) return NULL;
    if(!get_attr(canon, "g5x_offset_c", &g5xoffset[5])) return NULL;
    if(!get_attr(canon, "g5x_offset_u", &g5xoffset[6])) return NULL;
    if(!get_attr(canon, "g5x_offset_v", &g5xoffset[7])) return NULL;
    if(!get_attr(canon, "g5x_offset_w", &g5xoffset[8])) return NULL;
    if(!get_attr(canon, "g92_offset_x", &g92offset[0])) return NULL;
    if(!get_attr(canon, "g92_offset_y", &g92offset[1])) return NULL;
    if(!get_attr(canon, "g92_offset_z", &g92offset[2])) return NULL;
    if(!get_attr(canon, "g92_offset_a", &g92offset[3])) return NULL;
    if(!get_attr(canon, "g92_offset_b", &g92offset[4])) return NULL;
    if(!get_attr(canon, "g92_offset_c", &g92offset[5])) return NULL;
    if(!get_attr(canon, "g92_offset_u", &g92offset[6])) return NULL;
    if(!get_attr(canon, "g92_offset_v", &g92offset[7])) return NULL;
    if(!get_attr(canon, "g92_offset_w", &g92offset[8])) return NULL;

    if(plane == 1) {
        X=0; Y=1; Z=2;
    } else if(plane == 3) {
        X=2; Y=0; Z=1;
    } else {
        X=1; Y=2; Z=0;
    }
    n[X] = x1;
    n[Y] = y1;
    n[Z] = z1;
    n[3] = a;
    n[4] = b;
    n[5] = c;
    n[6] = u;
    n[7] = v;
    n[8] = w;
    for(int ax=0; ax<9; ax++) o[ax] -= g5xoffset[ax];
    unrotate(o[0], o[1], rotation_cos, rotation_sin);
    for(int ax=0; ax<9; ax++) o[ax] -= g92offset[ax];

    double theta1 = atan2(o[Y]-cy, o[X]-cx);
    double theta2 = atan2(n[Y]-cy, n[X]-cx);
    /* Issue #1528 1/2/22 andypugh */
    /*_posemath checks for small arcs too, but uses config units */
    double len = hypot(o[X]-n[X], o[Y]-n[Y]) * (25.4 * GET_EXTERNAL_LENGTH_UNITS());
    /* If the signs of the angles differ, make them the same to allow monotonic progress through the arc */
    /* If start and end points are nearly identical, then interpret as a full turn */
    if(rot < 0) { // CW G2
        if (theta1 < theta2) theta2 -= 2*M_PI;
        if (len < CART_FUZZ) theta2 -= 2*M_PI;
    } else { // CCW G3
        if (theta1 > theta2) theta2 += 2*M_PI;
        if (len < CART_FUZZ) theta2 += 2*M_PI;
    }

    // if multi-turn, add the right number of full circles
    if(rot < -1) theta2 += 2*M_PI*(rot+1);
    if(rot > 1) theta2 += 2*M_PI*(rot-1);

    int steps = std::max(3, int(max_segments * fabs(theta1 - theta2) / M_PI));
    double rsteps = 1. / steps;
    PyObject *segs = PyList_New(steps);

    double dtheta = theta2 - theta1;
    double d[9] = {0, 0, 0, n[3]-o[3], n[4]-o[4], n[5]-o[5], n[6]-o[6], n[7]-o[7], n[8]-o[8]};
    d[Z] = n[Z] - o[Z];

    double tx = o[X] - cx, ty = o[Y] - cy, dc = cos(dtheta*rsteps), ds = sin(dtheta*rsteps);
    for(int i=0; i<steps-1; i++) {
        double f = (i+1) * rsteps;
        double p[9];
        rotate(tx, ty, dc, ds);
        p[X] = tx + cx;
        p[Y] = ty + cy;
        p[Z] = o[Z] + d[Z] * f;
        p[3] = o[3] + d[3] * f;
        p[4] = o[4] + d[4] * f;
        p[5] = o[5] + d[5] * f;
        p[6] = o[6] + d[6] * f;
        p[7] = o[7] + d[7] * f;
        p[8] = o[8] + d[8] * f;
        for(int ax=0; ax<9; ax++) p[ax] += g92offset[ax];
        rotate(p[0], p[1], rotation_cos, rotation_sin);
        for(int ax=0; ax<9; ax++) p[ax] += g5xoffset[ax];
        PyList_SET_ITEM(segs, i,
            Py_BuildValue("ddddddddd", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]));
    }
    for(int ax=0; ax<9; ax++) n[ax] += g92offset[ax];
    rotate(n[0], n[1], rotation_cos, rotation_sin);
    for(int ax=0; ax<9; ax++) n[ax] += g5xoffset[ax];
    PyList_SET_ITEM(segs, steps-1,
        Py_BuildValue("ddddddddd", n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8]));
    return segs;
}

static PyMethodDef gcode_methods[] = {
    {"parse", (PyCFunction)parse_file, METH_VARARGS, "Parse a G-Code file"},
    {"strerror", (PyCFunction)rs274_strerror, METH_VARARGS,
        "Convert a numeric error to a string"},
    {"calc_extents", (PyCFunction)rs274_calc_extents, METH_VARARGS,
        "Calculate information about extents of gcode"},
    {"arc_to_segments", (PyCFunction)rs274_arc_to_segments, METH_VARARGS,
        "Convert an arc to straight segments"},
    {NULL}
};

static struct PyModuleDef gcode_moduledef = {
    PyModuleDef_HEAD_INIT,                    /* m_base    */
    "gcode",                                  /* m_name    */
    "Interface to EMC rs274ngc interpreter",  /* m_doc     */
    -1,                                       /* m_size    */
    gcode_methods                             /* m_methods */
};

PyMODINIT_FUNC PyInit_gcode(void);
PyMODINIT_FUNC PyInit_gcode(void)
{

    PyObject *m = PyModule_Create(&gcode_moduledef);
    PyType_Ready(&LineCodeType);
    PyModule_AddObject(m, "linecode", (PyObject*)&LineCodeType);
    PyObject_SetAttrString(m, "MAX_ERROR", PyLong_FromLong(maxerror));
    PyObject_SetAttrString(m, "MIN_ERROR",
            PyLong_FromLong(INTERP_MIN_ERROR));
    return m;
}
// vim:ts=8:sts=4:sw=4:et:
