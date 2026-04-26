"""gmi.constants — Backward-compatible flat constants matching linuxcnc.*

These constants match the values from the IDL enum definitions in
emcstat.gmi, emccmd.gmi, and emcerror.gmi. They're provided for
easy migration — replace `linuxcnc.MODE_MANUAL` with `MODE_MANUAL`
after `from gmi.constants import *`.

For typed enums, import directly from the generated client modules:
    from gmi.emcstat_client import TaskMode, TaskState, InterpState
"""

# ─── Task Mode (emcstat.gmi: TaskMode) ───
MODE_MANUAL = 1
MODE_AUTO = 2
MODE_MDI = 3

# ─── Task State (emcstat.gmi: TaskState) ───
STATE_ESTOP = 1
STATE_ESTOP_RESET = 2
STATE_OFF = 3
STATE_ON = 4

# ─── Interpreter State (emcstat.gmi: InterpState) ───
INTERP_IDLE = 1
INTERP_READING = 2
INTERP_PAUSED = 3
INTERP_WAITING = 4

# ─── Exec State (emcstat.gmi: ExecState) ───
TASK_EXEC_ERROR = 1
TASK_EXEC_DONE = 2
TASK_EXEC_WAITING_FOR_MOTION = 3
TASK_EXEC_WAITING_FOR_MOTION_QUEUE = 4
TASK_EXEC_WAITING_FOR_IO = 5
TASK_EXEC_WAITING_FOR_MOTION_AND_IO = 7
TASK_EXEC_WAITING_FOR_DELAY = 8
TASK_EXEC_WAITING_FOR_SYSTEM_CMD = 9
TASK_EXEC_WAITING_FOR_SPINDLE_ORIENTED = 10

# ─── Trajectory Mode (emcstat.gmi: TrajMode) ───
TRAJ_MODE_FREE = 1
TRAJ_MODE_COORD = 2
TRAJ_MODE_TELEOP = 3

# ─── Kinematics Type (emcstat.gmi: KinematicsType) ───
KINEMATICS_IDENTITY = 1
KINEMATICS_FORWARD_ONLY = 2
KINEMATICS_INVERSE_ONLY = 3
KINEMATICS_BOTH = 4

# ─── Auto Commands (emccmd.gmi: AutoCmd) ───
AUTO_RUN = 0
AUTO_PAUSE = 1
AUTO_RESUME = 2
AUTO_STEP = 3
AUTO_REVERSE = 4
AUTO_FORWARD = 5

# ─── Jog Type (emccmd.gmi: JogType) ───
JOG_STOP = 0
JOG_CONTINUOUS = 1
JOG_INCREMENT = 2

# ─── Spindle Command (emccmd.gmi: SpindleCmd) ───
SPINDLE_OFF = 0
SPINDLE_FORWARD = 1
SPINDLE_REVERSE = -1
SPINDLE_INCREASE = 10
SPINDLE_DECREASE = 11
SPINDLE_CONSTANT = 12

# ─── Error Kind (emcerror.gmi: ErrorKind) ───
NML_ERROR = 1
NML_TEXT = 2
NML_DISPLAY = 3
OPERATOR_ERROR = 11
OPERATOR_TEXT = 12
OPERATOR_DISPLAY = 13

# ─── Size Constants (emcstat.gmi) ───
MAX_JOINTS = 16
MAX_AXIS = 9

# ─── RCS Status (for wait_complete) ───
RCS_DONE = 1
RCS_EXEC = 2
RCS_ERROR = 3
