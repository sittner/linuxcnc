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

// GetPublishErrorDrain returns the active drain instance, or nil if not started.
func GetPublishErrorDrain() *PublishErrorDrain {
	publish_errorDrainMu.Lock()
	defer publish_errorDrainMu.Unlock()
	return publish_errorDrainInst
}

// EnsureDrainStarted creates a publish_error ring and starts the drain
// if it hasn't been started yet (e.g. when no C milltask is present).
// Returns the active drain. The ring is registered with the API registry
// so that the standard WS watch hook fires.
func EnsureDrainStarted(instance string) *PublishErrorDrain {
	publish_errorDrainMu.Lock()
	if publish_errorDrainInst != nil {
		d := publish_errorDrainInst
		publish_errorDrainMu.Unlock()
		return d
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
	d := publish_errorDrainInst
	publish_errorDrainMu.Unlock()
	return d
}
