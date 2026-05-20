// IO task interface — GMI-based communication with iocontrol
//
// milltask calls the emcio GMI API (registered by iocontrol cmod)
// for tool changes, estop, coolant, lube, etc.
// Replaces the former NML-based implementation.

#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "rcs.hh"		// RCS_DONE etc. (status enum)
#include "emc.hh"		// EMC_IO_STAT
#include "emc_nml.hh"		// EMC_IO_STAT full definition
#include "emcglb.h"		// EMC_INIFILE
#include "inifile.hh"
#include "rcs_print.hh"

#include "gomc/generated/gmi/emcio/emcio_api.h"

// IO INTERFACE

// The GMI callback table — obtained from iocontrol via api registry
static const emcio_callbacks_t *emcio = NULL;

// IO instance name for lookup (set from milltask parameter).
const char *taskclass_iocontrol_instance = "iocontrol";

// gomc API pointer — set in emctaskmain_gomc.cc
extern const gomc_api_t *gomc_api_ptr;

// IO config from INI
static int use_iocontrol = 0;

// set the have_tool_change_position global
static int readToolChange(IniFile *toolInifile)
{
    int retval = 0;
    const char *inistring;

    if (NULL !=
	(inistring = toolInifile->Find("TOOL_CHANGE_POSITION", "EMCIO"))) {
        if (9 == sscanf(inistring, "%lf %lf %lf %lf %lf %lf %lf %lf %lf",
                        &tool_change_position.tran.x,
                        &tool_change_position.tran.y,
                        &tool_change_position.tran.z,
                        &tool_change_position.a,
                        &tool_change_position.b,
                        &tool_change_position.c,
                        &tool_change_position.u,
                        &tool_change_position.v,
                        &tool_change_position.w)) {
            have_tool_change_position=9;
            retval=0;
        } else if (6 == sscanf(inistring, "%lf %lf %lf %lf %lf %lf",
                        &tool_change_position.tran.x,
                        &tool_change_position.tran.y,
                        &tool_change_position.tran.z,
                        &tool_change_position.a,
                        &tool_change_position.b,
                        &tool_change_position.c)) {
	    tool_change_position.u = 0.0;
	    tool_change_position.v = 0.0;
	    tool_change_position.w = 0.0;
            have_tool_change_position = 6;
            retval = 0;
        } else if (3 == sscanf(inistring, "%lf %lf %lf",
                               &tool_change_position.tran.x,
                               &tool_change_position.tran.y,
                               &tool_change_position.tran.z)) {
	    tool_change_position.a = 0.0;
	    tool_change_position.b = 0.0;
	    tool_change_position.c = 0.0;
	    tool_change_position.u = 0.0;
	    tool_change_position.v = 0.0;
	    tool_change_position.w = 0.0;
	    have_tool_change_position = 3;
	    retval = 0;
	} else {
	    rcs_print("bad format for TOOL_CHANGE_POSITION\n");
	    have_tool_change_position = 0;
	    retval = -1;
	}
    } else {
	have_tool_change_position = 0;
    }
    return retval;
}

static int iniTool(const char *filename)
{
    int retval = 0;
    IniFile toolInifile;

    if (toolInifile.Open(filename) == false) {
	return -1;
    }
    if (0 != readToolChange(&toolInifile)) {
	retval = -1;
    }
    toolInifile.Close();

    return retval;
}

// Initialize task IO — read INI config
int emcTaskOnce(const char * /*filename*/)
{
    IniFile inifile;

    if (inifile.Open(emc_inifile)) {
	use_iocontrol = (inifile.Find("EMCIO", "EMCIO") != NULL);
    }
    return 0;
}

// --- Lifecycle ---

int emcIoInit()
{
    if (0 != iniTool(emc_inifile)) {
	return -1;
    }

    // Look up the emcio API registered by iocontrol
    emcio = emcio_api_get(gomc_api_ptr, taskclass_iocontrol_instance);
    if (!emcio) {
	rcs_print_error("emcIoInit: emcio API not available (instance '%s', iocontrol not started?)\n", taskclass_iocontrol_instance);
	return -1;
    }
    extern const char *milltask_instance_name;
    gomc_api_ptr->record_consumer(gomc_api_ptr->ctx, milltask_instance_name, "emcio", taskclass_iocontrol_instance);

    return 0;
}

