package task

import (
	"log/slog"
	"strconv"

	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func init() {
	gomc.RegisterModule("milltask", factory)
}

func factory(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	logger = logger.With("module", name)

	// TODO: look up motctl/emcio/motstat GMI client handles from the
	// gomc registry once those bindings exist. For now we store nil and
	// will wire them in a follow-up commit.
	t := NewTask(nil, nil, nil, logger)

	// Load configuration from INI
	t.numJoints = getIntOr(ini, "KINS", "JOINTS", 3)
	t.numSpindles = getIntOr(ini, "TRAJ", "SPINDLES", 1)

	return &milltaskModule{task: t, logger: logger}, nil
}

// milltaskModule wraps Task to satisfy the gomc.Module lifecycle.
type milltaskModule struct {
	task   *Task
	logger *slog.Logger
}

func (m *milltaskModule) Start() error {
	m.logger.Info("milltask started")
	// TODO: register emccmd GMI handler callbacks pointing to m.task methods
	return nil
}

func (m *milltaskModule) Stop() {
	m.logger.Info("milltask stopping")
	// TODO: abort interpreter, drain motion queue
}

func (m *milltaskModule) Destroy() {
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
