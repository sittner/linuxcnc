/********************************************************************
* Description: emctask.cc
*   Mode and state management for EMC_TASK class
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
* Last change:
********************************************************************/

#include <stdlib.h>
#include <rtapi_string.h>	// rtapi_strlcpy()
#include <sys/stat.h>		// struct stat
#include <unistd.h>		// stat()
#include <limits.h>		// PATH_MAX
#include <dlfcn.h>

#include "rcs.hh"		// INIFILE
#include "emc.hh"		// EMC NML
#include "emc_nml.hh"
#include "emcglb.h"		// EMC_INIFILE
#include "interpl.hh"		// NML_INTERP_LIST, interp_list
#include "canon.hh"		// CANON_VECTOR, GET_PROGRAM_ORIGIN()
#include "rs274ngc_interp.hh"	// the interpreter
#include "interp_return.hh"	// INTERP_FILE_NOT_OPEN
#include "inifile.hh"
#include "rcs_print.hh"
#include "task.hh"		// emcTaskCommand etc
#include "taskclass.hh"
#include "motion.h"
#include "emccanon_table.hh"

/* flag for how we want to interpret traj coord mode, as mdi or auto */
static int mdiOrAuto = EMC_TASK_MODE_AUTO;

InterpBase *pinterp=0;
#define interp (*pinterp)
setup_pointer _is = 0; // helper for gdb hardware watchpoints FIXME


// Print error messages thrown by interpreter
static char interp_error_text_buf[LINELEN];
static char interp_stack_buf[LINELEN];

static void print_interp_error(int retval)
{
    int index = 0;
    if (retval == 0) {
	return;
    }

    if (0 != emcStatus) {
	emcStatus->task.interpreter_errcode = retval;
    }

    interp_error_text_buf[0] = 0;
    interp.error_text(retval, interp_error_text_buf, LINELEN);
    if (0 != interp_error_text_buf[0]) {
	rcs_print_error("interp_error: %s\n", interp_error_text_buf);
    }
    emcOperatorError(0, "%s", interp_error_text_buf);
    index = 0;
    if (emc_debug & EMC_DEBUG_INTERP) {
	rcs_print("Interpreter stack: \t");
	while (index < 5) {
	    interp_stack_buf[0] = 0;
	    interp.stack_name(index, interp_stack_buf, LINELEN);
	    if (0 == interp_stack_buf[0]) {
		break;
	    }
	    rcs_print(" - %s ", interp_stack_buf);
	    index++;
	}
	rcs_print("\n");
    }
}

// USER_DEFINED_FUNCTION callback — creates an EMC_MCODE_CMD and appends
// it to the interpreter queue. num is 0-99, corresponding to M100-M199.
void user_defined_add_m_code(int num, double arg1, double arg2)
{
    EMC_MCODE_CMD mcode_cmd;

    // flush any linked motions before the M1xx call
    emccanon_get_callbacks()->finish(NULL);
    mcode_cmd.mcode = num + 100;
    mcode_cmd.p_number = arg1;
    mcode_cmd.q_number = arg2;
    interp_list.append(mcode_cmd);
}

int emcTaskInit()
{
    // No script scanning — M100-M199 handlers are registered by cmods
    // via mcode_handler_api. The USER_DEFINED_FUNCTION slots are populated
    // when handlers register (in mcode_api_register_handler).
    return 0;
}

int emcTaskHalt()
{
    return 0;
}

int emcTaskStateRestore()
{
    int res = 0;
    // Do NOT restore on MDI command
    if (emcStatus->task.mode == EMC_TASK_MODE_AUTO) {
        // Validity of state tag checked within restore function
        res = pinterp->restore_from_tag(emcStatus->motion.traj.tag);
    }
    return res;
}

int emcTaskAbort()
{
    emcMotionAbort();

    // clear out the pending command
    emcTaskCommand = 0;
    interp_list.clear();

    // clear out the interpreter state
    emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
    emcStatus->task.task_paused = 0;
    emcStatus->task.motionLine = 0;
    emcStatus->task.readLine = 0;
    emcStatus->task.command[0] = 0;
    emcStatus->task.callLevel = 0;

    stepping = 0;
    steppingWait = 0;

    // now queue up command to resynch interpreter
    EMC_TASK_PLAN_SYNCH taskPlanSynchCmd;
    emcTaskQueueCommand(&taskPlanSynchCmd);

    // without emcTaskPlanClose(), a new run command resumes at
    // aborted line-- feature that may be considered later
    {
	int was_open = taskplanopen;
	emcTaskPlanClose();
        emcTaskPlanReset();  // Flush any unflushed segments
	if (emc_debug & EMC_DEBUG_INTERP && was_open) {
	    rcs_print("emcTaskPlanClose() called at %s:%d\n", __FILE__,
		      __LINE__);
	}
    }

    return 0;
}

