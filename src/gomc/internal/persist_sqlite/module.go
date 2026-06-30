// Copyright (C) 2026 Sascha Ittner <sascha.ittner@modusoft.de>
// License: GPL Version 2
// Package persist_sqlite implements a generic persistence gomod backed by SQLite.
//
// It registers as "persist_sqlite" and exposes the persist GMI API for
// namespaced key-value storage. Modules that need persistence look up the
// "persistence" API instance by default (overrideable via persistence=<name>).
//
// Load: load persist_sqlite <persistence> [dbpath=<dir>]
// Default db directory: db/ next to the INI file.
package persist_sqlite

import (
	"database/sql"
	"fmt"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/persist"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"

	_ "modernc.org/sqlite"
)

func init() {
	gomc.RegisterModule("persist_sqlite", newPersistSQLite)
}

type module struct {
	logger *slog.Logger
	db     *sql.DB
	mu     sync.RWMutex
	tables map[string]bool // known-existing tables (checked under mu)
}

func newPersistSQLite(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	dbDir := ""
	for _, arg := range args {
		if k, v, ok := strings.Cut(arg, "="); ok && k == "dbpath" {
			dbDir = v
		}
	}

	// Default: db/ directory next to INI file.
	iniDir := filepath.Dir(ini.SourceFile())
	if dbDir == "" {
		dbDir = filepath.Join(iniDir, "db")
	} else if !filepath.IsAbs(dbDir) && iniDir != "" {
		dbDir = filepath.Join(iniDir, dbDir)
	}

	// Create directory if it doesn't exist.
	if err := os.MkdirAll(dbDir, 0755); err != nil {
		return nil, fmt.Errorf("persist_sqlite: create db dir %s: %w", dbDir, err)
	}

	dbPath := filepath.Join(dbDir, "persist.db")
	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		return nil, fmt.Errorf("persist_sqlite: open db %s: %w", dbPath, err)
	}

	if _, err := db.Exec("PRAGMA journal_mode=WAL"); err != nil {
		db.Close()
		return nil, fmt.Errorf("persist_sqlite: set WAL: %w", err)
	}

	m := &module{logger: logger, db: db, tables: make(map[string]bool)}

	// Populate table cache from existing tables.
	rows, err := db.Query("SELECT name FROM sqlite_master WHERE type='table'")
	if err != nil {
		db.Close()
		return nil, fmt.Errorf("persist_sqlite: list tables: %w", err)
	}
	for rows.Next() {
		var name string
		if err := rows.Scan(&name); err != nil {
			rows.Close()
			db.Close()
			return nil, fmt.Errorf("persist_sqlite: scan table: %w", err)
		}
		m.tables[name] = true
	}
	rows.Close()

	// Register API.
	reg := apiserver.DefaultRegistry()
	if err := persist.RegisterPersistAPI(reg, name, m); err != nil {
		db.Close()
		return nil, fmt.Errorf("persist_sqlite: register API: %w", err)
	}

	logger.Info("persist_sqlite: ready", "db", dbPath, "instance", name)
	return m, nil
}

func (m *module) Start() error { return nil }
func (m *module) Stop()        {}
func (m *module) Destroy() {
	if m.db != nil {
		m.db.Close()
	}
}

// --- Schema ---

// validName checks that a namespace name is safe for use as a SQLite table name.
// Only alphanumeric characters and underscores are allowed.
func validName(name string) bool {
	if name == "" {
		return false
	}
	for _, c := range name {
		if !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			return false
		}
	}
	return true
}

// ensureTable creates the namespace table if it doesn't exist.
// Caller must hold m.mu (write lock).
func (m *module) ensureTable(namespace string) error {
	if m.tables[namespace] {
		return nil
	}
	_, err := m.db.Exec(fmt.Sprintf(
		`CREATE TABLE IF NOT EXISTS "%s" (
			key     TEXT PRIMARY KEY,
			value   TEXT NOT NULL DEFAULT '',
			updated INTEGER NOT NULL DEFAULT 0
		)`, namespace))
	if err == nil {
		m.tables[namespace] = true
	}
	return err
}

// --- Persist API implementation ---

