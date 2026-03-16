// src/server/config/ini.go
package config

import (
	"fmt"
	"os"
	"path/filepath"

	"gopkg.in/ini.v1"
)

// iniFile wraps the raw INI data
type iniFile struct {
	*ini.File
}

// Load parses an INI file and returns a Config structure.
// It returns an error for missing files, parse errors, or failed section mapping.
func Load(path string) (*Config, error) {
	// Resolve absolute path
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, fmt.Errorf("invalid path: %w", err)
	}

	// Check file exists
	if _, err := os.Stat(absPath); err != nil {
		return nil, fmt.Errorf("file not found: %s", absPath)
	}

	// Parse INI file
	iniOpts := ini.LoadOptions{
		AllowBooleanKeys:          true,
		AllowShadows:              true,
		IgnoreInlineComment:       false,
		UnescapeValueDoubleQuotes: true,
	}

	f, err := ini.LoadSources(iniOpts, absPath)
	if err != nil {
		return nil, fmt.Errorf("parse error: %w", err)
	}

	cfg := &Config{
		IniPath: absPath,
		raw:     &iniFile{f},
	}

	// Map sections to struct fields using ini tags
	if err := f.MapTo(cfg); err != nil {
		return nil, fmt.Errorf("mapping error: %w", err)
	}

	// Handle HALFILE and POSTGUI_HALFILE specially — they may appear multiple times
	halSection := f.Section("HAL")
	if halSection != nil {
		cfg.HAL.Files = halSection.Key("HALFILE").ValueWithShadows()
		cfg.HAL.PostGUIFile = halSection.Key("POSTGUI_HALFILE").ValueWithShadows()
	}

	return cfg, nil
}

// GetSection returns all key-value pairs from a named INI section.
func (f *iniFile) GetSection(name string) map[string]string {
	section := f.Section(name)
	if section == nil {
		return nil
	}

	result := make(map[string]string)
	for _, key := range section.Keys() {
		result[key.Name()] = key.String()
	}
	return result
}

// ExpandPath expands a path relative to the INI file directory
func (c *Config) ExpandPath(path string) string {
	if filepath.IsAbs(path) {
		return path
	}
	return filepath.Join(filepath.Dir(c.IniPath), path)
}
