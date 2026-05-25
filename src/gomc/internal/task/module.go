package task

import (
	"fmt"
	"log/slog"
	"strconv"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcerror"
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
	m := &milltaskModule{ini: ini, logger: logger, name: name}

	// Register C-compatible callback structs so C modules (halui) can
	// call emccmd/emcstat via the standard api_get mechanism.
	// The CGO dispatch packages (emcstat, emccmd) register their metas in
	// init() — those metas know how to dispatch through C function pointers.
	reg := apiserver.DefaultRegistry()
	if reg == nil {
		return nil, fmt.Errorf("milltask: no API registry available")
	}
	cleanup, err := m.registerCAPIs(reg, name)
	if err != nil {
		return nil, fmt.Errorf("milltask: %w", err)
	}
	m.apiCleanup = cleanup

	// Register WebSocket watches and commands (direct Go path, no C thunk).
	m.registerWatches(name)

	return m, nil
}

// milltaskModule wraps Task to satisfy the gomc.Module lifecycle.
type milltaskModule struct {
	ini        *inifile.IniFile
	name       string
	task       *Task
	logger     *slog.Logger
	inihal     *iniHal
	mc         MotionConfig
	apiCleanup func()
	poslog     posLogger
	interp     *CInterp
	canonTable *canonCallbackTable
	mon        *monitor
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
	ioClient := emcio.NewEmcioClient(unsafe.Pointer(emcioCbs))
	io := &ioAdapter{EmcioClient: ioClient}

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

	// Wire the error publisher so operator messages reach UI clients.
	// EnsureDrainStarted creates the ring+drain if the C milltask didn't.
	if drain := emcerror.EnsureDrainStarted(m.name); drain != nil {
		t.SetErrorPublisher(&drainErrorPublisher{drain: drain})
	}

	// Create and configure the G-code interpreter.
	if err := m.initInterpreter(); err != nil {
		return fmt.Errorf("milltask: %w", err)
	}

	// Start the sequencer goroutine (executes queued motion commands).
	t.StartSequencer()

	// Start the monitoring goroutine (estop, errors, soft limits, inihal).
	m.mon = newMonitor(t, mc, ih, io)
	m.mon.start()

	// Register tools API (needs INI for tool table path).
	m.registerTools()

	m.logger.Info("milltask started")
	return nil
}

func (m *milltaskModule) Stop() {
	if m.mon != nil {
		m.mon.stop()
	}
	m.poslog.stopLogger()
	if m.task != nil {
		m.task.StopSequencer()
		if m.task.mcode != nil {
			m.task.mcode.Stop()
		}
	}
	m.logger.Info("milltask stopping")
}

func (m *milltaskModule) Destroy() {
	if m.interp != nil {
		m.interp.Destroy()
		m.interp = nil
	}
	if m.canonTable != nil {
		m.canonTable.release()
		m.canonTable = nil
	}
	if m.inihal != nil {
		m.inihal.exit()
	}
	if m.apiCleanup != nil {
		m.apiCleanup()
	}
	m.logger.Info("milltask destroyed")
}

// initInterpreter creates and configures the G-code interpreter.
func (m *milltaskModule) initInterpreter() error {
	// Determine interpreter library (default: built-in rs274ngc).
	interpLib := m.ini.Get("TASK", "RS274NGC_STARTUP_CODE")
	_ = interpLib // not used for library selection

	interp, err := NewCInterp()
	if err != nil {
		return fmt.Errorf("creating interpreter: %w", err)
	}

	// Set up canon callbacks so interpreter actions flow to the sequencer.
	ct := newCanonCallbackTable(m.task.canon)
	interp.SetCanonCallbacks(ct.ptr())

	// Load INI configuration into interpreter.
	if err := interp.IniLoad(m.ini.SourceFile()); err != nil {
		interp.Destroy()
		ct.release()
		return fmt.Errorf("interpreter ini_load: %w", err)
	}

	// Ensure canon knows the parameter file name so the interpreter can
	// load it during Init(). IniLoad should set this via the callback,
	// but we also set it explicitly as a safety net.
	if pf := m.ini.Get("RS274NGC", "PARAMETER_FILE"); pf != "" {
		m.task.canon.SetParameterFileName(pf)
	}

	// Initialize interpreter state.
	if err := interp.Init(); err != nil {
		interp.Destroy()
		ct.release()
		return fmt.Errorf("interpreter init: %w", err)
	}

	m.interp = interp
	m.canonTable = ct
	m.task.SetInterpreter(interp)

	// Register M-code trampoline for all M100-M199 slots so the interpreter
	// calls back into Go when user-defined M-codes are encountered.
	interp.RegisterAllMcodeSlots()

	// Synch interpreter state with motion and populate active G/M codes.
	// The C milltask calls emcTaskPlanSynch() at startup which does interp.synch(),
	// then the stat update calls active_g_codes/m_codes/settings.
	if err := interp.Synch(); err != nil {
		m.logger.Warn("interpreter initial synch failed (no motion?)", "err", err)
	}
	m.task.updateActiveCodes(interp)

	m.logger.Info("interpreter initialized")
	return nil
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

// ioAdapter wraps the generated EmcioClient to satisfy IOController interface.
type ioAdapter struct {
	*emcio.EmcioClient
}

// GetCmdStatus returns the IO command status (1=DONE, 2=EXEC, 3=ERROR).
func (a *ioAdapter) GetCmdStatus() (int32, error) {
	st, err := a.EmcioClient.GetStatus()
	if err != nil {
		return 0, err
	}
	return int32(st.Status), nil
}

// GetIOFullStatus returns the full IO status for the monitor.
func (a *ioAdapter) GetIOFullStatus() (IOFullStatus, error) {
	st, err := a.EmcioClient.GetStatus()
	if err != nil {
		return IOFullStatus{}, err
	}
	return IOFullStatus{
		Estop:  st.Estop,
		Status: int32(st.Status),
		Reason: st.Reason,
	}, nil
}

// GetToolInSpindle returns the current tool number loaded in the spindle.
func (a *ioAdapter) GetToolInSpindle() (int32, error) {
	st, err := a.EmcioClient.GetStatus()
	if err != nil {
		return 0, err
	}
	return st.Tool.ToolInSpindle, nil
}

// GetPocketPrepped returns the pocket number prepared for next tool change.
func (a *ioAdapter) GetPocketPrepped() (int32, error) {
	st, err := a.EmcioClient.GetStatus()
	if err != nil {
		return 0, err
	}
	return st.Tool.PocketPrepped, nil
}

// drainErrorPublisher implements ErrorPublisher by writing to the emcerror drain.
type drainErrorPublisher struct {
	drain *emcerror.PublishErrorDrain
}

func (p *drainErrorPublisher) OperatorError(text string) {
	p.drain.PublishError(emcerror.OPERATOR_ERROR, text)
}

func (p *drainErrorPublisher) OperatorText(text string) {
	p.drain.PublishError(emcerror.OPERATOR_TEXT, text)
}

func (p *drainErrorPublisher) OperatorDisplay(text string) {
	p.drain.PublishError(emcerror.OPERATOR_DISPLAY, text)
}
