package emcgateway

// Go types matching the GMI IDL definitions for emcstat, emccmd, emcerror.
// These are marshaled to JSON for REST/WebSocket clients.

// ─── emcstat types ───

type position struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	Z float64 `json:"z"`
	A float64 `json:"a"`
	B float64 `json:"b"`
	C float64 `json:"c"`
	U float64 `json:"u"`
	V float64 `json:"v"`
	W float64 `json:"w"`
}

type jointInfo struct {
	Homed          bool    `json:"homed"`
	Homing         bool    `json:"homing"`
	Enabled        bool    `json:"enabled"`
	Fault          bool    `json:"fault"`
	MinSoftLimit   float64 `json:"min_soft_limit"`
	MaxSoftLimit   float64 `json:"max_soft_limit"`
	MinHardLimit   bool    `json:"min_hard_limit"`
	MaxHardLimit   bool    `json:"max_hard_limit"`
	OverrideLimits bool    `json:"override_limits"`
	Velocity       float64 `json:"velocity"`
	Input          float64 `json:"input"`
	Output         float64 `json:"output"`
	Limit          int     `json:"limit"`
}

type spindleInfo struct {
	Speed           float64 `json:"speed"`
	Direction       int     `json:"direction"`
	Brake           bool    `json:"brake"`
	Enabled         bool    `json:"enabled"`
	Override        float64 `json:"override"`
	OverrideEnabled bool    `json:"override_enabled"`
	Homed           bool    `json:"homed"`
	OrientState     int     `json:"orient_state"`
	OrientFault     int     `json:"orient_fault"`
}

type axisInfo struct {
	Velocity         float64 `json:"velocity"`
	MinPositionLimit float64 `json:"min_position_limit"`
	MaxPositionLimit float64 `json:"max_position_limit"`
}

type statTaskInfo struct {
	Mode              int    `json:"mode"`
	State             int    `json:"state"`
	InterpState       int    `json:"interp_state"`
	ExecState         int    `json:"exec_state"`
	File              string `json:"file"`
	Command           string `json:"command"`
	Line              int    `json:"line"`
	MotionLine        int    `json:"motion_line"`
	CurrentLine       int    `json:"current_line"`
	ReadLine          int    `json:"read_line"`
	QueuedMdiCommands int    `json:"queued_mdi_commands"`
	OptionalStop      bool   `json:"optional_stop"`
	BlockDelete       bool   `json:"block_delete"`
	TaskPaused        bool   `json:"task_paused"`
	G5xIndex          int    `json:"g5x_index"`
}

type statMotionInfo struct {
	Mode         int      `json:"mode"`
	Enabled      bool     `json:"enabled"`
	InPosition   bool     `json:"in_position"`
	Paused       bool     `json:"paused"`
	Feedrate     float64  `json:"feedrate"`
	Rapidrate    float64  `json:"rapidrate"`
	MaxVelocity  float64  `json:"max_velocity"`
	Velocity     float64  `json:"velocity"`
	DistanceToGo float64  `json:"distance_to_go"`
	Dtg          position `json:"dtg"`
	CurrentVel   float64  `json:"current_vel"`
	MotionID     int      `json:"motion_id"`
	MotionLine   int      `json:"motion_line"`
}

type statFull struct {
	Task                statTaskInfo   `json:"task"`
	Motion              statMotionInfo `json:"motion"`
	Position            position       `json:"position"`
	ActualPosition      position       `json:"actual_position"`
	JointActualPosition [16]float64    `json:"joint_actual_position"`
	ProbedPosition      position       `json:"probed_position"`
	G5xOffset           position       `json:"g5x_offset"`
	G92Offset           position       `json:"g92_offset"`
	ToolOffset          position       `json:"tool_offset"`
	RotationXY          float64        `json:"rotation_xy"`
	Joints              []jointInfo    `json:"joints"`
	Spindle             []spindleInfo  `json:"spindle"`
	Axis                []axisInfo     `json:"axis"`
	ActiveGcodes        []int          `json:"active_gcodes"`
	ActiveMcodes        []int          `json:"active_mcodes"`
	ActiveSettings      []float64      `json:"active_settings"`
	KinematicsType      int            `json:"kinematics_type"`
	JointsCount         int            `json:"joints_count"`
	NumExtrajoints      int            `json:"num_extrajoints"`
	AxisMask            int            `json:"axis_mask"`
	Flood               bool           `json:"flood"`
	Mist                bool           `json:"mist"`
	ToolInSpindle       int            `json:"tool_in_spindle"`
	PocketPrepped       int            `json:"pocket_prepped"`
	LinearUnits         float64        `json:"linear_units"`
	Homed               [16]bool       `json:"homed"`
	Limit               [16]int        `json:"limit"`
	State               int            `json:"state"`
}

// ─── emcerror types ───

type errorMessage struct {
	Kind int    `json:"kind"`
	Text string `json:"text"`
}
