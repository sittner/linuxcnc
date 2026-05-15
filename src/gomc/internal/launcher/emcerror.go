package launcher

import (
	"time"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcerror"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcerrorapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// initEmcerrorRing allocates the emcerror publish ring, starts the drain
// goroutine, and registers the ring pointer in the API registry so that
// milltask can look it up via emcerror_publish_error_ring_get().
func (l *Launcher) initEmcerrorRing() {
	// Create drain (allocates the C ring internally).
	ring := emcerror.CreatePublishErrorRing()
	drain := emcerror.NewPublishErrorDrain(ring)
	drain.Start()
	l.emcerrorDrain = drain

	// Register the ring pointer in the API registry under the name that
	// the generated C lookup helper expects: "emcerror_publish_error" v1 "default".
	reg := apiserver.DefaultRegistry()
	if reg != nil {
		_ = reg.Register("emcerror_publish_error", 1, "default", drain.Ring())
	}

	// Register the drain's WatchFunc with the WS watch registry so that
	// WebSocket clients can subscribe to error events.
	// Instance "emcerror" matches what axis.py's ErrorChannel subscribes to.
	watchReg := apiserver.DefaultWatchRegistry()
	if watchReg == nil {
		watchReg = apiserver.NewWatchRegistry()
		apiserver.SetDefaultWatchRegistry(watchReg)
	}
	watchReg.Register(&apiserver.WatchAPI{
		APIName:  "emcerror",
		Instance: "emcerror",
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "get_errors",
				DefaultRate: 200 * time.Millisecond,
				Watch:       drain.WatchFunc(),
			},
		},
	})

	// Register API metadata so the watch registry can resolve the topic.
	apiserver.RegisterMeta(emcerrorapi.EmcerrorMeta)

	l.logger.Info("emcerror publish ring registered")
}
