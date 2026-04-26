// Package inirest exposes the parsed INI file via our REST API.
// It provides a single POST /query endpoint that accepts bulk lookups,
// allowing clients to fetch many INI values in a single round-trip.
package inirest

import (
	"encoding/json"
	"fmt"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

// ini holds the parsed INI file set by Register.
var ini *inifile.IniFile

// Register registers the INI REST API with the given registry.
// The parsed INI file is stored for use by the query dispatch function.
func Register(reg *apiserver.Registry, parsed *inifile.IniFile) error {
	ini = parsed

	meta := &apiserver.APIMeta{
		Name:       "ini",
		Version:    1,
		RESTExport: true,
		Prefix:     "ini",
		Funcs:      buildFuncMetas(),
	}
	apiserver.RegisterMeta(meta)
	return reg.Register("ini", 1, "ini", unsafe.Pointer(nil))
}

// ─── Request / Response types ───

// queryItem is one element in the bulk query request array.
type queryItem struct {
	Section string `json:"section"`
	Key     string `json:"key"`
	All     bool   `json:"all,omitempty"`
}

// resultItem is one element in the bulk query response array.
type resultItem struct {
	// Value is set for single-value lookups (find semantics).
	// null when the key is not found.
	Value *string `json:"value,omitempty"`
	// Values is set when All is true (findall semantics).
	Values []string `json:"values"`
}

// ─── Dispatch function ───

func dispatchQuery(_ unsafe.Pointer, body []byte) ([]byte, error) {
	if ini == nil {
		return nil, fmt.Errorf("INI file not loaded")
	}

	var items []queryItem
	if err := json.Unmarshal(body, &items); err != nil {
		return nil, fmt.Errorf("invalid query: %w", err)
	}

	results := make([]resultItem, len(items))
	for i, q := range items {
		if q.All {
			vals := ini.GetAll(q.Section, q.Key)
			if vals == nil {
				vals = []string{}
			}
			results[i] = resultItem{Values: vals}
		} else {
			v := ini.Get(q.Section, q.Key)
			if v == "" {
				// Distinguish "not found" from "empty value" by checking
				// whether the key actually exists.
				if !keyExists(q.Section, q.Key) {
					results[i] = resultItem{}
				} else {
					results[i] = resultItem{Value: &v}
				}
			} else {
				results[i] = resultItem{Value: &v}
			}
		}
	}
	return json.Marshal(results)
}

// keyExists checks whether a key exists in the INI file (even if empty).
func keyExists(section, key string) bool {
	entries := ini.GetSection(section)
	for _, e := range entries {
		if e.Key == key {
			return true
		}
	}
	return false
}

// ─── Func meta table ───

func buildFuncMetas() []apiserver.FuncMeta {
	return []apiserver.FuncMeta{
		{Name: "query", Method: "POST", Path: "/query", Dispatch: dispatchQuery},
	}
}
