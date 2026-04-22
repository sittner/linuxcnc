// Package launcher — gomodules.go handles loading, lifecycle management of
// Go modules compiled into the gomc-server binary.
//
// Go modules register themselves at init() time via gomc.RegisterModule().
// When a HAL "load" command references a module name that is not found as a
// cmod .so, the launcher looks it up in the gomc registry and calls the
// registered factory.
package launcher

import (
	"fmt"

	"github.com/sittner/linuxcnc/src/launcher/pkg/gomc"
)

// goModule wraps a gomc.Module instance for lifecycle management.
type goModule struct {
	mod  gomc.Module
	name string
}

// loadGoModule looks up a compiled-in Go module by name in the gomc registry,
// calls its factory, and appends the module to l.goModules.
//
// The factory is expected to create and fully initialize the module (including
// HAL component/pin creation) before returning.
func (l *Launcher) loadGoModule(name string, args []string) error {
	factory := gomc.GetFactory(name)
	if factory == nil {
		return fmt.Errorf("Go module %q not found in registry", name)
	}

	l.logger.Info("loading Go module", "name", name)

	mod, err := factory(l.ini, l.logger, name, args)
	if err != nil {
		return fmt.Errorf("load Go module %q: %w", name, err)
	}

	l.goModules = append(l.goModules, &goModule{
		mod:  mod,
		name: name,
	})
	l.logger.Info("Go module loaded and initialized", "name", name)

	return nil
}

// startGoModules calls Start() on all loaded Go modules.
// Called after HAL threads are started, matching the protocol start sequence.
func (l *Launcher) startGoModules() error {
	for _, gm := range l.goModules {
		if err := gm.mod.Start(); err != nil {
			return fmt.Errorf("Go module %q Start() failed: %w", gm.name, err)
		}
	}
	return nil
}

// stopGoModules calls Stop() on all loaded Go modules in reverse order.
// Called during cleanup before Destroy.
func (l *Launcher) stopGoModules() {
	for i := len(l.goModules) - 1; i >= 0; i-- {
		l.goModules[i].mod.Stop()
	}
}

// destroyGoModules calls Destroy() on all loaded Go modules in reverse order.
// Called after all modules have been stopped to release resources.
func (l *Launcher) destroyGoModules() {
	for i := len(l.goModules) - 1; i >= 0; i-- {
		l.goModules[i].mod.Destroy()
	}
}
