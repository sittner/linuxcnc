package halcmd

import hal "linuxcnc.org/hal"

// NewInst creates a new instance of an instantiable HAL component.
// compType is the component type name, instName is the new instance name.
func NewInst(compType, instName, arg string) error {
	return hal.CompMake(compType, instName, arg)
}
