// publish_go.go — Go-side helpers for publishing operator errors.
// Not auto-generated; safe to edit.

package emcerror

import (
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// PublishError injects an error event directly from Go code (no C ring needed).
// Thread-safe.
func (d *PublishErrorDrain) PublishError(kind ErrorKind, text string) {
	d.mu.Lock()
	d.events = append(d.events, PublishErrorEvent{Kind: kind, Text: text})
	d.mu.Unlock()
}

// GetPublishErrorDrain returns the active drain for the given instance, or nil.
func GetPublishErrorDrain(instance string) *PublishErrorDrain {
	publish_errorDrainMu.Lock()
	defer publish_errorDrainMu.Unlock()
	if publish_errorDrains == nil {
		return nil
	}
	return publish_errorDrains[instance]
}

// EnsureDrainStarted creates a publish_error ring and starts the drain
// for the given instance. Returns the active drain.
// Each instance gets its own drain (supports multi-instance milltask).
func EnsureDrainStarted(instance string) *PublishErrorDrain {
	publish_errorDrainMu.Lock()
	if publish_errorDrains != nil {
		if d, ok := publish_errorDrains[instance]; ok {
			publish_errorDrainMu.Unlock()
			return d
		}
	}
	publish_errorDrainMu.Unlock()

	// Create a C ring and register it — this triggers startPublishErrorDrain
	// via the OnRegister hook.
	ring := CreatePublishErrorRing()

	reg := apiserver.DefaultRegistry()
	if reg != nil {
		_ = reg.Register("emcerror_publish_error", 1, instance, ring)
	}

	// The hook should have started the drain now.
	publish_errorDrainMu.Lock()
	var d *PublishErrorDrain
	if publish_errorDrains != nil {
		d = publish_errorDrains[instance]
	}
	publish_errorDrainMu.Unlock()
	return d
}
