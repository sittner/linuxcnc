// Package launcher — gomodules.go handles loading, lifecycle management, and
// detection of Go plugin .so files loaded via the "load" HAL command.
package launcher

import (
	"debug/elf"
	"fmt"
	"plugin"
	"strings"

	"github.com/sittner/linuxcnc/src/launcher/pkg/gomodule"
)

// isGoPlugin inspects the ELF dynamic symbol table of the .so at path to
// determine whether it is a Go plugin built with "go build -buildmode=plugin".
//
// Go plugins always export symbols with "runtime." or "go:" prefixes that
// C .so files never have. This is a read-only ELF header check — the file
// is not executed or loaded into memory.
func isGoPlugin(path string) bool {
	f, err := elf.Open(path)
	if err != nil {
		return false
	}
	defer f.Close()

	syms, err := f.DynamicSymbols()
	if err != nil {
		return false
	}

	for _, s := range syms {
		if strings.HasPrefix(s.Name, "runtime.") || strings.HasPrefix(s.Name, "go:") {
			return true
		}
	}

	return false
}

// loadGoPlugin loads a Go plugin .so, looks up the "New" symbol, validates
// its signature against gomodule.Factory, calls the factory with the launcher's
// INI file and logger, calls Init(), and appends the module to l.goModules.
//
// Note: Go plugins can be loaded but never unloaded (plugin.Open has no Close).
// Stop() handles logical shutdown; the plugin code stays resident until the
// process exits.
func (l *Launcher) loadGoPlugin(path, params string) error {
	l.logger.Info("loading Go plugin", "path", path)

	p, err := plugin.Open(path)
	if err != nil {
		return fmt.Errorf("load Go plugin %q: %w", path, err)
	}

	sym, err := p.Lookup("New")
	if err != nil {
		return fmt.Errorf("load Go plugin %q: missing \"New\" symbol: %w", path, err)
	}

	factory, ok := sym.(gomodule.Factory)
	if !ok {
		return fmt.Errorf("load Go plugin %q: \"New\" symbol has wrong type %T (expected func(*inifile.IniFile, *slog.Logger, string) (gomodule.Module, error))", path, sym)
	}

	mod, err := factory(l.ini, l.logger, params)
	if err != nil {
		return fmt.Errorf("load Go plugin %q: factory error: %w", path, err)
	}

	if err := mod.Init(); err != nil {
		return fmt.Errorf("load Go plugin %q: Init() error: %w", path, err)
	}

	l.goModules = append(l.goModules, mod)
	l.logger.Info("Go plugin loaded and initialized", "path", path)

	return nil
}

// startGoModules calls Start() on all loaded Go plugin modules.
// Called after HAL threads are started, matching the protocol start sequence.
func (l *Launcher) startGoModules() error {
	for _, m := range l.goModules {
		if err := m.Start(); err != nil {
			return fmt.Errorf("Go module Start() failed: %w", err)
		}
	}
	return nil
}

// stopGoModules calls Stop() on all loaded Go plugin modules in reverse order.
// Called during cleanup before protocol shutdown.
func (l *Launcher) stopGoModules() {
	for i := len(l.goModules) - 1; i >= 0; i-- {
		l.goModules[i].Stop()
	}
}
