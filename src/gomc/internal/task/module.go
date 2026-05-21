package task

import (
	"fmt"
	"log/slog"
	"strconv"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcio"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/motctl"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/motstat"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func init() {
	gomc.RegisterModule("milltask", factory)
}

// Compile-time interface checks.
var _ MotionConfig = (*motctl.MotctlClient)(nil)

func factory(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	logger = logger.With("module", name)
	return &milltaskModule{ini: ini, logger: logger}, nil
}

// milltaskModule wraps Task to satisfy the gomc.Module lifecycle.
type milltaskModule struct {
	ini    *inifile.IniFile
	task   *Task
	logger *slog.Logger
	inihal *iniHal
	mc     MotionConfig
}

func (m *milltaskModule) Start() error {
	reg := apiserver.DefaultRegistry()
	if reg == nil {
		return fmt.Errorf("milltask: no API registry available")
	}

	// Determine motion module instance name from INI (default "motmod").
	motInstance := m.ini.Get("EMCMOT", "EMCMOT")
	if motInstance == "" {
		motInstance = "motmod"
	}

	// Determine IO controller instance name (always "iocontrol").
	ioInstance := "iocontrol"

	// Look up registered GMI callbacks.
	motctlCbs, err := reg.GetAPI("motctl", motInstance, 1)
	if err != nil {
		return fmt.Errorf("milltask: motctl API lookup (%s): %w", motInstance, err)
	}
	motstatCbs, err := reg.GetAPI("motstat", motInstance, 1)
	if err != nil {
		return fmt.Errorf("milltask: motstat API lookup (%s): %w", motInstance, err)
	}
	emcioCbs, err := reg.GetAPI("emcio", ioInstance, 1)
	if err != nil {
		return fmt.Errorf("milltask: emcio API lookup (%s): %w", ioInstance, err)
	}

	// Wrap C callback pointers in typed Go clients.
	mc := motctl.NewMotctlClient(unsafe.Pointer(motctlCbs))
	ms := motstat.NewMotstatClient(unsafe.Pointer(motstatCbs))
	io := emcio.NewEmcioClient(unsafe.Pointer(emcioCbs))

	t := NewTask(mc, io, ms, m.logger)

	// Load configuration from INI and send to motion controller.
	if err := loadConfig(m.ini, t, mc); err != nil {
		return fmt.Errorf("milltask: %w", err)
	}

	// Create inihal HAL component for runtime INI parameter override.
	ih, err := newIniHal(t.numJoints)
	if err != nil {
		return fmt.Errorf("milltask: %w", err)
	}
	ih.initPins(t)

	m.task = t
	m.inihal = ih
	m.mc = mc

	m.logger.Info("milltask started")
	return nil
}

func (m *milltaskModule) Stop() {
	m.logger.Info("milltask stopping")
	// TODO: abort interpreter, drain motion queue
}

func (m *milltaskModule) Destroy() {
	if m.inihal != nil {
		m.inihal.exit()
	}
	m.logger.Info("milltask destroyed")
}

func getIntOr(ini *inifile.IniFile, section, key string, def int) int {
	s := ini.Get(section, key)
	if s == "" {
		return def
	}
	v, err := strconv.Atoi(s)
	if err != nil {
		return def
	}
	return v
}
