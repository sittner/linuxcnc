package apiserver

import (
	"context"
	"net/http"
	"sync"

	"nhooyr.io/websocket"
)

// handleStreamUpgrade upgrades the HTTP connection to WebSocket and
// dispatches to the StreamServer's ServeConn in a goroutine.
func (s *Server) handleStreamUpgrade(w http.ResponseWriter, r *http.Request, server StreamServer) {
	conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		s.logger.Warn("stream websocket accept failed", "error", err)
		return
	}

	ctx, cancel := context.WithCancel(r.Context())
	sc := &streamConn{
		conn:   conn,
		ctx:    ctx,
		cancel: cancel,
	}

	// ServeConn blocks until the stream is done (poll_transmit returns <=0
	// or a write error occurs).
	server.ServeConn(sc)

	conn.Close(websocket.StatusNormalClosure, "")
	cancel()
}

// streamConn implements StreamConn backed by a nhooyr WebSocket.
type streamConn struct {
	conn    *websocket.Conn
	ctx     context.Context
	cancel  context.CancelFunc
	writeMu sync.Mutex
}

func (c *streamConn) WriteBinary(data []byte) error {
	c.writeMu.Lock()
	defer c.writeMu.Unlock()
	return c.conn.Write(c.ctx, websocket.MessageBinary, data)
}

func (c *streamConn) ReadBinary() ([]byte, error) {
	_, data, err := c.conn.Read(c.ctx)
	return data, err
}
