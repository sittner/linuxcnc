// IO task interface declarations — free functions that send NML to iocontrol.
// The former Task class / TaskWrap Python override mechanism has been removed.
#ifndef TASKCLASS_HH
#define TASKCLASS_HH

#include "emc.hh"

extern int emcIoInit();
extern int emcIoHalt();
extern int emcIoAbort(int reason);
extern int emcToolStartChange();
extern int emcAuxEstopOn();
extern int emcAuxEstopOff();
extern int emcCoolantMistOn();
extern int emcCoolantMistOff();
extern int emcCoolantFloodOn();
extern int emcCoolantFloodOff();
extern int emcLubeOn();
extern int emcLubeOff();
extern int emcIoSetDebug(int debug);
extern int emcToolSetOffset(int pocket, int toolno, EmcPose offset, double diameter,
			    double frontangle, double backangle, int orientation);
extern int emcToolPrepare(int tool);
extern int emcToolLoad();
extern int emcToolLoadToolTable(const char *file);
extern int emcToolUnload();
extern int emcToolSetNumber(int number);
extern int emcIoUpdate(EMC_IO_STAT * stat);

#endif