int emcTaskSetMode(int mode)
{
    int retval = 0;

    if (jogging_is_active()) {
        emcOperatorError(0, "Ignoring task mode change while jogging");
        return 0;
    }

    switch (mode) {
    case EMC_TASK_MODE_MANUAL:
	// go to manual mode
        if (all_homed()) {
            emcTrajSetMode(EMC_TRAJ_MODE_TELEOP);
        } else {
            emcTrajSetMode(EMC_TRAJ_MODE_FREE);
        }
	mdiOrAuto = EMC_TASK_MODE_AUTO;	// we'll default back to here
	break;

    case EMC_TASK_MODE_MDI:
	// go to mdi mode
	emcTrajSetMode(EMC_TRAJ_MODE_COORD);
	emcTaskAbort();
	emcTaskPlanSynch();
	mdiOrAuto = EMC_TASK_MODE_MDI;
	break;

    case EMC_TASK_MODE_AUTO:
	// go to auto mode
	emcTrajSetMode(EMC_TRAJ_MODE_COORD);
	emcTaskAbort();
	emcTaskPlanSynch();
	mdiOrAuto = EMC_TASK_MODE_AUTO;
	break;

    default:
	retval = -1;
	break;
    }

    return retval;
}

int emcTaskSetState(int state)
{
    int t;
    int retval = 0;

    switch (state) {
    case EMC_TASK_STATE_OFF:
        emcMotionAbort();
	// turn the machine servos off-- go into READY state
    for (t = 0; t < emcStatus->motion.traj.spindles; t++)  emcSpindleAbort(t);
	for (t = 0; t < emcStatus->motion.traj.joints; t++) {
	    emcJointDisable(t);
	}
	emcTrajDisable();
	emcIoAbort(EMC_ABORT_TASK_STATE_OFF);
	emcLubeOff();
	emcTaskAbort();
    emcJointUnhome(-2); // only those joints which are volatile_home
	emcAbortCleanup(EMC_ABORT_TASK_STATE_OFF);
	emcTaskPlanSynch();
	break;

    case EMC_TASK_STATE_ON:
	// turn the machine servos on
	emcTrajEnable();
	for (t = 0; t < emcStatus->motion.traj.joints; t++){
		emcJointEnable(t);
	}
	emcLubeOn();
	break;

    case EMC_TASK_STATE_ESTOP_RESET:
	// reset the estop
	emcAuxEstopOff();
	emcLubeOff();
	emcTaskAbort();
        emcIoAbort(EMC_ABORT_TASK_STATE_ESTOP_RESET);
    for (t = 0; t < emcStatus->motion.traj.spindles; t++) emcSpindleAbort(t);
	emcAbortCleanup(EMC_ABORT_TASK_STATE_ESTOP_RESET);
	emcTaskPlanSynch();
	break;

    case EMC_TASK_STATE_ESTOP:
        emcMotionAbort();
	for (t = 0; t < emcStatus->motion.traj.spindles; t++) emcSpindleAbort(t);
	// go into estop-- do both IO estop and machine servos off
	emcAuxEstopOn();
	for (t = 0; t < emcStatus->motion.traj.joints; t++) {
	    emcJointDisable(t);
	}
	emcTrajDisable();
	emcLubeOff();
	emcTaskAbort();
        emcIoAbort(EMC_ABORT_TASK_STATE_ESTOP);
	for (t = 0; t < emcStatus->motion.traj.spindles; t++) emcSpindleAbort(t);
        emcJointUnhome(-2); // only those joints which are volatile_home
	emcAbortCleanup(EMC_ABORT_TASK_STATE_ESTOP);
	emcTaskPlanSynch();
	break;

    default:
	retval = -1;
	break;
    }

    return retval;
}

// WM access functions

/*
  determineMode()

  Looks at mode of subsystems, and returns associated mode

  Depends on traj mode, and mdiOrAuto flag

  traj mode   mdiOrAuto     task mode
  ---------   ---------     ---------
  FREE        XXX           MANUAL
  TELEOP      XXX           MANUAL
  COORD       MDI           MDI
  COORD       AUTO          AUTO
  */
static int determineMode()
{
    if (emcStatus->motion.traj.mode == EMC_TRAJ_MODE_FREE) {
        return EMC_TASK_MODE_MANUAL;
    }
    if (emcStatus->motion.traj.mode == EMC_TRAJ_MODE_TELEOP) {
        return EMC_TASK_MODE_MANUAL;
    }
    // for EMC_TRAJ_MODE_COORD
    return mdiOrAuto;
}

