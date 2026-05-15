package emcgateway

import (
	"encoding/json"
	"sync"
	"time"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstat"
)

const (
	poslogMaxPoints      = 10000
	poslogDropFraction   = 10    // drop oldest 1/10 when full
	poslogDefaultRateUS  = 10000 // 10ms = 100Hz
	poslogDefaultChunk   = 100
	poslogDefaultFlushMS = 200
)

// posPoint is one sampled position (raw 9-axis, tool-offset subtracted).
type posPoint struct {
	MotionType int        `json:"t"`
	Pos        [9]float64 `json:"p"` // x,y,z,a,b,c,u,v,w
}

// posLogger samples position from NML and buffers for WS delivery.
type posLogger struct {
	mu      sync.Mutex
	running bool
	stopCh  chan struct{}

	// Ring buffer of sampled points (server side).
	points []posPoint
	npts   int

	// Pending chunk: points added since last drain.
	pending []posPoint

	// Last sampled position for dedup.
	lastPos [9]float64
	lastMT  int
	hasPrev bool

	// Config (set on start).
	intervalUS int
}

// startLogger begins the sampling goroutine.
func (pl *posLogger) startLogger(gw *emcGateway, intervalUS int) {
	pl.mu.Lock()
	defer pl.mu.Unlock()

	if pl.running {
		return
	}
	if intervalUS <= 0 {
		intervalUS = poslogDefaultRateUS
	}
	pl.intervalUS = intervalUS
	pl.running = true
	pl.stopCh = make(chan struct{})

	go pl.sampleLoop(gw)
}

// stopLogger stops the sampling goroutine.
func (pl *posLogger) stopLogger() {
	pl.mu.Lock()
	defer pl.mu.Unlock()

	if !pl.running {
		return
	}
	pl.running = false
	close(pl.stopCh)
}

// clearLogger resets all buffered points.
func (pl *posLogger) clearLogger() {
	pl.mu.Lock()
	defer pl.mu.Unlock()

	pl.points = pl.points[:0]
	pl.npts = 0
	pl.pending = pl.pending[:0]
	pl.hasPrev = false
}

// drainPending returns accumulated points since last drain and resets the buffer.
// Returns nil if no new points (caller should send empty JSON array or skip).
func (pl *posLogger) drainPending() []posPoint {
	pl.mu.Lock()
	defer pl.mu.Unlock()

	if len(pl.pending) == 0 {
		return nil
	}
	out := pl.pending
	pl.pending = make([]posPoint, 0, poslogDefaultChunk)
	return out
}

func (pl *posLogger) sampleLoop(gw *emcGateway) {
	ticker := time.NewTicker(time.Duration(pl.intervalUS) * time.Microsecond)
	defer ticker.Stop()

	for {
		select {
		case <-pl.stopCh:
			return
		case <-ticker.C:
			pl.sample(gw)
		}
	}
}

func (pl *posLogger) sample(gw *emcGateway) {
	// Read latest stat from push_watch cache (no cgo, no NML).
	s := emcstat.GetLatestStat()
	if s == nil {
		return
	}

	mt := int(s.Motion.MotionType)
	if mt < 0 || mt > 5 {
		mt = 0
	}

	// position - toolOffset (same as C positionlogger)
	pos := [9]float64{
		s.Position.X - s.ToolOffset.X,
		s.Position.Y - s.ToolOffset.Y,
		s.Position.Z - s.ToolOffset.Z,
		s.Position.A - s.ToolOffset.A,
		s.Position.B - s.ToolOffset.B,
		s.Position.C - s.ToolOffset.C,
		s.Position.U - s.ToolOffset.U,
		s.Position.V - s.ToolOffset.V,
		s.Position.W - s.ToolOffset.W,
	}

	pl.mu.Lock()
	defer pl.mu.Unlock()

	// Dedup: skip if position and motion_type unchanged.
	if pl.hasPrev && mt == pl.lastMT && pos == pl.lastPos {
		return
	}
	pl.lastPos = pos
	pl.lastMT = mt
	pl.hasPrev = true

	pt := posPoint{MotionType: mt, Pos: pos}

	// Append to ring buffer.
	if pl.npts >= poslogMaxPoints {
		drop := poslogMaxPoints / poslogDropFraction
		if drop < 2 {
			drop = 2
		}
		copy(pl.points, pl.points[drop:pl.npts])
		pl.npts -= drop
	}
	if pl.npts >= len(pl.points) {
		pl.points = append(pl.points, pt)
	} else {
		pl.points[pl.npts] = pt
	}
	pl.npts++

	// Append to pending buffer for WS delivery.
	pl.pending = append(pl.pending, pt)
}

// ─── Watch + Command handlers ───

// pollPositions is the WatchFunc for "get_positions".
// Returns new points since last poll, or null JSON ([]byte("null")) if none.
func (gw *emcGateway) pollPositions() (json.RawMessage, error) {
	pts := gw.poslog.drainPending()
	if pts == nil {
		return nil, nil // no data — pushLoop will skip this tick
	}
	return json.Marshal(pts)
}

func (gw *emcGateway) cmdStartLogger(req json.RawMessage) (json.RawMessage, error) {
	var args struct {
		IntervalUS int `json:"interval_us"`
	}
	if req != nil {
		if err := json.Unmarshal(req, &args); err != nil {
			return nil, err
		}
	}
	gw.poslog.startLogger(gw, args.IntervalUS)
	return json.RawMessage(`{"ok":true}`), nil
}

func (gw *emcGateway) cmdStopLogger(req json.RawMessage) (json.RawMessage, error) {
	gw.poslog.stopLogger()
	return json.RawMessage(`{"ok":true}`), nil
}

func (gw *emcGateway) cmdClearLogger(req json.RawMessage) (json.RawMessage, error) {
	gw.poslog.clearLogger()
	return json.RawMessage(`{"ok":true}`), nil
}
