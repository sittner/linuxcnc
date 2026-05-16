/********************************************************************
* Description: nmlmsg.hh
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

#ifndef NMLMSG_HH
#define NMLMSG_HH

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>		/* size_t */
#include <string.h>		/* memset */

#ifdef __cplusplus
};
#endif

class CMS;

#include "nml_type.hh"

/* Class NMLmsg — base class for all NML message types.
 * Constructors are inline so no link dependency on libnml is needed. */
class NMLmsg {
  protected:
    NMLmsg(NMLTYPE t, long s) : type(t), size(s) {
	memset(this, 0, s);
	type = t;
	size = s;
    }
    NMLmsg(NMLTYPE t, size_t s) : type(t), size((long)s) {
	memset(this, 0, s);
	type = t;
	size = (long)s;
    }
    NMLmsg(NMLTYPE t, long s, int /*noclear*/) : type(t), size(s) {
    }

  public:
    void clear() {
	long temp_size = size;
	NMLTYPE temp_type = type;
	memset(this, 0, size);
	size = temp_size;
	type = temp_type;
    }

    NMLTYPE type;		/* Each derived type should have a unique id */
    long size;			/* The size is used so that the entire buffer
				   is not copied unnecessarily. */
};

// This is just a symbol passed to the RCS Java Tools (CodeGen, RCS-Design, RCS-Diagnostis)
#define NML_DYNAMIC_LENGTH_ARRAY

#define DECLARE_NML_DYNAMIC_LENGTH_ARRAY(type, name, size) int name##_length; type name[size];

#endif /* !defined(NMLMSG_HH) */
