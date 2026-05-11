/*    This is a component of LinuxCNC
 *    Copyright 2011, 2012 Michael Haberler <git@mah.priv.at>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
// Python support has been removed. These are error stubs that produce
// clear messages when Python features are invoked.

#include "rs274ngc.hh"
#include "interp_return.hh"
#include "interp_internal.hh"
#include "rs274ngc_interp.hh"

int Interp::py_reload()
{
    ERS("py_reload: Python support has been removed");
    return INTERP_ERROR;
}

bool Interp::is_pycallable(setup_pointer settings,
                           const char *module,
                           const char *funcname)
{
    return false;
}

int Interp::pycall(setup_pointer settings,
                   context_pointer frame,
                   const char *module,
                   const char *funcname,
                   int calltype)
{
    ERS("pycall(%s.%s): Python support has been removed - "
        "register a cmod/gomod handler instead",
        module ? module : "", funcname);
    return INTERP_ERROR;
}

int Interp::py_execute(const char *cmd, bool as_file)
{
    ERS("py_execute: Python support has been removed");
    return INTERP_ERROR;
}