/*
  determineState()

  Looks at state of subsystems, and returns associated state

  Depends on traj enabled, io estop, and desired task state

  traj enabled   io estop      state
  ------------   --------      -----
  DISABLED       ESTOP         ESTOP
  ENABLED        ESTOP         ESTOP
  DISABLED       OUT OF ESTOP  ESTOP_RESET
  ENABLED        OUT OF ESTOP  ON
  */
static int determineState()
{
    if (emcStatus->io.aux.estop) {
	return EMC_TASK_STATE_ESTOP;
    }

    if (!emcStatus->motion.traj.enabled) {
	return EMC_TASK_STATE_ESTOP_RESET;
    }

    return EMC_TASK_STATE_ON;
}

static int waitFlag = 0;

int emcTaskPlanCreate()
{
    if(!pinterp) {
	IniFile inifile;
	const char *inistring;
	inifile.Open(emc_inifile);
	if((inistring = inifile.Find("INTERPRETER", "TASK"))) {
	    pinterp = interp_from_shlib(inistring);
	    fprintf(stderr, "interp_from_shlib() -> %p\n", pinterp);
            if (!pinterp) {
                fprintf(stderr, "failed to load [TASK]INTERPRETER (%s)\n", inistring);
                return -1;
            }
	}
        inifile.Close();
    }
    if(!pinterp) {
        pinterp = new Interp;
    }

    Interp *i = dynamic_cast<Interp*>(pinterp);
    if(i) {
        _is = &i->_setup; // FIXME
        i->set_canon_callbacks(emccanon_get_callbacks());
        i->_setup.task_mode = 1;
    }
    else  _is = 0;
    interp.ini_load(emc_inifile);
    return 0;
}

int emcTaskPlanInit()
{
    if(!pinterp) {
        if (emcTaskPlanCreate() != 0)
            return -1;
    }

    waitFlag = 0;

    int retval = interp.init();
    // In task, enable M99 main program endless looping
    interp.set_loop_on_main_m99(true);
    if (retval > INTERP_MIN_ERROR) {  // I'd think this should be fatal.
	print_interp_error(retval);
    } else {
	if (0 != rs274ngc_startup_code[0]) {
	    retval = interp.execute(rs274ngc_startup_code);
	    while (retval == INTERP_EXECUTE_FINISH) {
		retval = interp.execute(0);
	    }
	    if (retval > INTERP_MIN_ERROR) {
		print_interp_error(retval);
	    }
	}
    }

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanInit() returned %d\n", retval);
    }

    return retval;
}

int emcTaskPlanSetWait()
{
    waitFlag = 1;

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanSetWait() called\n");
    }

    return 0;
}

int emcTaskPlanIsWait()
{
    return waitFlag;
}

int emcTaskPlanClearWait()
{
    waitFlag = 0;

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanClearWait() called\n");
    }

    return 0;
}

int emcTaskPlanSetOptionalStop(bool state)
{
    emccanon_get_callbacks()->set_optional_program_stop(NULL, state ? 1 : 0);
    return 0;
}

int emcTaskPlanSetBlockDelete(bool state)
{
    emccanon_get_callbacks()->set_block_delete(NULL, state ? 1 : 0);
    return 0;
}


int emcTaskPlanSynch()
{
    int retval = interp.synch();
    if (retval == INTERP_ERROR) {
        emcTaskAbort();
    }

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanSynch() returned %d\n", retval);
    }

    return retval;
}

void emcTaskPlanExit()
{
    if (pinterp != NULL) {
        interp.exit();
    }
}

int emcTaskPlanOpen(const char *file)
{
    if (emcStatus != 0) {
	emcStatus->task.motionLine = 0;
	emcStatus->task.currentLine = 0;
	emcStatus->task.readLine = 0;
    }

    int retval = interp.open(file);
    if (retval > INTERP_MIN_ERROR) {
	print_interp_error(retval);
	return retval;
    }
    taskplanopen = 1;

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanOpen(%s) returned %d\n", file, retval);
    }

    return retval;
}


int emcTaskPlanRead()
{
    int retval = interp.read();
    if (retval == INTERP_FILE_NOT_OPEN) {
	if (emcStatus->task.file[0] != 0) {
	    retval = interp.open(emcStatus->task.file);
	    if (retval > INTERP_MIN_ERROR) {
		print_interp_error(retval);
	    }
	    retval = interp.read();
	}
    }
    if (retval > INTERP_MIN_ERROR) {
	print_interp_error(retval);
    }
    
    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanRead() returned %d\n", retval);
    }
    
    return retval;
}

