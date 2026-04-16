package apiserver

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"strings"
	"syscall"
)

// Server is the REST API server that dispatches to registered APIs.
type Server struct {
	registry *Registry
	mux      *http.ServeMux
	server   *http.Server
	prefix   string // e.g. "/api/v1"
}

// NewServer creates a new API server bound to the given registry.
// addr is the listen address (e.g. "localhost:8080").
func NewServer(registry *Registry, addr string) *Server {
	s := &Server{
		registry: registry,
		mux:      http.NewServeMux(),
		prefix:   "/api/v1",
	}

	s.mux.HandleFunc(s.prefix+"/", s.handleAPIRequest)

	s.server = &http.Server{
		Addr:    addr,
		Handler: s.mux,
	}

	return s
}

// ListenAndServe starts the HTTP server. Blocks until the server stops.
func (s *Server) ListenAndServe() error {
	return s.server.ListenAndServe()
}

// Serve accepts connections on the given listener. Useful for tests.
func (s *Server) Serve(ln net.Listener) error {
	return s.server.Serve(ln)
}

// Shutdown gracefully shuts down the server.
func (s *Server) Shutdown(ctx context.Context) error {
	return s.server.Shutdown(ctx)
}

// Handler returns the http.Handler for use with httptest.
func (s *Server) Handler() http.Handler {
	return s.mux
}

// handleAPIRequest is the generic REST dispatcher.
// URL format: /api/v1/{instance}/{func-path...}
func (s *Server) handleAPIRequest(w http.ResponseWriter, r *http.Request) {
	// Strip prefix: "/api/v1/hal0/pin/axis.0" → "hal0/pin/axis.0"
	path := strings.TrimPrefix(r.URL.Path, s.prefix+"/")
	if path == "" {
		writeErrorJSON(w, http.StatusNotFound, "missing instance name")
		return
	}

	// Split into instance + remaining path
	instance, funcPath, _ := strings.Cut(path, "/")
	funcPath = "/" + funcPath // normalize: "" → "/", "pin/x" → "/pin/x"

	// Look up registered API
	api := s.registry.Get(instance)
	if api == nil {
		writeErrorJSON(w, http.StatusNotFound, "unknown API instance: "+instance)
		return
	}
	if !api.Meta.RESTExport {
		writeErrorJSON(w, http.StatusNotFound, "API not REST-exported: "+instance)
		return
	}

	// Match request against FuncMeta entries
	funcIndex := matchFunc(api.Meta, r.Method, funcPath)
	if funcIndex < 0 {
		writeErrorJSON(w, http.StatusNotFound, "no matching function")
		return
	}

	fn := &api.Meta.Funcs[funcIndex]
	if fn.Dispatch == nil {
		writeErrorJSON(w, http.StatusNotImplemented, "function not dispatachable: "+fn.Name)
		return
	}

	// Read request body
	body, err := io.ReadAll(r.Body)
	if err != nil {
		writeErrorJSON(w, http.StatusBadRequest, "failed to read request body")
		return
	}

	// For GET/DELETE, encode path+query params as JSON if no body provided
	if len(body) == 0 && (r.Method == http.MethodGet || r.Method == http.MethodDelete) {
		body = encodeParams(fn.Path, funcPath, r.URL.Query())
	}

	// Dispatch
	resp, err := fn.Dispatch(api.Callbacks, body)
	if err != nil {
		writeDispatchError(w, err)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	w.Write(resp)
}

// matchFunc finds the FuncMeta index matching the given HTTP method and path.
// Returns -1 if no match found.
func matchFunc(meta *APIMeta, method, requestPath string) int {
	for i := range meta.Funcs {
		f := &meta.Funcs[i]
		if f.Method == "" || f.Path == "" {
			continue // not REST-exported
		}
		if f.Method != method {
			continue
		}
		if matchPath(f.Path, requestPath) {
			return i
		}
	}
	return -1
}

// matchPath matches a pattern like "/pin/{name}" against a request path like "/pin/axis.0".
// Supports {param} wildcards that match exactly one path segment,
// and {param...} that matches the rest of the path.
func matchPath(pattern, requestPath string) bool {
	patParts := strings.Split(strings.Trim(pattern, "/"), "/")
	reqParts := strings.Split(strings.Trim(requestPath, "/"), "/")

	for i, pat := range patParts {
		// Wildcard rest: {name...} matches remaining segments
		if strings.HasPrefix(pat, "{") && strings.HasSuffix(pat, "...}") {
			return true // matches everything from here
		}
		if i >= len(reqParts) {
			return false // request path too short
		}
		// Wildcard segment: {name} matches one segment
		if strings.HasPrefix(pat, "{") && strings.HasSuffix(pat, "}") {
			continue // matches any single segment
		}
		// Literal match
		if pat != reqParts[i] {
			return false
		}
	}

	return len(patParts) == len(reqParts)
}

// extractPathParams extracts named parameters from a pattern and request path.
func extractPathParams(pattern, requestPath string) map[string]string {
	params := make(map[string]string)
	patParts := strings.Split(strings.Trim(pattern, "/"), "/")
	reqParts := strings.Split(strings.Trim(requestPath, "/"), "/")

	for i, pat := range patParts {
		if strings.HasPrefix(pat, "{") && strings.HasSuffix(pat, "...}") {
			name := pat[1 : len(pat)-4]
			params[name] = strings.Join(reqParts[i:], "/")
			break
		}
		if i >= len(reqParts) {
			break
		}
		if strings.HasPrefix(pat, "{") && strings.HasSuffix(pat, "}") {
			name := pat[1 : len(pat)-1]
			params[name] = reqParts[i]
		}
	}
	return params
}

// encodeParams builds a JSON object from path parameters and query string.
func encodeParams(pattern, requestPath string, query map[string][]string) []byte {
	params := extractPathParams(pattern, requestPath)

	// Add query parameters (first value only)
	for k, v := range query {
		if _, exists := params[k]; !exists && len(v) > 0 {
			params[k] = v[0]
		}
	}

	if len(params) == 0 {
		return nil
	}

	data, _ := json.Marshal(params)
	return data
}

// apiError is the JSON error response format.
type apiError struct {
	Error string `json:"error"`
	Code  int    `json:"code"`
}

func writeErrorJSON(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(apiError{Error: msg, Code: code})
}

func writeDispatchError(w http.ResponseWriter, err error) {
	// Map errno to HTTP status
	code := http.StatusInternalServerError
	switch err {
	case syscall.EINVAL:
		code = http.StatusBadRequest
	case syscall.ENOENT:
		code = http.StatusNotFound
	case syscall.EPERM:
		code = http.StatusForbidden
	case syscall.EEXIST:
		code = http.StatusConflict
	case syscall.ENOSYS:
		code = http.StatusNotImplemented
	case syscall.EBUSY:
		code = http.StatusConflict
	case syscall.ERANGE:
		code = http.StatusBadRequest
	}
	writeErrorJSON(w, code, err.Error())
}
