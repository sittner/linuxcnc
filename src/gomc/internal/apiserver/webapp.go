package apiserver

import (
	"net/http"
	"os"
	"path/filepath"
	"strings"
)

// AddWebApps registers static file handlers for each web application
// found under webappDir. Each subdirectory becomes an app served at
// /app/<name>/. A root handler at / lists available apps.
//
// SPA fallback: requests under /app/<name>/ that don't match a real
// file are served index.html so client-side routing works.
func (s *Server) AddWebApps(webappDir string) {
	if webappDir == "" {
		return
	}

	entries, err := os.ReadDir(webappDir)
	if err != nil {
		return // directory doesn't exist yet — not an error
	}

	var apps []string
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		name := e.Name()
		appDir := filepath.Join(webappDir, name)
		prefix := "/app/" + name + "/"

		// SPA-aware file server: serve real files, fall back to index.html.
		fs := http.Dir(appDir)
		fileServer := http.FileServer(fs)
		s.mux.HandleFunc(prefix, func(w http.ResponseWriter, r *http.Request) {
			// Strip the prefix to get the path within the app directory.
			relPath := strings.TrimPrefix(r.URL.Path, prefix)
			if relPath == "" {
				relPath = "index.html"
			}

			// Check if the file exists. If not, serve index.html (SPA fallback).
			if _, err := os.Stat(filepath.Join(appDir, relPath)); os.IsNotExist(err) {
				r.URL.Path = prefix + "index.html"
			}

			http.StripPrefix(prefix, fileServer).ServeHTTP(w, r)
		})

		apps = append(apps, name)
	}

	// Root handler: list available apps as simple HTML links.
	s.mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write([]byte("<!doctype html><html><head><title>GOMC Web Apps</title></head><body>\n"))
		w.Write([]byte("<h1>GOMC Web Applications</h1><ul>\n"))
		for _, name := range apps {
			w.Write([]byte(`<li><a href="/app/` + name + `/">` + name + `</a></li>` + "\n"))
		}
		w.Write([]byte("</ul></body></html>\n"))
	})
}
