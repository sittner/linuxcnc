/********************************************************************
* Description: cmd_msg.cc
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: LGPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
* Last change: 
********************************************************************/

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#ifdef __cplusplus
}
#endif
#include "nml.hh"
#include "nmlmsg.hh"
#include "cms.hh"
NMLTYPE nmltype;

#include "cmd_msg.hh"
#include "linklist.hh"

int RCS_CMD_MSG_format(NMLTYPE t, void *buf, CMS * cms)
{
    cms->update(((RCS_CMD_MSG *) buf)->serial_number);
    return (0);
}

RCS_CMD_CHANNEL::RCS_CMD_CHANNEL(NML_FORMAT_PTR f_ptr, const char *name,
    const char *process, const char *file,
    int set_to_server):NML(name, process, file, set_to_server)
{
    format_chain = new LinkedList;
    prefix_format_chain(f_ptr);
    prefix_format_chain(RCS_CMD_MSG_format);
    channel_type = RCS_CMD_CHANNEL_TYPE;

    register_with_server();

}

RCS_CMD_CHANNEL::~RCS_CMD_CHANNEL()
{
    // Something funny happens to gdb without this being explicitly defined.
}

int RCS_CMD_CHANNEL::write(RCS_CMD_MSG * cmd_msg)
{
    return NML::write(cmd_msg, &(cmd_msg->serial_number));
}