func (m *module) GetNamespaces() ([]string, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	rows, err := m.db.Query(
		"SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var namespaces []string
	for rows.Next() {
		var ns string
		if err := rows.Scan(&ns); err != nil {
			return nil, err
		}
		namespaces = append(namespaces, ns)
	}
	return namespaces, rows.Err()
}

func (m *module) GetEntries(namespace string) ([]persist.Entry, error) {
	if !validName(namespace) {
		return nil, fmt.Errorf("invalid namespace: %q", namespace)
	}
	m.mu.RLock()
	defer m.mu.RUnlock()

	rows, err := m.db.Query(fmt.Sprintf(
		`SELECT key, value, updated FROM "%s" ORDER BY key`, namespace))
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var entries []persist.Entry
	for rows.Next() {
		e := persist.Entry{Namespace: namespace}
		if err := rows.Scan(&e.Key, &e.Value, &e.Updated); err != nil {
			return nil, err
		}
		entries = append(entries, e)
	}
	return entries, rows.Err()
}

func (m *module) GetEntry(namespace, key string) (*persist.Entry, error) {
	if !validName(namespace) {
		return nil, fmt.Errorf("invalid namespace: %q", namespace)
	}
	m.mu.RLock()
	defer m.mu.RUnlock()

	e := persist.Entry{Namespace: namespace}
	err := m.db.QueryRow(fmt.Sprintf(
		`SELECT key, value, updated FROM "%s" WHERE key = ?`, namespace),
		key,
	).Scan(&e.Key, &e.Value, &e.Updated)
	if err == sql.ErrNoRows {
		return nil, fmt.Errorf("not found: %s/%s", namespace, key)
	}
	if err != nil {
		return nil, err
	}
	return &e, nil
}

func (m *module) SetEntry(namespace, key, value string) (*persist.SetResult, error) {
	if !validName(namespace) {
		return &persist.SetResult{Ok: false}, fmt.Errorf("invalid namespace: %q", namespace)
	}
	m.mu.Lock()
	defer m.mu.Unlock()

	if err := m.ensureTable(namespace); err != nil {
		return &persist.SetResult{Ok: false}, err
	}

	now := time.Now().Unix()
	_, err := m.db.Exec(fmt.Sprintf(
		`INSERT INTO "%s" (key, value, updated) VALUES (?, ?, ?)
		 ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated = excluded.updated`,
		namespace),
		key, value, now,
	)
	if err != nil {
		return &persist.SetResult{Ok: false}, err
	}
	return &persist.SetResult{Ok: true}, nil
}

func (m *module) DeleteEntry(namespace, key string) (*persist.DeleteResult, error) {
	if !validName(namespace) {
		return &persist.DeleteResult{Ok: false}, fmt.Errorf("invalid namespace: %q", namespace)
	}
	m.mu.Lock()
	defer m.mu.Unlock()

	res, err := m.db.Exec(fmt.Sprintf(
		`DELETE FROM "%s" WHERE key = ?`, namespace),
		key,
	)
	if err != nil {
		return &persist.DeleteResult{Ok: false}, err
	}
	n, _ := res.RowsAffected()
	return &persist.DeleteResult{Ok: n > 0, Count: int32(n)}, nil
}

func (m *module) SetEntries(namespace string, entries []persist.Entry) (*persist.SetResult, error) {
	if !validName(namespace) {
		return &persist.SetResult{Ok: false}, fmt.Errorf("invalid namespace: %q", namespace)
	}
	m.mu.Lock()
	defer m.mu.Unlock()

	if err := m.ensureTable(namespace); err != nil {
		return &persist.SetResult{Ok: false}, err
	}

	tx, err := m.db.Begin()
	if err != nil {
		return &persist.SetResult{Ok: false}, err
	}
	defer tx.Rollback()

	now := time.Now().Unix()
	stmt, err := tx.Prepare(fmt.Sprintf(
		`INSERT INTO "%s" (key, value, updated) VALUES (?, ?, ?)
		 ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated = excluded.updated`,
		namespace))
	if err != nil {
		return &persist.SetResult{Ok: false}, err
	}
	defer stmt.Close()

	for _, e := range entries {
		ts := e.Updated
		if ts == 0 {
			ts = now
		}
		if _, err := stmt.Exec(e.Key, e.Value, ts); err != nil {
			return &persist.SetResult{Ok: false}, err
		}
	}

	if err := tx.Commit(); err != nil {
		return &persist.SetResult{Ok: false}, err
	}
	return &persist.SetResult{Ok: true}, nil
}

func (m *module) DeleteNamespace(namespace string) (*persist.DeleteResult, error) {
	if !validName(namespace) {
		return &persist.DeleteResult{Ok: false}, fmt.Errorf("invalid namespace: %q", namespace)
	}
	m.mu.Lock()
	defer m.mu.Unlock()

	// DROP TABLE is O(1) — much faster than row-by-row delete.
	_, err := m.db.Exec(fmt.Sprintf(`DROP TABLE IF EXISTS "%s"`, namespace))
	if err != nil {
		return &persist.DeleteResult{Ok: false}, err
	}
	delete(m.tables, namespace)
	return &persist.DeleteResult{Ok: true, Count: 1}, nil
}
