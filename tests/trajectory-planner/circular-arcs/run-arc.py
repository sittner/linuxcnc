#!/usr/bin/env python3
# Drive the arc.ngc program to completion via the gmi REST client.
import gmi
from gmi.constants import *
import sys, time

c = gmi.Command()
s = gmi.Stat()

c.state(STATE_ESTOP_RESET)
c.state(STATE_ON)
c.home(-1)
if c.wait_complete(60) == -1:
    sys.exit("run-arc: homing did not complete within 60s")

c.mode(MODE_AUTO)
c.program_open('arc.ngc')
c.auto(AUTO_RUN, 0)

# Wait for the program to actually START (leave IDLE) before waiting for it to
# finish, else the pre-run IDLE state ends the wait immediately.  FAIL if it
# never starts: silently falling through here let the finish-wait see the
# still-idle interp and exit 0 with zero motion captured (the old 5s window
# expired on loaded CI runners).
t = time.time()
started = False
while time.time() - t < 60:
    s.poll()
    if s.interp_state != INTERP_IDLE:
        started = True
        break
    time.sleep(0.02)
if not started:
    sys.exit("run-arc: program never started (interp stayed IDLE for 60s)")

# Now wait for it to run the full arc back to idle.
t = time.time()
finished = False
while time.time() - t < 120:
    s.poll()
    if s.interp_state == INTERP_IDLE:
        finished = True
        break
    time.sleep(0.05)
if not finished:
    sys.exit("run-arc: program did not finish within 120s")

c.wait_complete()
time.sleep(0.3)   # let the final servo cycles capture
sys.exit(0)