int emcIoHalt()
{
    emcio = NULL;
    return 0;
}

// --- Commands ---

int emcIoAbort(int reason)
{
    if (!emcio) return -1;
    return emcio->io_abort(emcio->ctx, reason);
}

int emcIoSetDebug(int debug)
{
    if (!emcio) return -1;
    return emcio->set_debug(emcio->ctx, debug);
}

int emcAuxEstopOn()
{
    if (!emcio) return -1;
    return emcio->estop_on(emcio->ctx);
}

int emcAuxEstopOff()
{
    if (!emcio) return -1;
    return emcio->estop_off(emcio->ctx);
}

int emcCoolantMistOn()
{
    if (!emcio) return -1;
    return emcio->coolant_mist_on(emcio->ctx);
}

int emcCoolantMistOff()
{
    if (!emcio) return -1;
    return emcio->coolant_mist_off(emcio->ctx);
}

int emcCoolantFloodOn()
{
    if (!emcio) return -1;
    return emcio->coolant_flood_on(emcio->ctx);
}

int emcCoolantFloodOff()
{
    if (!emcio) return -1;
    return emcio->coolant_flood_off(emcio->ctx);
}

int emcLubeOn()
{
    if (!emcio) return -1;
    return emcio->lube_on(emcio->ctx);
}

int emcLubeOff()
{
    if (!emcio) return -1;
    return emcio->lube_off(emcio->ctx);
}

int emcToolPrepare(int tool)
{
    if (!emcio) return -1;
    return emcio->tool_prepare(emcio->ctx, tool);
}

int emcToolStartChange()
{
    if (!emcio) return -1;
    return emcio->tool_start_change(emcio->ctx);
}

int emcToolLoad()
{
    if (!emcio) return -1;
    return emcio->tool_load(emcio->ctx);
}

int emcToolUnload()
{
    if (!emcio) return -1;
    return emcio->tool_unload(emcio->ctx);
}

int emcToolLoadToolTable(const char *file)
{
    if (!emcio) return -1;
    return emcio->tool_load_table(emcio->ctx, file);
}

int emcToolSetOffset(int pocket, int toolno, EmcPose offset, double diameter,
                     double frontangle, double backangle, int orientation)
{
    if (!emcio) return -1;
    return emcio->tool_set_offset(emcio->ctx,
        pocket, toolno,
        offset.tran.x, offset.tran.y, offset.tran.z,
        offset.a, offset.b, offset.c,
        offset.u, offset.v, offset.w,
        diameter, frontangle, backangle, orientation);
}

int emcToolSetNumber(int number)
{
    if (!emcio) return -1;
    return emcio->tool_set_number(emcio->ctx, number);
}

// --- Status ---

int emcIoUpdate(EMC_IO_STAT * stat)
{
    if (!use_iocontrol) {
	return 0;
    }
    if (!emcio) {
	return -1;
    }

    emcio_io_status_t s = emcio->get_status(emcio->ctx);

    // Map GMI status to EMC_IO_STAT
    stat->heartbeat = s.heartbeat;
    stat->status = RCS_DONE;  // commands are synchronous now
    stat->reason = s.reason;
    stat->fault = s.fault;

    stat->tool.pocketPrepped = s.tool.pocket_prepped;
    stat->tool.toolInSpindle = s.tool.tool_in_spindle;
    stat->tool.toolFromPocket = s.tool.tool_from_pocket;

    stat->coolant.mist = s.coolant.mist;
    stat->coolant.flood = s.coolant.flood;

    stat->aux.estop = s.estop ? 1 : 0;

    stat->lube.on = s.lube_on;
    stat->lube.level = s.lube_level;

    stat->debug = s.debug;

    return 0;
}
