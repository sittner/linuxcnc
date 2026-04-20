package launcher

import (
	"context"
	"time"

	"github.com/sittner/linuxcnc/src/launcher/internal/apiserver"
)

const (
	// defaultRESTAddr is the default listen address for the REST API server.
	// Can be overridden via [GMC]REST_ADDR in the INI file.
	defaultRESTAddr = "localhost:5080"
)

// startAPIServer starts the REST API server in the background.
// The listen address is read from [GMC]REST_ADDR in the INI file,
// defaulting to "localhost:5080".
func (l *Launcher) startAPIServer() {
	addr := defaultRESTAddr
	if l.ini != nil {
		if v := l.ini.Get("GMC", "REST_ADDR"); v != "" {
			addr = v
		}
	}

	reg := apiserver.DefaultRegistry()
	if reg == nil {
		l.logger.Warn("no API registry available, REST server not started")
		return
	}

	l.apiServer = apiserver.NewServer(reg, addr)

	go func() {
		l.logger.Info("starting REST API server", "addr", addr)
		if err := l.apiServer.ListenAndServe(); err != nil {
			// http.ErrServerClosed is expected on graceful shutdown
			if err.Error() != "http: Server closed" {
				l.logger.Error("REST API server error", "error", err)
			}
		}
	}()
}

// stopAPIServer gracefully shuts down the REST API server.
func (l *Launcher) stopAPIServer() {
	if l.apiServer == nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := l.apiServer.Shutdown(ctx); err != nil {
		l.logger.Debug("REST API server shutdown error", "error", err)
	}
	l.apiServer = nil
}