int emcTaskPlanExecute(const char *command)
{
    int inpos = emcStatus->motion.traj.inpos;	// 1 if in position, 0 if not.

    if (command != 0) {		// Command is 0 if in AUTO mode, non-null if in MDI mode.
	// Don't sync if not in position.
	if ((*command != 0) && (inpos)) {
	    interp.synch();
	}
    }
    int retval = interp.execute(command);
    if (retval > INTERP_MIN_ERROR) {
	print_interp_error(retval);
    }
    if(command != 0) {
	emccanon_get_callbacks()->finish(NULL);
    }

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanExecute(0) return %d\n", retval);
    }

    return retval;
}

int emcTaskPlanExecute(const char *command, int line_number)
{
    int retval = interp.execute(command, line_number);
    if (retval > INTERP_MIN_ERROR) {
	print_interp_error(retval);
    }
    if(command != 0) { // this means MDI
	emccanon_get_callbacks()->finish(NULL);
    }

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanExecute(%s) returned %d\n", command, retval);
    }

    return retval;
}

int emcTaskPlanClose()
{
    int retval = interp.close();
    if (retval > INTERP_MIN_ERROR) {
	print_interp_error(retval);
    }

    taskplanopen = 0;
    return retval;
}

int emcTaskPlanReset()
{
    int retval = interp.reset();
    if (retval > INTERP_MIN_ERROR) {
	print_interp_error(retval);
    }

    return retval;
}

int emcTaskPlanLine()
{
    int retval = interp.line();
    
    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanLine() returned %d\n", retval);
    }

    return retval;
}

int emcTaskPlanLevel()
{
    int retval = interp.call_level();

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanLevel() returned %d\n", retval);
    }

    return retval;
}

int emcTaskPlanCommand(char *cmd)
{
    char buf[LINELEN];

    strcpy(cmd, interp.command(buf, LINELEN));

    if (emc_debug & EMC_DEBUG_INTERP) {
        rcs_print("emcTaskPlanCommand(%s) called. (line_number=%d)\n",
          cmd, emcStatus->task.readLine);
    }

    return 0;
}

int emcTaskUpdate(EMC_TASK_STAT * stat)
{
    stat->mode = (enum EMC_TASK_MODE_ENUM) determineMode();
    int oldstate = stat->state;
    stat->state = (enum EMC_TASK_STATE_ENUM) determineState();

    if(oldstate == EMC_TASK_STATE_ON && oldstate != stat->state) {
	emcTaskAbort();
    for (int s = 0; s < emcStatus->motion.traj.spindles; s++) emcSpindleAbort(s);
        emcIoAbort(EMC_ABORT_TASK_STATE_NOT_ON);
	emcAbortCleanup(EMC_ABORT_TASK_STATE_NOT_ON);
    }

    // execState set in main
    // interpState set in main
    if (emcStatus->motion.traj.id > 0) {
	stat->motionLine = emcStatus->motion.traj.id;
    }
    // currentLine set in main
    // readLine set in main

    char buf[LINELEN];
    rtapi_strxcpy(stat->file, interp.file(buf, LINELEN));
    // command set in main

    // update active G and M codes
    // Start by assuming that we can't unpack a state tag from motion
    int res_state = INTERP_ERROR;
    if (emcStatus->task.interpState != EMC_TASK_INTERP_IDLE) {
        res_state = interp.active_modes(&stat->activeGCodes[0],
					&stat->activeMCodes[0],
					&stat->activeSettings[0],
					emcStatus->motion.traj.tag);
    } 
    // If we get an error from trying to unpack from the motion state, always
    // use interp's internal state, so the active state is never out of date
    if (emcStatus->task.mode != EMC_TASK_MODE_AUTO ||
	res_state == INTERP_ERROR) {
        interp.active_g_codes(&stat->activeGCodes[0]);
        interp.active_m_codes(&stat->activeMCodes[0]);
        interp.active_settings(&stat->activeSettings[0]);
    }

    //update state of optional stop
    stat->optional_stop_state = emccanon_get_callbacks()->get_optional_program_stop(NULL);
    
    //update state of block delete
    stat->block_delete_state = emccanon_get_callbacks()->get_block_delete(NULL);
    
    stat->heartbeat++;

    return 0;
}

int emcAbortCleanup(int reason, const char *message)
{
    int status = interp.on_abort(reason,message);
    if (status > INTERP_MIN_ERROR)
	print_interp_error(status);
    return status;
}

